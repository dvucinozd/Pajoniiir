#include <assert.h>
#include <stdio.h>

unsigned review_submits;
unsigned review_live_transfers, review_hs_payload_allocations;
size_t review_live_payload_bytes;
unsigned review_ticks;
int review_fail_alloc_packets = -1, review_fail_submit_endpoint = -1;
int review_claim_result = 0;
/* Compile the actual stream owner against USB/RTOS boundary stubs. A critical
 * section hook deterministically pauses the producer before ring publication. */
#define CONTROLLER_USB_AUDIO_PC_TEST
#include "controller_usb_audio_stream.c"
#include "fixtures/s2_mk3_hs_config.h"
#include "fixtures/hercules_inpulse_500_fs_config.h"
#include "fixtures/ddj400_hs_as_config.h"
#include "fixtures/numark_touch_fs_as_config.h"
#include "fixtures/ddj_sb3_fs_config.h"

static unsigned packet_detail_count;
static uint32_t packet_details[4][4];
static unsigned packet_context_count;
static uint32_t packet_context[4][4];
void service_log_event(service_log_event_t event, service_log_severity_t severity,
                       uint8_t count, uint32_t a0, uint32_t a1,
                       uint32_t a2, uint32_t a3, const char *text)
{
    assert(event == SERVICE_LOG_UAC_PACKET_FAULT && severity == SERVICE_LOG_WARN);
    assert(count == 4u && text);
    const uint32_t args[] = {a0, a1, a2, a3};
    if (strcmp(text, "packet index/count/transfer/callback gap ticks") == 0) {
        assert(packet_context_count < 4u);
        memcpy(packet_context[packet_context_count++], args, sizeof(args));
        return;
    }
    assert(packet_detail_count < 4u);
    memcpy(packet_details[packet_detail_count++], args, sizeof(args));
}

static bool stop_before_publish;
static bool reject_second_writer;
static unsigned cleanup_deferrals;
static int16_t pcm[256 * 2];
static bool drain_on_delay;
static bool cancel_on_delay;
void review_delay(void)
{
    if (cancel_on_delay) { cancel_on_delay = false; controller_usb_audio_stream_request_stop(true); }
    if (drain_on_delay) {
        int16_t consumed[256u * 4u];
        controller_audio_ring_read(&s_ring, consumed, 256u, false);
    }
}
static const uint8_t descriptor[] = {
    9, 2, 36, 0, 1, 1, 0, 0x80, 50,
    9, 4, 1, 1, 1, 1, 2, 0, 0,
    11, 0x24, 2, 1, 4, 2, 16, 1, 0x44, 0xac, 0,
    7, 5, 1, 1, 0x68, 1, 1,
};

void review_enter_critical(void)
{
    if (reject_second_writer) {
        reject_second_writer = false;
        assert(controller_usb_audio_stream_write(pcm, pcm, 256, 44100) ==
               ESP_ERR_INVALID_STATE);
    }
    if (stop_before_publish) {
        stop_before_publish = false;
        const uint64_t converted = s_resampler.input_frames_seen;
        assert(converted > 0);
        controller_usb_audio_stream_request_stop(false);
        assert(!controller_usb_audio_stream_poll_cleanup());
        assert(!controller_usb_audio_stream_is_quiesced());
        assert(s_resampler.input_frames_seen == converted);
        assert(controller_usb_audio_stream_write(pcm, pcm, 256, 44100) ==
               ESP_ERR_INVALID_STATE);
        cleanup_deferrals++;
    }
}

static void start(void)
{
    const uint32_t previous_epoch =
        __atomic_load_n(&s_stream_epoch, __ATOMIC_ACQUIRE);
    assert(controller_usb_audio_stream_is_quiesced());
    assert(controller_usb_audio_stream_set_policy(true, false, false) == ESP_OK);
    assert(controller_usb_audio_stream_start((void *)1, (void *)2,
        descriptor, sizeof(descriptor), (void *)3, 10, 5) == ESP_OK);
    assert(s_configuring && !s_streaming);
    s_control->status = USB_TRANSFER_STATUS_COMPLETED;
    control_callback(s_control);
    assert(s_control_step == 2);
    control_callback(s_control);
    assert(s_streaming && !s_configuring);
    assert(__atomic_load_n(&s_stream_epoch, __ATOMIC_ACQUIRE) ==
           previous_epoch + 1u);
}

static void test_clocked_ring_keeps_underflow_runway(void)
{
    controller_audio_ring_t ring;
    int16_t storage[2048u * 4u] = {0};
    int16_t block[256u * 4u] = {0};
    assert(controller_audio_ring_init(&ring, storage, 2048u, 4u, 44100u));

    assert(controller_audio_ring_write(&ring, storage, 1024u) == 1024u);
    assert(controller_audio_ring_write_clocked(&ring, block, 256u) == 257u);
    assert(ring.clock_duplicated_frames == 1u);

    controller_audio_ring_reset(&ring, 44100u);
    assert(controller_audio_ring_write(&ring, storage, 1536u) == 1536u);
    assert(controller_audio_ring_write_clocked(&ring, block, 256u) == 256u);
    assert(ring.clock_trimmed_frames == 0u);
}

static void retire_transfers(void)
{
    for (unsigned i = 0; i < STREAM_TRANSFER_COUNT; ++i) {
        if (s_isoc_active[i]) {
            s_isoc[i]->status = USB_TRANSFER_STATUS_CANCELED;
            isoc_callback(s_isoc[i]);
        }
    }
    if (s_feedback_active) {
        s_feedback->status = USB_TRANSFER_STATUS_CANCELED;
        feedback_callback(s_feedback);
    }
}

static void finish_cleanup(void)
{
    if (!controller_usb_audio_stream_poll_cleanup()) {
        assert(s_control_active && s_control_step == 3u);
        assert(((usb_setup_packet_t *)s_control->data_buffer)->wValue == 0);
        s_control->status = USB_TRANSFER_STATUS_COMPLETED;
        control_callback(s_control);
        assert(controller_usb_audio_stream_poll_cleanup());
    }
}

static void test_packet_loss(void)
{
    start();
    usb_transfer_t *t = s_isoc[0];
    t->status = USB_TRANSFER_STATUS_COMPLETED;
    for (int i = 0; i < 4; ++i) {
        t->isoc_packet_desc[i].num_bytes = 352;
        t->isoc_packet_desc[i].actual_num_bytes = 352;
        t->isoc_packet_desc[i].status = USB_TRANSFER_STATUS_COMPLETED;
    }
    t->isoc_packet_desc[1].status = USB_TRANSFER_STATUS_SKIPPED;
    t->isoc_packet_desc[1].actual_num_bytes = 0;
    t->isoc_packet_desc[2].status = USB_TRANSFER_STATUS_ERROR;
    t->isoc_packet_desc[3].actual_num_bytes = 336;
    const unsigned submits = review_submits;
    isoc_callback(t);
    controller_usb_audio_stream_stats_t stats;
    controller_usb_audio_stream_get_stats(&stats);
    assert(stats.stream_epoch == 1u);
    assert(stats.packet_failures == 3);
    assert(stats.packet_lost_frames == 90); /* 44 + 44 + 2 */
    assert(packet_detail_count == 3u);
    assert(packet_context_count == 3u);
    for (unsigned i = 0; i < 3u; ++i) {
        assert(packet_context[i][0] == i + 1u);
        assert(packet_context[i][1] == 4u && packet_context[i][2] == 0u);
    }
    assert(packet_details[0][0] == USB_TRANSFER_STATUS_SKIPPED);
    assert(packet_details[0][1] == 0u && packet_details[0][2] == 352u);
    assert(packet_details[1][0] == USB_TRANSFER_STATUS_ERROR);
    assert(packet_details[2][0] == USB_TRANSFER_STATUS_COMPLETED);
    assert(packet_details[2][1] == 336u);
    assert(stats.transfer_failures == 0 && !stats.faulted);
    assert(review_submits == submits + 1 && stats.streaming);
    for (int i = 0; i < 4; ++i) {
        t->isoc_packet_desc[i].status = USB_TRANSFER_STATUS_COMPLETED;
        t->isoc_packet_desc[i].actual_num_bytes = t->isoc_packet_desc[i].num_bytes;
    }
    isoc_callback(t);
    assert(s_packet_failures == 3 && s_packet_lost_frames == 90);
    assert(packet_detail_count == 3u); /* Healthy completions do not log. */
    review_ticks += 7u;
    t->isoc_packet_desc[0].status = USB_TRANSFER_STATUS_SKIPPED;
    t->isoc_packet_desc[0].actual_num_bytes = 0;
    const uint32_t wanted = (uint32_t)t->isoc_packet_desc[0].num_bytes;
    isoc_callback(t);
    assert(packet_detail_count == 4u && packet_details[3][2] == wanted);
    assert(packet_details[3][3] == 7u);
    assert(packet_context_count == 4u && packet_context[3][0] == 0u &&
           packet_context[3][3] == 7u);
    t->isoc_packet_desc[0].status = USB_TRANSFER_STATUS_SKIPPED;
    t->isoc_packet_desc[0].actual_num_bytes = 0;
    isoc_callback(t);
    assert(packet_detail_count == 4u && s_packet_failures > 4u);
    assert(packet_context_count == 4u); /* Both detail records remain bounded. */
    t->status = USB_TRANSFER_STATUS_ERROR;
    isoc_callback(t);
    assert(s_faulted && s_transfer_failures == 1);
    assert(!controller_usb_audio_stream_poll_cleanup());
    retire_transfers();
    finish_cleanup();
    assert(controller_usb_audio_stream_is_quiesced());
}

