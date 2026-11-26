// SPDX-License-Identifier: MPL-2.0
import { renderSong } from "./render.mjs";
let moduleRequest;
self.onmessage = async ({ data }) => {
  try {
    moduleRequest ??= fetch("./song-render.wasm").then(async response => {
      if (!response.ok) throw Error("Could not download the song renderer.");
      return WebAssembly.compile(await response.arrayBuffer());
    });
    self.postMessage({ wav: await renderSong(data, await moduleRequest) });
  } catch (error) {
    moduleRequest = null;
    self.postMessage({ error: error.message });
  }
};
