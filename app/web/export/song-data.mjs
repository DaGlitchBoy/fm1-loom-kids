// SPDX-License-Identifier: MPL-2.0
import { pairs } from "./fm1ports.mjs";

export const SONG_LIMIT = 16364;
export const CHUNK = 112;
const PREFIX = [0xf0, 0x7d, 0x4c, 0x4b];
const IDENT = 0x53444b4c;

export function requestBytes(token, slot, offset) {
  return [...PREFIX, 0x51, token, slot, offset & 127, offset >> 7, 0xf7];
}

export function replyBytes(data, token, slot, offset) {
  if (data.length < 9 || !PREFIX.every((b, i) => data[i] === b) ||
      data[4] !== 0x52 || data[5] !== token || data[6] !== slot ||
      (data[7] | data[8] << 7) !== offset) return null;
  if (data.length < 18 || data.at(-1) !== 0xf7 || data.subarray(5, -1).some(b => b > 127)) {
    throw Error("The FM-1 sent an incomplete song response.");
  }
  if (data[9] === 1) throw Error("Stop playback and recording on the FM-1, then try again.");
  if (data[9]) throw Error(`Slot ${slot + 1} could not be read.`);
  const size = data[10] | data[11] << 7;
  if (size > SONG_LIMIT || offset > size || data[16] > 15) throw Error("The song transfer is invalid.");
  const revision = data.subarray(12, 17).reduce((n, digit, i) => n + digit * 2 ** (i * 7), 0);
  const chunk = new Uint8Array(Math.min(CHUNK, size - offset));
  let at = 17;
  for (let i = 0; i < chunk.length;) {
    const mask = data[at++];
    for (let bit = 0; bit < 7 && i < chunk.length; bit++, i++) {
      chunk[i] = data[at++] | ((mask >> bit & 1) << 7);
    }
  }
  if (at !== data.length - 1) throw Error("The FM-1 sent the wrong number of song bytes.");
  return { size, revision, chunk };
}

function pull({ input, output }, token, slot, offset, timeout) {
  return new Promise((resolve, reject) => {
    function done(value, error) {
      clearTimeout(timer);
      input.removeEventListener("midimessage", receive);
      if (error) reject(error);
      else resolve(value);
    }
    function receive({ data }) {
      try {
        const reply = replyBytes(data, token, slot, offset);
        if (reply) done(reply);
      } catch (error) { done(null, error); }
    }
    const timer = setTimeout(() => done(null), timeout);
    input.addEventListener("midimessage", receive);
    try { output.send(requestBytes(token, slot, offset)); }
    catch (error) { done(null, error); }
  });
}

export async function readSongs(access, progress = () => {}) {
  // Probe pairs with our read-only command. A matching response identifies this firmware.
  const connected = pairs(access);
  const preferred = connected.filter(p => /lion kids|fm-?1|felucca/i.test(p.name));
  const token = crypto.getRandomValues(new Uint8Array(1))[0] & 127;
  let link, first;
  for (const candidate of preferred.length ? preferred : connected) {
    try {
      try { await candidate.input.open(); } catch { continue; }
      first = await pull(candidate, token, 0, 0, 480);
      if (first) {
        link = candidate;
        break;
      }
    } finally {
      if (!link) await closeLink(candidate);
    }
  }
  if (!link) throw Error("No LION KIDS FM-1 answered. Connect it over USB and check that it runs preview 0.0.3 or later.");

  try {
    async function request(slot, offset) {
      for (let tries = 0; tries < 3; tries++) {
        const reply = await pull(link, token, slot, offset, 420);
        if (reply) return reply;
      }
      throw Error("The FM-1 stopped responding. Check the USB connection and try again.");
    }
    const slots = [], revisions = [];
    for (let slot = 0; slot < 3; slot++) {
      progress(slot + 1);
      const head = slot === 0 ? first : await request(slot, 0);
      revisions.push(head.revision);
      if (!head.size) {
        slots.push(null);
        continue;
      }
      const bytes = new Uint8Array(head.size);
      bytes.set(head.chunk);
      for (let offset = head.chunk.length; offset < bytes.length; offset += CHUNK) {
        const reply = await request(slot, offset);
        if (reply.size !== head.size || reply.revision !== head.revision) {
          throw Error("A saved song changed during the transfer. Read the songs again.");
        }
        bytes.set(reply.chunk, offset);
      }
      describeSong(bytes);
      slots.push(bytes);
    }
    for (const [slot, revision] of revisions.entries()) {
      if ((await request(slot, 0)).revision !== revision) {
        throw Error("A saved song changed during the transfer. Read the songs again.");
      }
    }
    return slots;
  } finally {
    await closeLink(link);
  }
}

function closeLink({ input, output }) {
  return Promise.allSettled([input.close(), output.close()]);
}

function crc32(bytes) {
  let crc = 0xffffffff;
  for (const byte of bytes) {
    crc ^= byte;
    for (let i = 0; i < 8; i++) crc = (crc >>> 1) ^ ((crc & 1) ? 0xedb88320 : 0);
  }
  return (~crc) >>> 0;
}

export function describeSong(bytes) {
  const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
  if (bytes.length < 61 || bytes.length > SONG_LIMIT || view.getUint32(0, true) !== IDENT ||
      view.getUint32(4, true) !== 3 || view.getUint32(12, true) !== bytes.length - 61 ||
      crc32(bytes.subarray(28, 61)) !== view.getUint32(24, true)) {
    throw Error("The saved song file is damaged or uses an unsupported version.");
  }
  return { used: bytes.length > 61, pixels: bytes.subarray(29, 61) };
}

export function packSongs(slots) {
  return JSON.stringify({ format: "LION KIDS songs", version: 1,
    slots: slots.map(bytes => bytes && btoa(String.fromCharCode(...bytes))) });
}

export function unpackSongs(text) {
  const { format, version, slots } = JSON.parse(text);
  if (format !== "LION KIDS songs" || version !== 1 || !Array.isArray(slots) || slots.length !== 3) {
    throw Error("Choose a LION KIDS songs backup.");
  }
  return slots.map(value => {
    if (value === null) return null;
    if (typeof value !== "string" || value.length > Math.ceil(SONG_LIMIT / 3) * 4) {
      throw Error("A song in this backup is too large.");
    }
    const bytes = Uint8Array.from(atob(value), c => c.charCodeAt(0));
    describeSong(bytes);
    return bytes;
  });
}
