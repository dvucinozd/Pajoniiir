#!/usr/bin/env python3
import json
import sys
import os
import argparse

# Mapiranje semanticId (iz web profila) u firmware event nazive
SEMANTIC_ID_TO_EVENT = {
    # Deck 1
    "CTRL_ID_DECK1_PLAY": "deck1.play",
    "CTRL_ID_DECK1_CUE": "deck1.cue",
    "CTRL_ID_DECK1_JOG_SCRATCH": "deck1.jog_scratch",
    "CTRL_ID_DECK1_JOG_BEND": "deck1.jog_bend",
    "CTRL_ID_DECK1_JOG_TOUCH": "deck1.jog_touch",
    "CTRL_ID_DECK1_TEMPO": "deck1.tempo",
    "CTRL_ID_DECK1_SHIFT": "deck1.shift",
    "CTRL_ID_DECK1_CUE_SHIFT": "deck1.to_start",
    "CTRL_ID_DECK1_SYNC": "deck1.sync",
    "CTRL_ID_DECK1_TEMPO_RANGE": "deck1.tempo_range",
    "CTRL_ID_DECK1_LOOP_IN": "deck1.loop_in",
    "CTRL_ID_DECK1_LOOP_OUT": "deck1.loop_out",
    "CTRL_ID_DECK1_RELOOP_EXIT": "deck1.reloop_exit",
    "CTRL_ID_DECK1_LOOP_HALVE": "deck1.loop_halve",
    "CTRL_ID_DECK1_LOOP_DOUBLE": "deck1.loop_double",
    "CTRL_ID_DECK1_BEAT_JUMP_BACK": "deck1.beat_jump_back",
    "CTRL_ID_DECK1_BEAT_JUMP_FORWARD": "deck1.beat_jump_forward",
    "CTRL_ID_DECK1_PAD_MODE_HOT_CUE": "deck1.pad_mode_hot_cue",
    "CTRL_ID_DECK1_PAD_MODE_BEAT_LOOP": "deck1.pad_mode_beat_loop",
    "CTRL_ID_DECK1_PAD_MODE_BEAT_JUMP": "deck1.pad_mode_beat_jump",
    "CTRL_ID_DECK1_PAD_MODE_KEY_SHIFT": "deck1.pad_mode_key_shift",
    "CTRL_ID_DECK1_PAD_MODE_KEYBOARD": "deck1.pad_mode_keyboard",
    "CTRL_ID_DECK1_PAD_MODE_PAD_FX1": "deck1.pad_mode_pad_fx1",
    "CTRL_ID_DECK1_PAD_MODE_PAD_FX2": "deck1.pad_mode_pad_fx2",
    "CTRL_ID_DECK1_PAD_MODE_SAMPLER": "deck1.pad_mode_sampler",
    "CTRL_ID_DECK1_JOG_SEARCH": "deck1.jog_search",
    "CTRL_ID_DECK1_JOG_SEARCH_TOUCH": "deck1.jog_search_touch",
    "CTRL_ID_DECK1_PFL": "deck1.pfl",
    
    # Deck 2
    "CTRL_ID_DECK2_PLAY": "deck2.play",
    "CTRL_ID_DECK2_CUE": "deck2.cue",
    "CTRL_ID_DECK2_JOG_SCRATCH": "deck2.jog_scratch",
    "CTRL_ID_DECK2_JOG_BEND": "deck2.jog_bend",
    "CTRL_ID_DECK2_JOG_TOUCH": "deck2.jog_touch",
    "CTRL_ID_DECK2_TEMPO": "deck2.tempo",
    "CTRL_ID_DECK2_SHIFT": "deck2.shift",
    "CTRL_ID_DECK2_CUE_SHIFT": "deck2.to_start",
    "CTRL_ID_DECK2_SYNC": "deck2.sync",
    "CTRL_ID_DECK2_TEMPO_RANGE": "deck2.tempo_range",
    "CTRL_ID_DECK2_LOOP_IN": "deck2.loop_in",
    "CTRL_ID_DECK2_LOOP_OUT": "deck2.loop_out",
    "CTRL_ID_DECK2_RELOOP_EXIT": "deck2.reloop_exit",
    "CTRL_ID_DECK2_LOOP_HALVE": "deck2.loop_halve",
    "CTRL_ID_DECK2_LOOP_DOUBLE": "deck2.loop_double",
    "CTRL_ID_DECK2_BEAT_JUMP_BACK": "deck2.beat_jump_back",
    "CTRL_ID_DECK2_BEAT_JUMP_FORWARD": "deck2.beat_jump_forward",
    "CTRL_ID_DECK2_PAD_MODE_HOT_CUE": "deck2.pad_mode_hot_cue",
    "CTRL_ID_DECK2_PAD_MODE_BEAT_LOOP": "deck2.pad_mode_beat_loop",
    "CTRL_ID_DECK2_PAD_MODE_BEAT_JUMP": "deck2.pad_mode_beat_jump",
    "CTRL_ID_DECK2_PAD_MODE_KEY_SHIFT": "deck2.pad_mode_key_shift",
    "CTRL_ID_DECK2_PAD_MODE_KEYBOARD": "deck2.pad_mode_keyboard",
    "CTRL_ID_DECK2_PAD_MODE_PAD_FX1": "deck2.pad_mode_pad_fx1",
    "CTRL_ID_DECK2_PAD_MODE_PAD_FX2": "deck2.pad_mode_pad_fx2",
    "CTRL_ID_DECK2_PAD_MODE_SAMPLER": "deck2.pad_mode_sampler",
    "CTRL_ID_DECK2_JOG_SEARCH": "deck2.jog_search",
    "CTRL_ID_DECK2_JOG_SEARCH_TOUCH": "deck2.jog_search_touch",
    "CTRL_ID_DECK2_PFL": "deck2.pfl",

    # Mixer
    "CTRL_ID_CH1_VOLUME": "mixer.ch1_volume",
    "CTRL_ID_CH2_VOLUME": "mixer.ch2_volume",
    "CTRL_ID_CROSSFADER": "mixer.crossfader",
    "CTRL_ID_CH1_TRIM": "mixer.ch1_trim",
    "CTRL_ID_CH2_TRIM": "mixer.ch2_trim",
    "CTRL_ID_CH1_EQ_HIGH": "mixer.ch1_eq_high",
    "CTRL_ID_CH2_EQ_HIGH": "mixer.ch2_eq_high",
    "CTRL_ID_CH1_EQ_MID": "mixer.ch1_eq_mid",
    "CTRL_ID_CH2_EQ_MID": "mixer.ch2_eq_mid",
    "CTRL_ID_CH1_EQ_LOW": "mixer.ch1_eq_low",
    "CTRL_ID_CH2_EQ_LOW": "mixer.ch2_eq_low",
    "CTRL_ID_CH1_FILTER": "mixer.ch1_filter",
    "CTRL_ID_CH2_FILTER": "mixer.ch2_filter",
    "CTRL_ID_HEADPHONE_MIX": "mixer.headphone_mix",

    # Browser
    "CTRL_ID_BROWSE_DELTA": "browser.delta",
    "CTRL_ID_BROWSE_PRESS": "browser.press",
    "CTRL_ID_BROWSE_SHIFT_DELTA": "browser.shift_delta",
    "CTRL_ID_BROWSE_SHIFT_PRESS": "browser.shift_press",
    "CTRL_ID_LOAD_DECK1": "browser.load_deck1",
    "CTRL_ID_LOAD_DECK2": "browser.load_deck2",
    "CTRL_ID_SHIFT_LOAD_DECK1": "browser.shift_load_deck1",
    "CTRL_ID_SHIFT_LOAD_DECK2": "browser.shift_load_deck2",

    # System / Beat FX
    "CTRL_ID_SMART_CFX": "system.smart_cfx",
    "CTRL_ID_SMART_FADER": "system.smart_fader",
    "CTRL_ID_SMART_CFX_SHIFT": "system.smart_cfx_shift",
    "CTRL_ID_SMART_FADER_SHIFT": "system.smart_fader_shift",
    "CTRL_ID_BEAT_FX_SELECT_NEXT": "system.beat_fx_select_next",
    "CTRL_ID_BEAT_FX_SELECT_PREV": "system.beat_fx_select_prev",
    "CTRL_ID_BEAT_FX_BEAT_DEC": "system.beat_fx_beat_dec",
    "CTRL_ID_BEAT_FX_BEAT_INC": "system.beat_fx_beat_inc",
    "CTRL_ID_BEAT_FX_BEAT_DEC_SHIFT": "system.beat_fx_beat_dec_shift",
    "CTRL_ID_BEAT_FX_BEAT_INC_SHIFT": "system.beat_fx_beat_inc_shift",
    "CTRL_ID_BEAT_FX_ON": "system.beat_fx_on",
    "CTRL_ID_BEAT_FX_CLEAR": "system.beat_fx_clear",
    "CTRL_ID_BEAT_FX_TARGET": "system.beat_fx_target",
    "CTRL_ID_BEAT_FX_DEPTH": "system.beat_fx_depth",
    "CTRL_ID_MASTER_VOLUME": "system.master_volume",
    "CTRL_ID_MASTER_CUE": "system.master_cue",
    "CTRL_ID_HEADPHONE_LEVEL": "system.headphone_level",
}

