/* SPDX-License-Identifier: MPL-2.0 */
/* Versioned Kids snapshots reuse LOOM's native settings, patterns and LZ codec.
 * Decode and allocate every typed bank before changing the working song. */
typedef struct {
    uint8_t scale[NTRK], chord[NTRK], root[NTRK];
    uint16_t picks[NTRK][KC_COUNT], kit_mutes[NSCENE + 1][NTRK];
    uint8_t shaping[NTRK][KD_COUNT], fx_on[NTRK][2], fx_amount[NTRK][2];
    uint8_t arp_on[NTRK], arp_order[NTRK], arp_rate[NTRK];
    int8_t octave;
    uint8_t metro, delay;
    uint32_t sample_crc;
    uint8_t user_samples;
} ks_meta_t;

typedef struct {
    settings_t settings;
    patterns_t patterns;
    ks_meta_t kids;
} ks_song_t;

#define KS_VERSION 3u
#define KS_HEADER_V1 24u
#define KS_LEGACY_PIXELS 72u
#define KS_HEADER_V2 (KS_HEADER_V1 + 4u + KS_LEGACY_PIXELS)
#define KS_HEADER (KS_HEADER_V1 + 4u + 1u + KPIX_BYTES)
#define KS_RAW_MAX (sizeof(ks_song_t) + NTRK * (4u + sizeof pats.pat[0] + NLANE * sizeof(lane_t)))
#define KS_BLOB_MAX (KS_HEADER + KS_RAW_MAX)
#define KS_MAGIC 0x53444b4cu /* LKDS */

struct ks_blob {
    uint32_t len, refs;
    uint8_t data[];
};

static ks_blob_t *ks_blob(uint32_t len)
{
    if (!len || len > KS_BLOB_MAX) return NULL;
    ks_blob_t *blob = malloc(sizeof *blob + len);
    if (blob) *blob = (ks_blob_t){.len = len, .refs = 1};
    return blob;
}

static ks_blob_t *ks_keep(ks_blob_t *blob)
{
    if (blob) blob->refs++;
    return blob;
}

static void ks_drop(ks_blob_t *blob)
{
    if (blob && --blob->refs == 0) free(blob);
}

static int ks_same(const ks_blob_t *a, const ks_blob_t *b)
{
    return a == b || (a && b && a->len == b->len && !memcmp(a->data, b->data, a->len));
}

/* The fixed pixel-name prefix can be read or replaced without unpacking music. */
static uint32_t ks_header(const ks_blob_t *blob)
{
    if (!blob || blob->len < KS_HEADER_V1 || get32(blob->data) != KS_MAGIC) return 0;
    unsigned version = get32(blob->data + 4);
    if (version == 1) return KS_HEADER_V1;
    uint32_t header = version == 2 ? KS_HEADER_V2 : version == KS_VERSION ? KS_HEADER : 0;
    if (!header || blob->len < header ||
        get32(blob->data + KS_HEADER_V1) != crc32_of(blob->data + KS_HEADER_V1 + 4, header - KS_HEADER_V1 - 4))
        return 0;
    return header;
}

static uint8_t ks_read_name(const ks_blob_t *blob, int slot, uint8_t *pixels)
{
    uint32_t header = ks_header(blob);
    uint8_t name = kpix_default_ids[slot];
    const uint8_t *stored = blob && header ? blob->data + KS_HEADER_V1 + 4 : NULL;
    if (header == KS_HEADER) {
        if (stored[0] < KPIX_COUNT || stored[0] == KPIX_UNSET) {
            memcpy(pixels, stored + 1, KPIX_BYTES);
            return stored[0];
        }
    } else if (header == KS_HEADER_V2) {
        memset(pixels, 0, KPIX_BYTES);
        kpix_from_legacy(pixels, stored);
        for (int i = 0; i < KPIX_COUNT; i++) {
            if (memcmp(pixels, kpix_bank[i], KPIX_BYTES)) continue;
            for (int j = 0; j < KPIX_BYTES; j++) if (pixels[j]) return (uint8_t)i;
        }
        for (int i = 0; i < KPIX_BYTES; i++) if (pixels[i]) return KPIX_UNSET;
    }
    kpix_default(name, pixels);
    return name;
}

