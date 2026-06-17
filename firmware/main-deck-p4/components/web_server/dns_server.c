#include "web_server.h"
#include "esp_log.h"
#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include <lwip/netdb.h>
#include <string.h>

#define DNS_PORT 53
#define DNS_ANSWER_IP "192.168.4.1"

static const char *TAG = "dns_server";
static int s_dns_socket = -1;
static TaskHandle_t s_dns_task_handle = NULL;

typedef struct __attribute__((packed)) {
    uint16_t id;
    uint16_t flags;
    uint16_t qdcount;
    uint16_t ancount;
    uint16_t nscount;
    uint16_t arcount;
} dns_header_t;

typedef struct __attribute__((packed)) {
    uint16_t type;
    uint16_t class;
} dns_question_t;

static void dns_server_task(void *pvParameters)
{
    uint8_t rx_buffer[512];
    uint8_t tx_buffer[512];
    struct sockaddr_in source_addr;
    socklen_t socklen = sizeof(source_addr);

    ESP_LOGI(TAG, "DNS server započinje slušanje na portu %d...", DNS_PORT);

    while (1) {
        int len = recvfrom(s_dns_socket, rx_buffer, sizeof(rx_buffer), 0, (struct sockaddr *)&source_addr, &socklen);

        if (len < 0) {
            if (errno == EBADF) {
                // Socket je zatvoren, izađi mirno
                break;
            }
            ESP_LOGE(TAG, "recvfrom failed: errno %d", errno);
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        if (len < sizeof(dns_header_t)) {
            continue;
        }

        dns_header_t *rx_header = (dns_header_t *)rx_buffer;
        
        // Zanimaju nas samo standardni upiti (flags QR == 0)
        if ((ntohs(rx_header->flags) & 0x8000) != 0) {
            continue;
        }

        // Pripremi zaglavlje odgovora
        dns_header_t *tx_header = (dns_header_t *)tx_buffer;
        memset(tx_buffer, 0, sizeof(tx_buffer));

        tx_header->id = rx_header->id;
        // QR = 1 (odgovor), AA = 1 (autoritativan), RD = 1 (preuzet iz upita)
        tx_header->flags = htons(0x8400 | (ntohs(rx_header->flags) & 0x0100));
        tx_header->qdcount = rx_header->qdcount;
        tx_header->ancount = rx_header->qdcount; // Jedan odgovor po pitanju

        // Kopiraj pitanje iz rx u tx
        int tx_pos = sizeof(dns_header_t);
        int rx_pos = sizeof(dns_header_t);

        for (int q = 0; q < ntohs(rx_header->qdcount); q++) {
            // Kopiraj ime domene (niz labela)
            while (rx_pos < len && rx_buffer[rx_pos] != 0) {
                int label_len = rx_buffer[rx_pos];
                if (rx_pos + 1 + label_len > len) {
                    break;
                }
                memcpy(&tx_buffer[tx_pos], &rx_buffer[rx_pos], 1 + label_len);
                tx_pos += 1 + label_len;
                rx_pos += 1 + label_len;
            }
            if (rx_pos < len) {
                tx_buffer[tx_pos++] = 0; // null terminator za ime
                rx_pos++;
            }

            // Kopiraj Type i Class
            if (rx_pos + 4 <= len) {
                memcpy(&tx_buffer[tx_pos], &rx_buffer[rx_pos], 4);
                tx_pos += 4;
                rx_pos += 4;
            }
        }

        // Generiraj odgovor za svako pitanje (vraća A zapis za "192.168.4.1")
        rx_pos = sizeof(dns_header_t);

        for (int q = 0; q < ntohs(rx_header->qdcount); q++) {
            // Vrati pointer na ime (0xc000 + offset do početka pitanja)
            tx_buffer[tx_pos++] = 0xc0;
            tx_buffer[tx_pos++] = (uint8_t)rx_pos;

            // Tip A (1)
            tx_buffer[tx_pos++] = 0;
            tx_buffer[tx_pos++] = 1;
            // Klasa IN (1)
            tx_buffer[tx_pos++] = 0;
            tx_buffer[tx_pos++] = 1;
            // TTL: 60 sekundi (0x0000003c)
            tx_buffer[tx_pos++] = 0;
            tx_buffer[tx_pos++] = 0;
            tx_buffer[tx_pos++] = 0;
            tx_buffer[tx_pos++] = 60;
            // Duljina podatka: 4 bajta (IPv4 adresa)
            tx_buffer[tx_pos++] = 0;
            tx_buffer[tx_pos++] = 4;

            // Vrati IP adresu 192.168.4.1
            uint8_t ip[4] = {192, 168, 4, 1};
            memcpy(&tx_buffer[tx_pos], ip, 4);
            tx_pos += 4;

            // Pomakni se na sljedeće pitanje u rx bufferu
            while (rx_pos < len && rx_buffer[rx_pos] != 0) {
                rx_pos += 1 + rx_buffer[rx_pos];
            }
            rx_pos += 5; // skip null and type/class
        }

        sendto(s_dns_socket, tx_buffer, tx_pos, 0, (struct sockaddr *)&source_addr, socklen);
    }

    ESP_LOGI(TAG, "DNS server task završen.");
    vTaskDelete(NULL);
}

esp_err_t dns_server_start(void)
{
    if (s_dns_socket != -1) {
        return ESP_OK; // Već pokrenut
    }

    struct sockaddr_in dest_addr;
    dest_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port = htons(DNS_PORT);

    s_dns_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (s_dns_socket < 0) {
        ESP_LOGE(TAG, "Ne mogu kreirati socket: errno %d", errno);
        return ESP_FAIL;
    }

    int err = bind(s_dns_socket, (struct sockaddr *)&dest_addr, sizeof(dest_addr));
    if (err < 0) {
        ESP_LOGE(TAG, "Ne mogu bindati socket: errno %d", errno);
        close(s_dns_socket);
        s_dns_socket = -1;
        return ESP_FAIL;
    }

    if (xTaskCreate(dns_server_task, "dns_server", 4096, NULL, 3, &s_dns_task_handle) != pdPASS) {
        ESP_LOGE(TAG, "Ne mogu stvoriti DNS task");
        close(s_dns_socket);
        s_dns_socket = -1;
        return ESP_ERR_NO_MEM;
    }

    return ESP_OK;
}

void dns_server_stop(void)
{
    if (s_dns_socket != -1) {
        close(s_dns_socket);
        s_dns_socket = -1;
    }
    s_dns_task_handle = NULL;
}
