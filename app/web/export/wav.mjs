// SPDX-License-Identifier: MPL-2.0
export function pcm16(value) {
  return Math.trunc(Math.max(-1, Math.min(1, value)) * (value < 0 ? 32768 : 32767));
}

export function stereoWav(chunks, rate) {
  const size = chunks.reduce((sum, chunk) => sum + chunk.byteLength, 0);
  if (size > 0xffffffff - 36) throw Error("The recording is too large for a WAV file.");
  const header = new DataView(new ArrayBuffer(44));
  function tag(offset, text) {
    for (let i = 0; i < text.length; i++) header.setUint8(offset + i, text.charCodeAt(i));
  }
  tag(0, "RIFF");
  header.setUint32(4, size + 36, true);
  tag(8, "WAVE");
  tag(12, "fmt ");
  header.setUint32(16, 16, true);
  header.setUint16(20, 1, true);
  header.setUint16(22, 2, true);
  header.setUint32(24, rate, true);
  header.setUint32(28, rate * 4, true);
  header.setUint16(32, 4, true);
  header.setUint16(34, 16, true);
  tag(36, "data");
  header.setUint32(40, size, true);
  return new Blob([header, ...chunks], { type: "audio/wav" });
}
