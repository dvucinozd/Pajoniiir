#!/usr/bin/env python3
"""Compile a p4-controller-profile-v1 JSON profile into S3CP binary form.

Usage:
    python compile_profile.py profile.json -o profile.s3bin
    python compile_profile.py --dump profile.s3bin

The semantic vocabulary mirrors firmware control_link.h. The
controller_profile host parity test (tests/controller_profile/) catches any
drift between these tables and the C headers.
"""

import argparse
import json
import struct
import sys
import zlib

S3CP_MAGIC = b"S3CP"
S3CP_VERSION = 1
HEADER_SIZE = 32
INPUT_ENTRY_SIZE = 16
OUTPUT_ENTRY_SIZE = 12

# Firmware bounds — keep in sync with controller_profile.h (S3 parser) and
# controller_profile_manager.h (P4 transfer). A profile exceeding any of these
# is rejected on-device (CP_ERR_BOUNDS / NACK_SIZE); catch it here instead.
MAX_INPUTS = 320        # CP_MAX_INPUTS
MAX_OUTPUTS = 160       # CP_MAX_OUTPUTS
MAX_PAIR_SLOTS = 40     # CP_MAX_PAIR_SLOTS
MAX_PROFILE_SIZE = 16384  # CPM_MAX_PROFILE_SIZE

# control_link.h CTRL_TYPE_*
TYPE_BUTTON = 0x01
TYPE_ENCODER = 0x02
TYPE_PITCH = 0x03

# raw_type values (see docs/CONTROLLER_PROFILE_SCHEMA.md)
RAW_NOTE_BUTTON = 0
RAW_NOTE_VALUE = 1
RAW_CC_REL64 = 2
RAW_CC_REL_2C = 3
RAW_CC14_MSB = 4
RAW_CC14_LSB = 5
RAW_CC7_ABS = 6
RAW_NOTE_STATE_PAIR = 7

RAW_TYPE_NAMES = {
    RAW_NOTE_BUTTON: "note_button",
    RAW_NOTE_VALUE: "note_value",
    RAW_CC_REL64: "cc_rel64",
    RAW_CC_REL_2C: "cc_rel_2c",
    RAW_CC14_MSB: "cc14_msb",
    RAW_CC14_LSB: "cc14_lsb",
    RAW_CC7_ABS: "cc7_abs",
    RAW_NOTE_STATE_PAIR: "note_state_pair",
}

FLAG_REPLAY = 0x0001
FLAG_PAIR_MEMBER_B = 0x0002

OUT_NOTE_ONOFF = 0
OUT_CC_VALUE = 1

# Header capability flags
PF_LED_FEEDBACK = 1 << 0
PF_USB_AUDIO = 1 << 1
PF_JOG_TOUCH = 1 << 2
PF_PITCH_14BIT = 1 << 3

# control_link.h namespaces
NS_DECK1 = 0x10
NS_DECK2 = 0x30
NS_MIXER = 0x50
NS_BROWSER = 0x60
NS_SYSTEM = 0x70

# ctrl_deck_control_t enum order
DECK_CTL = {
    "play": 0, "cue": 1, "jog_scratch": 2, "jog_bend": 3, "jog_touch": 4,
    "tempo": 5, "shift": 6, "to_start": 7, "sync": 8, "tempo_range": 9,
    "loop_in": 10, "loop_out": 11, "reloop_exit": 12, "loop_halve": 13,
    "loop_double": 14, "beat_jump_back": 15, "beat_jump_forward": 16,
    "pad_mode_hot_cue": 17, "pad_mode_beat_loop": 18, "pad_mode_beat_jump": 19,
    "pad_mode_key_shift": 20, "pad_action": 21, "pad_mode_keyboard": 22,
    "pad_mode_pad_fx1": 23, "pad_mode_pad_fx2": 24, "pad_mode_sampler": 25,
    "jog_search": 26, "jog_search_touch": 27, "ext_action": 28,
}

DECK_EVENT_TYPES = {
    "jog_scratch": TYPE_ENCODER, "jog_bend": TYPE_ENCODER,
    "jog_search": TYPE_ENCODER, "tempo": TYPE_PITCH,
}

