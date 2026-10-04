import pathlib
import sys
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]
TOOLS = ROOT / "tools" / "controller_profile"
sys.path.insert(0, str(TOOLS))

from compile_profile import compile_profile  # noqa: E402
from convert_web_profile import convert_profile  # noqa: E402


def profile(controls=None, feedback_outputs=None):
    return {
        "displayName": "Converter fixture",
        "vendor": "Test",
        "usb": {"vendorId": "0x1234", "productId": "0x5678"},
        "firmwareAbi": {"decks": 2, "capabilities": {}},
        "controls": controls or [],
        "feedbackOutputs": feedback_outputs or [],
    }


class ConverterTests(unittest.TestCase):
    def test_web_v4_roundtrip_matches_shared_golden(self):
        p = profile([
            {"id": "fx.target.master", "semanticId": "CTRL_ID_BEAT_FX_TARGET",
             "midi": {"message": "note", "status": 0x94, "number": 20, "mode": "select", "selectValue": 2}},
            {"id": "deck1.play", "deck": 1, "semanticId": "CTRL_ID_DECK1_PLAY",
             "midi": {"message": "note", "mode": "button", "status": 0x90, "number": 11},
             "feedback": {"type": "led", "source": "deck1.playing",
                          "midi": {"message": "note", "status": 0x90, "number": 11, "valueScale": 150}}}
        ])
        p["firmwareAbi"].update(version=4, initSysex=[240, 1, 247], capabilities={"channelFilterRequiresSmartCfx": False, "ledFeedback": False})
        converted = convert_profile(p)
        # Browser compiler only sets explicitly declared capabilities.
        self.assertEqual(compile_profile(converted).hex(),
                         (pathlib.Path(__file__).parent / "s3cp-v4-golden.hex").read_text().strip())

    def test_ddj400_generated_binary_is_reproducible(self):
        import json
        path = ROOT / "controllers/pioneer_ddj_400/profile.json"
        self.assertEqual(compile_profile(json.loads(path.read_text())), path.with_suffix(".s3bin").read_bytes())

    def test_v4_layout_and_oldest_version(self):
        import struct
        p = {"schema": "p4-controller-profile-v1", "vid": 1, "pid": 2,
             "inputs": [{"type": "note_select", "event": "system.beat_fx_target",
                         "status": 0x94, "data1": 0x14, "value": 2}],
             "outputs": [{"kind": "cc_value", "led": "vu_meter", "deck": "any",
                          "status": 0xB0, "data1": 2, "value_scale": 254}],
             "init_sysex": [0xF0, 1, 0xF7],
             "capabilities": {"channel_filter_requires_smart_cfx": False}}
        b = compile_profile(p)
        self.assertEqual(struct.unpack_from("<H", b, 4)[0], 4)
        self.assertEqual(b[34], 9)
        self.assertEqual(struct.unpack_from("<H", b, 30)[0], 3)
        self.assertEqual(struct.unpack_from("<H", b, 56)[0], 254)
        self.assertEqual(b[-3:], bytes([0xF0, 1, 0xF7]))
        for sysex in ([0xF0], [0xF0, 0x80, 0xF7], [0xF0] + [1] * 127 + [0xF7]):
            p["init_sysex"] = sysex
            with self.assertRaises(ValueError): compile_profile(p)

    def test_jog_modes_use_existing_packed_action_format(self):
        import struct
        for deck in (1, 2):
            for action, number in (("jog_vinyl", 8), ("jog_cdj", 9)):
                p = {"schema": "p4-controller-profile-v1", "vid": 1, "pid": 2,
                     "inputs": [{"type": "ext_action", "deck": deck,
                                 "action": action, "status": 0x90 + deck - 1,
                                 "data1": 0x30}]}
                blob = compile_profile(p)
                self.assertEqual(int.from_bytes(blob[4:6], "little"), 2)
                self.assertEqual(blob[34], 1)  # NOTE_VALUE, unchanged ABI
                self.assertEqual(struct.unpack_from("<hH", blob, 40), (number, 0x80))
                self.assertEqual(blob[37], 0x10 + (deck - 1) * 0x20 + 0x1c)

    def test_scaled_cc_requires_v3_without_changing_legacy_profiles(self):
        p = {"schema": "p4-controller-profile-v1", "vid": 1, "pid": 2,
             "inputs": [{"type": "cc7_to14", "event": "deck1.tempo",
                         "status": 0xB0, "data1": 9}]}
        blob = compile_profile(p)
        self.assertEqual(int.from_bytes(blob[4:6], "little"), 3)
        self.assertEqual(blob[34], 8)
        self.assertEqual(blob[38], 0)
        p["inputs"][0]["type"] = "cc7_abs"
        self.assertEqual(int.from_bytes(compile_profile(p)[4:6], "little"), 2)
        p["inputs"][0].update(type="cc7_to14", event="deck1.play")
        with self.assertRaisesRegex(ValueError, "PITCH"):
            compile_profile(p)

    def assert_compiles(self, converted):
        blob = compile_profile(converted)
        self.assertEqual(blob[:4], b"S3CP")

    def test_zero_midi_number_is_preserved(self):
        converted = convert_profile(profile([{
            "id": "deck1.play", "deck": 1, "action": "transport.play_toggle",
            "midi": {"mode": "button", "status": "0x90",
                     "number": 0, "hexNumber": "0x33"},
        }]))
        self.assertEqual(converted["inputs"][0]["data1"], "0x0")
        self.assert_compiles(converted)

    def test_hex_pad_range_is_parsed_and_compiles(self):
        converted = convert_profile(profile([{
            "id": "deck1.pad.mode.hot_cue", "deck": 1,
            "action": "pad.mode_hot_cue",
            "midi": {"mode": "button_range", "status": "0x97",
                     "numberRange": {"hexStart": "0x00", "hexEnd": "0x07"}},
        }]))
        self.assertEqual(converted["inputs"][0]["first_data1"], "0x0")
        self.assertEqual(converted["inputs"][0]["count"], 8)
        self.assertEqual(converted["outputs"], [])
        self.assert_compiles(converted)

    def test_deck2_only_led_keeps_deck_and_exact_address(self):
        converted = convert_profile(profile(feedback_outputs=[{
            "source": "deck2.playing", "type": "note",
            "midi": {"status": "0x92", "number": "0x2A"},
        }]))
        self.assertEqual(converted["outputs"], [{
            "kind": "note", "led": "play", "deck": 1,
            "status": "0x92", "data1": "0x2a",
        }])
        self.assert_compiles(converted)

    def test_mismatched_deck_led_addresses_are_not_merged(self):
        converted = convert_profile(profile(feedback_outputs=[
            {"source": "deck1.playing", "type": "note",
             "midi": {"status": "0x90", "number": "0x0B"}},
            {"source": "deck2.playing", "type": "note",
             "midi": {"status": "0x92", "number": "0x4B"}},
        ]))
        self.assertEqual(
            [(o["deck"], o["status"], o["data1"]) for o in converted["outputs"]],
            [(0, "0x90", "0xb"), (1, "0x92", "0x4b")],
        )
        self.assert_compiles(converted)

    def test_key_lock_is_rejected_instead_of_becoming_tempo_range(self):
        with self.assertRaisesRegex(ValueError, "key_lock.*not representable"):
            convert_profile(profile([{
                "id": "deck1.key_lock", "deck": 1, "action": "tempo.key_lock",
                "midi": {"mode": "button", "status": "0x90", "number": 1},
            }]))

    def test_invalid_deck_and_midi_range_are_rejected(self):
        with self.assertRaisesRegex(ValueError, "control.deck"):
            convert_profile(profile([{
                "id": "deck0.play", "deck": 0, "action": "transport.play_toggle",
                "midi": {"mode": "button", "status": "0x90", "number": 1},
            }]))
        with self.assertRaisesRegex(ValueError, "outside"):
            convert_profile(profile([{
                "id": "deck1.play", "deck": 1, "action": "transport.play_toggle",
                "midi": {"mode": "button", "status": "0x90", "number": 128},
            }]))


if __name__ == "__main__":
    unittest.main()
