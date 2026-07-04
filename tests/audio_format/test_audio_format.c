#include "audio_format.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>

static void test_header_detection(void)
{
    const uint8_t wav[] = { 'R', 'I', 'F', 'F', 0, 0, 0, 0, 'W', 'A', 'V', 'E' };
    const uint8_t flac[] = { 'f', 'L', 'a', 'C', 0, 0, 0, 0 };
    const uint8_t id3_mp3[] = { 'I', 'D', '3', 4, 0, 0 };
    const uint8_t frame_mp3[] = { 0xff, 0xfb, 0x90, 0x64 };
    const uint8_t unknown[] = { 'O', 'g', 'g', 'S', 0, 0, 0, 0 };

    assert(audio_format_detect_header(wav, sizeof(wav)) == AUDIO_FORMAT_WAV);
    assert(audio_format_detect_header(flac, sizeof(flac)) == AUDIO_FORMAT_FLAC);
    assert(audio_format_detect_header(id3_mp3, sizeof(id3_mp3)) == AUDIO_FORMAT_MP3);
    assert(audio_format_detect_header(frame_mp3, sizeof(frame_mp3)) == AUDIO_FORMAT_MP3);
    assert(audio_format_detect_header(unknown, sizeof(unknown)) == AUDIO_FORMAT_UNKNOWN);
    assert(audio_format_detect_header(NULL, sizeof(wav)) == AUDIO_FORMAT_UNKNOWN);
    assert(audio_format_detect_header(wav, 4) == AUDIO_FORMAT_UNKNOWN);
}

static void test_path_detection(void)
{
    assert(audio_format_detect_path("/usb/Music/Track.MP3") == AUDIO_FORMAT_MP3);
    assert(audio_format_detect_path("/usb/Music/Track.wav") == AUDIO_FORMAT_WAV);
    assert(audio_format_detect_path("/usb/Music/Track.WAVE") == AUDIO_FORMAT_WAV);
    assert(audio_format_detect_path("/usb/Music/Track.flac") == AUDIO_FORMAT_FLAC);
    assert(audio_format_detect_path("/usb/Music/Track") == AUDIO_FORMAT_UNKNOWN);
    assert(audio_format_detect_path(NULL) == AUDIO_FORMAT_UNKNOWN);
}

static void test_format_names(void)
{
    assert(audio_format_name(AUDIO_FORMAT_MP3)[0] == 'M');
    assert(audio_format_name(AUDIO_FORMAT_WAV)[0] == 'W');
    assert(audio_format_name(AUDIO_FORMAT_FLAC)[0] == 'F');
    assert(audio_format_name(AUDIO_FORMAT_UNKNOWN)[0] == 'U');
}

int main(void)
{
    test_header_detection();
    test_path_detection();
    test_format_names();
    puts("audio_format tests passed");
    return 0;
}