static void test_stop_during_write(uint32_t rate)
{
    start();
    /* Model completions with no outstanding USB ownership, so only the
     * preempted producer can prevent cleanup from destroying the resampler. */
    memset(s_isoc_active, 0, sizeof(s_isoc_active));
    reject_second_writer = true;
    stop_before_publish = true;
    assert(controller_usb_audio_stream_write(pcm, pcm, 256, rate) == ESP_OK);
    assert(s_resampler.input_frames_seen == 256);
    assert(!controller_usb_audio_stream_is_quiesced());
    finish_cleanup();
    assert(controller_usb_audio_stream_is_quiesced());
    assert(s_ring.queued_frames == 0 && s_resampler.source_rate == 0);
    assert(controller_usb_audio_stream_write(pcm, pcm, 256, rate) == ESP_ERR_INVALID_STATE);
}

static void test_generic_pacing_and_timeout(void)
{
    uint8_t d[sizeof(descriptor) + 14u];
    memcpy(d, descriptor, sizeof(descriptor));
    const uint8_t general[] = {7, 0x24, 1, 1, 1, 1, 0};
    memcpy(d + sizeof(descriptor), general, sizeof(general));
    const uint8_t cs_endpoint[] = {7, 0x25, 1, 1, 0, 0, 0};
    memcpy(d + sizeof(descriptor) + sizeof(general), cs_endpoint, sizeof(cs_endpoint));
    d[2] = sizeof(d);
    d[23] = 3; d[24] = 24; d[26] = 0x80; d[27] = 0xbb;
    d[32] = 9; d[33] = 0x40; d[34] = 2;
    assert(controller_usb_audio_stream_set_policy(false, true, false) == ESP_OK);
    assert(controller_usb_audio_stream_start((void *)1, (void *)2,
        d, sizeof(d), (void *)3, 10, 5) == ESP_OK);
    s_control->status = USB_TRANSFER_STATUS_COMPLETED;
    control_callback(s_control);
    assert(s_control->data_buffer[8] == 0x80 && s_control->data_buffer[9] == 0xbb);
    control_callback(s_control);
    assert(s_target_rate == 48000 && s_format.bytes_per_sample == 3);
    for (unsigned i = 0; i < 256u * 2u; ++i) pcm[i] = i % 2 ? -32768 : 32767;
    for (unsigned i = 0; i < 2000; ++i) {
        assert(controller_usb_audio_stream_write(pcm, pcm, 256, 48000) == ESP_OK);
        int16_t output[256 * 4];
        assert(controller_audio_ring_read(&s_ring, output, 256, false) == 256);
        assert(output[0] == 32767 && output[1] == -32768);
        assert(output[2] == 32767 && output[3] == -32768);
    }
    assert(s_ring.clock_duplicated_frames == 0 && s_ring.clock_trimmed_frames == 0);
    int16_t cue[256u * 2u];
    for (unsigned i = 0; i < 256u; ++i) { cue[i * 2u] = 1000; cue[i * 2u + 1u] = -2000; }
    assert(controller_usb_audio_stream_write(pcm, cue, 256, 48000) == ESP_OK);
    usb_transfer_t *packet = s_isoc[0];
    s_isoc_active[0] = false; /* Model completion ownership before re-submission. */
    assert(prepare_and_submit(packet) == ESP_OK);
    const uint8_t routed[] = {0, 0xff, 0x7f, 0, 0, 0x80, 0, 0xe8, 3, 0, 0x30, 0xf8};
    assert(packet->isoc_packet_desc[0].num_bytes == 576);
    assert(memcmp(packet->data_buffer, routed, sizeof(routed)) == 0);
    int16_t converted[512u * 4u];
    controller_audio_ring_read(&s_ring, converted, s_ring.queued_frames, false);
    uint64_t generated = 0;
    for (unsigned i = 0; i < 2000; ++i) {
        assert(controller_usb_audio_stream_write(pcm, cue, 256, 44100) == ESP_OK);
        uint32_t queued = s_ring.queued_frames;
        assert(queued <= 512u);
        generated += controller_audio_ring_read(&s_ring, converted, queued, false);
        assert(converted[0] == 32767 && converted[1] == -32768);
        assert(converted[2] == 1000 && converted[3] == -2000);
    }
    const uint64_t expected = (uint64_t)2000u * 256u * 48000u / 44100u;
    assert(generated >= expected - 2u && generated <= expected + 2u);
    assert(s_ring.clock_duplicated_frames == 0 && s_ring.clock_trimmed_frames == 0);
    int16_t fill[1024u * 4u] = {0};
    assert(controller_audio_ring_write(&s_ring, fill, 1024) == 1024);
    unsigned ticks = review_ticks;
    drain_on_delay = true;
    assert(controller_usb_audio_stream_write(pcm, pcm, 256, 48000) == ESP_OK);
    drain_on_delay = false;
    assert(review_ticks > ticks && s_ring.queued_frames == 1024);
    ticks = review_ticks;
    assert(controller_usb_audio_stream_write(pcm, pcm, 256, 48000) == ESP_ERR_TIMEOUT);
    assert(review_ticks - ticks == 100 && s_ring.queued_frames == 1024);
    cancel_on_delay = true;
    assert(controller_usb_audio_stream_write(pcm, pcm, 256, 48000) == ESP_ERR_INVALID_STATE);
    retire_transfers();
    assert(controller_usb_audio_stream_poll_cleanup());
    assert(controller_usb_audio_stream_set_policy(false, true, false) == ESP_OK);
    assert(controller_usb_audio_stream_start((void *)1, (void *)2,
        d, sizeof(d), (void *)3, 10, 5) == ESP_OK);
    review_ticks += 1000;
    assert(!controller_usb_audio_stream_poll_cleanup());
    assert(s_control_expired && s_control_active && s_faulted);
    assert(controller_usb_audio_stream_needs_recovery());
    s_control->status = USB_TRANSFER_STATUS_NO_DEVICE;
    control_callback(s_control);
    assert(controller_usb_audio_stream_poll_cleanup());
    assert(controller_usb_audio_stream_is_quiesced());
    assert(controller_usb_audio_stream_set_policy(true, false, false) == ESP_OK);
}

static void test_generic_fixed_16bit(uint32_t rate)
{
    uint8_t d[sizeof(descriptor) + 7u];
    memcpy(d, descriptor, sizeof(descriptor));
    const uint8_t general[] = {7, 0x24, 1, 1, 1, 1, 0};
    memcpy(d + sizeof(descriptor), general, sizeof(general));
    d[2] = sizeof(d); d[32] = 9;
    d[26] = (uint8_t)rate; d[27] = (uint8_t)(rate >> 8); d[28] = 0;
    d[33] = 0x80; d[34] = 1; /* 384 bytes covers both rates. */
    assert(controller_usb_audio_stream_set_policy(false, true, false) == ESP_OK);
    flx4_uac_playback_format_t format;
    assert(!select_stream_format(descriptor, sizeof(descriptor), &format));
    d[sizeof(descriptor) + 5u] = 3; /* IEEE float is not integer PCM. */
    assert(!select_stream_format(d, sizeof(d), &format));
    d[sizeof(descriptor) + 5u] = 1;
    assert(controller_usb_audio_stream_start((void *)1, (void *)2,
        d, sizeof(d), (void *)3, 10, 5) == ESP_OK);
    s_control->status = USB_TRANSFER_STATUS_COMPLETED;
    control_callback(s_control);
    assert(s_streaming && !s_control_active && s_control_step == 0u);
    assert(s_target_rate == rate && s_format.bits_per_sample == 16);
    for (unsigned i = 0; i < 256u * 2u; ++i) pcm[i] = i % 2u ? -32768 : 32767;
    assert(controller_usb_audio_stream_write(pcm, NULL, 256, rate) == ESP_OK);
    int16_t sample[4];
    assert(controller_audio_ring_read(&s_ring, sample, 1, false) == 1);
    assert(sample[0] == 32767 && sample[1] == -32768 && sample[2] == sample[0] && sample[3] == sample[1]);
    controller_usb_audio_stream_request_stop(true);
    retire_transfers();
    assert(controller_usb_audio_stream_poll_cleanup());
    assert(controller_usb_audio_stream_set_policy(true, false, false) == ESP_OK);
}

