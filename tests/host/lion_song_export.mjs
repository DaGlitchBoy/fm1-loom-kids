// SPDX-License-Identifier: MPL-2.0
import assert from "node:assert/strict";
import { spawnSync } from "node:child_process";
import { mkdtemp, readFile, rm, writeFile } from "node:fs/promises";
import { tmpdir } from "node:os";
import { join, resolve } from "node:path";
import { pathToFileURL } from "node:url";

const [binary, directory] = process.argv.slice(2).map(resolvePath => resolve(resolvePath));
const page = pathToFileURL(join(directory, "export/")).href + "/";
const { CHUNK, SONG_LIMIT, describeSong, packSongs, unpackSongs, readSongs, requestBytes, replyBytes } =
  await import(new URL("song-data.mjs", page));
const { renderSong } = await import(new URL("render.mjs", page));
const temp = await mkdtemp(join(tmpdir(), "lion-export-test-"));

function sysex(slot, offset) {
  return "sysex " + requestBytes(7, slot, offset).map(b => b.toString(16).padStart(2, "0")).join(" ");
}

async function simulate(lines) {
  const script = join(temp, "songs.xy");
  await writeFile(script, ["wait 300", "sysex_log out:replies.bin", ...lines].join("\n") + "\n");
  const run = spawnSync(binary, [script, temp], { cwd: temp, encoding: "utf8" });
  assert.equal(run.status, 0, run.stdout + run.stderr);
  const replies = [];
  let start = 0;
  const bytes = await readFile(join(temp, "replies.bin"));
  while (start < bytes.length) {
    assert.equal(bytes[start], 0xf0);
    const end = bytes.indexOf(0xf7, start);
    assert.ok(end > start);
    replies.push(bytes.subarray(start, end + 1));
    start = end + 1;
  }
  return replies;
}

try {
  const setup = ["bpm 120", "notes 1 1 1 1 105 48", "notes 1 1 9 1 105 55", "tap SAVE",
    "tap GLO", "spin K4 1", "tap EDIT", "tap HOME",
    "bpm 120", "pat 2 1 32", "notes 2 1 1 1 110 64", "notes 2 1 25 1 110 67", "tap SAVE",
    "expect kids_shelf_mask 3", "wait 50"];
  const probe = await simulate([...setup, "tap PLAY", sysex(0, 0), "tap PLAY", "wait 50",
    sysex(0, 0), sysex(1, 0), sysex(2, 0), sysex(3, 0)]);
  assert.equal(probe.length, 5);
  assert.throws(() => replyBytes(probe[0], 7, 0, 0), /Stop playback/);
  assert.equal(replyBytes(probe[1], 8, 0, 0), null);
  assert.throws(() => replyBytes(probe[4], 7, 3, 0), /could not be read/);
  const lines = [...setup];
  for (let slot = 0; slot < 3; slot++) {
    const { size } = replyBytes(probe[1 + slot], 7, slot, 0);
    for (let at = 0; at < Math.max(size, 1) && at < SONG_LIMIT; at += CHUNK) lines.push(sysex(slot, at), "wait 16");
  }
  const frames = await simulate(lines);
  const midi = new EventTarget();
  Object.assign(midi, { name: "LION KIDS FM-1", open: async () => {}, close: async () => {} });
  const output = { name: midi.name, close: async () => {}, send(packet) {
    const frame = frames.find(f => f[6] === packet[6] && f[7] === packet[7] && f[8] === packet[8]);
    assert.ok(frame, "unexpected read request");
    const data = Uint8Array.from(frame);
    data[5] = packet[5];
    midi.dispatchEvent(Object.assign(new Event("midimessage"), { data }));
  }};
  if (!globalThis.crypto) globalThis.crypto = (await import("node:crypto")).webcrypto;
  const slots = await readSongs({ inputs: new Map([[0, midi]]), outputs: new Map([[0, output]]) });
  assert.equal(slots[2], null);
  assert.deepEqual(unpackSongs(packSongs(slots)), slots);
  assert.throws(() => describeSong(new Uint8Array(61)), /damaged/);

  const wasm = await WebAssembly.compile(await readFile(join(directory, "export/song-render.wasm")));
  for (const [slot, seconds] of [[0, 2], [1, 4]]) {
    const wav = await renderSong(slots[slot], wasm);
    const bytes = await wav.arrayBuffer();
    const riff = new DataView(bytes);
    assert.equal(riff.getUint16(22, true), 2);
    assert.equal(riff.getUint32(24, true), 44100);
    assert.equal(riff.getUint32(40, true), bytes.byteLength - 44);
    const length = (bytes.byteLength - 44) / (44100 * 4);
    assert.ok(length > seconds && length < seconds + 4, `slot ${slot + 1} rendered ${length}s`);
    const pcm = new Int16Array(bytes, 44);
    assert.ok(pcm.some(sample => Math.abs(sample) > 500), "render contains audible music");
    assert.ok(!pcm.subarray(-4096).some(sample => Math.abs(sample) > 12), "effect tail has faded");
  }
  const broken = slots[0].slice();
  broken[16] ^= 1;
  await assert.rejects(renderSong(broken, wasm), /could not be opened/);
  console.log("Read-only FM-1 transfer, backup round trip and single-loop stereo WAV exports passed.");
} finally {
  await rm(temp, { recursive: true, force: true });
}