# Kontrole koje trebaju replay flag (analogne kontrole/faderi/knobovi)
REPLAY_EVENTS = {
    "mixer.ch1_volume", "mixer.ch2_volume", "mixer.ch1_trim", "mixer.ch2_trim",
    "mixer.ch1_eq_high", "mixer.ch2_eq_high", "mixer.ch1_eq_mid", "mixer.ch2_eq_mid",
    "mixer.ch1_eq_low", "mixer.ch2_eq_low", "mixer.crossfader", "mixer.headphone_mix",
    "system.headphone_level", "system.master_volume", "mixer.ch1_filter", "mixer.ch2_filter",
    "system.beat_fx_depth"
}

# Mapiranje izvora LED feedbacka u firmware LED nazive
FEEDBACK_SOURCE_TO_LED = {
    "deck1.playing": ("play", 0),
    "deck2.playing": ("play", 1),
    "deck1.cue_active": ("cue", 0),
    "deck2.cue_active": ("cue", 1),
    "deck1.pfl_enabled": ("pfl", 0),
    "deck2.pfl_enabled": ("pfl", 1),
    "deck1.sync_enabled": ("sync", 0),
    "deck2.sync_enabled": ("sync", 1),
    "deck1.loop_in": ("loop_in", 0),
    "deck2.loop_in": ("loop_in", 1),
    "deck1.loop_out": ("loop_out", 0),
    "deck2.loop_out": ("loop_out", 1),
    "deck1.censor_active": ("censor", 0),
    "deck2.censor_active": ("censor", 1),
    "deck1.loop_adjust_in": ("loop_adjust_in", 0),
    "deck2.loop_adjust_in": ("loop_adjust_in", 1),
    "deck1.loop_adjust_out": ("loop_adjust_out", 0),
    "deck2.loop_adjust_out": ("loop_adjust_out", 1),
    
    # Pad modovi
    "deck1.pad_mode_hot_cue": ("pad_mode_hot_cue", 0),
    "deck2.pad_mode_hot_cue": ("pad_mode_hot_cue", 1),
    "deck1.pad_mode_pad_fx1": ("pad_mode_pad_fx1", 0),
    "deck2.pad_mode_pad_fx1": ("pad_mode_pad_fx1", 1),
    "deck1.pad_mode_pad_fx2": ("pad_mode_pad_fx2", 0),
    "deck2.pad_mode_pad_fx2": ("pad_mode_pad_fx2", 1),
    "deck1.pad_mode_beat_jump": ("pad_mode_beat_jump", 0),
    "deck2.pad_mode_beat_jump": ("pad_mode_beat_jump", 1),
    "deck1.pad_mode_beat_loop": ("pad_mode_beat_loop", 0),
    "deck2.pad_mode_beat_loop": ("pad_mode_beat_loop", 1),
    "deck1.pad_mode_sampler": ("pad_mode_sampler", 0),
    "deck2.pad_mode_sampler": ("pad_mode_sampler", 1),
    "deck1.pad_mode_key_shift": ("pad_mode_key_shift", 0),
    "deck2.pad_mode_key_shift": ("pad_mode_key_shift", 1),
    
    # Globalni/Sustavni
    "beat_fx.enabled": ("beat_fx_on", "any"),
    "smart_cfx.enabled": ("smart_cfx", "any"),
    "smart_fader.enabled": ("smart_fader", "any"),
    "master_cue.enabled": ("master_cue", "any"),
    "track_load_deck1": ("track_load_deck1", "any"),
    "track_load_deck2": ("track_load_deck2", "any")
}

