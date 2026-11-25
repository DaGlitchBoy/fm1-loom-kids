// SPDX-License-Identifier: MPL-2.0
import { stereoWav, pcm16 } from "./wav.mjs";

const RATE = 44100;
const BLOCK = 256;

export async function renderSong(song, wasm) {
  let memory;
  const wasi = {
    clock_time_get: (_, __, ptr) => { new DataView(memory.buffer).setBigUint64(ptr, 0n, true); return 0; },
    fd_write: (_, __, ___, ptr) => { new DataView(memory.buffer).setUint32(ptr, 0, true); return 0; },
    environ_sizes_get: (count, bytes) => {
      const view = new DataView(memory.buffer);
      view.setUint32(count, 0, true);
      view.setUint32(bytes, 0, true);
      return 0;
    },
    environ_get: () => 0,
    fd_read: (_, __, ___, ptr) => { new DataView(memory.buffer).setUint32(ptr, 0, true); return 0; },
    fd_seek: () => 70, // ESPIPE for stdout and stderr
    fd_close: () => 0,
    proc_exit: code => { throw Error(`The song renderer stopped (${code}).`); },
  };
  const imports = { wasi_snapshot_preview1: wasi, env: {} };
  for (const { module, name, kind } of WebAssembly.Module.imports(wasm)) {
    if (name in (imports[module] ?? {}) || kind !== "function") continue;
    (imports[module] ??= {})[name] = () => { throw Error(`Unsupported renderer call ${name}.`); };
  }
  const { exports: ex } = await WebAssembly.instantiate(wasm, imports);
  memory = ex.memory;
  ex._initialize?.();
  ex.web_boot();
  const ptr = ex.lion_song_input(song.length);
  if (!ptr) throw Error("The song does not fit in the renderer.");
  new Uint8Array(memory.buffer, ptr, song.length).set(song);
  const frames = ex.lion_song_start();
  if (!frames) throw Error("This song could not be opened by the renderer.");
  const chunks = [];
  function block(n) {
    const source = new Int32Array(memory.buffer, ex.lion_song_pcm(n), n * 2);
    const pcm = new Int16Array(n * 2);
    let peak = 0;
    for (let i = 0; i < pcm.length; i++) {
      pcm[i] = pcm16(source[i] / 8388608);
      peak = Math.max(peak, Math.abs(pcm[i]));
    }
    chunks.push(pcm);
    return peak;
  }
  for (let i = 0; i < frames; i += BLOCK) block(Math.min(BLOCK, frames - i));
  ex.lion_song_stop();
  let quiet = 0;
  for (let i = 0; i < RATE * 20 && quiet < RATE / 2; i += BLOCK) {
    quiet = block(BLOCK) <= 12 ? quiet + BLOCK : 0;
  }
  return stereoWav(chunks, RATE);
}
