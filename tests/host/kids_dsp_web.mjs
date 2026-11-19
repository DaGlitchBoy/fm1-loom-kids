// SPDX-License-Identifier: MPL-2.0
// Load every track with live sequencer work through the public emulator controls.
import assert from "node:assert/strict";
import fs from "node:fs";
import { performance } from "node:perf_hooks";

const FRAMES = 128, RATE = 44100, DEADLINE_MS = 1000 * FRAMES / RATE;
const FX = 1, SEL = 2, ENV = 4, LFO = 8, EDIT = 16, HOME = 64, SEQ = 512, PLAY = 1024;
const SELECT = 0, K1 = 3, K3 = 5, K4 = 6;
const WHITE = [0, 2, 4, 6, 7, 9, 11, 12, 14, 16, 18, 19, 21, 23, 24, 26];
const BLACK = [1, 3, 5, 8, 10, 13, 15, 17, 20, 22, 25];

async function boot(path) {
  let memory;
  const wasi = new Proxy({
    clock_time_get: (_id, _p, out) => { new DataView(memory.buffer).setBigUint64(out, 0n, true); return 0; },
  }, { get: (obj, key) => obj[key] ?? (() => 0) });
  const { instance } = await WebAssembly.instantiate(fs.readFileSync(path), {
    wasi_snapshot_preview1: wasi, env: new Proxy({}, { get: () => () => 0 }),
  });
  const ex = instance.exports;
  memory = ex.memory;
  ex._initialize?.();
  ex.web_master(2800);
  ex.web_boot();
  ex.web_render(4096);
  return ex;
}

async function run(path, shaping) {
  const ex = await boot(path);
  const wait = (n = 12) => { for (let i = 0; i < n; ++i) ex.web_render(FRAMES); };
  const tap = (mask) => { ex.web_buttons(mask); wait(); ex.web_buttons(0); wait(); };
  const key = (index) => { ex.web_keys(1 << index); wait(); ex.web_keys(0); wait(); };
  const turn = (role, delta) => { ex.web_enc(role, delta); wait(); };
  for (let track = 0; track < 8; ++track) {
    if (track) turn(SELECT, 1);
    tap(HOME);
    if (track) turn(K3, 2);  // three-note melodic chords
    tap(SEQ);
    if (!track) key(BLACK[0]);
    for (let step = 0; step < 16; step += 4) key(WHITE[step]);
    key(WHITE[2 + track % 2]);
    tap(FX);
    tap(SEL);
    if (shaping) {
      tap(ENV);
      tap(LFO);
      turn(K1, 27);
      turn(K3, track % 5);
      turn(K4, track % 4);
      tap(EDIT);
      assert.equal(ex.web_lit_buttons() & 31, 31, `track ${track + 1} all five switches`);
    }
    tap(HOME);
  }
  tap(PLAY);
  wait(512);

  const durations = [];
  let peak = 0, active = 0;
  const l = new Float32Array(ex.memory.buffer, ex.web_out_l(), FRAMES);
  const r = new Float32Array(ex.memory.buffer, ex.web_out_r(), FRAMES);
  for (let i = 0; i < 4096; ++i) {
    const begin = performance.now();
    ex.web_render(FRAMES);
    durations.push(performance.now() - begin);
    let block = 0;
    for (let j = 0; j < FRAMES; ++j) block = Math.max(block, Math.abs(l[j]), Math.abs(r[j]));
    assert.ok(Number.isFinite(block));
    if (block > 0.002) active++;
    peak = Math.max(peak, block);
  }
  durations.sort((a, b) => a - b);
  const p95 = durations[Math.floor(durations.length * 0.95)];
  const mean = durations.reduce((a, b) => a + b, 0) / durations.length;
  assert.ok(active > 2000 && peak > 0.04 && peak <= 1, `eight-track audio peak ${peak}, active ${active}`);
  assert.ok(p95 < DEADLINE_MS, `p95 ${p95.toFixed(3)} ms exceeded the ${DEADLINE_MS.toFixed(3)} ms audio budget`);
  return { peak: +peak.toFixed(3), active, meanMs: +mean.toFixed(3), p95Ms: +p95.toFixed(3),
           budgetMs: +DEADLINE_MS.toFixed(3) };
}