static void ks_set_name(ks_blob_t *blob, uint8_t name, const uint8_t *pixels)
{
    uint8_t *stored = blob->data + KS_HEADER_V1 + 4;
    stored[0] = name;
    memcpy(stored + 1, pixels, KPIX_BYTES);
    put32(blob->data + KS_HEADER_V1, crc32_of(stored, 1 + KPIX_BYTES));
}

/* A header without a music payload reserves the picture on an empty slot. */
static int ks_has_song(const ks_blob_t *blob)
{
    return blob && !(blob->len == KS_HEADER && ks_header(blob) == KS_HEADER &&
        !get32(blob->data + 8) && !get32(blob->data + 12) &&
        !get32(blob->data + 16) && !get32(blob->data + 20));
}

static ks_blob_t *ks_name_only(uint8_t name, const uint8_t *pixels)
{
    ks_blob_t *blob = ks_blob(KS_HEADER);
    if (!blob) return NULL;
    memset(blob->data, 0, KS_HEADER_V1);
    put32(blob->data, KS_MAGIC);
    put32(blob->data + 4, KS_VERSION);
    ks_set_name(blob, name, pixels);
    return blob;
}

static ks_blob_t *ks_rename(const ks_blob_t *old, uint8_t name, const uint8_t *pixels)
{
    uint32_t header = ks_header(old);
    if (!header || get32(old->data + 12) != old->len - header) return NULL;
    ks_blob_t *blob = ks_blob(KS_HEADER + old->len - header);
    if (!blob) return NULL;
    memcpy(blob->data, old->data, KS_HEADER_V1);
    put32(blob->data + 4, KS_VERSION);
    ks_set_name(blob, name, pixels);
    memcpy(blob->data + KS_HEADER, old->data + header, old->len - header);
    return blob;
}

/* User sample numbers are local bank references. Refuse to open with a
 * different/missing bank instead of silently playing a different recording. */
static int ks_user_sample(int index, int *missing)
{
    if (index < smp_factory()) return 0;
    if (!smp_get(index)) *missing = 1;
    return 1;
}

static int ks_sample_refs(const settings_t *settings, const patterns_t *patterns, int *missing)
{
    int refs = 0;
    for (int t = 0; t < NTRK; t++) {
        const uint8_t *p = settings->trk[t].par;
        if (p[TP_ENGINE] == EG_SAMPLE || p[TP_ENGINE] == EG_SLICE)
            refs |= ks_user_sample(p[TP_E1], missing);
        if (p[TP_ENGINE] == EG_DRUMLINE)
            for (int k = 0; k < NDRUM; k++)
                if (p[TP_DRUM(k, DP_MODEL)] % NDM == DM_SAMPLE)
                    refs |= ks_user_sample(p[TP_DRUM(k, DP_K1)], missing);
    }
    for (int i = 0; i < NLANE; i++) {
        const lane_t *l = &patterns->lane[i];
        if (!l->used || l->trk >= NTRK) continue;
        const uint8_t *p = settings->trk[l->trk].par;
        int sample = (p[TP_ENGINE] == EG_SAMPLE || p[TP_ENGINE] == EG_SLICE) && l->param == TP_E1;
        if (p[TP_ENGINE] == EG_DRUMLINE)
            for (int k = 0; k < NDRUM; k++)
                sample |= l->param == TP_DRUM(k, DP_K1) && p[TP_DRUM(k, DP_MODEL)] % NDM == DM_SAMPLE;
        if (sample) for (int step = 0; step < NSTEP; step++)
            if (l->val[step] != LOCK_NONE) refs |= ks_user_sample(l->val[step], missing);
    }
    return refs;
}

static int ks_bank_fingerprint(uint32_t *crc)
{
    uint32_t len = smp_user_bytes();
    return len && !smp_bank_crc(len, crc);
}

