# DDJ-FLX4
## List of MIDI messages

| Group | Fig. | UI name | Deck | Trigger | +SHIFT Condition (mode) | MIDI Channel (Dec) | NOTE/CC (Dec) | MIDI Data (Data 1) (English scale) | MIDI-IN Status (Hex) | MIDI-IN Data 1 (Hex) | MIDI-IN Data 2 (Hex) | MIDI-OUT Status (Hex) | MIDI-OUT Data 1 (Hex) | MIDI-OUT Data 2 (Hex) | Details (Data 2) |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| 1. DECK | 1-1 | PLAY/PAUSE | Deck 1 | Press | | 11 | B-1 | NOTE | 90 | 0B | hh | 90 | 0B | hh | OFF=0x00, ON=0x7F |
| | | | | +SHIFT | | 14 | D0 | NOTE | 90 | 0E | hh | 90 | 0E | hh | OFF=0x00, ON=0x7F |
| | | | Deck 2 | Press | | 11 | B-1 | NOTE | 91 | 0B | hh | 91 | 0B | hh | OFF=0x00, ON=0x7F |
| | | | | +SHIFT | | 14 | D0 | NOTE | 91 | 0E | hh | 91 | 0E | hh | OFF=0x00, ON=0x7F |
| | 1-2 | CUE | Deck 1 | Press | | 12 | C0 | NOTE | 90 | 0C | hh | 90 | 0C | hh | OFF=0x00, ON=0x7F |
| | | | | +SHIFT | | 72 | C5 | NOTE | 90 | 48 | hh | 90 | 48 | hh | OFF=0x00, ON=0x7F |
| | | | Deck 2 | Press | | 12 | C0 | NOTE | 91 | 0C | hh | 91 | 0C | hh | OFF=0x00, ON=0x7F |
| | | | | +SHIFT | | 72 | C5 | NOTE | 91 | 48 | hh | 91 | 48 | hh | OFF=0x00, ON=0x7F |
| | 1-3 | SHIFT | Deck 1 | Press | | 63 | D#4 | NOTE | 90 | 3F | hh | | | | OFF=0x00, ON=0x7F |
| | | | Deck 2 | Press | | 63 | D#4 | NOTE | 91 | 3F | hh | | | | OFF=0x00, ON=0x7F |
| | 1-4 | JOG DIAL (Platter) | Deck 1 | Rotate | Vinyl On *2 | 34 | | CC | B0 | 22 | hh | | | | Difference count value from previous operation |
| | | | | | Vinyl Off *2 | 35 | | CC | B0 | 23 | hh | | | | Turn clockwise: Increases from 0x41 |
| | | | | +SHIFT | | 41 | | CC | B0 | 29 | hh | | | | Turn counterclockwise: Decreases from 0x3F |
| | | | | Touch | | 54 | F#3 | NOTE | 90 | 36 | hh | | | | OFF=0x00, ON=0x7F |
| | | | | +SHIFT | | 103 | G7 | NOTE | 90 | 67 | hh | | | | OFF=0x00, ON=0x7F |
| | | | Deck 2 | Rotate | Vinyl On *2 | 34 | | CC | B1 | 22 | hh | | | | Difference count value from previous operation |
| | | | | | Vinyl Off *2 | 35 | | CC | B1 | 23 | hh | | | | Turn clockwise: Increases from 0x41 |
| | | | | +SHIFT | | 41 | | CC | B1 | 29 | hh | | | | Turn counterclockwise: Decreases from 0x3F |
| | | | | Touch | | 54 | F#3 | NOTE | 91 | 36 | hh | | | | OFF=0x00, ON=0x7F |
| | | | | +SHIFT | | 103 | G7 | NOTE | 91 | 67 | hh | | | | OFF=0x00, ON=0x7F |
| | | JOG DIAL (Wheel side) | Deck 1 | Rotate | | 33 | | CC | B0 | 21 | hh | | | | Difference count value from previous operation |
| | | | | +SHIFT | | 33 | | CC | B0 | 21 | hh | | | | Turn clockwise: Increases from 0x41 |
| | | | Deck 2 | Rotate | | 33 | | CC | B1 | 21 | hh | | | | Turn counterclockwise: Decreases from 0x3F |
| | | | | +SHIFT | | 33 | | CC | B1 | 21 | hh | | | | |
| | 1-5 | IN | Deck 1 | Press | | 16 | E0 | NOTE | 90 | 10 | hh | 90 | 10 | hh | OFF=0x00, ON=0x7F |
| | | | | +SHIFT | | 76 | E5 | NOTE | 90 | 4C | hh | 90 | 4C | hh | OFF=0x00, ON=0x7F |
| | | | Deck 2 | Press | | 16 | E0 | NOTE | 91 | 10 | hh | 91 | 10 | hh | OFF=0x00, ON=0x7F |
| | | | | +SHIFT | | 76 | E5 | NOTE | 91 | 4C | hh | 91 | 4C | hh | OFF=0x00, ON=0x7F |
| | 1-6 | OUT | Deck 1 | Press | | 17 | F0 | NOTE | 90 | 11 | hh | 90 | 11 | hh | OFF=0x00, ON=0x7F |
| | | | | +SHIFT | | 78 | F#5 | NOTE | 90 | 4E | hh | 90 | 4E | hh | OFF=0x00, ON=0x7F |
| | | | Deck 2 | Press | | 17 | F0 | NOTE | 91 | 11 | hh | 91 | 11 | hh | OFF=0x00, ON=0x7F |
| | | | | +SHIFT | | 78 | F#5 | NOTE | 91 | 4F | hh | 91 | 4E | hh | OFF=0x00, ON=0x7F |
| | 1-7 | 4 BEAT/EXIT | Deck 1 | Press | | 77 | F5 | NOTE | 90 | 4D | hh | | | | OFF=0x00, ON=0x7F |
| | | | | +SHIFT | | 80 | G#5 | NOTE | 90 | 50 | hh | | | | OFF=0x00, ON=0x7F |
| | | | Deck 2 | Press | | 77 | F5 | NOTE | 91 | 4D | hh | | | | OFF=0x00, ON=0x7F |
| | | | | +SHIFT | | 80 | G#5 | NOTE | 91 | 50 | hh | | | | OFF=0x00, ON=0x7F |
| | 1-8 | CUE/LOOP CALL | Deck 1 | Press | | 81 | A5 | NOTE | 90 | 51 | hh | | | | OFF=0x00, ON=0x7F |
| | | | | +SHIFT | | 62 | D4 | NOTE | 90 | 3F | hh | | | | OFF=0x00, ON=0x7F |
| | | | Deck 2 | Press | | 81 | A5 | NOTE | 91 | 51 | hh | | | | OFF=0x00, ON=0x7F |
| | | | | +SHIFT | | 62 | D4 | NOTE | 91 | 3E | hh | | | | OFF=0x00, ON=0x7F |
| | 1-9 | CUE/LOOP CALL ▷ | Deck 1 | Press | | 83 | B5 | NOTE | 90 | 53 | hh | | | | OFF=0x00, ON=0x7F |
| | | | | +SHIFT | | 61 | C#4 | NOTE | 90 | 3D | hh | | | | OFF=0x00, ON=0x7F |
| | | | Deck 2 | Press | | 83 | B5 | NOTE | 91 | 53 | hh | | | | OFF=0x00, ON=0x7F |
| | | | | +SHIFT | | 61 | C#4 | NOTE | 91 | 3D | hh | | | | OFF=0x00, ON=0x7F |
| PERFORMANCE PAD 8 | | HOT CUE | Deck 1 | Press | | 8 | G-1 | NOTE | 97 | 07 | hh | 97 | 07 | hh | OFF=0x00, ON=0x7F |
| | | | | +SHIFT | MODE | 9 | G-1 | NOTE | 98 | 07 | hh | 98 | 07 | hh | OFF=0x00, ON=0x7F |
| | | PAD FX 1 | | | | 8 | 23 | NOTE | B0 | 17 | hh | 97 | 17 | hh | OFF=0x00, ON=0x7F |
| | | | | +SHIFT | MODE | 9 | 23 | NOTE | B0 | 17 | hh | 98 | 17 | hh | OFF=0x00, ON=0x7F |
| | | BEAT JUMP | | | | 8 | 39 | NOTE | D#2 | 27 | hh | 97 | 27 | hh | OFF=0x00, ON=0x7F |
| | | | | +SHIFT | MODE | 9 | 39 | NOTE | D#2 | 27 | hh | 98 | 27 | hh | OFF=0x00, ON=0x7F |
| | | SAMPLER | | | | 8 | 55 | NOTE | G3 | 37 | hh | 97 | 37 | hh | OFF=0x00, ON=0x7F |
| | | | | +SHIFT | MODE | 9 | 55 | NOTE | G3 | 37 | hh | 98 | 37 | hh | OFF=0x00, ON=0x7F |
| | | KEYBOARD | | | | 8 | 71 | NOTE | B4 | 47 | hh | 97 | 47 | hh | OFF=0x00, ON=0x7F |
| | | | | +SHIFT | MODE | 9 | 71 | NOTE | B4 | 47 | hh | 98 | 47 | hh | OFF=0x00, ON=0x7F |
| | | PAD FX 2 | | | | 8 | 87 | NOTE | D#6 | 57 | hh | 97 | 57 | hh | OFF=0x00, ON=0x7F |
| | | | | +SHIFT | MODE | 9 | 87 | NOTE | D#6 | 57 | hh | 98 | 57 | hh | OFF=0x00, ON=0x7F |
| | | BEAT LOOP | | | | 8 | 103 | NOTE | G7 | 67 | hh | 97 | 67 | hh | OFF=0x00, ON=0x7F |
| | | | | +SHIFT | MODE | 9 | 103 | NOTE | G7 | 67 | hh | 98 | 67 | hh | OFF=0x00, ON=0x7F |
| | | KEY SHIFT | | | | 8 | 119 | NOTE | B8 | 77 | hh | 97 | 77 | hh | OFF=0x00, ON=0x7F |
| | | | | +SHIFT | MODE | 9 | 119 | NOTE | B8 | 77 | hh | 98 | 77 | hh | OFF=0x00, ON=0x7F |
| | | HOT CUE | Deck 2 | Press | | 10 | G-1 | NOTE | 99 | 07 | hh | 99 | 07 | hh | OFF=0x00, ON=0x7F |
| | | | | +SHIFT | MODE | 11 | G-1 | NOTE | 9A | 07 | hh | 9A | 07 | hh | OFF=0x00, ON=0x7F |
| | | PAD FX 1 | | | | 10 | 23 | NOTE | B0 | 17 | hh | 99 | 17 | hh | OFF=0x00, ON=0x7F |
| | | | | +SHIFT | MODE | 11 | 23 | NOTE | B0 | 17 | hh | 9A | 17 | hh | OFF=0x00, ON=0x7F |
| | | BEAT JUMP | | | | 10 | 39 | NOTE | D#2 | 27 | hh | 99 | 27 | hh | OFF=0x00, ON=0x7F |
| | | | | +SHIFT | MODE | 11 | 39 | NOTE | D#2 | 27 | hh | 9A | 27 | hh | OFF=0x00, ON=0x7F |
| | | SAMPLER | | | | 10 | 55 | NOTE | G3 | 37 | hh | 99 | 37 | hh | OFF=0x00, ON=0x7F |
| | | | | +SHIFT | MODE | 11 | 55 | NOTE | G3 | 37 | hh | 9A | 37 | hh | OFF=0x00, ON=0x7F |
| | | KEYBOARD | | | | 10 | 71 | NOTE | B4 | 47 | hh | 99 | 47 | hh | OFF=0x00, ON=0x7F |
| | | | | +SHIFT | MODE | 11 | 71 | NOTE | B4 | 47 | hh | 9A | 47 | hh | OFF=0x00, ON=0x7F |
| | | PAD FX 2 | | | | 10 | 87 | NOTE | D#6 | 57 | hh | 99 | 57 | hh | OFF=0x00, ON=0x7F |
| | | | | +SHIFT | MODE | 11 | 87 | NOTE | D#6 | 57 | hh | 9A | 57 | hh | OFF=0x00, ON=0x7F |
| | | BEAT LOOP | | | | 10 | 103 | NOTE | G7 | 67 | hh | 99 | 67 | hh | OFF=0x00, ON=0x7F |
| | | | | +SHIFT | MODE | 11 | 103 | NOTE | G7 | 67 | hh | 9A | 67 | hh | OFF=0x00, ON=0x7F |
| | | KEY SHIFT | | | | 10 | 119 | NOTE | B8 | 77 | hh | 99 | 77 | hh | OFF=0x00, ON=0x7F |
| | | | | +SHIFT | MODE | 11 | 119 | NOTE | B8 | 77 | hh | 9A | 77 | hh | OFF=0x00, ON=0x7F |