static void test_uac2(void)
{
    flx4_uac_playback_format_t fmt;
    assert(controller_uac2_parse(s2_hs_config, sizeof(s2_hs_config), &fmt));
    assert(fmt.uac2 && fmt.interface_num == 1 && fmt.alternate_setting == 1);
    assert(fmt.endpoint_addr == 1 && fmt.max_packet_size == 112);
    assert(fmt.feedback_endpoint == 0x81 && fmt.feedback_interval == 4);
    assert(fmt.clock_source == 0x11 && fmt.control_interface == 0);
    assert(fmt.channels == 4 && fmt.bits_per_sample == 24 && fmt.bytes_per_sample == 4);
    for (size_t n = 0; n < sizeof(s2_hs_config); ++n)
        assert(!controller_uac2_parse(s2_hs_config, n, &fmt));
    uint8_t corrupt[sizeof(s2_hs_config)];
    memcpy(corrupt, s2_hs_config, sizeof(corrupt)); corrupt[sizeof(corrupt)-1] = 1;
    assert(!controller_uac2_parse(corrupt, sizeof(corrupt), &fmt));
    memcpy(corrupt, s2_hs_config, sizeof(corrupt));
    for (size_t off = 9; corrupt[off]; off += corrupt[off]) {
        if (corrupt[off+1] == 5 && corrupt[off+2] == 0x81) {
            corrupt[off+3] = 1; break; /* Missing explicit feedback. */
        }
    }
    assert(!controller_uac2_parse(corrupt, sizeof(corrupt), &fmt));
    const uint8_t range[] = {1,0, 0x80,0xbb,0,0, 0x80,0xbb,0,0, 0,0,0,0};
    assert(controller_uac2_range_supports(range, sizeof(range), 48000));
    assert(!controller_uac2_range_supports(range, sizeof(range), 44100));
    assert(!controller_uac2_range_supports(range, sizeof(range)-1, 48000));
    uint32_t feedback;
    const uint8_t nominal[] = {0,0,6,0};
    assert(controller_uac2_feedback(nominal, 4, 48000, &feedback) && feedback == 6u << 16);
    assert(!controller_uac2_feedback(nominal, 3, 48000, &feedback));
    const uint8_t wrong[] = {0xff,0xff,0xff,0xff};
    assert(!controller_uac2_feedback(wrong, 4, 48000, &feedback));
    const int16_t values[] = {32767, -32768, 1, -1};
    const uint8_t expected[] = {0,0,0xff,0x7f, 0,0,0,0x80, 0,0,1,0, 0,0,0xff,0xff};
    uint8_t packed[16];
    assert(controller_uac2_pack_pcm(packed, 16, values, 1, 4, 24));
    assert(memcmp(packed, expected, 16) == 0);
    assert(!controller_uac2_pack_pcm(packed, 15, values, 1, 4, 24));

    assert(controller_usb_audio_stream_is_quiesced());
    assert(controller_usb_audio_stream_set_policy(false, false, false) == ESP_OK);
    assert(!select_stream_format(s2_hs_config, sizeof(s2_hs_config), &fmt));
    assert(controller_usb_audio_stream_set_policy(false, false, true) == ESP_OK);
    assert(controller_usb_audio_stream_start((void *)1, (void *)2,
        s2_hs_config, sizeof(s2_hs_config), (void *)3, 10, 5) == ESP_OK);
    assert(s_control_step == 4 && s_configuring);
    usb_setup_packet_t *setup = (usb_setup_packet_t *)s_control->data_buffer;
    assert(setup->bmRequestType == 0xa1 && setup->bRequest == 2 &&
           setup->wValue == 0x0100 && setup->wIndex == 0x1100 && setup->wLength == 2);
    memcpy(s_control->data_buffer + 8, range, 2);
    s_control->actual_num_bytes = 10;
    s_control->status = USB_TRANSFER_STATUS_COMPLETED;
    control_callback(s_control);
    assert(s_control_step == 7 && setup->wLength == sizeof(range));
    memcpy(s_control->data_buffer + 8, range, sizeof(range));
    s_control->actual_num_bytes = 8 + sizeof(range);
    s_control->status = USB_TRANSFER_STATUS_COMPLETED;
    control_callback(s_control);
    assert(s_control_step == 5 && setup->wLength == 4 && setup->bmRequestType == 0x21);
    assert(s_control->data_buffer[8] == 0x80 && s_control->data_buffer[9] == 0xbb);
    s_control->actual_num_bytes = 12;
    control_callback(s_control);
    assert(s_control_step == 6 && setup->bmRequestType == 0xa1 && setup->bRequest == 1);
    memcpy(s_control->data_buffer + 8, range + 2, 4);
    s_control->actual_num_bytes = 12;
    control_callback(s_control);
    assert(s_control_step == 8 && setup->wValue == 0x0200 && setup->wLength == 1);
    s_control->data_buffer[8] = 1;
    s_control->actual_num_bytes = 9;
    control_callback(s_control);
    assert(s_control_step == 1 && setup->bRequest == 11);
    s_control->actual_num_bytes = 8;
    control_callback(s_control);
    assert(s_streaming && s_feedback_active && s_target_rate == 48000);
    for (unsigned i = 0; i < STREAM_TRANSFER_COUNT; ++i) {
        assert(s_isoc[i]->num_isoc_packets == 56 && s_isoc[i]->num_bytes == 5376);
        assert(s_isoc[i]->data_buffer_size == 6272);
        assert(s_isoc[i]->num_isoc_packets <= 64 - 3);
        for (unsigned p = 0; p < 56; ++p) assert(s_isoc[i]->isoc_packet_desc[p].num_bytes == 96);
    }
    memcpy(s_feedback->data_buffer, nominal, 4);
    s_feedback->status = USB_TRANSFER_STATUS_COMPLETED;
    s_feedback->isoc_packet_desc[0].status = USB_TRANSFER_STATUS_COMPLETED;
    s_feedback->isoc_packet_desc[0].actual_num_bytes = 4;
    uint32_t before = s_feedback_updates;
    feedback_callback(s_feedback);
    assert(s_feedback_active && s_feedback_updates == before + 1);
    memcpy(s_feedback->data_buffer, wrong, 4);
    before = s_feedback_invalid;
    feedback_callback(s_feedback);
    assert(s_feedback_active && s_feedback_invalid == before + 1);
    /* Fractional HS feedback must carry into future packets without bias. */
    s_feedback_q16 = (6u << 16) + 32768u;
    s_feedback_phase = 0;
    assert(prepare_and_submit(s_isoc[0]) == ESP_OK);
    for (unsigned p = 0; p < 56; ++p)
        assert(s_isoc[0]->isoc_packet_desc[p].num_bytes == (p % 2 ? 112 : 96));
    review_ticks += 500;
    assert(!controller_usb_audio_stream_poll_cleanup() && s_faulted);
    retire_transfers(); finish_cleanup();
    assert(controller_usb_audio_stream_is_quiesced() && !s_feedback);

    /* Malformed range cannot reach SET_CUR, stream priming or feedback. */
    assert(controller_usb_audio_stream_start((void *)1, (void *)2,
        s2_hs_config, sizeof(s2_hs_config), (void *)3, 10, 5) == ESP_OK);
    s_control->actual_num_bytes = 9;
    s_control->status = USB_TRANSFER_STATUS_COMPLETED;
    control_callback(s_control);
    assert(s_faulted && !s_streaming && !s_feedback);
    assert(controller_usb_audio_stream_fault_isolatable());
    controller_usb_audio_stream_stats_t failed;
    controller_usb_audio_stream_get_stats(&failed);
    assert(failed.configuration_fault && failed.fault_control_step == 4);
    assert(failed.last_control_status == USB_TRANSFER_STATUS_COMPLETED && failed.last_control_bytes == 1);
    s_control_expired = true;
    assert(!controller_usb_audio_stream_fault_isolatable());
    s_control_expired = false;
    s_device_gone = true;
    assert(!controller_usb_audio_stream_fault_isolatable());
    s_device_gone = false;
    s_high_speed = false;
    assert(!controller_usb_audio_stream_fault_isolatable());
    s_high_speed = true;
    finish_cleanup();
    assert(controller_usb_audio_stream_is_quiesced());
    controller_usb_audio_stream_get_stats(&failed);
    assert(failed.configuration_fault && failed.fault_control_step == 4);
    /* An invalid clock cannot start endpoints, even after the settle budget. */
    assert(controller_usb_audio_stream_start((void *)1, (void *)2,
        s2_hs_config, sizeof(s2_hs_config), (void *)3, 10, 5) == ESP_OK);
    s_control_step = 8;
    s_control->data_buffer[8] = 0;
    s_control->actual_num_bytes = 9;
    s_control->status = USB_TRANSFER_STATUS_COMPLETED;
    review_ticks += 500;
    control_callback(s_control);
    assert(s_faulted && !s_streaming && !s_feedback);
    finish_cleanup();
    assert(controller_usb_audio_stream_is_quiesced());
    puts("PASS UAC2 captured HS descriptor, clock RANGE/CUR, feedback, packing and teardown");
}