typedef struct { uint8_t *data; uint32_t used, limit; } ks_sink_t;
static int ks_write_bytes(void *ctx, const uint8_t *p, uint32_t n)
{
    ks_sink_t *s = ctx;
    if (n > s->limit - s->used) return -1;
    if (s->data) memcpy(s->data + s->used, p, n);
    s->used += n;
    return 0;
}

static ks_blob_t *ks_encode(const ks_song_t *song, uint32_t len, uint8_t name, const uint8_t *pixels)
{
    uint8_t *compressed = malloc(len);
    if (!compressed) return NULL;
    ks_sink_t out = {compressed, 0, (uint32_t)len};
    lz_sink_t sink = {ks_write_bytes, &out};
    int packed = !lz_compress((const uint8_t *)song, (uint32_t)len, &sink) && out.used < len;
    uint32_t stored = packed ? out.used : (uint32_t)len;
    ks_blob_t *blob = ks_blob(KS_HEADER + stored);
    if (blob) {
        put32(blob->data, KS_MAGIC);
        put32(blob->data + 4, KS_VERSION);
        put32(blob->data + 8, (uint32_t)len);
        put32(blob->data + 12, stored);
        put32(blob->data + 16, crc32_of((const uint8_t *)song, (uint32_t)len));
        put32(blob->data + 20, (uint32_t)packed);
        ks_set_name(blob, name, pixels);
        memcpy(blob->data + KS_HEADER, packed ? compressed : (const uint8_t *)song, stored);
    }
    free(compressed);
    return blob;
}

typedef struct { const void *data; uint32_t size; } ks_part_t;

static int ks_write_parts(const ks_part_t *parts, int count, lz_sink_t *sink, int packed)
{
    for (int i = 0; i < count; i++) {
        const ks_part_t *part = &parts[i];
        if (packed) {
            /* Each segment uses local matches. Concatenated tokens still form
             * one ordinary LZ stream that the existing reader can decode. */
            if (lz_compress(part->data, part->size, sink)) return -1;
        } else {
            const uint8_t *p = part->data;
            for (uint32_t offset = 0; offset < part->size;) {
                uint32_t bytes = part->size - offset;
                if (bytes > LZ_MAXLIT) bytes = LZ_MAXLIT;
                if (sink->put(sink->ctx, p + offset, bytes)) return -1;
                offset += bytes;
            }
        }
    }
    return 0;
}