MIXER_IDS = {
    "ch1_volume": 0x00, "ch2_volume": 0x01, "crossfader": 0x02,
    "ch1_trim": 0x05, "ch2_trim": 0x06, "ch1_eq_high": 0x07,
    "ch2_eq_high": 0x08, "ch1_eq_mid": 0x09, "ch2_eq_mid": 0x0A,
    "ch1_eq_low": 0x0B, "ch2_eq_low": 0x0C, "ch1_filter": 0x0D,
    "ch2_filter": 0x0E, "headphone_mix": 0x0F,
}

BROWSER_IDS = {
    "delta": 0x00, "load_deck1": 0x01, "load_deck2": 0x02, "press": 0x03,
    "shift_delta": 0x04, "shift_press": 0x05, "shift_load_deck1": 0x06,
    "shift_load_deck2": 0x07,
}
BROWSER_ENCODERS = {"delta", "shift_delta"}

SYSTEM_IDS = {
    "flx4_connection": NS_SYSTEM | 0x00, "smart_cfx": NS_SYSTEM | 0x01,
    "smart_fader": NS_SYSTEM | 0x02, "beat_fx_select_next": NS_SYSTEM | 0x03,
    "beat_fx_select_prev": NS_SYSTEM | 0x04, "beat_fx_beat_dec": NS_SYSTEM | 0x05,
    "beat_fx_beat_inc": NS_SYSTEM | 0x06, "beat_fx_target": NS_SYSTEM | 0x07,
    "beat_fx_depth": NS_SYSTEM | 0x08, "beat_fx_on": NS_SYSTEM | 0x09,
    "beat_fx_clear": NS_SYSTEM | 0x0A, "master_volume": NS_SYSTEM | 0x0B,
    "master_cue": NS_SYSTEM | 0x0C, "headphone_level": 0x7D,
    "smart_cfx_shift": 0x7E, "smart_fader_shift": 0x7F,
    "beat_fx_beat_dec_shift": 0x83, "beat_fx_beat_inc_shift": 0x84,
}
SYSTEM_PITCH = {"beat_fx_depth", "master_volume", "headphone_level"}

# ctrl_pad_mode_t
PAD_MODES = {
    "hot_cue": 0, "beat_loop": 1, "beat_jump": 2, "key_shift": 3,
    "keyboard": 4, "pad_fx1": 5, "pad_fx2": 6, "sampler": 7,
}

# ctrl_deck_ext_action_t
EXT_ACTIONS = {
    "censor": 0, "sync_master": 1, "reloop_stop": 2, "loop_adjust_in": 3,
    "loop_adjust_out": 4, "quantize": 5,
}

# control_link.h LED ids
LED_IDS = {
    "cue": 0, "play": 1, "pfl": 4, "vu_meter": 5,
    "pad_mode_hot_cue": 6, "pad_mode_keyboard": 7, "pad_mode_pad_fx1": 8,
    "pad_mode_pad_fx2": 9, "pad_mode_beat_jump": 10, "pad_mode_beat_loop": 11,
    "pad_mode_sampler": 12, "pad_mode_key_shift": 13, "sync": 14,
    "loop_in": 15, "loop_out": 16, "smart_cfx": 41, "smart_fader": 42,
    "beat_fx_on": 43, "master_cue": 52, "censor": 53, "cue_shift": 54,
    "loop_adjust_in": 55, "loop_adjust_out": 56, "track_load_deck1": 57,
    "track_load_deck2": 58,
}
LED_BANKS = {
    "beat_loop_pads": (17, 8), "pad_fx1_pads": (25, 8), "pad_fx2_pads": (33, 8),
    "hot_cue_pads": (44, 8), "beat_jump_pads": (59, 8),
    "beat_jump_shift_helpers": (67, 2),
}

LED_NAME_BY_ID = {v: k for k, v in LED_IDS.items()}
for bank, (first, count) in LED_BANKS.items():
    for i in range(count):
        LED_NAME_BY_ID[first + i] = "%s[%d]" % (bank, i)


def num(v):
    if isinstance(v, int):
        return v
    return int(str(v), 0)