static void start_clock_readback_ranges(bool include_44100)
{
    uint8_t range[] = {1, 0, 0x80, 0xbb, 0, 0, 0x80, 0xbb, 0, 0, 0, 0, 0, 0};
    if (include_44100) {
        range[2] = 0x44; range[3] = 0xac;
        range[10] = 0x3c; range[11] = 0x0f; /* 3900-Hz step: 44100 and 48000. */
    }
    assert(controller_usb_audio_stream_start((void *)1, (void *)2,
        s2_hs_config, sizeof(s2_hs_config), (void *)3, 10, 5) == ESP_OK);
    memcpy(s_control->data_buffer + 8, range, 2);
    s_control->actual_num_bytes = 10;
    s_control->status = USB_TRANSFER_STATUS_COMPLETED;
    control_callback(s_control);
    memcpy(s_control->data_buffer + 8, range, sizeof(range));
    s_control->actual_num_bytes = 8 + sizeof(range);
    control_callback(s_control);
    assert(s_control_step == 5);
    s_control->actual_num_bytes = 12;
    control_callback(s_control);
    assert(s_control_step == 6 && s_control_active);
}

static void start_clock_readback(void) { start_clock_readback_ranges(false); }

static void clock_reply(uint32_t rate)
{
    for (unsigned i = 0; i < 4; ++i) s_control->data_buffer[8 + i] = rate >> (8u * i);
    s_control->actual_num_bytes = 12;
    control_callback(s_control);
}

static void test_clock_settle(void)
{
    /* Exact bench failure: SET_CUR succeeds, first GET_CUR still says 44100.
     * A later correct reply may advance; no USB callback delay or premature ISO. */
    review_ticks = UINT32_MAX - 20u;
    start_clock_readback();
    clock_reply(44100);
    assert(!s_faulted && !s_streaming && !s_control_active && s_clock_retry_step == 6);
    review_ticks += 49;
    assert(!controller_usb_audio_stream_poll_cleanup() && !s_control_active);
    review_ticks++;
    assert(!controller_usb_audio_stream_poll_cleanup() && s_control_active);
    assert(s_clock_readback_retries == 1);
    clock_reply(48000);
    assert(s_control_step == 8 && s_clock_reported_rate == 48000);
    s_control->actual_num_bytes = 9;
    s_control->data_buffer[8] = 0;
    control_callback(s_control);
    assert(s_clock_retry_step == 8 && !s_faulted && !s_feedback);
    review_ticks += 50;
    assert(!controller_usb_audio_stream_poll_cleanup() && s_control_active);
    s_control->data_buffer[8] = 1;
    control_callback(s_control);
    assert(s_control_step == 1 && s_clock_readback_retries == 2);
    s_control->actual_num_bytes = 8;
    control_callback(s_control);
    assert(s_streaming && s_feedback_active);
    controller_usb_audio_stream_request_stop(false);
    retire_transfers(); finish_cleanup();

    start_clock_readback();
    clock_reply(44100);
    review_ticks += 500;
    assert(!controller_usb_audio_stream_poll_cleanup());
    assert(s_faulted && s_fault_control_step == 6 && !s_streaming && !s_feedback);
    finish_cleanup();

    start_clock_readback();
    clock_reply(44100);
    controller_usb_audio_stream_request_stop(true);
    review_ticks += 50;
    assert(controller_usb_audio_stream_poll_cleanup());
    assert(!s_clock_retry_step && !s_control && s_clock_readback_retries == 0);
    start_clock_readback();
    assert(s_clock_reported_rate == 0 && s_clock_readback_retries == 0);
    s_control->actual_num_bytes = 11; /* Truncated reply is not a settling clock. */
    control_callback(s_control);
    assert(s_faulted && !s_clock_retry_step);
    finish_cleanup();
    puts("PASS UAC2 bounded asynchronous clock settle, wrap, timeout and disconnect");
}

static void test_uac2_44100_fallback(void)
{
    start_clock_readback_ranges(true);
    clock_reply(44100);
    review_ticks += 500;
    assert(!controller_usb_audio_stream_poll_cleanup());
    assert(s_control_step == 5 && s_control_active && !s_faulted && !s_streaming);
    assert(s_target_rate == 44100 && s_ring.sample_rate == 44100);
    assert(s_control->data_buffer[8] == 0x44 && s_control->data_buffer[9] == 0xac);
    s_control->actual_num_bytes = 12;
    control_callback(s_control);
    clock_reply(44100);
    assert(s_control_step == 8);
    s_control->data_buffer[8] = 1; s_control->actual_num_bytes = 9;
    control_callback(s_control);
    s_control->actual_num_bytes = 8; control_callback(s_control);
    assert(s_streaming && s_target_rate == 44100);
    const uint8_t nominal[] = {0x33, 0x83, 5, 0};
    uint32_t feedback;
    assert(controller_uac2_feedback(nominal, 4, 44100, &feedback));
    assert(!controller_uac2_feedback(nominal, 4, 48000, &feedback));
    const uint8_t rate48[] = {0, 0, 6, 0};
    assert(!controller_uac2_feedback(rate48, 4, 44100, &feedback));
    uint64_t frames = 0;
    s_feedback_phase = 0;
    for (unsigned i = 0; i < 1000; ++i) {
        s_isoc_active[0] = false;
        assert(prepare_and_submit(s_isoc[0]) == ESP_OK);
        for (unsigned p = 0; p < 56; ++p) {
            int bytes = s_isoc[0]->isoc_packet_desc[p].num_bytes;
            assert(bytes == 80 || bytes == 96);
            frames += bytes / 16;
        }
    }
    assert(frames >= 308699 && frames <= 308700); /* Seven seconds of HS packets. */
    assert(controller_usb_audio_stream_write(pcm, pcm, 256, 48000) == ESP_OK);
    assert(s_resampler.source_rate == 48000 && s_resampler.target_rate == 44100);
    controller_usb_audio_stream_request_stop(false);
    retire_transfers(); finish_cleanup();

    /* A fallback which also refuses its requested rate must fail, not loop. */
    start_clock_readback_ranges(true); clock_reply(44100);
    review_ticks += 500; assert(!controller_usb_audio_stream_poll_cleanup());
    s_control->actual_num_bytes = 12; control_callback(s_control);
    clock_reply(48000); review_ticks += 500;
    assert(!controller_usb_audio_stream_poll_cleanup());
    assert(s_faulted && !s_streaming && !s_feedback);
    finish_cleanup();
    puts("PASS UAC2 advertised 44.1-kHz fallback, exact readback, fractional HS cadence and resampling");
}

static void test_prime_fault_evidence(void)
{
    const int rejected[] = {ESP_ERR_NOT_SUPPORTED, ESP_ERR_NO_MEM};
    for (unsigned n = 0; n < sizeof(rejected) / sizeof(rejected[0]); ++n) {
        assert(controller_usb_audio_stream_is_quiesced());
        for (unsigned i = 0; i < STREAM_TRANSFER_COUNT; ++i) assert(release_idle_isoc(i));
        assert(controller_usb_audio_stream_set_policy(false, false, false) == ESP_OK);
        const unsigned allocations = review_live_transfers;
        review_claim_result = rejected[n];
        assert(controller_usb_audio_stream_start((void *)1, (void *)2,
            hercules_fs_config, sizeof(hercules_fs_config), (void *)3, 10, 5) == rejected[n]);
        controller_usb_audio_stream_stats_t stats;
        controller_usb_audio_stream_get_stats(&stats);
        assert(stats.prime_fault_stage == 5 && stats.prime_fault_index == 1);
        assert(stats.prime_fault_code == rejected[n]);
        assert(!stats.claimed && !stats.configuring && !stats.streaming);
        assert(review_live_transfers == allocations);
        assert(controller_usb_audio_stream_poll_cleanup());
        controller_usb_audio_stream_get_stats(&stats);
        assert(stats.prime_fault_stage == 5 && stats.prime_fault_code == rejected[n]);
        review_claim_result = ESP_OK;
    }
    assert(controller_usb_audio_stream_set_policy(false, false, true) == ESP_OK);
    for (unsigned stage = 1; stage <= 4; ++stage) {
        /* These cases model a cold allocation, not a retained HS pool. */
        assert(controller_usb_audio_stream_is_quiesced());
        for (unsigned i = 0; i < STREAM_TRANSFER_COUNT; ++i) assert(release_idle_isoc(i));
        start_clock_readback(); clock_reply(48000);
        s_control->data_buffer[8] = 1; s_control->actual_num_bytes = 9;
        control_callback(s_control);
        assert(s_control_step == 1);
        review_fail_alloc_packets = stage == 1 ? 1 : stage == 3 ? 56 : -1;
        review_fail_submit_endpoint = stage == 2 ? 0x81 : stage == 4 ? 1 : -1;
        s_control->actual_num_bytes = 8; control_callback(s_control);
        assert(s_faulted && !s_streaming && !s_control_active);
        controller_usb_audio_stream_stats_t stats;
        controller_usb_audio_stream_get_stats(&stats);
        assert(stats.prime_fault_stage == stage && stats.prime_fault_index == 0);
        assert(stats.prime_fault_code == ((stage & 1) ? ESP_ERR_NO_MEM : ESP_ERR_INVALID_STATE));
        review_fail_alloc_packets = review_fail_submit_endpoint = -1;
        retire_transfers(); finish_cleanup();
        controller_usb_audio_stream_get_stats(&stats);
        assert(stats.prime_fault_stage == stage); /* Teardown preserves the evidence. */
    }
    start_clock_readback();
    assert(s_prime_fault_stage == 0 && s_prime_fault_code == ESP_OK);
    controller_usb_audio_stream_request_stop(true);
    s_control->status = USB_TRANSFER_STATUS_NO_DEVICE;
    control_callback(s_control);
    assert(controller_usb_audio_stream_poll_cleanup());
    puts("PASS UAC startup alloc/submit fault provenance and ownership cleanup");
}