static ks_blob_t *ks_pack(void)
{
    _Static_assert(sizeof(ks_song_t) == sizeof(settings_t) + sizeof(patterns_t) + sizeof(ks_meta_t),
                   "streaming requires a packed sequence of the three song members");
    ks_meta_t metadata;
    ks_meta_t *m = &metadata;
    memset(m, 0, sizeof *m);
#define KS_STORE(field, source) memcpy(m->field, source, sizeof m->field)
    KS_STORE(scale, kids_scale); KS_STORE(chord, kids_chord); KS_STORE(root, kids_root);
    KS_STORE(picks, kids_picks); KS_STORE(kit_mutes, kids_kit_mutes);
    KS_STORE(shaping, kids_shaping); KS_STORE(fx_on, kids_fx_on); KS_STORE(fx_amount, kids_fx_amount);
    KS_STORE(arp_on, kids_arp_on); KS_STORE(arp_order, kids_arp_order); KS_STORE(arp_rate, kids_arp_rate);
#undef KS_STORE
    m->octave = ui.oct;
    m->metro = kids_metro;
    m->delay = kids_delay_choice;
    int missing = 0;
    m->user_samples = (uint8_t)ks_sample_refs(&proj, &pats, &missing);
    if (m->user_samples) ks_bank_fingerprint(&m->sample_crc);

    uint32_t settings_prefix[] = {proj.magic, PROJ_FORMAT};
    _Static_assert(offsetof(settings_t, name) == sizeof settings_prefix,
                   "settings prefix contains only magic and format");
    ks_part_t parts[4 + 3 * NTRK] = {
        {settings_prefix, sizeof settings_prefix},
        {(const uint8_t *)&proj + sizeof settings_prefix, sizeof proj - sizeof settings_prefix},
        {&pats, sizeof pats}, {m, sizeof *m}
    };
    uint8_t banks[NTRK][4];
    int count = 4;
    for (int t = 0; t < NTRK; t++) {
        const kids_bank_t *b = kids_other[t];
        put32(banks[t], b ? (uint32_t)b->nlanes + 1 : 0);
        parts[count++] = (ks_part_t){banks[t], sizeof banks[t]};
        if (!b) continue;
        parts[count++] = (ks_part_t){b->patterns, sizeof b->patterns};
        if (b->nlanes)
            parts[count++] = (ks_part_t){b->lanes, (uint32_t)b->nlanes * sizeof(lane_t)};
    }

    uint32_t len = 0, crc = 0;
    for (int i = 0; i < count; i++) {
        len += parts[i].size;
        crc = crc32_add(crc, parts[i].data, parts[i].size);
    }
    if (len > KS_RAW_MAX) return NULL;
    ks_sink_t measure = {.limit = len};
    lz_sink_t sink = {ks_write_bytes, &measure};
    int packed = !ks_write_parts(parts, count, &sink, 1) && measure.used < len;
    uint32_t stored = packed ? measure.used : len;
    ks_blob_t *blob = ks_blob(KS_HEADER + stored);
    if (!blob) return NULL;
    ks_sink_t out = {blob->data + KS_HEADER, 0, stored};
    sink.ctx = &out;
    if (ks_write_parts(parts, count, &sink, packed) || out.used != stored) {
        ks_drop(blob);
        return NULL;
    }
    put32(blob->data, KS_MAGIC);
    put32(blob->data + 4, KS_VERSION);
    put32(blob->data + 8, len);
    put32(blob->data + 12, stored);
    put32(blob->data + 16, crc);
    put32(blob->data + 20, (uint32_t)packed);
    ks_set_name(blob, kpix_song_id, kpix_song);
    return blob;
}

static ks_blob_t *ks_new_song(uint8_t name, const uint8_t *pixels)
{
    uint32_t len = sizeof(ks_song_t) + NTRK * 4u;
    ks_song_t *song = calloc(1, len);
    if (!song) return NULL;
    project_defaults_into(&song->settings, &song->patterns);
    static const char *const names[NTRK] = {
        "acid line", "marimba", "Twill", "bellows", "dusty pad", "breath lead", "flute", "whisper pad"
    };
    static const uint8_t engines[NTRK] = {
        EG_ACID, EG_RAYON, EG_DRUMLINE, EG_BELLOWS, EG_KNIT, EG_CANE, EG_SAMPLE, EG_VOILE
    };
    for (int t = 0; t < NTRK; t++) {
        trk_t *track = &song->settings.trk[t];
        sound_default(track->par, engines[t]);
        if (engines[t] == EG_DRUMLINE) {
            uint8_t kit[KIT_LEN];
            int k = 0;
            for (int i = 1; i < NFKITS; i++)
                if (name_is(kit_name(i), names[t])) { k = i; break; }
            kit_factory(k, kit);
            kit_unpack_sound(track, kit, 0);
        } else {
            int preset = preset_named(engines[t], names[t]);
            apply_preset(track, preset);
            if (preset >= 0 && kids_wet_category(kids_role(PRESETS[preset].role)))
                song->kids.fx_on[t][0] = song->kids.fx_on[t][1] = 1;
        }
        track->par[TP_CHAN] = 1;
    }
    song->settings.trk[AUX_FX1].par[TP_SEND2] = 0;
    song->kids.delay = 2;
    for (int t = 0; t < NTRK; t++) {
        song->kids.root[t] = 60;
        song->kids.fx_amount[t][0] = 48;
        song->kids.fx_amount[t][1] = 52;
        song->kids.arp_rate[t] = 3;
        memcpy(song->kids.shaping[t], kids_shape_defaults, KD_COUNT);
    }
    ks_blob_t *blob = ks_encode(song, len, name, pixels);
    free(song);
    return blob;
}

