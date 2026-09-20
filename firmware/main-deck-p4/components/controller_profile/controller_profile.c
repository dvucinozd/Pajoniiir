#include "controller_profile.h"

#include <string.h>

/* ── CRC-32 (IEEE 802.3, reflected, zlib-compatible) ───────────────────────── */

uint32_t cp_crc32(const uint8_t *data, size_t len)
{
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int b = 0; b < 8; b++) {
            crc = (crc >> 1) ^ (0xEDB88320u & (uint32_t)(-(int32_t)(crc & 1u)));
        }
    }
    return crc ^ 0xFFFFFFFFu;
}

/* ── Little-endian readers ─────────────────────────────────────────────────── */

static uint16_t rd_u16(const uint8_t *p)
{
    return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}

static uint32_t rd_u32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static int16_t rd_i16(const uint8_t *p)
{
    return (int16_t)rd_u16(p);
}

/* ── Parser ────────────────────────────────────────────────────────────────── */

int cp_profile_parse(const uint8_t *data, size_t len, cp_profile_t *out)
{
    if (!data || !out) {
        return CP_ERR_ARG;
    }
    if (len < CP_HEADER_SIZE) {
        return CP_ERR_SIZE;
    }
    if (memcmp(data, CP_MAGIC, 4) != 0) {
        return CP_ERR_MAGIC;
    }

    uint16_t version = rd_u16(data + 4);
    uint16_t header_size = rd_u16(data + 6);
    uint32_t profile_size = rd_u32(data + 8);
    uint32_t crc = rd_u32(data + 12);

    if (version != CP_VERSION || header_size != CP_HEADER_SIZE) {
        return CP_ERR_VERSION;
    }
    if (profile_size != len || profile_size < CP_HEADER_SIZE) {
        return CP_ERR_SIZE;
    }
    if (cp_crc32(data + 16, profile_size - 16) != crc) {
        return CP_ERR_CRC;
    }

    memset(out, 0, sizeof(*out));
    out->vid = rd_u16(data + 16);
    out->pid = rd_u16(data + 18);
    out->flags = rd_u32(data + 20);
    out->input_count = rd_u16(data + 24);
    out->output_count = rd_u16(data + 26);
    out->pair_slot_count = data[28];
    out->decks = data[29];

    if (out->input_count > CP_MAX_INPUTS ||
        out->output_count > CP_MAX_OUTPUTS ||
        out->pair_slot_count > CP_MAX_PAIR_SLOTS) {
        return CP_ERR_BOUNDS;
    }

    size_t need = CP_HEADER_SIZE +
                  (size_t)out->input_count * CP_INPUT_ENTRY_SIZE +
                  (size_t)out->output_count * CP_OUTPUT_ENTRY_SIZE;
    if (need != len) {
        return CP_ERR_SIZE;
    }

    const uint8_t *p = data + CP_HEADER_SIZE;
    for (uint16_t i = 0; i < out->input_count; i++, p += CP_INPUT_ENTRY_SIZE) {
        cp_input_entry_t *e = &out->inputs[i];
        e->match_status = p[0];
        e->match_data1 = p[1];
        e->raw_type = p[2];
        e->pair_slot = p[3];
        e->semantic_type = p[4];
        e->semantic_id = p[5];
        e->flags = rd_u16(p + 6);
        e->base_value = rd_i16(p + 8);
        e->press_mask = rd_u16(p + 10);
        for (int b = 0; b < 4; b++) {
            e->lut[b] = (int8_t)p[12 + b];
        }
        if (e->raw_type > CP_IN_NOTE_STATE_PAIR) {
            return CP_ERR_BOUNDS;
        }
        bool needs_slot = e->raw_type == CP_IN_CC14_MSB ||
                          e->raw_type == CP_IN_CC14_LSB ||
                          e->raw_type == CP_IN_NOTE_STATE_PAIR;
        if (needs_slot &&
            (e->pair_slot == CP_PAIR_SLOT_NONE ||
             e->pair_slot >= out->pair_slot_count)) {
            return CP_ERR_BOUNDS;
        }
    }

    for (uint16_t i = 0; i < out->output_count; i++, p += CP_OUTPUT_ENTRY_SIZE) {
        cp_output_entry_t *o = &out->outputs[i];
        o->led_id = p[0];
        o->deck = p[1];
        o->out_kind = p[2];
        o->status = p[3];
        o->data1 = p[4];
        o->off_value = p[5];
        o->on_value = p[6];
        o->blink_value = p[7];
        if (o->out_kind > CP_OUT_CC_VALUE) {
            return CP_ERR_BOUNDS;
        }
    }

    return CP_OK;
}

/* ── Runtime ───────────────────────────────────────────────────────────────── */

void cp_runtime_init(cp_runtime_t *rt)
{
    if (rt) {
        memset(rt, 0, sizeof(*rt));
    }
}

static int16_t rel64_delta(uint8_t data2)
{
    return (int16_t)data2 - 64;
}

static int16_t rel_2c_delta(uint8_t data2)
{
    data2 &= 0x7F;
    if (data2 == 0x00 || data2 == 0x40) {
        return 0;
    }
    return data2 < 0x40 ? (int16_t)data2 : (int16_t)data2 - 0x80;
}

