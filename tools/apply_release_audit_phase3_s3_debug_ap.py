#!/usr/bin/env python3
"""Replace blocking S3 SSE and stop immediate AP start retry storms."""

from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / "firmware/control-board-s3/components/s3_debug_ap/s3_debug_ap.c"


def replace_once(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        raise RuntimeError(f"{label}: expected one match, found {count}")
    return text.replace(old, new, 1)


def replace_function(text: str, signature: str, replacement: str) -> str:
    start = text.find(signature)
    if start < 0:
        raise RuntimeError(f"missing function {signature}")
    brace = text.find("{", start)
    depth = 0
    for pos in range(brace, len(text)):
        if text[pos] == "{":
            depth += 1
        elif text[pos] == "}":
            depth -= 1
            if depth == 0:
                return text[:start] + replacement.rstrip() + text[pos + 1:]
    raise RuntimeError(f"unterminated function {signature}")


def main() -> None:
    text = SRC.read_text(encoding="utf-8")

    text = replace_once(
        text,
        "static bool s_ap_worker_running;\n",
        "static bool s_ap_worker_running;\nstatic bool s_ap_start_failed_latched;\n",
        "AP failure latch",
    )

    old_html = """        \"<script>const log=document.getElementById('log');\"\n        \"function start(){const es=new EventSource('/events');\"\n        \"es.onmessage=e=>{log.textContent+=e.data+'\\n';window.scrollTo(0,document.body.scrollHeight);};\"\n        \"es.onerror=()=>{es.close();setTimeout(start,1000);};}start();</script></body></html>\";\n"""
    new_html = """        \"<script>const log=document.getElementById('log');let seq=0;\"\n        \"async function poll(){try{const r=await fetch('/events?after='+seq,{cache:'no-store'});\"\n        \"const next=Number(r.headers.get('X-Log-Seq')||seq),body=await r.text();\"\n        \"for(const line of body.split('\\n')){if(line.startsWith('data: '))log.textContent+=line.slice(6)+'\\n';}\"\n        \"seq=next;window.scrollTo(0,document.body.scrollHeight);}\"\n        \"catch(e){}finally{setTimeout(poll,1000);}}poll();</script></body></html>\";\n"""
    text = replace_once(text, old_html, new_html, "polling debug page")

    text = replace_function(
        text,
        "static esp_err_t events_get_handler(httpd_req_t *req)",
        r'''static esp_err_t events_get_handler(httpd_req_t *req)
{
    if (!s3_api_request_allowed(req, false)) return ESP_FAIL;

    uint32_t last_seq = 0u;
    char query[48] = {0};
    char after[16] = {0};
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK &&
        httpd_query_key_value(query, "after", after, sizeof(after)) == ESP_OK) {
        char *end = NULL;
        unsigned long parsed = strtoul(after, &end, 10);
        if (end && *end == '\0' && parsed <= UINT32_MAX) {
            last_seq = (uint32_t)parsed;
        }
    }

    char chunk[512] = {0};
    uint32_t next_seq = last_seq;
    if (s_lock && xSemaphoreTake(s_lock, pdMS_TO_TICKS(50)) == pdTRUE) {
        (void)s3_debug_log_ring_snapshot(&s_log_ring, chunk, sizeof(chunk), last_seq);
        next_seq = s_log_ring.next_seq;
        xSemaphoreGive(s_lock);
    }

    char seq_header[16];
    snprintf(seq_header, sizeof(seq_header), "%u", (unsigned)next_seq);
    httpd_resp_set_type(req, "text/event-stream");
    httpd_resp_set_hdr(req, "Cache-Control", "no-cache, no-store");
    httpd_resp_set_hdr(req, "X-Log-Seq", seq_header);

    /* One bounded response per request: never hold the synchronous httpd task.
     * The browser polls once per second and supplies the last sequence number. */
    if (chunk[0] != '\0' && events_send_sse(req, chunk) != ESP_OK) {
        return ESP_FAIL;
    }
    return httpd_resp_sendstr_chunk(req, NULL);
}''',
    )

    text = replace_function(
        text,
        "static void s3_debug_ap_worker(void *arg)",
        r'''static void s3_debug_ap_worker(void *arg)
{
    (void)arg;
    for (;;) {
        xSemaphoreTake(s_lock, portMAX_DELAY);
        bool desired = s_ap_desired;
        bool active = s_ap_active;
        if (desired == active) {
            s_ap_worker_running = false;
            xSemaphoreGive(s_lock);
            break;
        }
        xSemaphoreGive(s_lock);

        esp_err_t rc = s3_debug_ap_apply(desired);

        xSemaphoreTake(s_lock, portMAX_DELAY);
        if (desired && rc != ESP_OK) {
            /* Latch ERROR and stop. An explicit OFF request clears this latch;
             * a repeated ON request cannot create a CPU/log/resource storm. */
            s_ap_active = false;
            s_ap_desired = false;
            s_ap_start_failed_latched = true;
            s_ap_worker_running = false;
            xSemaphoreGive(s_lock);
            break;
        }
        s_ap_active = desired;
        xSemaphoreGive(s_lock);
    }
    vTaskDelete(NULL);
}''',
    )

    text = replace_function(
        text,
        "esp_err_t s3_debug_ap_request(bool enable)",
        r'''esp_err_t s3_debug_ap_request(bool enable)
{
    if (!s_lock) {
        /* init not run — fall back to a direct (blocking) apply. */
        esp_err_t rc = s3_debug_ap_apply(enable);
        s_ap_active = enable ? (rc == ESP_OK) : false;
        return rc;
    }

    bool clear_error_status = false;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (enable && s_ap_start_failed_latched) {
        xSemaphoreGive(s_lock);
        ESP_LOGW(TAG, "debug AP start is latched in ERROR; request OFF before retry");
        return ESP_ERR_INVALID_STATE;
    }
    if (!enable && s_ap_start_failed_latched) {
        s_ap_start_failed_latched = false;
        s_ap_desired = false;
        s_ap_active = false;
        clear_error_status = true;
    } else {
        s_ap_desired = enable;
    }
    bool spawn = !clear_error_status && !s_ap_worker_running &&
                 (s_ap_desired != s_ap_active);
    if (spawn) {
        s_ap_worker_running = true;
    }
    xSemaphoreGive(s_lock);

    if (clear_error_status) {
        set_status(S3_DEBUG_AP_STATUS_OFF);
        return ESP_OK;
    }
    if (spawn) {
        if (xTaskCreate(s3_debug_ap_worker, "s3dbgap", 4096, NULL, 3, NULL) != pdPASS) {
            ESP_LOGE(TAG, "failed to spawn s3 debug AP worker");
            xSemaphoreTake(s_lock, portMAX_DELAY);
            s_ap_worker_running = false;
            xSemaphoreGive(s_lock);
            return ESP_ERR_NO_MEM;
        }
    }
    return ESP_OK;
}''',
    )

    # Initialize all state deterministically on component startup.
    text = replace_once(
        text,
        """    s3_debug_log_ring_init(&s_log_ring);\n    s_status = S3_DEBUG_AP_STATUS_OFF;\n""",
        """    s3_debug_log_ring_init(&s_log_ring);\n    s_ap_desired = false;\n    s_ap_active = false;\n    s_ap_worker_running = false;\n    s_ap_start_failed_latched = false;\n    s_status = S3_DEBUG_AP_STATUS_OFF;\n""",
        "debug AP init state",
    )

    SRC.write_text(text, encoding="utf-8")
    print("Applied S3 debug AP nonblocking and retry-latch fixes.")


if __name__ == "__main__":
    main()
