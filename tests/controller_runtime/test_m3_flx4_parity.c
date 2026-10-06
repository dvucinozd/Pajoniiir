/* Execute the accepted M3 mapper beside the one shared production mapper.
 * Channel-qualified FX targets and repeated identical target publications are
 * explicit donor differences; every other input/output must agree exactly. */
#include "legacy_m3/legacy_m3_flx4_map.h"
#include "flx4_map.h"
#include "flx4_led_midi.h"
#include "usb_midi_codec.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static unsigned messages, matches, repeated_targets, rejected_wrong_channels;
static legacy_m3_flx4_map_state_t old_state;
static flx4_map_state_t new_state;

static void check_message(unsigned status, unsigned note, unsigned value)
{
    legacy_m3_flx4_midi_message_t old_msg = {
        .len=3, .status=status, .data1=note, .data2=value};
    flx4_midi_message_t new_msg = {
        .len=3, .status=status, .data1=note, .data2=value};
    legacy_m3_flx4_control_event_t old_event = {0};
    flx4_control_event_t new_event = {0};
    bool old_match=legacy_m3_flx4_map_translate_message(&old_state,&old_msg,&old_event);
    bool new_match=flx4_map_message(&new_state,&new_msg,&new_event);
    ++messages;
    if (old_match != new_match) {
        unsigned target=old_state.beat_fx_channel==1 ? CTRL_BEAT_FX_TARGET_CH1 :
            old_state.beat_fx_channel==2 ? CTRL_BEAT_FX_TARGET_CH2 : CTRL_BEAT_FX_TARGET_BOTH;
        bool redundant=!old_match && new_match && old_state.beat_fx_channel &&
            new_event.type==CTRL_TYPE_BUTTON && new_event.id==CTRL_ID_BEAT_FX_TARGET &&
            new_event.value==(int)target &&
            ((status==0x94 && note==0x10) || (status==0x95 && note==0x11));
        if (redundant) { ++repeated_targets; return; }
        fprintf(stderr,"M3 match mismatch %02x %02x %02x: old=%d new=%d\n",
            status,note,value,old_match,new_match);
        assert(old_match==new_match);
    }
    if (old_match) {
        ++matches;
        if (old_event.type!=new_event.type || old_event.id!=new_event.id || old_event.value!=new_event.value)
            fprintf(stderr,"M3 event mismatch %02x %02x %02x: old=%u/%u/%d new=%u/%u/%d\n",
                status,note,value,old_event.type,old_event.id,old_event.value,
                new_event.type,new_event.id,new_event.value);
        assert(old_event.type==new_event.type && old_event.id==new_event.id && old_event.value==new_event.value);
    }
}

int main(void)
{
    legacy_m3_flx4_map_init(&old_state);
    flx4_map_init(&new_state);
    for (unsigned status=0x80; status<=0xef; ++status)
        for (unsigned note=0; note<128; ++note)
            for (unsigned value=0; value<128; ++value) {
                /* Wrong-channel target notes are not in the authoritative XML.
                 * The donor rejects them without poisoning held target state. */
                if ((status==0x94 && note==0x11) || (status==0x95 && note==0x10)) {
                    flx4_midi_message_t msg={.len=3,.status=status,.data1=note,.data2=value};
                    flx4_control_event_t event;
                    flx4_map_state_t before=new_state;
                    assert(!flx4_map_message(&new_state,&msg,&event));
                    assert(!memcmp(&before,&new_state,sizeof before));
                    ++rejected_wrong_channels;
                    continue;
                }
                check_message(status,note,value);
            }
    /* Both selector transitions, release fallback and 14-bit controls use
     * state accumulated by the real implementations. */
    legacy_m3_flx4_map_init(&old_state); flx4_map_init(&new_state);
    check_message(0x94,0x10,127); check_message(0x95,0x11,127);
    check_message(0x94,0x10,0); check_message(0x95,0x11,0);
    check_message(0x94,0x10,127); check_message(0x94,0x10,127);
    unsigned leds=0;
    for (unsigned deck=0; deck<2; ++deck)
        for (unsigned led=0; led<256; ++led)
            for (unsigned state=0; state<256; ++state) {
                uint8_t a[4],b[4];
                bool old=legacy_m3_flx4_led_midi_build_packet(led,state,deck,a);
                bool current=flx4_led_midi_build_packet(led,state,deck,b);
                assert(old==current); if(old) assert(!memcmp(a,b,4));
                old=legacy_m3_flx4_led_midi_build_shifted_mirror_packet(led,state,deck,a);
                current=flx4_led_midi_build_shifted_mirror_packet(led,state,deck,b);
                assert(old==current); if(old) assert(!memcmp(a,b,4));
                ++leds;
            }
    /* USB packets are now decoded by the transport-neutral production codec. */
    for(unsigned header=0;header<256;++header) {
        uint8_t packet[]={header,0x90,0x0b,0x7f};
        legacy_m3_flx4_midi_message_t a={0}; usb_midi_message_t b={0};
        bool old=legacy_m3_flx4_midi_parse_usb_packet(packet,&a);
        bool current=usb_midi_parse_event_packet(packet,&b);
        assert(old==current);
        if(old) {
            assert(a.cable==b.cable && a.cin==b.cin && a.len==b.len && a.status==b.status);
            if(a.len>1) assert(a.data1==b.data1);
            if(a.len>2) assert(a.data2==b.data2);
            /* Bytes beyond CIN length are USB padding, not MIDI values. */
            if(a.len<3) {
                legacy_m3_flx4_control_event_t x; flx4_control_event_t y;
                assert(!legacy_m3_flx4_map_translate_message(&old_state,&a,&x));
                assert(!flx4_map_message(&new_state,&b,&y));
            }
        }
    }
    assert(matches>10000 && repeated_targets==254 && rejected_wrong_channels==256);
    printf("PASS M3 FLX4 parity: %u messages, %u matched, %u LED cases, %u redundant target publications, %u wrong-channel rejects\n",
        messages,matches,leds,repeated_targets,rejected_wrong_channels);
    return 0;
}