typedef struct {
    ks_song_t *song;
    kids_bank_t *bank[NTRK];
} ks_decoded_t;

static void ks_unload(ks_decoded_t *d)
{
    free(d->song);
    for (int t = 0; t < NTRK; t++) free(d->bank[t]);
    *d = (ks_decoded_t){0};
}

static int ks_lane_valid(const lane_t *lane)
{
    return !lane->used || (lane->trk < NALLTRK && lane->pat < NPATT &&
        (lane->param < NTP || lane_is_note(lane->param)));
}

static int ks_pattern_valid(const tpat_t *p)
{
    return p->len >= 1 && p->len <= NSTEP && p->scale < NTS &&
        p->groove >= -99 && p->groove <= 99 && p->nlen <= 100 && p->quant <= 100;
}

static int ks_meta_valid(const ks_meta_t *m)
{
    if (m->octave < -4 || m->octave > 4 || m->delay >= 5 || m->metro > 100 || m->user_samples > 1) return 0;
    for (int t = 0; t < NTRK; t++) {
        const uint8_t *p = m->shaping[t];
        if (m->scale[t] >= KS_COUNT || m->chord[t] >= KC_VOICINGS || !m->root[t] || m->root[t] > 127 ||
            m->arp_on[t] > 1 || m->arp_order[t] >= 5 || m->arp_rate[t] >= 5 ||
            p[KD_TARGET] >= KD_TARGET_COUNT || p[KD_RATE] >= KD_RATE_COUNT || p[KD_WAVE] >= KD_WAVE_COUNT ||
            p[KD_DEPTH] > 2 * KD_DEPTH_ZERO || p[KD_ENV_ON] > 1 || p[KD_LFO_ON] > 1 || p[KD_EDIT_ON] > 1)
            return 0;
        for (int c = 0; c < KC_COUNT; c++)
            if (m->picks[t][c] > (unsigned)kids_catalog_size(c)) return 0;
        for (int b = 0; b < 2; b++)
            if (m->fx_on[t][b] > 1 || m->fx_amount[t][b] > 127) return 0;
    }
    return 1;
}

/* 0 success, -1 malformed/unsupported, -2 allocation, -3 unavailable asset. */
static int ks_unpack(const ks_blob_t *blob, ks_decoded_t *d, int assets)
{
    uint32_t header = ks_header(blob);
    if (!header) return -1;
    const uint8_t *src = blob->data;
    uint32_t len = get32(src + 8), stored = get32(src + 12), packed = get32(src + 20);
    if (len < sizeof(ks_song_t) + NTRK * 4 || len > KS_RAW_MAX || stored != blob->len - header ||
        packed > 1 || (!packed && stored != len)) return -1;
    d->song = malloc(len);
    if (!d->song) return -2;
    uint8_t *raw = (uint8_t *)d->song;
    int rc = -1;
    if (packed) {
        set_in_t in = {src + header, stored};
        lz_src_t stream = {set_get, &in};
        if (lz_decompress(&stream, raw, len) || in.left) goto fail;
    } else memcpy(raw, src + header, len);
    if (crc32_of(raw, len) != get32(src + 16)) goto fail;

    const ks_song_t *song = d->song;
    settings_t settings = song->settings;
    if (settings.format != PROJ_FORMAT || settings.trk_sel >= NTRK || !settings_valid(&settings) ||
        !ks_meta_valid(&song->kids)) goto fail;
    int kits = 0;
    for (int t = 0; t < NTRK; t++) kits += settings.trk[t].par[TP_ENGINE] == EG_DRUMLINE;
    if (kits > NDL) goto fail;
    if (assets) {
        int missing = 0;
        int references = ks_sample_refs(&settings, &song->patterns, &missing);
        uint32_t crc;
        if (references != song->kids.user_samples || missing ||
            (references && (!ks_bank_fingerprint(&crc) || crc != song->kids.sample_crc))) {
            rc = -3;
            goto fail;
        }
        d->song->settings = settings;
    }
    for (int t = 0; t < NSEQT; t++)
        for (int p = 0; p < NPATT; p++) if (!ks_pattern_valid(&song->patterns.pat[t][p])) goto fail;
    for (int i = 0; i < NLANE; i++) if (!ks_lane_valid(&song->patterns.lane[i])) goto fail;
    const uint8_t *p = raw + sizeof *song, *end = raw + len;
    for (int t = 0; t < NTRK; t++) {
        if (end - p < 4) goto fail;
        uint32_t count = get32(p);
        p += 4;
        if (!count) continue;
        count--;
        if (count > NLANE) goto fail;
        size_t banksize = sizeof pats.pat[0] + (size_t)count * sizeof(lane_t);
        if ((size_t)(end - p) < banksize) goto fail;
        kids_bank_t *b = d->bank[t] = malloc(sizeof *b + (size_t)count * sizeof(lane_t));
        if (!b) { rc = -2; goto fail; }
        b->nlanes = (int)count;
        memcpy(b->patterns, p, sizeof b->patterns);
        memcpy(b->lanes, p + sizeof b->patterns, (size_t)count * sizeof(lane_t));
        p += banksize;
        for (int j = 0; j < NPATT; j++) if (!ks_pattern_valid(&b->patterns[j])) goto fail;
        for (uint32_t j = 0; j < count; j++)
            if (!ks_lane_valid(&b->lanes[j]) || !b->lanes[j].used || b->lanes[j].trk != t) goto fail;
    }
    if (p != end) goto fail;
    return 0;
fail:
    ks_unload(d);
    return rc;
}

