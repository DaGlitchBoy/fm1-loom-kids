/* SPDX-License-Identifier: MPL-2.0 */
/* A separate browser renderer with the same pattern and sample layout as the FM-1. */
#define KRENDER __attribute__((used, visibility("default")))

static ks_blob_t *kr_input;
static int32_t kr_pcm[APP_BLOCK * 2];

KRENDER uint8_t *lion_song_input(uint32_t size)
{
    ks_drop(kr_input);
    kr_input = size <= KFS_SONG_MAX ? ks_blob(size) : NULL;
    return kr_input ? kr_input->data : NULL;
}

KRENDER uint32_t lion_song_start(void)
{
    ks_decoded_t decoded = {0};
    ks_blob_t *input = kr_input;
    kr_input = NULL;
    int rc = !ks_has_song(input) || ks_unpack(input, &decoded, 1);
    ks_drop(input);
    if (rc) return 0;

    uint32_t steps = 0;
    for (int t = 0; t < NTRK; t++) {
        uint32_t len = decoded.song->patterns.pat[t][0].len;
        if (len != 16 && len != 32 && len != 64) {
            ks_unload(&decoded);
            return 0;
        }
        if (len > steps) steps = len;
    }
    ks_apply(&decoded, 0);
    /* Flush the song's settings and Stop before requesting the first note. */
    engine_lookahead(0);
    engine_kids_metro(0);
    engine_render(kr_pcm, APP_BLOCK, 4096);
    engine_transport(3);
    /* All Kids track lengths are powers of two with the same step rate. Round
     * down to the last frame of the cycle so the next downbeat never starts. */
    uint32_t frames_q8 = seq_step_q8(&seq, 0) * steps;
    return (frames_q8 - 1u) >> 8;
}

KRENDER const int32_t *lion_song_pcm(uint32_t frames)
{
    if (frames && frames <= APP_BLOCK) engine_render(kr_pcm, frames, 4096);
    return kr_pcm;
}

KRENDER void lion_song_stop(void) { engine_transport(0); }
#undef KRENDER
