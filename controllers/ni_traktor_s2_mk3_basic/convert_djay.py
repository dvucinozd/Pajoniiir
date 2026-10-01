"""Build the limited S2 MK3 profile from the pinned djay factory mapping.

Usage: python convert_djay.py <NI Traktor Kontrol S2 MK3.djayMidiMapping>
This is intentionally not a general djay converter.
"""
import hashlib
import json
from pathlib import Path
import plistlib
import sys

SOURCE_SHA256 = "abd3519e35ab24235d950ab5940dfdf72cbb6d5342f7d62fba089945cbebfe0e"


def convert(source):
    assert hashlib.sha256(source).hexdigest() == SOURCE_SHA256, "Unexpected source mapping"
    mapping = plistlib.loads(source)
    assert mapping["USBID"] == 0x17CC1710
    inputs, outputs = [], []

    def control(key, condition=None):
        matches = [c for c in mapping["controls"]
                   if c.get("keyPath") == key and c.get("condition") == condition]
        assert len(matches) == 1, (key, matches)
        return matches[0]

    def address(c):
        # djay channels are zero-based (also checked against its DDJ-400 map).
        assert c["midiMessageType"] in (1, 3)
        return {"status": hex((0x90 if c["midiMessageType"] == 1 else 0xB0)
                              | c["midiChannel"]), "data1": c["midiData"]}

    for deck in (1, 2):
        for key, event, led in (
            (f"turntable{deck}.playPause", f"deck{deck}.play", "play"),
            (f"turntable{deck}.cuePositionOrJumpConsideringPlayState1", f"deck{deck}.cue", "cue"),
            (f"turntable{deck}.bpmSync", f"deck{deck}.sync", "sync"),
            (f"mixer.monitorActive{deck}", f"deck{deck}.pfl", "pfl"),
            (f"musicLibrary.load{deck}", f"browser.load_deck{deck}", None),
            (f"turntable{deck}.autoLoopOnOff", f"deck{deck}.reloop_exit", None),
        ):
            c = control(key)
            assert c["midiMessageType"] == 1
            inputs.append({"type": "button", "event": event, **address(c)})
            if led:
                feedback = c["output"]
                outputs.append({"kind": "note", "led": led, "deck": deck - 1,
                                **address(c), "off": int(feedback["midiMinValue"]),
                                "on": int(feedback["midiMaxValue"]),
                                "blink": int(feedback["midiMaxValue"])})
        c = control(f"turntable{deck}.autoLoopDurationRotary")
        assert c["controlType"] == "rotary-64" and not c.get("flipped")
        inputs.append({"type": "encoder_rel64", "event": f"deck{deck}.loop_size", **address(c)})
        # Fixed hot-cue bank. djay's other modifier layers are deliberately omitted.
        for shifted, key in ((False, "cueOrJumpIfAlreadySet"), (True, "clearCuePoint")):
            records = [control(f"turntable{deck}.{key}{pad}", f"modifier{deck} == 0")
                       for pad in range(1, 9)]
            first = address(records[0])
            assert all(address(c)["status"] == first["status"] and
                       c["midiData"] == first["data1"] + i for i, c in enumerate(records))
            inputs.append({"type": "pad_bank", "deck": deck, "shifted": shifted,
                           "status": first["status"], "first_data1": first["data1"],
                           "count": 8, "mode": "hot_cue"})

    return {"schema": "p4-controller-profile-v1",
            "name": "NI Traktor Kontrol S2 MK3 - basic transport (unqualified)",
            "vendor": "Native Instruments", "vid": "0x17CC", "pid": "0x1710", "decks": 2,
            "capabilities": {"led_feedback": True, "usb_audio": False,
                             "jog_touch": False, "pitch_14bit": False},
            "inputs": inputs, "outputs": outputs}


if __name__ == "__main__":
    result = convert(Path(sys.argv[1]).read_bytes())
    destination = Path(__file__).with_name("profile.json")
    destination.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    print(destination)
