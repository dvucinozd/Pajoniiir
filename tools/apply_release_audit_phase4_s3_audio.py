#!/usr/bin/env python3
"""Fix S3 monitor-ring overwrite and reject unsupported UAC formats."""

from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def replace_once(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        raise RuntimeError(f"{label}: expected one match, found {count}")
    return text.replace(old, new, 1)


def patch_ring() -> None:
    path = ROOT / "firmware/control-board-s3/components/p4_audio_link/p4_audio_link.c"
    text = path.read_text(encoding="utf-8")
    text = replace_once(
        text,
        """/* SPSC ring with free-running counters: the I2S RX task is the sole writer\n   (bumps s_write_count) and the FLX4 USB Audio transfer callback is the sole\n   reader (bumps s_read_count). used = write_count - read_count. Overrun is\n   resolved reader-side by fast-forwarding, so the writer never touches the\n   reader's counter. Cross-context visibility uses acquire/release. */\n""",
        """/* SPSC ring with free-running counters: the I2S RX task is the sole writer\n   and the FLX4 USB Audio transfer callback is the sole reader. The writer uses\n   drop-newest when full, so it never overwrites a slot the reader may currently\n   be copying. Cross-context visibility uses acquire/release. */\n""",
        "ring ownership comment",
    )
    text = replace_once(
        text,
        """    if (w - r >= P4_AUDIO_LINK_RING_CAPACITY_FRAMES) {\n        /* Full: overwrite the oldest slot and let the reader fast-forward its\n           own counter past the lapped frames. Never move s_read_count here. */\n        stats_add(&s_stats.overruns, 1u);\n    }\n\n    s_ring[w % P4_AUDIO_LINK_RING_CAPACITY_FRAMES].left = left;\n""",
        """    if (w - r >= P4_AUDIO_LINK_RING_CAPACITY_FRAMES) {\n        /* Drop newest. Overwriting the oldest slot is unsafe because the reader\n         * may already have sampled its index but not copied both stereo words. */\n        stats_add(&s_stats.overruns, 1u);\n        return;\n    }\n\n    s_ring[w % P4_AUDIO_LINK_RING_CAPACITY_FRAMES].left = left;\n""",
        "drop-newest ring policy",
    )
    text = replace_once(
        text,
        """    if (avail > P4_AUDIO_LINK_RING_CAPACITY_FRAMES) {\n        /* Writer lapped us; drop the oldest lapped frames (overruns already\n           counted writer-side) and resync to the newest full window. */\n        r = w - P4_AUDIO_LINK_RING_CAPACITY_FRAMES;\n        avail = P4_AUDIO_LINK_RING_CAPACITY_FRAMES;\n    }\n""",
        """    if (avail > P4_AUDIO_LINK_RING_CAPACITY_FRAMES) {\n        /* Defensive recovery for counter corruption/wrap. Normal operation can\n         * never lap because the writer drops new frames while the ring is full. */\n        r = w - P4_AUDIO_LINK_RING_CAPACITY_FRAMES;\n        avail = P4_AUDIO_LINK_RING_CAPACITY_FRAMES;\n    }\n""",
        "reader defensive comment",
    )
    path.write_text(text, encoding="utf-8")

    test_path = ROOT / "tests/p4_audio_link/test_p4_audio_link.c"
    test = test_path.read_text(encoding="utf-8")
    test = replace_once(
        test,
        "static int test_overrun_drops_oldest_frames(void)",
        "static int test_overrun_drops_newest_frames(void)",
        "ring test name",
    )
    test = replace_once(
        test,
        'EXPECT_EQ_U32(stats.overruns, 8u, "oldest frames dropped on overrun");',
        'EXPECT_EQ_U32(stats.overruns, 8u, "newest frames dropped on overrun");',
        "ring overrun message",
    )
    test = replace_once(
        test,
        'EXPECT_EQ_U32((uint16_t)first[0], 8u, "oldest eight frames were dropped");',
        'EXPECT_EQ_U32((uint16_t)first[0], 0u, "oldest frame remains intact while newest frames drop");',
        "ring retained frame",
    )
    test = replace_once(
        test,
        "if (test_overrun_drops_oldest_frames() != 0) return 1;",
        "if (test_overrun_drops_newest_frames() != 0) return 1;",
        "ring test registration",
    )
    test_path.write_text(test, encoding="utf-8")


def patch_uac() -> None:
    path = ROOT / "firmware/control-board-s3/components/flx4_usb_audio/flx4_uac_descriptors.c"
    text = path.read_text(encoding="utf-8")

    anchor = """static bool is_complete_playback_format(const flx4_uac_playback_format_t *fmt)\n{\n"""
    helper = """static uint32_t packet_bytes_for_rate(const flx4_uac_playback_format_t *fmt,\n                                      uint32_t rate)\n{\n    if (!fmt || rate == 0u) {\n        return UINT32_MAX;\n    }\n    const uint32_t frames_per_ms = (rate + 999u) / 1000u;\n    return frames_per_ms * (uint32_t)fmt->channels * (uint32_t)fmt->bytes_per_sample;\n}\n\nstatic bool format_has_supported_packetization(const flx4_uac_playback_format_t *fmt)\n{\n    if (!fmt || fmt->bits_per_sample != 16u || fmt->bytes_per_sample != 2u ||\n        (fmt->channels != 2u && fmt->channels != 4u)) {\n        return false;\n    }\n    for (uint8_t i = 0; i < fmt->sample_rate_count; ++i) {\n        const uint32_t rate = fmt->sample_rates[i];\n        if ((rate == 44100u || rate == 48000u) &&\n            packet_bytes_for_rate(fmt, rate) <= fmt->max_packet_size) {\n            return true;\n        }\n    }\n    return false;\n}\n\nstatic bool is_complete_playback_format(const flx4_uac_playback_format_t *fmt)\n{\n"""
    text = replace_once(text, anchor, helper, "UAC packetization helper")

    text = replace_once(
        text,
        """        if (!is_complete_playback_format(fmt)) {\n            continue;\n        }\n\n        int score = 0;\n""",
        """        if (!is_complete_playback_format(fmt) ||\n            !format_has_supported_packetization(fmt)) {\n            continue;\n        }\n\n        int score = 0;\n""",
        "UAC supported-format gate",
    )
    text = replace_once(
        text,
        """        if (fmt->bits_per_sample == 16u) {\n            score += 200;\n        } else if (fmt->bits_per_sample == 24u) {\n            score += 100;\n        }\n""",
        """        /* Unsupported sample widths never reach scoring. */\n        score += 200;\n""",
        "remove 24-bit scoring",
    )
    path.write_text(text, encoding="utf-8")

    test_path = ROOT / "tests/flx4_usb_audio/test_flx4_uac_descriptors.c"
    test = test_path.read_text(encoding="utf-8")
    anchor = """static int test_select_preferred_format(void)\n{\n"""
    new_test = """static int test_rejects_unsupported_or_oversized_formats(void)\n{\n    flx4_uac_descriptor_result_t result = {\n        .formats = {\n            { .interface_num = 1, .alternate_setting = 1, .endpoint_addr = 0x01,\n              .max_packet_size = 576, .channels = 4, .bits_per_sample = 24,\n              .bytes_per_sample = 3, .sample_rates = { 48000 }, .sample_rate_count = 1 },\n            { .interface_num = 1, .alternate_setting = 2, .endpoint_addr = 0x01,\n              .max_packet_size = 100, .channels = 4, .bits_per_sample = 16,\n              .bytes_per_sample = 2, .sample_rates = { 48000 }, .sample_rate_count = 1 },\n        },\n        .format_count = 2,\n    };\n    flx4_uac_playback_format_t selected = {0};\n    EXPECT_TRUE(!flx4_uac_select_preferred_format(&result, &selected),\n                \"24-bit and undersized-MPS formats rejected\");\n    return 0;\n}\n\nstatic int test_select_preferred_format(void)\n{\n"""
    test = replace_once(test, anchor, new_test, "UAC rejection test")
    test = replace_once(
        test,
        """    if (test_select_preferred_format() != 0) {\n        return 1;\n    }\n""",
        """    if (test_select_preferred_format() != 0) {\n        return 1;\n    }\n    if (test_rejects_unsupported_or_oversized_formats() != 0) {\n        return 1;\n    }\n""",
        "UAC rejection test registration",
    )
    test_path.write_text(test, encoding="utf-8")


def main() -> None:
    patch_ring()
    patch_uac()
    print("Applied S3 audio ring and UAC format fixes.")


if __name__ == "__main__":
    main()
