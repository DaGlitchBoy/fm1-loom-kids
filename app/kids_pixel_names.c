/* SPDX-License-Identifier: MPL-2.0 */
enum { KPIX_SIDE = 8, KPIX_BYTES = KPIX_SIDE * KPIX_SIDE / 2, KPIX_COUNT = 15, KPIX_COLORS = 16, KPIX_UNSET = 255 };

static uint8_t kpix_song[KPIX_BYTES], kpix_song_id;
static const uint8_t kpix_default_ids[] = {3, 0, 7};
static uint8_t kpix_bank[KPIX_COUNT][KPIX_BYTES];
static uint8_t kpix_open, kpix_slot, kpix_x, kpix_y, kpix_color;
static uint8_t kpix_dirty;
static uint32_t kpix_changed, kpix_generation;

static const char kpix_defaults[10][8][9] = {
    { "00000000","00333300","03333330","03133130","03133130","03311330","00333300","00000000" }, /* smile */
    { "00000000","55000055","52555525","55555555","51555155","55525555","05555550","00000000" }, /* cat */
    { "00000000","04400440","44444444","44444444","04444440","00444400","00044000","00000000" }, /* heart */
    { "05000050","57777775","57333375","73133137","73355337","73311337","57333375","05777750" }, /* lion */
    { "00000000","00066600","00666660","06616660","66666660","00666700","06667770","00000000" }, /* duck */
    { "00000000","80088800","08888880","00881888","08888880","80088800","00000000","00000000" }, /* fish */
    { "00900900","00099000","09999990","09911990","09999990","09922990","09099090","00000000" }, /* robot */
    { "00077000","00722700","00722700","00777700","07777770","07044070","00033000","00000000" }, /* rocket */
    { "00000660","00000660","00000600","00000600","00000600","00066600","00666600","00066000" }, /* note */
    { "00022000","002AA200","02AAAA20","2AA11AA2","2AAAAAA2","2A222AA2","2A0AA0A2","00000000" }, /* ghost */
};

static int kpix_get(const uint8_t *pixels, int x, int y)
{
    int p = y * KPIX_SIDE + x;
    return (pixels[p / 2] >> ((p & 1) * 4)) & 15;
}

static void kpix_put(uint8_t *pixels, int x, int y, int color)
{
    int p = y * KPIX_SIDE + x, shift = (p & 1) * 4;
    pixels[p / 2] = (uint8_t)((pixels[p / 2] & ~(15 << shift)) | (color << shift));
}

static void kpix_default(int slot, uint8_t *pixels)
{
    memset(pixels, 0, KPIX_BYTES);
    if (slot < 10)
        for (int y = 0; y < 8; y++)
            for (int x = 0; x < 8; x++) {
                char c = kpix_defaults[slot][y][x];
                kpix_put(pixels, x, y, c <= '9' ? c - '0' : c - 'A' + 10);
            }
}

static void kpix_reset_slot(int slot) { kpix_default(slot, kpix_bank[slot]); }

static void kpix_init(void)
{
    memset(kpix_song, 0, sizeof kpix_song);
    kpix_song_id = KPIX_UNSET;
    for (int i = 0; i < KPIX_COUNT; i++) kpix_reset_slot(i);
    kpix_open = kpix_slot = kpix_x = kpix_y = kpix_dirty = 0;
    kpix_color = 3;
    kpix_generation = 0;
}

static void kpix_touch(void)
{
    kpix_dirty = 1;
    kpix_changed = plat_ms();
    kpix_generation++;
}

/* Old canvases placed the same eight-pixel drawing inside a two-pixel margin.
 * Keep that drawing and carry colored margin pixels into empty edge cells. */
static void kpix_from_legacy(uint8_t *pixels, const uint8_t *old)
{
    for (int y = 2; y < 10; y++)
        for (int x = 2; x < 10; x++) {
            unsigned p = (unsigned)(y * 12 + x);
            kpix_put(pixels, x - 2, y - 2, (old[p / 2] >> (4 * (p & 1))) & 15);
        }
    for (int y = 0; y < 12; y++)
        for (int x = 0; x < 12; x++) {
            int col = clampi(x - 2, 0, KPIX_SIDE - 1), row = clampi(y - 2, 0, KPIX_SIDE - 1);
            unsigned p = (unsigned)(y * 12 + x);
            int color = (old[p / 2] >> (4 * (p & 1))) & 15;
            if (color && !kpix_get(pixels, col, row)) kpix_put(pixels, col, row, color);
        }
}
