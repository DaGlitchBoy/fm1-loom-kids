/* SPDX-License-Identifier: MPL-2.0 */
#include <assert.h>
#include "kids_music.h"

int main(void)
{
    static const int degrees[][12] = {
        {1,1,1,1,1,1,1,1,1,1,1,1}, {1,0,1,0,1,1,0,1,0,1,0,1},
        {1,0,1,1,0,1,0,1,1,0,1,0}, {1,0,1,0,1,0,0,1,0,1,0,0},
        {1,0,0,1,0,1,0,1,0,0,1,0}, {1,0,1,1,0,1,0,1,0,1,1,0},
        {1,0,1,0,0,1,0,1,0,0,1,0}
    };
    for (int scale = 0; scale < KS_COUNT; scale++)
        for (int octave = -4; octave <= 4; octave++) {
            int previous = 0;
            for (int key = 0; key < 27; key++) {
                int note = 53 + octave * 12 + key;
                int root = kids_snap_note(note, scale);
                assert(root >= previous && degrees[scale][root % 12]);
                previous = root;
                for (int chord = 0; chord < KC_VOICINGS; chord++) {
                    uint8_t voiced[4];
                    int n = kids_voice_notes(note, scale, chord, voiced);
                    assert(n >= 1 && n <= 4);
                    for (int i = 0; i < n; i++) {
                        assert(voiced[i] >= 1 && voiced[i] <= 127);
                        assert(i == 0 || voiced[i] > voiced[i - 1]);
                        if (chord == KC_IN_SCALE) assert(degrees[scale][voiced[i] % 12]);
                    }
                }
            }
        }
    uint8_t notes[4];
    assert(kids_voice_notes(62, KS_EGYPTIAN, KC_IN_SCALE, notes) == 3);
    assert(notes[0] == 62 && notes[1] == 67 && notes[2] == 72);
    assert(kids_voice_notes(61, KS_MAJOR, KC_MINOR7, notes) == 4);
    assert(notes[0] == 60 && notes[1] == 63 && notes[2] == 67 && notes[3] == 70);
    assert(kids_voice_notes(127, KS_CHROMATIC, KC_OCTAVES, notes) == 2);
    assert(notes[0] == 115 && notes[1] == 127);
    return 0;
}
