#include <assert.h>
#include <stdio.h>

unsigned review_submits;
/* Compile the actual stream owner against USB/RTOS boundary stubs. A critical
 * section hook deterministically pauses the producer before ring publication. */
#include "controller_usb_audio_stream.c"

static bool stop_before_publish;
static bool reject_second_writer;
static unsigned cleanup_deferrals;
static int16_t pcm[256 * 2];
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
    assert(stats.transfer_failures == 0 && !stats.faulted);
    assert(review_submits == submits + 1 && stats.streaming);
    for (int i = 0; i < 4; ++i) {
        t->isoc_packet_desc[i].status = USB_TRANSFER_STATUS_COMPLETED;
        t->isoc_packet_desc[i].actual_num_bytes = t->isoc_packet_desc[i].num_bytes;
    }
    isoc_callback(t);
    assert(s_packet_failures == 3 && s_packet_lost_frames == 90);
    t->status = USB_TRANSFER_STATUS_ERROR;
    isoc_callback(t);
    assert(s_faulted && s_transfer_failures == 1);
    assert(!controller_usb_audio_stream_poll_cleanup());
    retire_transfers();
    assert(controller_usb_audio_stream_poll_cleanup());
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
    assert(controller_usb_audio_stream_poll_cleanup());
    assert(controller_usb_audio_stream_is_quiesced());
    assert(s_ring.queued_frames == 0 && s_resampler.source_rate == 0);
    assert(controller_usb_audio_stream_write(pcm, pcm, 256, rate) == ESP_ERR_INVALID_STATE);
}

int main(void)
{
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
    puts("PASS UAC configuration, packet accounting, producer ownership, reconnect and channel routing");
    return 0;
}