static void test_hs_reconnect_under_heap_pressure(void)
{
    usb_transfer_t *pool[STREAM_TRANSFER_COUNT] = {0};
    unsigned initial_hs_allocations = 0;
    size_t retained_payload_bytes = 0;
    for (unsigned cycle = 0; cycle < 30; ++cycle) {
        start_clock_readback(); clock_reply(48000);
        s_control->data_buffer[8] = 1; s_control->actual_num_bytes = 9;
        control_callback(s_control);
        s_control->actual_num_bytes = 8; control_callback(s_control);
        assert(s_streaming && s_prime_fault_stage == 0);
        for (unsigned i = 0; i < STREAM_TRANSFER_COUNT; ++i) {
            if (!cycle) pool[i] = s_isoc[i];
            assert(s_isoc[i] == pool[i] && s_isoc[i]->device_handle == (void *)2);
        }
        if (!cycle) {
            initial_hs_allocations = review_hs_payload_allocations;
            for (unsigned i = 0; i < STREAM_TRANSFER_COUNT; ++i)
                retained_payload_bytes += pool[i]->data_buffer_size;
        }
        /* Only control and feedback may allocate again; OUT reservations
         * remain bounded even when reconnect alternates gone/soft-stop. */
        assert(review_hs_payload_allocations == initial_hs_allocations);
        assert(review_live_transfers == STREAM_TRANSFER_COUNT + 2u);
        assert(review_live_payload_bytes == retained_payload_bytes +
               s_control->data_buffer_size + s_feedback->data_buffer_size);
        controller_usb_audio_stream_request_stop(cycle % 2u == 0u);
        assert(!controller_usb_audio_stream_poll_cleanup());
        assert(!controller_usb_audio_stream_is_quiesced());
        /* No reuse or detach while the old USB callbacks still own the URBs. */
        assert(s_isoc[0]->device_handle == (void *)2);
        retire_transfers(); finish_cleanup();
        assert(controller_usb_audio_stream_is_quiesced() && !s_feedback && !s_control);
        assert(review_live_transfers == STREAM_TRANSFER_COUNT);
        assert(review_live_payload_bytes == retained_payload_bytes);
        for (unsigned i = 0; i < STREAM_TRANSFER_COUNT; ++i) {
            assert(s_isoc[i] == pool[i] && !s_isoc[i]->device_handle);
            assert(!s_isoc[i]->num_bytes && !s_isoc[i]->bEndpointAddress);
            for (int p = 0; p < 56; ++p) assert(!s_isoc[i]->isoc_packet_desc[p].actual_num_bytes);
        }
        /* Exact bench failure: no new 56-packet DMA buffer can be allocated. */
        review_fail_alloc_packets = 56;
    }
    review_fail_alloc_packets = -1;
    /* A larger, valid HS endpoint must not reuse an undersized reservation. */
    uint8_t larger[sizeof(s2_hs_config)];
    memcpy(larger, s2_hs_config, sizeof(larger));
    for (size_t off = 9; larger[off]; off += larger[off]) {
        if (larger[off + 1] == 5 && larger[off + 2] == 1) {
            larger[off + 4] = 128; larger[off + 5] = 0;
            break;
        }
    }
    assert(controller_usb_audio_stream_start((void *)1, (void *)2,
        larger, sizeof(larger), (void *)3, 10, 5) == ESP_OK);
    assert(s_format.max_packet_size == 128);
    for (unsigned i = 0; i < STREAM_TRANSFER_COUNT; ++i) assert(!s_isoc[i]);
    controller_usb_audio_stream_request_stop(true);
    s_control->status = USB_TRANSFER_STATUS_CANCELED; control_callback(s_control);
    assert(controller_usb_audio_stream_poll_cleanup());
    /* Recreate a HS pool before exercising the FS switch. */
    start_clock_readback(); clock_reply(48000);
    s_control->data_buffer[8] = 1; s_control->actual_num_bytes = 9;
    control_callback(s_control);
    s_control->actual_num_bytes = 8; control_callback(s_control);
    assert(s_streaming);
    controller_usb_audio_stream_request_stop(true);
    retire_transfers(); finish_cleanup();
    /* Switching to FS releases the HS reservation and uses four-packet URBs. */
    start();
    for (unsigned i = 0; i < STREAM_TRANSFER_COUNT; ++i)
        assert(s_isoc[i]->num_isoc_packets == 4 && s_isoc[i]->data_buffer_size == 1440);
    controller_usb_audio_stream_request_stop(true);
    retire_transfers(); finish_cleanup();
    for (unsigned i = 0; i < STREAM_TRANSFER_COUNT; ++i) assert(!s_isoc[i]);
    assert(review_live_transfers == 0 && review_live_payload_bytes == 0);
    puts("PASS 30 host HS reconnects: bounded retained payload, no new OUT allocation, FS releases all transfers");
}

static void test_fs_48000_fast_feedback(uint8_t sample_bytes)
{
    /* Synthetic FS asynchronous endpoint; this does not change the captured
     * Hercules descriptor, whose actual advertised rate remains 44.1 kHz. */
    uint8_t config[sizeof(hercules_fs_config)];
    memcpy(config, hercules_fs_config, sizeof(config));
    const unsigned packet_bytes = 50u * 4u * sample_bytes;
    for (size_t off = 9; off < sizeof(config); off += config[off]) {
        if (config[off] == 11 && config[off+1] == 0x24 &&
            config[off+2] == 2 && config[off+4] == 4) {
            config[off+5] = sample_bytes; config[off+6] = sample_bytes * 8u;
            config[off+8] = 0x80; config[off+9] = 0xbb; config[off+10] = 0;
        }
        if (config[off+1] == 5 && config[off+2] == 1) {
            config[off+4] = (uint8_t)packet_bytes;
            config[off+5] = (uint8_t)(packet_bytes >> 8);
        }
    }
    assert(controller_usb_audio_stream_is_quiesced());
    assert(controller_usb_audio_stream_set_policy(false, true, false) == ESP_OK);
    assert(controller_usb_audio_stream_start((void*)1, (void*)2,
        config, sizeof(config), (void*)3, 10, 5) == ESP_OK);
    s_control->status = USB_TRANSFER_STATUS_COMPLETED;
    control_callback(s_control);
    assert(s_streaming && s_target_rate == 48000 && s_format.requires_feedback);
    const uint32_t q14 = (48u << 14) + 164u; /* approximately 48.01 frames/ms */
    const uint8_t feedback[] = {(uint8_t)q14, (uint8_t)(q14 >> 8), (uint8_t)(q14 >> 16)};
    assert(flx4_uac_feedback_q16(&s_format, feedback, sizeof(feedback), 48000, &s_feedback_q16));
    s_feedback_phase = 0;
    unsigned extra_packets = 0;
    for (unsigned n = 0; n < 250; ++n) {
        s_isoc_active[0] = false; /* completed ownership */
        assert(prepare_and_submit(s_isoc[0]) == ESP_OK);
        for (int p = 0; p < s_isoc[0]->num_isoc_packets; ++p) {
            const unsigned frames = s_isoc[0]->isoc_packet_desc[p].num_bytes / (4u * sample_bytes);
            assert(frames == 48 || frames == 49);
            extra_packets += frames == 49;
        }
    }
    assert(extra_packets == 10 && s_streaming && !s_faulted);
    s_format.max_packet_size = 48u * 4u * sample_bytes;
    uint32_t ignored;
    assert(!flx4_uac_feedback_q16(&s_format, feedback, sizeof(feedback), 48000, &ignored));
    s_feedback_q16 = 49u << 16; s_feedback_phase = 0;
    s_isoc_active[0] = false;
    const unsigned submits = review_submits;
    assert(prepare_and_submit(s_isoc[0]) == ESP_ERR_INVALID_SIZE);
    assert(review_submits == submits);
    controller_usb_audio_stream_request_stop(false);
    retire_transfers(); finish_cleanup();
    puts("PASS FS 48-kHz fast feedback: 49-frame packets and endpoint bounds");
}