static bool pair_value(const cp_pair_slot_t *slot, int16_t *out)
{
    if (!slot->msb_valid || !slot->lsb_valid) {
        return false;
    }
    *out = (int16_t)(((uint16_t)(slot->msb & 0x7F) << 7) | (slot->lsb & 0x7F));
    return true;
}

bool cp_runtime_process(const cp_profile_t *profile, cp_runtime_t *rt,
                        uint8_t status, uint8_t data1, uint8_t data2,
                        cp_event_t *out)
{
    if (!profile || !rt || !out) {
        return false;
    }

    for (uint16_t i = 0; i < profile->input_count; i++) {
        const cp_input_entry_t *e = &profile->inputs[i];
        if (e->match_status != status || e->match_data1 != data1) {
            continue;
        }

        const bool pressed = data2 > 0;
        out->type = e->semantic_type;
        out->id = e->semantic_id;

        switch (e->raw_type) {
        case CP_IN_NOTE_BUTTON:
            out->value = pressed ? 1 : 0;
            return true;
        case CP_IN_NOTE_VALUE:
            out->value = (int16_t)(e->base_value |
                                   (pressed ? (int16_t)e->press_mask : 0));
            return true;
        case CP_IN_CC_REL64: {
            int16_t delta = rel64_delta(data2);
            if (delta == 0) {
                return false;
            }
            out->value = delta;
            return true;
        }
        case CP_IN_CC_REL_2C: {
            int16_t delta = rel_2c_delta(data2);
            if (delta == 0) {
                return false;
            }
            out->value = delta;
            return true;
        }
        case CP_IN_CC14_MSB:
        case CP_IN_CC14_LSB: {
            cp_pair_slot_t *slot = &rt->slots[e->pair_slot];
            if (e->raw_type == CP_IN_CC14_MSB) {
                slot->msb = data2 & 0x7F;
                slot->msb_valid = true;
            } else {
                slot->lsb = data2 & 0x7F;
                slot->lsb_valid = true;
            }
            return pair_value(slot, &out->value);
        }
        case CP_IN_CC7_ABS:
            out->value = (int16_t)(data2 & 0x7F);
            rt->cc7_value[i] = out->value;
            rt->cc7_valid[i] = true;
            return true;
        case CP_IN_NOTE_STATE_PAIR: {
            cp_pair_slot_t *slot = &rt->slots[e->pair_slot];
            uint8_t bit = (e->flags & CP_IN_FLAG_PAIR_MEMBER_B) ? 0x02 : 0x01;
            if (pressed) {
                slot->pair_bits |= bit;
            } else {
                slot->pair_bits &= (uint8_t)~bit;
            }
            int8_t v = e->lut[slot->pair_bits & 0x03];
            if (v < 0) {
                return false;
            }
            out->value = v;
            return true;
        }
        default:
            return false;
        }
    }

    return false;
}

size_t cp_runtime_emit_snapshot(const cp_profile_t *profile,
                                const cp_runtime_t *rt,
                                cp_event_emit_cb_t cb, void *ctx)
{
    if (!profile || !rt || !cb) {
        return 0;
    }

    size_t count = 0;
    for (uint16_t i = 0; i < profile->input_count; i++) {
        const cp_input_entry_t *e = &profile->inputs[i];
        if (!(e->flags & CP_IN_FLAG_REPLAY)) {
            continue;
        }
        /* One event per control: the LSB half of a 14-bit pair is skipped so
         * the shared slot is reported exactly once. */
        if (e->raw_type == CP_IN_CC14_MSB) {
            int16_t value;
            if (!pair_value(&rt->slots[e->pair_slot], &value)) {
                continue;
            }
            if (!cb(e->semantic_type, e->semantic_id, value, ctx)) {
                return count;
            }
            count++;
        } else if (e->raw_type == CP_IN_CC7_ABS) {
            if (!rt->cc7_valid[i]) {
                continue;
            }
            if (!cb(e->semantic_type, e->semantic_id, rt->cc7_value[i], ctx)) {
                return count;
            }
            count++;
        }
    }
    return count;
}

bool cp_profile_map_led(const cp_profile_t *profile, uint8_t led, uint8_t deck,
                        uint8_t state, uint8_t midi_out[3])
{
    if (!profile || !midi_out) {
        return false;
    }

    for (uint16_t i = 0; i < profile->output_count; i++) {
        const cp_output_entry_t *o = &profile->outputs[i];
        if (o->led_id != led) {
            continue;
        }
        if (o->deck != CP_DECK_ANY && o->deck != deck) {
            continue;
        }

        midi_out[0] = o->status;
        midi_out[1] = o->data1;
        if (o->out_kind == CP_OUT_CC_VALUE) {
            midi_out[2] = state & 0x7F;
        } else if (state == 0) {
            midi_out[2] = o->off_value;
        } else if (state == 2) {
            midi_out[2] = o->blink_value;
        } else {
            midi_out[2] = o->on_value;
        }
        return true;
    }

    return false;
}
