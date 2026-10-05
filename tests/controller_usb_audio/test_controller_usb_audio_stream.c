#include <assert.h>
#include <stdio.h>

unsigned review_submits;
unsigned review_ticks;
/* Compile the actual stream owner against USB/RTOS boundary stubs. A critical
 * section hook deterministically pauses the producer before ring publication. */
#define CONTROLLER_USB_AUDIO_PC_TEST
#include "controller_usb_audio_stream.c"

static unsigned packet_detail_count;
static uint32_t packet_details[4][4];
void service_log_event(service_log_event_t event, service_log_severity_t severity,
                       uint8_t count, uint32_t a0, uint32_t a1,
                       uint32_t a2, uint32_t a3, const char *text)
{
    assert(event == SERVICE_LOG_UAC_PACKET_FAULT && severity == SERVICE_LOG_WARN);
    assert(count == 4u && text && packet_detail_count < 4u);
    const uint32_t args[] = {a0, a1, a2, a3};
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
    assert(controller_usb_audio_stream_set_policy(true, false) == ESP_OK);
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
    t->isoc_packet_desc[0].status = USB_TRANSFER_STATUS_SKIPPED;
    t->isoc_packet_desc[0].actual_num_bytes = 0;
    isoc_callback(t);
    assert(packet_detail_count == 4u && s_packet_failures > 4u);
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
    assert(controller_usb_audio_stream_set_policy(false, true) == ESP_OK);
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
    assert(controller_usb_audio_stream_set_policy(false, true) == ESP_OK);
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
    assert(controller_usb_audio_stream_set_policy(true, false) == ESP_OK);
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
    assert(controller_usb_audio_stream_set_policy(false, true) == ESP_OK);
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
    assert(controller_usb_audio_stream_set_policy(true, false) == ESP_OK);
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
    puts("PASS UAC 16/24-bit, consumer pacing, bounded timeout, ownership, reconnect and channel routing");
    return 0;
}