static void test_hercules_fs_feedback(void)
{
    flx4_uac_descriptor_result_t parsed;
    flx4_uac_playback_format_t format;
    uint8_t bad[sizeof(hercules_fs_config)];
    assert(flx4_uac_parse_playback_formats(hercules_fs_config, sizeof(hercules_fs_config), &parsed));
    assert(flx4_uac_select_preferred_format(&parsed, &format));
    assert(format.channels == 4 && format.bits_per_sample == 24 && format.max_packet_size == 540);
    assert(format.requires_feedback && format.feedback_endpoint == 0x81 && format.feedback_refresh == 5);
    assert(format.sync_address == 0x81 && !format.frequency_control);
    assert(flx4_uac_format_supports_rate(&format, 44100));
    assert(!flx4_uac_format_supports_rate(&format, 48000));
    /* Real descriptor mutations cannot silently turn feedback into fixed pacing. */
    for (unsigned mutation = 0; mutation < 4; ++mutation) {
        memcpy(bad, hercules_fs_config, sizeof(bad));
        for (size_t off = 9; off < sizeof(bad); off += bad[off]) {
            if (bad[off+1] != 5) continue;
            if (mutation == 0 && bad[off+2] == 1) bad[off+8] = 0x82;
            if (mutation == 1 && bad[off+2] == 0x81) bad[off+4] = 4;
            if (mutation == 2 && bad[off+2] == 0x81) bad[off+7] = 0;
            if (mutation == 3 && bad[off+2] == 0x81) bad[off+3] = 0x15;
        }
        assert(!flx4_uac_parse_playback_formats(bad, sizeof(bad), &parsed) ||
               !flx4_uac_select_preferred_format(&parsed, &format));
    }
    assert(controller_usb_audio_stream_is_quiesced());
    assert(controller_usb_audio_stream_set_policy(false, false, false) == ESP_OK);
    assert(controller_usb_audio_stream_start((void*)1, (void*)2,
        hercules_fs_config, sizeof(hercules_fs_config), (void*)3, 10, 5) == ESP_OK);
    s_control->status = USB_TRANSFER_STATUS_COMPLETED;
    control_callback(s_control);
    assert(s_streaming && s_feedback_active && s_target_rate == 44100 && s_sample_bits == 24);
    assert(s_feedback->num_bytes == 3 && s_feedback->isoc_packet_desc[0].num_bytes == 3);
    assert(s_format.interface_num == 1 && s_format.endpoint_addr == 1 && !s_format.uac2);
    s_feedback->status = USB_TRANSFER_STATUS_COMPLETED;
    s_feedback->isoc_packet_desc[0].status = USB_TRANSFER_STATUS_COMPLETED;
    s_feedback->isoc_packet_desc[0].actual_num_bytes = 3;
    uint32_t q14 = (uint32_t)(((uint64_t)44100 << 14) / 1000);
    for (unsigned b = 0; b < 3; ++b) s_feedback->data_buffer[b] = (uint8_t)(q14 >> (8*b));
    unsigned updates = s_feedback_updates, invalid = s_feedback_invalid;
    feedback_callback(s_feedback);
    assert(s_feedback_updates == updates+1 && s_feedback_q16 == q14*4);
    /* Reject malformed width/rate/capacity; keep the last valid clock. */
    const uint32_t retained = s_feedback_q16;
    s_feedback->isoc_packet_desc[0].actual_num_bytes = 4;
    feedback_callback(s_feedback);
    assert(s_feedback_invalid == ++invalid && s_feedback_q16 == retained);
    s_feedback->isoc_packet_desc[0].actual_num_bytes = 0;
    feedback_callback(s_feedback);
    assert(s_feedback_invalid == invalid); /* no update between refresh periods */
    s_feedback->isoc_packet_desc[0].actual_num_bytes = 3;
    q14 = (45u << 14) + 1u; /* 46 frames would exceed the 540-byte endpoint */
    for (unsigned b = 0; b < 3; ++b) s_feedback->data_buffer[b] = (uint8_t)(q14 >> (8*b));
    feedback_callback(s_feedback);
    assert(s_feedback_invalid == ++invalid && s_feedback_q16 == retained);
    s_feedback_q16 = (44u << 16) + 16384u; s_feedback_phase = 0;
    assert(prepare_and_submit(s_isoc[0]) == ESP_OK);
    assert(s_isoc[0]->num_bytes == 177*12);
    for (int p = 0; p < 4; ++p) assert(s_isoc[0]->isoc_packet_desc[p].num_bytes == (p == 3 ? 540 : 528));
    review_ticks += 500;
    (void)controller_usb_audio_stream_poll_cleanup();
    assert(s_faulted && !s_streaming); /* stale feedback requires bounded stop */
    retire_transfers(); finish_cleanup();
    assert(controller_usb_audio_stream_is_quiesced() && !s_feedback);
    assert(controller_usb_audio_stream_set_policy(true, false, false) == ESP_OK);
    assert(controller_usb_audio_stream_start((void*)1, (void*)2,
        hercules_fs_config, sizeof(hercules_fs_config), (void*)3, 10, 5) == ESP_ERR_NOT_SUPPORTED);
    puts("PASS captured Hercules UAC1: endpoint association, 10.14 feedback, capacity, fractional pacing, stale stop and FLX4 isolation");
}

static void test_ddj400_hs_uac1_cadence(void)
{
    uint8_t config[sizeof(ddj400_hs_as_config)];
    flx4_uac_playback_format_t fmt;
    memcpy(config, ddj400_hs_as_config, sizeof(config));
    assert(controller_usb_audio_stream_set_policy(false, true, true) == ESP_OK);
    assert(select_stream_format(config, sizeof(config), &fmt));
    assert(fmt.interval == 4u && fmt.service_interval_us == 1000u && !fmt.uac2);
    /* Both captured alternates have the same clock, even though widths differ. */
    for (uint8_t interval = 1u; interval <= 5u; ++interval) {
        config[42] = config[85] = interval;
        assert(select_stream_format(config, sizeof(config), &fmt) == (interval == 4u));
    }
    memcpy(config, ddj400_hs_as_config, sizeof(config));
    config[39] = config[82] = 9u; /* adaptive, not the qualified synchronous family */
    assert(!select_stream_format(config, sizeof(config), &fmt));
    memcpy(config, ddj400_hs_as_config, sizeof(config));
    assert(controller_usb_audio_stream_set_policy(true, false, true) == ESP_OK);
    assert(!select_stream_format(config, sizeof(config), &fmt));
    assert(controller_usb_audio_stream_set_policy(false, true, true) == ESP_OK);
    assert(controller_usb_audio_stream_start((void*)1, (void*)2, config,
        sizeof(config), (void*)3, 10, 5) == ESP_OK);
    assert(s_control_step == 1u); /* UAC1 SET_CUR then SET_INTERFACE */
    s_control->status = USB_TRANSFER_STATUS_COMPLETED;
    control_callback(s_control); control_callback(s_control);
    assert(s_streaming && !s_faulted && s_target_rate == 44100u);
    assert(s_format.interval == 4u && s_isoc[0]->num_isoc_packets == STREAM_PACKETS_PER_TRANSFER);
    unsigned bytes = 0;
    for (int p = 0; p < s_isoc[0]->num_isoc_packets; ++p) {
        int n = s_isoc[0]->isoc_packet_desc[p].num_bytes;
        assert(n == 352 || n == 360); /* 44/45 frames * four PCM16 channels */
        bytes += (unsigned)n;
    }
    assert(bytes == (unsigned)s_isoc[0]->num_bytes);
    assert(controller_usb_audio_stream_write(pcm, pcm, 256, 48000) == ESP_OK);
    controller_usb_audio_stream_request_stop(false);
    retire_transfers(); finish_cleanup();
    assert(controller_usb_audio_stream_is_quiesced());
    assert(controller_usb_audio_stream_set_policy(true, false, false) == ESP_OK);
    puts("PASS DDJ-400 HS UAC1: raw interval 4, 1-ms cadence, wrong cadence/family/FLX4 rejected, control/stream/stop");
}