def to_hex_str(val):
    if val is None:
        return None
    if isinstance(val, str):
        if val.startswith("0x") or val.startswith("0X"):
            return val.lower()
        return hex(int(val, 0)).lower()
    return hex(int(val)).lower()

def convert_profile(web_data):
    # Temeljni izlazni JSON
    firmware_profile = {
        "schema": "p4-controller-profile-v1",
        "name": web_data.get("displayName", "Generic Controller"),
        "vendor": web_data.get("vendor", "Generic"),
        "vid": to_hex_str(web_data.get("usb", {}).get("vendorId", "0x0000")),
        "pid": to_hex_str(web_data.get("usb", {}).get("productId", "0x0000")),
        "decks": web_data.get("firmwareAbi", {}).get("decks", 2),
        "capabilities": {
            "led_feedback": web_data.get("firmwareAbi", {}).get("capabilities", {}).get("ledFeedback", True),
            "usb_audio": web_data.get("firmwareAbi", {}).get("capabilities", {}).get("usbAudio", False),
            "jog_touch": web_data.get("firmwareAbi", {}).get("capabilities", {}).get("jogTouch", False),
            "pitch_14bit": web_data.get("firmwareAbi", {}).get("capabilities", {}).get("pitch14bit", False)
        },
        "inputs": [],
        "outputs": [],
        "audio": web_data.get("audio", {
            "enabled": True,
            "headphone_channels": [3, 4],
            "preferred_sample_rate": 44100
        })
    }

    # Mapiranje kontrola u inpute
    controls = web_data.get("controls", [])
    
    # Poseban flag za Pioneer beat_fx_target state_pair
    has_fx_target_ch1 = False
    has_fx_target_ch2 = False
    fx_target_ch1_addr = None
    fx_target_ch2_addr = None
    
    for c in controls:
        sem_id = c.get("semanticId")
        event = None
        if sem_id and sem_id.startswith("CTRL_ID_"):
            event = SEMANTIC_ID_TO_EVENT.get(sem_id)
            
        # Fallback ako nemamo semanticId
        if not event:
            action = c.get("action")
            c_id = c.get("id", "")
            deck = c.get("deck") or 1
            
            # Mapiranje preko action / id
            if action == "motor.start_stop" or c_id.endswith("motor_start_stop") or action == "transport.play_toggle" or c_id.endswith(".play"):
                event = f"deck{deck}.play"
            elif action == "transport.cue" or c_id.endswith(".cue"):
                event = f"deck{deck}.cue"
            elif action == "transport.load" or c_id.endswith(".load"):
                event = f"browser.load_deck{deck}"
            elif action == "transport.sync" or c_id.endswith(".sync"):
                event = f"deck{deck}.sync"
            elif action == "transport.censor" or c_id.endswith(".censor"):
                event = f"deck{deck}.ext_action"
            elif action == "browser.rotate" or c_id.endswith("browser.rotate"):
                event = "browser.delta"
            elif action == "browser.press" or c_id.endswith("browser.press"):
                event = "browser.press"
            elif action == "browser.press_shift" or c_id.endswith("browser.press.shift"):
                event = "browser.shift_press"
            elif action == "mixer.channel_fader" or c_id.endswith("channel_fader"):
                event = f"mixer.ch{deck}_volume"
            elif action == "mixer.master_level" or c_id.endswith("master.level"):
                event = "system.master_volume"
            elif action == "mixer.crossfader" or c_id == "mixer.crossfader":
                event = "mixer.crossfader"
            elif action == "mixer.trim" or c_id.endswith(".trim"):
                event = f"mixer.ch{deck}_trim"
            elif action == "mixer.eq_high" or c_id.endswith(".eq.high"):
                event = f"mixer.ch{deck}_eq_high"
            elif action == "mixer.eq_mid" or c_id.endswith(".eq.mid"):
                event = f"mixer.ch{deck}_eq_mid"
            elif action == "mixer.eq_low" or c_id.endswith(".eq.low"):
                event = f"mixer.ch{deck}_eq_low"
            elif action == "mixer.filter" or c_id.endswith(".filter"):
                event = f"mixer.ch{deck}_filter"
            elif action in ("headphones.mix", "headphones.cue_fader") or c_id.endswith("headphones.mix"):
                event = "mixer.headphone_mix"
            elif action == "headphones.level" or c_id.endswith("headphones.level"):
                event = "system.headphone_level"
            elif action == "tempo.fader" or c_id.endswith(".tempo"):
                event = f"deck{deck}.tempo"
            elif action == "tempo.key_lock" or c_id.endswith(".key_lock"):
                event = f"deck{deck}.tempo_range"
            elif action == "loop.half" or c_id.endswith("loop_half"):
                event = f"deck{deck}.loop_halve"
            elif action == "loop.double" or c_id.endswith("loop_double"):
                event = f"deck{deck}.loop_double"
            elif action == "fx.level_depth" or c_id.endswith("fx.beat.depth"):
                event = "system.beat_fx_depth"
            elif action == "fx.beat_forward" or c_id.endswith("fx.beat.inc"):
                event = "system.beat_fx_beat_inc"
            elif action == "fx.beat_back" or c_id.endswith("fx.beat.dec"):
                event = "system.beat_fx_beat_dec"
            elif action == "pad.mode_hot_cue" or c_id.endswith("pad.mode.hot_cue"):
                event = f"deck{deck}.pad_mode_hot_cue"
            elif action == "pad.mode_sampler" or c_id.endswith("pad.mode.sampler"):
                event = f"deck{deck}.pad_mode_sampler"
            elif action == "pad.mode_roll" or c_id.endswith("pad.mode.roll"):
                event = f"deck{deck}.pad_mode_pad_fx1"
            elif action == "pad.mode_saved_loop" or c_id.endswith("pad.mode.saved_loop"):
                event = f"deck{deck}.pad_mode_beat_loop"
            elif action == "pad.mode_scratch_bank" or c_id.endswith("pad.mode.scratch_bank"):
                event = f"deck{deck}.pad_mode_pad_fx2"

        if not event:
            continue

        # Ignoriraj deck 3 i 4 jer firmware podržava samo 2 decka
        if any(event.startswith(prefix) for prefix in ("deck3.", "deck4.")):
            continue
        if event in ("browser.load_deck3", "browser.load_deck4", "browser.shift_load_deck3", "browser.shift_load_deck4"):
            continue
        if any(event.startswith(prefix) for prefix in ("mixer.ch3", "mixer.ch4")):
            continue

        midi = c.get("midi", {})
        mode = midi.get("mode")
        status = to_hex_str(midi.get("status"))
        
        # Ako je event deckX.ext_action, pretvaramo ga u ext_action tip
        if event.endswith(".ext_action"):
            ext_act = "censor"
            c_id = c.get("id", "").lower()
            c_act = c.get("action", "").lower()
            if "censor" in c_act or "censor" in c_id:
                ext_act = "censor"
            elif "sync_master" in c_act or "sync_master" in c_id:
                ext_act = "sync_master"
            elif "reloop_stop" in c_act or "reloop_stop" in c_id:
                ext_act = "reloop_stop"
            elif "loop_adjust_in" in c_act or "loop_adjust_in" in c_id:
                ext_act = "loop_adjust_in"
            elif "loop_adjust_out" in c_act or "loop_adjust_out" in c_id:
                ext_act = "loop_adjust_out"
            elif "quantize" in c_act or "quantize" in c_id:
                ext_act = "quantize"
                
            firmware_profile["inputs"].append({
                "type": "ext_action",
                "deck": c.get("deck") or 1,
                "action": ext_act,
                "status": status,
                "data1": to_hex_str(midi.get("number") or midi.get("hexNumber"))
            })
            continue

        # 1. Gumb (button)
        if mode == "button":
            data1 = to_hex_str(midi.get("number") or midi.get("hexNumber"))
            firmware_profile["inputs"].append({
                "type": "button",
                "event": event,
                "status": status,
                "data1": data1
            })
            
        # 2. Pad banka (button_range)
        elif mode == "button_range":
            num_range = midi.get("numberRange", {})
            first_data1 = to_hex_str(num_range.get("start") or num_range.get("hexStart"))
            count = int(num_range.get("end") or num_range.get("hexEnd")) - int(num_range.get("start") or num_range.get("hexStart")) + 1
            
            # Određivanje mode parametra
            pad_mode = "hot_cue"
            c_id = c.get("id", "").lower()
            c_act = c.get("action", "").lower()
            if "hot_cue" in c_id or "hot_cue" in c_act:
                pad_mode = "hot_cue"
            elif "pad_fx1" in c_id or "pad_fx1" in c_act:
                pad_mode = "pad_fx1"
            elif "pad_fx2" in c_id or "pad_fx2" in c_act:
                pad_mode = "pad_fx2"
            elif "beat_jump" in c_id or "beat_jump" in c_act:
                pad_mode = "beat_jump"
            elif "beat_loop" in c_id or "beat_loop" in c_act:
                pad_mode = "beat_loop"
                
            firmware_profile["inputs"].append({
                "type": "pad_bank",
                "deck": c.get("deck") or 1,
                "shifted": c.get("layer") == "shift",
                "status": status,
                "first_data1": first_data1,
                "count": count,
                "mode": pad_mode
            })
            
        # 3. Relativni enkoderi (relative)
        elif mode == "relative":
            data1 = to_hex_str(midi.get("number") or midi.get("hexNumber"))
            enc_type = "encoder_2c" if event in {"browser.delta", "browser.shift_delta"} else "encoder_rel64"
            firmware_profile["inputs"].append({
                "type": enc_type,
                "event": event,
                "status": status,
                "data1": data1
            })
            
        # 4. Apsolutni faderi i knobovi (fader / knob)
        elif mode in ("fader", "knob"):
            res = midi.get("resolution", "7bit")
            replay = event in REPLAY_EVENTS
            
            if res == "14bit":
                msb = to_hex_str(midi.get("msb"))
                lsb = to_hex_str(midi.get("lsb") or midi.get("lsbHex"))
                firmware_profile["inputs"].append({
                    "type": "cc14",
                    "event": event,
                    "status": status,
                    "msb": msb,
                    "lsb": lsb,
                    "replay": replay
                })
            else: # 7bit
                data1 = to_hex_str(midi.get("number") or midi.get("hexNumber"))
                firmware_profile["inputs"].append({
                    "type": "cc7_abs",
                    "event": event,
                    "status": status,
                    "data1": data1,
                    "replay": replay
                })
                
    # Generiranje state_pair za Pioneer target sklopke
    for c in controls:
        c_id = c.get("id", "").lower()
        if "fx.beat.target" in c_id or "beat_fx_target" in c.get("semanticId", "") or ("beat" in c_id and "target" in c_id):
            midi = c.get("midi", {})
            status = to_hex_str(midi.get("status"))
            num_val = to_hex_str(midi.get("number") or midi.get("hexNumber"))
            if "ch1" in c_id or "deck1" in c_id:
                has_fx_target_ch1 = True
                fx_target_ch1_addr = {"status": status, "data1": num_val}
            elif "ch2" in c_id or "deck2" in c_id:
                has_fx_target_ch2 = True
                fx_target_ch2_addr = {"status": status, "data1": num_val}
                
    if has_fx_target_ch1 and has_fx_target_ch2:
        firmware_profile["inputs"].append({
            "type": "state_pair",
            "event": "system.beat_fx_target",
            "members": [
                fx_target_ch1_addr,
                fx_target_ch2_addr
            ],
            "values": [None, 0, 1, 2]
        })

    # LED izlazi (outputs)
    led_groups = {}
    
    # 1. Skeniranje feedback sekcije unutar controls
    for c in controls:
        feedback = c.get("feedback", {})
        if not feedback or feedback.get("type") != "led":
            continue
            
        source = feedback.get("source")
        if not source:
            continue
            
        midi = feedback.get("midi", {})
        status = to_hex_str(midi.get("status"))
        data1 = to_hex_str(midi.get("number") or midi.get("hexNumber"))
        
        mapping = FEEDBACK_SOURCE_TO_LED.get(source)
        if not mapping:
            # Fallback ako nemamo u rječniku
            deck_idx = c.get("deck")
            if deck_idx is None:
                deck_idx = "any"
            else:
                deck_idx = int(deck_idx) - 1
                
            s_lower = source.lower()
            if "playing" in s_lower or "play.active" in s_lower or "motor_start_stop.active" in s_lower:
                mapping = ("play", deck_idx)
            elif "cue_active" in s_lower or "cue.active" in s_lower:
                mapping = ("cue", deck_idx)
            elif "pfl" in s_lower or "headphones_cue" in s_lower:
                mapping = ("pfl", deck_idx)
            elif "sync" in s_lower:
                mapping = ("sync", deck_idx)
            elif "loop_in" in s_lower:
                mapping = ("loop_in", deck_idx)
            elif "loop_out" in s_lower:
                mapping = ("loop_out", deck_idx)
            elif "censor" in s_lower:
                mapping = ("censor", deck_idx)
                
        if not mapping:
            continue
            
        led_name, deck = mapping
        if deck != "any" and int(deck) not in (0, 1):
            continue
            
        kind = "note"
        if midi.get("message") == "cc" or "vu_meter" in led_name:
            kind = "cc_value"
            
        if led_name not in led_groups:
            led_groups[led_name] = {
                "kind": kind,
                "deck": deck,
                "entries": {}
            }
            
        if deck == "any":
            led_groups[led_name]["entries"]["any"] = (status, data1)
        else:
            led_groups[led_name]["entries"][deck] = (status, data1)

    # 2. Skeniranje feedbackOutputs sekcije na vrhu (npr. Numark)
    feedback_outputs = web_data.get("feedbackOutputs", [])
    for out in feedback_outputs:
        source = out.get("source")
        if not source:
            continue
            
        midi = out.get("midi", {})
        status = to_hex_str(midi.get("status"))
        data1 = to_hex_str(midi.get("number") or midi.get("hexNumber"))
        
        mapping = FEEDBACK_SOURCE_TO_LED.get(source)
        if not mapping:
            continue
            
        led_name, deck = mapping
        if deck != "any" and int(deck) not in (0, 1):
            continue
            
        kind = "note"
        if out.get("type") == "cc" or midi.get("message") == "cc" or "vu_meter" in led_name:
            kind = "cc_value"
            
        if led_name not in led_groups:
            led_groups[led_name] = {
                "kind": kind,
                "deck": deck,
                "entries": {}
            }
            
        if deck == "any":
            led_groups[led_name]["entries"]["any"] = (status, data1)
        else:
            led_groups[led_name]["entries"][deck] = (status, data1)

    # Formiranje konačnih outputa za firmware
    for led_name, group in led_groups.items():
        kind = group["kind"]
        deck = group["deck"]
        entries = group["entries"]
        
        if deck == "any" and "any" in entries:
            status, data1 = entries["any"]
            firmware_profile["outputs"].append({
                "kind": kind,
                "led": led_name,
                "deck": "any",
                "status": status,
                "data1": data1
            })
        else:
            status_0 = entries.get(0, (None, None))[0]
            status_1 = entries.get(1, (None, None))[0]
            data1_0 = entries.get(0, (None, None))[1]
            
            if status_0 and status_1:
                firmware_profile["outputs"].append({
                    "kind": kind,
                    "led": led_name,
                    "deck_status": [status_0, status_1],
                    "data1": data1_0
                })
            elif status_0:
                firmware_profile["outputs"].append({
                    "kind": kind,
                    "led": led_name,
                    "deck_status": [status_0, status_0],
                    "data1": data1_0
                })

    # Automatski dodaj note_bank za padove ako detektiramo ulazne pad_banke
    for inp in firmware_profile["inputs"]:
        if inp["type"] == "pad_bank" and inp["shifted"] is False:
            mode = inp["mode"]
            led_bank = None
            if mode == "hot_cue":
                led_bank = "hot_cue_pads"
            elif mode == "pad_fx1":
                led_bank = "pad_fx1_pads"
            elif mode == "pad_fx2":
                led_bank = "pad_fx2_pads"
            elif mode == "beat_jump":
                led_bank = "beat_jump_pads"
            elif mode == "beat_loop":
                led_bank = "beat_loop_pads"
                
            if led_bank:
                status_0 = inp["status"]
                status_1 = to_hex_str(int(status_0, 16) + 2)
                
                exists = any(o.get("led_bank") == led_bank for o in firmware_profile["outputs"])
                if not exists:
                    firmware_profile["outputs"].append({
                        "kind": "note_bank",
                        "led_bank": led_bank,
                        "deck_status": [status_0, status_1],
                        "first_data1": inp["first_data1"],
                        "count": inp["count"]
                    })

    return firmware_profile

def main():
    ap = argparse.ArgumentParser(description="Convert web controller profile to firmware profile")
    ap.add_argument("input", help="Path to input web-profile.json")
    ap.add_argument("-o", "--output", help="Path to output firmware profile.json")
    args = ap.parse_args()
    
    with open(args.input, "r", encoding="utf-8") as f:
        web_data = json.load(f)
        
    fw_profile = convert_profile(web_data)
    
    out_path = args.output
    if not out_path:
        out_path = args.input.rsplit(".", 1)[0] + ".p4.json"
        
    with open(out_path, "w", encoding="utf-8") as f:
        json.dump(fw_profile, f, indent=2)
        
    print(f"Successfully converted {args.input} -> {out_path}")
    return 0

if __name__ == "__main__":
    sys.exit(main())