async function history(path) {
  const ex = await boot(path);
  const wait = (n = 16) => { for (let i = 0; i < n; ++i) ex.web_render(FRAMES); };
  const tap = (mask) => { ex.web_buttons(mask); wait(); ex.web_buttons(0); wait(); };
  tap(FX);
  tap(SEL);
  tap(FX);
  assert.equal(ex.web_lit_buttons() & 3, 3, "opening the reverb controls preserves both sends");
  ex.web_buttons(HOME);
  wait(110);
  ex.web_enc(K1, -1);
  wait();
  assert.equal(ex.web_lit_buttons() & 3, 1, "HOME + K1 left undoes the delay toggle");
  ex.web_enc(K1, 1);
  wait();
  assert.equal(ex.web_lit_buttons() & 3, 3, "HOME + K1 right redoes the delay toggle");
  ex.web_buttons(0);
  wait();
  tap(FX);
  assert.equal(ex.web_lit_buttons() & 3, 2, "using the hold preserves reverb focus on release");
  console.log("WebAssembly effect focus, undo and redo verified");
}

async function shelf(path) {
  const ex = await boot(path);
  const wait = (n = 16) => { for (let i = 0; i < n; ++i) ex.web_render(FRAMES); };
  const tap = (mask) => { ex.web_buttons(mask); wait(); ex.web_buttons(0); wait(); };
  assert.equal(ex.web_kids_seed_pending(), 1);
  ex.web_kids_ready(1);
  tap(FX);
  tap(128);
  let id = ex.web_kids_write_id();
  assert.ok(id);
  assert.equal(ex.web_kids_write_rev(), 0);
  ex.web_kids_complete(id, -1);
  assert.equal(ex.web_kids_write_id(), 0);
  tap(128);
  id = ex.web_kids_write_id();
  assert.equal(ex.web_kids_write_rev(), 0);
  const song = new Uint8Array(ex.memory.buffer, ex.web_kids_write_data(), ex.web_kids_write_len()).slice();
  assert.ok(song.length > 100);
  ex.web_kids_complete(id, 0);
  ex.web_buttons(HOME);
  wait(110);
  ex.web_enc(K1, -1);
  wait();
  assert.ok(ex.web_kids_write_id());
  assert.equal(ex.web_kids_write_rev(), 1);
  assert.equal(ex.web_kids_write_len(), 0);
  ex.web_kids_complete(ex.web_kids_write_id(), 0);
  ex.web_enc(K1, 1);
  wait();
  assert.equal(ex.web_kids_write_rev(), 2);
  assert.deepEqual(new Uint8Array(ex.memory.buffer, ex.web_kids_write_data(), ex.web_kids_write_len()), song);
  ex.web_kids_complete(ex.web_kids_write_id(), 0);
  const restarted = await boot(path);
  new Uint8Array(restarted.memory.buffer, restarted.web_kids_incoming(), song.length).set(song);
  restarted.web_kids_seed(0, 3, song.length, 0);
  restarted.web_kids_ready(1);
  assert.equal(restarted.web_lit_buttons() & FX, 0);
  for (const button of [32, EDIT]) {
    restarted.web_buttons(button);
    for (let i = 0; i < 16; ++i) restarted.web_render(FRAMES);
    restarted.web_buttons(0);
    for (let i = 0; i < 16; ++i) restarted.web_render(FRAMES);
  }
  assert.equal(restarted.web_lit_buttons() & FX, FX);
  console.log("WebAssembly song shelf, failed and acknowledged writes, undo/redo and reload verified");
}

const [current, baseline] = process.argv.slice(2);
await shelf(current);
await history(current);
if (baseline) console.log("Delivery 4, eight tracks and two sends", await run(baseline, false));
console.log("Current build, eight tracks and two sends", await run(current, false));
console.log("Current build, eight tracks, ENV, LFO, four quick effects and two sends", await run(current, true));