static void test_numark_touch_fs(void)
{
    assert(controller_usb_audio_stream_is_quiesced());
    assert(controller_usb_audio_stream_set_policy(false, false, false) == ESP_OK);
    flx4_uac_playback_format_t fmt;
    assert(select_stream_format(numark_touch_fs_as_config,
                                sizeof(numark_touch_fs_as_config), &fmt));
    assert(fmt.channels == 4 && fmt.bits_per_sample == 16 && fmt.bytes_per_sample == 2);
    assert(fmt.max_packet_size == 384 && fmt.interval == 1 && !fmt.requires_feedback);
    assert(flx4_uac_format_supports_rate(&fmt, 44100));
    assert(!flx4_uac_format_supports_rate(&fmt, 48000));
    assert(controller_usb_audio_stream_start((void*)1, (void*)2,
        numark_touch_fs_as_config, sizeof(numark_touch_fs_as_config), (void*)3, 10, 5) == ESP_OK);
    s_control->status = USB_TRANSFER_STATUS_COMPLETED;
    control_callback(s_control);
    assert(s_streaming && !s_faulted && s_target_rate == 44100 && !s_feedback_active);
    /* Exercise conversion and the generic MAIN 1/2, CUE 3/4 owner. */
    int16_t main_bus[256*2], cue_bus[256*2];
    for (unsigned i = 0; i < 256; ++i) {
        main_bus[i*2] = 4000; main_bus[i*2+1] = -4000;
        cue_bus[i*2] = 8000; cue_bus[i*2+1] = -8000;
    }
    assert(controller_usb_audio_stream_write(main_bus, cue_bus, 256, 48000) == ESP_OK);
    int16_t routed[4];
    assert(controller_audio_ring_read(&s_ring, routed, 1, false) == 1);
    assert(routed[0] == 4000 && routed[1] == -4000);
    assert(routed[2] == 8000 && routed[3] == -8000);
    controller_usb_audio_stream_request_stop(false);
    retire_transfers(); finish_cleanup();
    assert(controller_usb_audio_stream_is_quiesced());
    assert(controller_usb_audio_stream_set_policy(true, false, false) == ESP_OK);
    puts("PASS Numark FS focused fixture: PCM16 44.1k, sync cadence, MAIN/CUE routing, 48k source conversion, stop");
}

static void test_ddj_sb3_trailing_fragment(void)
{
    flx4_uac_descriptor_result_t parsed;
    flx4_uac_playback_format_t selected;
    uint8_t extended[sizeof(descriptor) + 2u];

    memcpy(extended, descriptor, sizeof(descriptor));
    extended[2] = (uint8_t)(sizeof(descriptor) + 1u);
    extended[sizeof(descriptor)] = 0x09;
    assert(flx4_uac_parse_playback_formats(extended, sizeof(descriptor) + 1u, &parsed));
    assert(parsed.format_count == 1u);

    memcpy(extended, descriptor, sizeof(descriptor));
    extended[2] = (uint8_t)(sizeof(descriptor) + 2u);
    extended[sizeof(descriptor)] = 0x09;
    extended[sizeof(descriptor) + 1u] = 0x24;
    assert(!flx4_uac_parse_playback_formats(extended, sizeof(descriptor) + 2u, &parsed));

    assert(flx4_uac_parse_playback_formats(ddj_sb3_fs_config, sizeof(ddj_sb3_fs_config), &parsed));
    assert(parsed.format_count == 1u);
    assert(parsed.formats[0].interface_num == 1u && parsed.formats[0].alternate_setting == 1u);
    assert(parsed.formats[0].endpoint_addr == 0x01u && parsed.formats[0].max_packet_size == 576u);
    assert(parsed.formats[0].channels == 4u && parsed.formats[0].bits_per_sample == 24u);
    assert(parsed.formats[0].bytes_per_sample == 3u && parsed.formats[0].interval == 1u);
    assert(parsed.formats[0].sync_type == 3u && !parsed.formats[0].requires_feedback);
    assert(parsed.formats[0].pcm_explicit && parsed.formats[0].frequency_control);
    assert(flx4_uac_format_supports_rate(&parsed.formats[0], 44100u));
    assert(flx4_uac_format_supports_rate(&parsed.formats[0], 48000u));
    assert(!flx4_uac_parse_playback_formats(ddj_sb3_fs_config, sizeof(ddj_sb3_fs_config) - 1u, &parsed));

    assert(controller_usb_audio_stream_is_quiesced());
    assert(controller_usb_audio_stream_set_policy(false, false, false) == ESP_OK);
    assert(select_stream_format(ddj_sb3_fs_config, sizeof(ddj_sb3_fs_config), &selected));
    assert(selected.channels == 4u && selected.bits_per_sample == 24u && selected.bytes_per_sample == 3u);
    assert(selected.max_packet_size == 576u && selected.sync_type == 3u && !selected.requires_feedback);
    assert(s_target_rate == 44100u);
    assert(controller_usb_audio_stream_set_policy(true, false, false) == ESP_OK);
    puts("PASS DDJ-SB3 FS: 4ch packed-24 with one-byte tail, overrun tail still rejected");
}

/* Fills the ring with a constant per-channel pattern, submits one URB and
 * decodes every frame; returns the frame count after checking all frames match. */
static int submit_constant_sb3(int sample_bytes, int16_t decoded[4])
{
    int16_t main[32 * 2], phones[32 * 2];
    for (unsigned i = 0; i < 32u; ++i) {
        main[i * 2] = 4000; main[i * 2 + 1] = -8000;
        phones[i * 2] = 12000; phones[i * 2 + 1] = -16000;
    }
    for (unsigned chunk = 0; chunk < 12u; ++chunk)
        (void)controller_usb_audio_stream_write(main, phones, 32, 44100);
    usb_transfer_t *transfer = s_isoc[0];
    s_isoc_active[0] = false;
    assert(prepare_and_submit(transfer) == ESP_OK);
    const int frame_bytes = 4 * sample_bytes;
    int total = 0, frames = 0;
    for (int p = 0; p < transfer->num_isoc_packets; ++p) {
        const int n = transfer->isoc_packet_desc[p].num_bytes;
        assert(n == 44 * frame_bytes || n == 45 * frame_bytes);
        total += n;
    }
    assert(transfer->num_bytes == total);
    for (int f = 0; f < total / frame_bytes; ++f, ++frames) {
        for (int c = 0; c < 4; ++c) {
            const uint8_t *s = &transfer->data_buffer[f * frame_bytes + c * sample_bytes];
            int16_t v;
            if (sample_bytes == 3) {
                assert(s[0] == 0u);
                v = (int16_t)(s[1] | (s[2] << 8));
            } else v = (int16_t)(s[0] | (s[1] << 8));
            if (f == 0) decoded[c] = v;
            else assert(v == decoded[c]);
        }
    }
    return frames;
}

static void test_ddj_sb3_s16le_wire_format(void)
{
    controller_usb_audio_stream_stats_t stats;
    assert(controller_usb_audio_stream_set_wire_format((controller_uac_wire_format_t)2) ==
           ESP_ERR_INVALID_ARG);
    controller_usb_audio_stream_get_stats(&stats);
    assert(stats.wire_format == CONTROLLER_UAC_WIRE_DESCRIPTOR);

    assert(controller_usb_audio_stream_is_quiesced());
    assert(controller_usb_audio_stream_set_policy(false, false, false) == ESP_OK);
    assert(controller_usb_audio_stream_start((void *)1, (void *)2,
        ddj_sb3_fs_config, sizeof(ddj_sb3_fs_config), (void *)3, 10, 5) == ESP_OK);
    s_control->status = USB_TRANSFER_STATUS_COMPLETED;
    control_callback(s_control);
    control_callback(s_control);
    assert(s_streaming && !s_faulted && s_target_rate == 44100u && s_format.bytes_per_sample == 3u);

    /* Descriptor format: 24-bit LE, 12 bytes per frame. */
    int16_t packed24[4], packed16[4];
    const int frames24 = submit_constant_sb3(3, packed24);
    assert(packed24[0] != 0 && packed24[0] != packed24[1] && packed24[2] != packed24[3]);

    /* The profile switches a running stream; the next URB is 16-bit LE with the
     * same samples on the same channels and 16-bit packet lengths. */
    assert(controller_usb_audio_stream_set_wire_format(CONTROLLER_UAC_WIRE_S16LE) == ESP_OK);
    controller_usb_audio_stream_get_stats(&stats);
    assert(stats.wire_format == CONTROLLER_UAC_WIRE_S16LE && stats.subslot_bytes == 3u);
    const int frames16 = submit_constant_sb3(2, packed16);
    assert(frames16 >= 4 * 44 && frames24 >= 4 * 44);
    assert(memcmp(packed16, packed24, sizeof(packed16)) == 0);

    /* The setting survives a stream restart; only the profile owner resets it. */
    controller_usb_audio_stream_request_stop(false);
    retire_transfers(); finish_cleanup();
    assert(controller_usb_audio_stream_is_quiesced());
    assert(controller_usb_audio_stream_start((void *)1, (void *)2,
        ddj_sb3_fs_config, sizeof(ddj_sb3_fs_config), (void *)3, 10, 5) == ESP_OK);
    s_control->status = USB_TRANSFER_STATUS_COMPLETED;
    control_callback(s_control);
    control_callback(s_control);
    assert(s_streaming);
    (void)submit_constant_sb3(2, packed16);
    assert(controller_usb_audio_stream_set_wire_format(CONTROLLER_UAC_WIRE_DESCRIPTOR) == ESP_OK);
    (void)submit_constant_sb3(3, packed24);
    controller_usb_audio_stream_request_stop(false);
    retire_transfers(); finish_cleanup();
    assert(controller_usb_audio_stream_is_quiesced());
    assert(controller_usb_audio_stream_set_policy(true, false, false) == ESP_OK);
    puts("PASS DDJ-SB3 S16LE wire format: 16-bit LE packets in a 24-bit alternate, live switch, restart persistence");
}