def resolve_event(event):
    """event name -> (semantic_type, semantic_id)."""
    domain, _, name = event.partition(".")
    if domain in ("deck1", "deck2"):
        ns = NS_DECK1 if domain == "deck1" else NS_DECK2
        if name == "pfl":
            return TYPE_BUTTON, NS_MIXER | (0x03 if domain == "deck1" else 0x04)
        if name not in DECK_CTL:
            raise ValueError("unknown deck event: " + event)
        return DECK_EVENT_TYPES.get(name, TYPE_BUTTON), ns + DECK_CTL[name]
    if domain == "mixer":
        if name not in MIXER_IDS:
            raise ValueError("unknown mixer event: " + event)
        return TYPE_PITCH, NS_MIXER | MIXER_IDS[name]
    if domain == "browser":
        if name not in BROWSER_IDS:
            raise ValueError("unknown browser event: " + event)
        t = TYPE_ENCODER if name in BROWSER_ENCODERS else TYPE_BUTTON
        return t, NS_BROWSER | BROWSER_IDS[name]
    if domain == "system":
        if name not in SYSTEM_IDS:
            raise ValueError("unknown system event: " + event)
        t = TYPE_PITCH if name in SYSTEM_PITCH else TYPE_BUTTON
        return t, SYSTEM_IDS[name]
    raise ValueError("unknown event domain: " + event)


def pad_action_base(mode, pad, shifted):
    return (pad & 0x07) | ((mode & 0x07) << 3) | (0x40 if shifted else 0x00)


class Entry:
    def __init__(self, status, data1, raw_type, sem_type, sem_id,
                 pair_slot=0xFF, flags=0, base_value=0, press_mask=0,
                 lut=(-1, -1, -1, -1)):
        self.status = status
        self.data1 = data1
        self.raw_type = raw_type
        self.pair_slot = pair_slot
        self.sem_type = sem_type
        self.sem_id = sem_id
        self.flags = flags
        self.base_value = base_value
        self.press_mask = press_mask
        self.lut = lut

    def pack(self):
        return struct.pack("<BBBBBBHhH4b", self.status, self.data1,
                           self.raw_type, self.pair_slot, self.sem_type,
                           self.sem_id, self.flags, self.base_value,
                           self.press_mask, *self.lut)


class Output:
    def __init__(self, led_id, deck, kind, status, data1,
                 off=0x00, on=0x7F, blink=0x7F):
        self.led_id = led_id
        self.deck = deck
        self.kind = kind
        self.status = status
        self.data1 = data1
        self.off = off
        self.on = on
        self.blink = blink

    def pack(self):
        return struct.pack("<BBBBBBBBHH", self.led_id, self.deck, self.kind,
                           self.status, self.data1, self.off, self.on,
                           self.blink, 0, 0)


