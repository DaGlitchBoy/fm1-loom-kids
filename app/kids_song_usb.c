/* SPDX-License-Identifier: MPL-2.0 */
/* Pull-only song export over USB MIDI. Each request identifies one bounded chunk. */
#define LK_USB_CHUNK 112u

static int kids_song_usb(const uint8_t *p, uint32_t n)
{
    static const uint8_t prefix[] = {0x7d, 'L', 'K'};
    if (n != 8 || memcmp(p, prefix, sizeof prefix) || p[3] != 'Q') return 0;
    unsigned slot = p[5], offset = p[6] | (unsigned)p[7] << 7;
    uint8_t reply[17 + LK_USB_CHUNK + LK_USB_CHUNK / 7 + 1] = {
        0xf0, 0x7d, 'L', 'K', 'R', p[4], p[5], p[6], p[7]
    };
    const ks_slot_t *saved = slot < KS_SLOTS ? &ks_slots[slot] : NULL;
    unsigned size = 0, written = 17;
    uint32_t revision = 0;
    if (seq.playing || seq.req_start || seq.req_cont || seq.rec || seq.rec_wait) reply[9] = 1;
    else if (!saved || !ks_ready || (ks_bad >> slot & 1u)) reply[9] = 2;
    else {
        revision = saved->revision;
        if (saved->blob) size = saved->blob->len;
        if (size > KFS_SONG_MAX || offset > size) {
            reply[9] = 2;
            size = 0;
        }
    }
    reply[10] = size & 127u;
    reply[11] = size >> 7;
    for (unsigned i = 0; i < 5; i++) reply[12 + i] = (revision >> (7 * i)) & 127u;
    if (!reply[9] && offset < size) {
        const uint8_t *data = saved->blob->data;
        unsigned end = size - offset < LK_USB_CHUNK ? size : offset + LK_USB_CHUNK;
        while (offset < end) {
            unsigned mask = written++;
            for (unsigned bit = 0; bit < 7 && offset < end; bit++, offset++) {
                uint8_t byte = data[offset];
                reply[mask] |= (byte >> 7) << bit;
                reply[written++] = byte & 127u;
            }
        }
    }
    reply[written++] = 0xf7;
    /* One complete frame or none. The browser retries when the MIDI queue is full. */
    if (plat_sysex_room() >= written) plat_sysex_send(reply, written);
    return 1;
}