static void test_flx4_dedicated_headphones_preserve_pcm_level(void)
{
    assert(controller_usb_audio_stream_set_policy(true, false, false) == ESP_OK);
    start();
    const int16_t main[] = {4000, -4000, 32764, -32768, 0, 0};
    const int16_t phones[] = {16000, -16000, 32767, -32768, 1, -1};
    assert(controller_usb_audio_stream_write(main, phones, 3, 44100) == ESP_OK);
    int16_t output[3 * 4];
    assert(controller_audio_ring_read(&s_ring, output, 3, false) == 3);
    for (unsigned i = 0; i < 3; ++i) {
        /* The legacy MAIN mirror retains its established quarter level. Cue
         * is already mixed/level-controlled: USB transport must not cut 12 dB. */
        assert(output[i * 4] == main[i * 2] / 4);
        assert(output[i * 4 + 1] == main[i * 2 + 1] / 4);
        assert(output[i * 4 + 2] == phones[i * 2]);
        assert(output[i * 4 + 3] == phones[i * 2 + 1]);
    }
    controller_usb_audio_stream_request_stop(true);
    retire_transfers();
    assert(controller_usb_audio_stream_poll_cleanup());

    /* A headphone-only write must neither leak to MAIN nor lose level. */
    start();
    assert(controller_usb_audio_stream_write(NULL, phones, 3, 44100) == ESP_OK);
    assert(controller_audio_ring_read(&s_ring, output, 3, false) == 3);
    for (unsigned i = 0; i < 3; ++i) {
        assert(output[i * 4] == 0 && output[i * 4 + 1] == 0);
        assert(output[i * 4 + 2] == phones[i * 2]);
        assert(output[i * 4 + 3] == phones[i * 2 + 1]);
    }
    controller_usb_audio_stream_request_stop(true);
    retire_transfers();
    assert(controller_usb_audio_stream_poll_cleanup());
    /* The 48-kHz engine -> 44.1-kHz FLX4 resampler preserves DC amplitude. */
    start();
    int16_t constant_phones[64 * 2];
    for (unsigned i = 0; i < 64; ++i) {
        constant_phones[i * 2] = 24000;
        constant_phones[i * 2 + 1] = -24000;
    }
    assert(controller_usb_audio_stream_write(NULL, constant_phones, 64, 48000) == ESP_OK);
    assert(controller_audio_ring_read(&s_ring, output, 3, false) == 3);
    for (unsigned i = 0; i < 3; ++i) {
        assert(output[i * 4] == 0 && output[i * 4 + 1] == 0);
        assert(output[i * 4 + 2] == 24000 && output[i * 4 + 3] == -24000);
    }
    controller_usb_audio_stream_request_stop(true);
    retire_transfers();
    assert(controller_usb_audio_stream_poll_cleanup());
    puts("PASS FLX4 dedicated headphone unity, signed full-scale/low-level PCM, MAIN mirror isolation");
}

int main(void)
{
    uint8_t d[sizeof(descriptor)];
    flx4_uac_descriptor_result_t parsed;
    flx4_uac_playback_format_t selected;
    memcpy(d, descriptor, sizeof(d));
    d[23] = 3; d[24] = 24; d[33] = 0x1c; d[34] = 2; /* 540 bytes */
    assert(flx4_uac_parse_playback_formats(d, sizeof(d), &parsed));
    assert(flx4_uac_select_preferred_format(&parsed, &selected));
    assert(selected.bits_per_sample == 24 && selected.bytes_per_sample == 3);
    d[32] = 5; /* async OUT requires feedback */
    assert(flx4_uac_parse_playback_formats(d, sizeof(d), &parsed));
    assert(!flx4_uac_select_preferred_format(&parsed, &selected));
    d[32] = 1; d[35] = 2; /* Unsupported interval */
    assert(flx4_uac_parse_playback_formats(d, sizeof(d), &parsed));
    assert(!flx4_uac_select_preferred_format(&parsed, &selected));
    d[35] = 1; d[33] = 0xff; d[34] = 0x0f; /* high bandwidth */
    assert(flx4_uac_parse_playback_formats(d, sizeof(d), &parsed));
    assert(!flx4_uac_select_preferred_format(&parsed, &selected));
    assert(!flx4_uac_parse_playback_formats(d, sizeof(d) - 1, &parsed));
    memcpy(d, descriptor, sizeof(d));
    d[9] = 8; /* Short interface must not leak preceding alternate state. */
    assert(!flx4_uac_parse_playback_formats(d, sizeof(d), &parsed));
    memcpy(d, descriptor, sizeof(d)); d[25] = 2; /* Truncated discrete rate table. */
    assert(!flx4_uac_parse_playback_formats(d, sizeof(d), &parsed));
    memcpy(d, descriptor, sizeof(d)); d[16] = 0x20; /* UAC2 is unsupported. */
    assert(!flx4_uac_parse_playback_formats(d, sizeof(d), &parsed));
    memcpy(d, descriptor, sizeof(d)); d[23] = 4; d[24] = 24; /* 24 in 32 unsupported. */
    assert(flx4_uac_parse_playback_formats(d, sizeof(d), &parsed));
    assert(!flx4_uac_select_preferred_format(&parsed, &selected));
    const int16_t values[] = {32767, -32768, 1, -1};
    const uint8_t expected24[] = {0, 0xff, 0x7f, 0, 0, 0x80, 0, 1, 0, 0, 0xff, 0xff};
    uint8_t packed[12];
    assert(controller_uac_pack_pcm(packed, sizeof(packed), values, 1, 3));
    assert(memcmp(packed, expected24, sizeof(packed)) == 0);
    assert(!controller_uac_pack_pcm(packed, 11, values, 1, 3));
    assert(!controller_uac_pack_pcm(packed, sizeof(packed), values, 1, 4));
    test_clocked_ring_keeps_underflow_runway();
    test_packet_loss();
    test_stop_during_write(44100);
    test_stop_during_write(48000);
    assert(cleanup_deferrals == 2);
    start();
    for (unsigned i = 0; i < 256; ++i) {
        pcm[i * 2] = 4000;
        pcm[i * 2 + 1] = -4000;
    }
    assert(controller_usb_audio_stream_write(pcm, NULL, 256, 44100) == ESP_OK);
    int16_t frame[4];
    assert(controller_audio_ring_read(&s_ring, frame, 1, false) == 1);
    assert(frame[0] == 1000 && frame[1] == -1000);
    assert(frame[2] == frame[0] && frame[3] == frame[1]);
    controller_usb_audio_stream_request_stop(true);
    retire_transfers();
    assert(controller_usb_audio_stream_poll_cleanup());
    test_generic_pacing_and_timeout();
    test_generic_fixed_16bit(44100);
    test_generic_fixed_16bit(48000);
    test_hercules_fs_feedback();
    test_fs_48000_fast_feedback(2);
    test_fs_48000_fast_feedback(3);
    test_uac2();
    test_clock_settle();
    test_uac2_44100_fallback();
    test_prime_fault_evidence();
    test_hs_reconnect_under_heap_pressure();
    test_ddj400_hs_uac1_cadence();
    test_numark_touch_fs();
    test_ddj_sb3_trailing_fragment();
    test_ddj_sb3_s16le_wire_format();
    test_flx4_dedicated_headphones_preserve_pcm_level();
    assert(review_live_transfers == 0 && review_live_payload_bytes == 0);
    puts("PASS UAC 16/24-bit, consumer pacing, bounded timeout, ownership, reconnect and channel routing");
    return 0;
}