### Illumination control

| Group | Name | Deck | Function | MIDI Channel (Dec) | NOTE/CC (Dec) | MIDI Data (Data 1) (English scale) | MIDI-IN Status (Hex) | MIDI-IN Data 1 (Hex) | MIDI-IN Data 2 (Hex) | MIDI-OUT Status (Hex) | MIDI-OUT Data 1 (Hex) | MIDI-OUT Data 2 (Hex) | Details (Data 2) |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| LOADED | | Deck 1 | Track Load Illumination | 16 | NOTE | C-1 | | | | 9F | 00 | 7F | |
| | | Deck 2 | | 16 | NOTE | C#-1 | | | | 9F | 01 | 7F | |

### Settings

| Group | Name | Deck | Function | MIDI Channel (Dec) | NOTE/CC (Dec) | MIDI Data (Data 1) (English scale) | MIDI-IN Status (Hex) | MIDI-IN Data 1 (Hex) | MIDI-IN Data 2 (Hex) | MIDI-OUT Status (Hex) | MIDI-OUT Data 1 (Hex) | MIDI-OUT Data 2 (Hex) | Details (Data 2) |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| VINYL MODE | | Deck 1 | Vinyl mode on/off *2 | 1 | 23 | CC | B0 | 17 | hh | | | | OFF=0x00, ON=0x7F (Default: ON) |
| | | Deck 2 | | 2 | 23 | CC | B0 | 17 | hh | | | | OFF=0x00, ON=0x7F (Default: ON) |

*1 As a reference for MIDI assign, MIDI message sent from buttons and knobs of this controller are listed in decimal numbers and English scale. Please utilize this reference depending on the notation of your MIDI compatible software.
*2 Vinyl mode can't be changed from the unit. To make changes, send MIDI-OUT messages to the unit from the DJ application.
*3 Only the [BEAT SYNC] button sends MIDI messages to the computer when the finger is released from the button, not when the button is pressed.
*4 The [FX ON/OFF] button blinks when NOTE ON is received, and the button lights up when NOTE OFF is received.