def compile_inputs(inputs):
    entries = []
    pair_slots = 0

    def alloc_slot():
        nonlocal pair_slots
        slot = pair_slots
        pair_slots += 1
        if pair_slots > MAX_PAIR_SLOTS:
            raise ValueError("too many pairing slots (%d > %d)" %
                             (pair_slots, MAX_PAIR_SLOTS))
        return slot

    for item in inputs:
        kind = item["type"]
        if kind == "button":
            t, i = resolve_event(item["event"])
            entries.append(Entry(num(item["status"]), num(item["data1"]),
                                 RAW_NOTE_BUTTON, t, i))
        elif kind == "ext_action":
            deck = int(item["deck"])
            event = "deck%d.ext_action" % deck
            t, i = resolve_event(event)
            action = EXT_ACTIONS[item["action"]]
            entries.append(Entry(num(item["status"]), num(item["data1"]),
                                 RAW_NOTE_VALUE, t, i,
                                 base_value=action & 0x7F, press_mask=0x80))
        elif kind == "pad_bank":
            deck = int(item["deck"])
            event = "deck%d.pad_action" % deck
            t, i = resolve_event(event)
            mode = PAD_MODES[item["mode"]]
            first = num(item["first_data1"])
            for pad in range(int(item["count"])):
                entries.append(Entry(num(item["status"]), first + pad,
                                     RAW_NOTE_VALUE, t, i,
                                     base_value=pad_action_base(mode, pad,
                                                                item["shifted"]),
                                     press_mask=0x80))
        elif kind in ("encoder_rel64", "encoder_2c"):
            t, i = resolve_event(item["event"])
            raw = RAW_CC_REL64 if kind == "encoder_rel64" else RAW_CC_REL_2C
            entries.append(Entry(num(item["status"]), num(item["data1"]),
                                 raw, t, i))
        elif kind == "cc14":
            t, i = resolve_event(item["event"])
            slot = alloc_slot()
            flags = FLAG_REPLAY if item.get("replay") else 0
            entries.append(Entry(num(item["status"]), num(item["msb"]),
                                 RAW_CC14_MSB, t, i, pair_slot=slot,
                                 flags=flags))
            entries.append(Entry(num(item["status"]), num(item["lsb"]),
                                 RAW_CC14_LSB, t, i, pair_slot=slot,
                                 flags=flags))
        elif kind == "cc7_abs":
            t, i = resolve_event(item["event"])
            flags = FLAG_REPLAY if item.get("replay") else 0
            entries.append(Entry(num(item["status"]), num(item["data1"]),
                                 RAW_CC7_ABS, t, i, flags=flags))
        elif kind == "state_pair":
            t, i = resolve_event(item["event"])
            slot = alloc_slot()
            values = item["values"]
            if len(values) != 4:
                raise ValueError("state_pair needs exactly 4 values")
            lut = tuple(-1 if v is None else int(v) for v in values)
            members = item["members"]
            if len(members) != 2:
                raise ValueError("state_pair needs exactly 2 members")
            for idx, member in enumerate(members):
                flags = FLAG_PAIR_MEMBER_B if idx == 1 else 0
                entries.append(Entry(num(member["status"]),
                                     num(member["data1"]),
                                     RAW_NOTE_STATE_PAIR, t, i,
                                     pair_slot=slot, flags=flags, lut=lut))
        else:
            raise ValueError("unknown input type: " + kind)

    return entries, pair_slots


def compile_outputs(outputs):
    entries = []
    for item in outputs:
        kind = item["kind"]
        if kind == "note_bank":
            first_led, count = LED_BANKS[item["led_bank"]]
            if int(item["count"]) != count:
                raise ValueError("%s expects count %d" % (item["led_bank"], count))
            statuses = [num(s) for s in item["deck_status"]]
            first_data1 = num(item["first_data1"])
            for i in range(count):
                for deck, status in enumerate(statuses):
                    entries.append(Output(first_led + i, deck, OUT_NOTE_ONOFF,
                                          status, first_data1 + i))
            continue

        led_id = LED_IDS[item["led"]]
        out_kind = OUT_CC_VALUE if kind == "cc_value" else OUT_NOTE_ONOFF
        off = num(item.get("off", 0x00))
        on = num(item.get("on", 0x7F))
        blink = num(item.get("blink", 0x7F))
        if "deck_status" in item:
            for deck, status in enumerate(item["deck_status"]):
                entries.append(Output(led_id, deck, out_kind, num(status),
                                      num(item["data1"]), off, on, blink))
        else:
            if item.get("deck") != "any":
                raise ValueError("output needs deck_status or deck:'any'")
            entries.append(Output(led_id, 0xFF, out_kind, num(item["status"]),
                                  num(item["data1"]), off, on, blink))
    return entries


