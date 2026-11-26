/* SPDX-License-Identifier: MPL-2.0 */
#ifndef KIDS_MUSIC_H
#define KIDS_MUSIC_H

#include <stdint.h>

enum { KS_CHROMATIC, KS_MAJOR, KS_MINOR, KS_MAJOR_PENT, KS_MINOR_PENT, KS_DORIAN, KS_EGYPTIAN, KS_COUNT };
enum { KC_SINGLE, KC_IN_SCALE, KC_MAJOR, KC_MINOR, KC_POWER, KC_MINOR7, KC_OCTAVES, KC_MAJOR7, KC_VOICINGS };

static const uint16_t kids_scale_mask[KS_COUNT] = {
    0xfff, 0xab5, 0x5ad, 0x295, 0x4a9, 0x6ad, 0x4a5
};

static inline int kids_in_scale(int note, int scale)
{
    return (kids_scale_mask[scale] >> (note % 12 + 12) % 12) & 1u;
}

static inline int kids_snap_note(int note, int scale)
{
    for (int delta = 0; delta < 12; delta++) {
        if (note - delta >= 1 && kids_in_scale(note - delta, scale)) return note - delta;
        if (note + delta <= 127 && kids_in_scale(note + delta, scale)) return note + delta;
    }
    return note;
}

static inline int kids_voice_notes(int root, int scale, int chord, uint8_t notes[4])
{
    static const uint8_t count[] = {1, 3, 3, 3, 2, 4, 2, 4};
    static const uint8_t intervals[][4] = {
        {0}, {0, 4, 7}, {0, 4, 7}, {0, 3, 7}, {0, 7}, {0, 3, 7, 10}, {0, 12}, {0, 4, 7, 11}
    };
    int pitches[4];
    root = kids_snap_note(root, scale);
    int n = count[chord];
    for (int i = 0; i < n; i++) pitches[i] = root + intervals[chord][i];
    if (chord == KC_IN_SCALE && scale != KS_CHROMATIC) {
        int pitch = root;
        for (int i = 1; i < n; i++) {
            for (int degree = 0; degree < 2; degree++)
                do { pitch++; } while (!kids_in_scale(pitch, scale));
            pitches[i] = pitch;
        }
    }
    int shift = pitches[n - 1] > 127 ? -12 : 0;
    for (int i = 0; i < n; i++) notes[i] = (uint8_t)(pitches[i] + shift);
    return n;
}
#endif
