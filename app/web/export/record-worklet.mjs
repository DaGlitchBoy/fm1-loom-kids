// SPDX-License-Identifier: MPL-2.0
import { pcm16 } from "./wav.mjs";

class LionStereoCapture extends AudioWorkletProcessor {
  constructor() {
    super();
    this.pcm = new Int16Array(8192);
    this.used = 0;
    this.active = true;
    this.port.onmessage = () => {
      this.active = false;
      this.flush();
      this.port.postMessage({ done: true });
    };
  }
  flush() {
    if (!this.used) return;
    const pcm = this.used === this.pcm.length ? this.pcm : this.pcm.slice(0, this.used);
    this.port.postMessage({ pcm }, [pcm.buffer]);
    this.pcm = new Int16Array(8192);
    this.used = 0;
  }
  process(inputs) {
    if (!this.active) return false;
    const [left, right] = inputs[0];
    if (!left || !right) return true;
    for (let i = 0; i < left.length; i++) {
      this.pcm[this.used++] = pcm16(left[i]);
      this.pcm[this.used++] = pcm16(right[i]);
      if (this.used === this.pcm.length) this.flush();
    }
    return true;
  }
}

registerProcessor("lion-stereo-capture", LionStereoCapture);