static void ks_apply(ks_decoded_t *d, int dirty)
{
    kids_rec_stop();
    kids_notes_off();
    engine_transport(0);
    engine_panic();
    proj = d->song->settings;
    pats = d->song->patterns;
    proj_slot = NPROJ;
    patterns_clamp();
    /* Release old kit units first, including kits on higher-numbered tracks.
     * A new three-kit song must be able to claim all three units on its first load. */
    for (int t = 0; t < NTRK; t++)
        if (eng_drum[t] && !is_drum(t)) engine_set_all(t);
    project_apply();
    apply_brain();
    for (int t = 0; t < NSEQT; t++) weave_apply(t);
    const ks_meta_t *m = &d->song->kids;
#define KS_RESTORE(target, field) memcpy(target, m->field, sizeof m->field)
    KS_RESTORE(kids_scale, scale); KS_RESTORE(kids_chord, chord); KS_RESTORE(kids_root, root);
    KS_RESTORE(kids_picks, picks); KS_RESTORE(kids_kit_mutes, kit_mutes);
    KS_RESTORE(kids_shaping, shaping); KS_RESTORE(kids_fx_on, fx_on); KS_RESTORE(kids_fx_amount, fx_amount);
    KS_RESTORE(kids_arp_on, arp_on); KS_RESTORE(kids_arp_order, arp_order); KS_RESTORE(kids_arp_rate, arp_rate);
#undef KS_RESTORE
    for (int t = 0; t < NTRK; t++) {
        free(kids_other[t]);
        kids_other[t] = d->bank[t];
        d->bank[t] = NULL;
        for (int b = 0; b < 2; b++) kids_send_gain[t][b] = kids_fx_on[t][b] ? kids_fx_amount[t][b] : 0;
        for (int p = 0; p < KD_COUNT; p++) engine_kids_dsp(t, p, kids_shaping[t][p]);
    }
    kids_delay_choice = m->delay;
    kids_update_delay();
    kids_metro = m->metro;
    engine_kids_metro(kids_metro);
    ui.oct = m->octave;
    ui.bar = 0;
    ui.dsound = 0;
    ui.last_inst = proj.trk_sel;
    kids_sound_error = NULL;
    kids_cell.track = -1;
    memset(kids_perf_page, 0, sizeof kids_perf_page);
    memset(kids_cursor, 0, sizeof kids_cursor);
    kids_perf_manual = 0;
    kids_category_sync();
    kids_remember_chord(sel_trk());
    ui.dirty = (uint8_t)dirty;
    ks_unload(d);
}
