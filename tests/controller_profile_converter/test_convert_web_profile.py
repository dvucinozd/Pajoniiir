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