def compile_profile(profile):
    if profile.get("schema") != "p4-controller-profile-v1":
        raise ValueError("unsupported schema: %r" % profile.get("schema"))

    entries, pair_slots = compile_inputs(profile.get("inputs", []))
    outputs = compile_outputs(profile.get("outputs", []))

    if len(entries) > MAX_INPUTS:
        raise ValueError("too many input entries (%d > %d)" %
                         (len(entries), MAX_INPUTS))
    if len(outputs) > MAX_OUTPUTS:
        raise ValueError("too many output entries (%d > %d)" %
                         (len(outputs), MAX_OUTPUTS))

    caps = profile.get("capabilities", {})
    flags = 0
    if caps.get("led_feedback"):
        flags |= PF_LED_FEEDBACK
    if caps.get("usb_audio"):
        flags |= PF_USB_AUDIO
    if caps.get("jog_touch"):
        flags |= PF_JOG_TOUCH
    if caps.get("pitch_14bit"):
        flags |= PF_PITCH_14BIT

    body = b"".join(e.pack() for e in entries)
    body += b"".join(o.pack() for o in outputs)

    profile_size = HEADER_SIZE + len(body)
    tail = struct.pack("<HHIHHBBH", num(profile["vid"]), num(profile["pid"]),
                       flags, len(entries), len(outputs), pair_slots,
                       int(profile.get("decks", 2)), 0)
    if profile_size > MAX_PROFILE_SIZE:
        raise ValueError("profile too large (%d > %d bytes)" %
                         (profile_size, MAX_PROFILE_SIZE))
    crc = zlib.crc32(tail + body) & 0xFFFFFFFF
    header = S3CP_MAGIC + struct.pack("<HHII", S3CP_VERSION, HEADER_SIZE,
                                      profile_size, crc) + tail
    assert len(header) == HEADER_SIZE
    return header + body


def dump(blob):
    if blob[:4] != S3CP_MAGIC:
        raise ValueError("bad magic")
    version, header_size, profile_size, crc = struct.unpack_from("<HHII", blob, 4)
    vid, pid, flags, in_count, out_count, slots, decks, _ = \
        struct.unpack_from("<HHIHHBBH", blob, 16)
    actual_crc = zlib.crc32(blob[16:profile_size]) & 0xFFFFFFFF
    print("S3CP v%d size=%d crc=0x%08X (%s)" %
          (version, profile_size, crc,
           "OK" if crc == actual_crc else "MISMATCH 0x%08X" % actual_crc))
    print("vid=0x%04X pid=0x%04X flags=0x%08X inputs=%d outputs=%d "
          "pair_slots=%d decks=%d" %
          (vid, pid, flags, in_count, out_count, slots, decks))
    off = header_size
    for n in range(in_count):
        status, data1, raw, slot, st, sid, fl, base, mask, l0, l1, l2, l3 = \
            struct.unpack_from("<BBBBBBHhH4b", blob, off)
        extra = ""
        if slot != 0xFF:
            extra += " slot=%d" % slot
        if fl:
            extra += " flags=0x%04X" % fl
        if raw == RAW_NOTE_VALUE:
            extra += " base=0x%04X mask=0x%04X" % (base & 0xFFFF, mask)
        if raw == RAW_NOTE_STATE_PAIR:
            extra += " lut=[%d,%d,%d,%d]" % (l0, l1, l2, l3)
        print("  in[%3d] %02X %02X %-15s -> type=%d id=0x%02X%s" %
              (n, status, data1, RAW_TYPE_NAMES.get(raw, "?"), st, sid, extra))
        off += INPUT_ENTRY_SIZE
    for n in range(out_count):
        led, deck, kind, status, data1, offv, onv, blinkv, _, _ = \
            struct.unpack_from("<BBBBBBBBHH", blob, off)
        deck_s = "any" if deck == 0xFF else str(deck)
        print("  out[%3d] led=%-26s deck=%-3s %s %02X %02X off=%02X on=%02X blink=%02X" %
              (n, LED_NAME_BY_ID.get(led, str(led)), deck_s,
               "cc " if kind == OUT_CC_VALUE else "note", status, data1,
               offv, onv, blinkv))
        off += OUTPUT_ENTRY_SIZE


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("input", help="profile.json (or .s3bin with --dump)")
    ap.add_argument("-o", "--output", help="output .s3bin path")
    ap.add_argument("--dump", action="store_true",
                    help="pretty-print an existing .s3bin")
    args = ap.parse_args()

    if args.dump:
        with open(args.input, "rb") as f:
            dump(f.read())
        return 0

    with open(args.input, "r", encoding="utf-8") as f:
        profile = json.load(f)
    blob = compile_profile(profile)
    out_path = args.output or args.input.rsplit(".", 1)[0] + ".s3bin"
    with open(out_path, "wb") as f:
        f.write(blob)
    print("wrote %s (%d bytes)" % (out_path, len(blob)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
