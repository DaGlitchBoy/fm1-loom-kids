// SPDX-License-Identifier: MPL-2.0
import { describeSong, packSongs, readSongs, unpackSongs } from "./song-data.mjs";
import { stereoWav } from "./wav.mjs";

const $ = id => document.getElementById(id);
let slots, recording, recordedUrl;
const colors = [
  "#283449", "#1b1d2a", "#f4f9ff", "#ffd046", "#f65667", "#dd9958", "#ffea6d", "#fe8a40",
  "#4acce8", "#7ebede", "#b889f3", "#60d97e", "#119ba5", "#ff9bcf", "#787be4", "#778aa1",
];

function finishDownload(blob, name) {
  const link = document.createElement("a");
  link.href = URL.createObjectURL(blob);
  link.download = name;
  link.click();
  setTimeout(() => URL.revokeObjectURL(link.href), 60_000);
}

function updateSongs(saved) {
  slots = saved;
  $("slots").replaceChildren();
  $("slots").hidden = false;
  $("backup").disabled = false;
  saved.forEach((song, slot) => {
    const info = song && describeSong(song);
    const card = document.createElement("article");
    const name = document.createElement("strong");
    name.className = "slot-name";
    if (info) {
      const tile = document.createElement("canvas");
      tile.width = tile.height = 8;
      tile.setAttribute("aria-hidden", "true");
      const ctx = tile.getContext("2d");
      for (let i = 0; i < 64; i++) {
        ctx.fillStyle = colors[(info.pixels[i >> 1] >> (4 * (i & 1))) & 15];
        ctx.fillRect(i % 8, i >> 3, 1, 1);
      }
      name.append(tile);
    }
    name.append(`Slot ${slot + 1}${info?.used ? "" : " · Empty"}`);
    const button = document.createElement("button");
    button.type = "button";
    button.textContent = "Export WAV";
    button.disabled = !info?.used;
    button.onclick = async () => {
      button.disabled = true;
      $("song-status").textContent = `Rendering slot ${slot + 1}…`;
      const worker = new Worker("./render-worker.mjs", { type: "module" });
      try {
        const { wav, error } = await new Promise((resolve, reject) => {
          worker.onmessage = event => resolve(event.data);
          worker.onerror = () => reject(Error("The song renderer could not start."));
          worker.postMessage(song);
        });
        if (error) throw Error(error);
        finishDownload(wav, `lion-kids-slot-${slot + 1}.wav`);
        $("song-status").textContent = `WAV downloaded for slot ${slot + 1}.`;
      } catch (error) {
        $("song-status").textContent = error.message;
      } finally {
        worker.terminate();
        button.disabled = false;
      }
    };
    card.append(name, button);
    $("slots").append(card);
  });
}

$("read").onclick = async () => {
  $("read").disabled = $("open").disabled = true;
  try {
    if (!navigator.requestMIDIAccess) throw Error("Open this page in Chrome or Edge to read songs from the FM‑1.");
    $("song-status").textContent = "Looking for the FM‑1…";
    const midi = await navigator.requestMIDIAccess({ sysex: true });
    updateSongs(await readSongs(midi, slot => { $("song-status").textContent = `Reading slot ${slot} of 3…`; }));
    $("song-status").textContent = "The three song slots are ready.";
  } catch (error) {
    $("song-status").textContent = error.message;
  } finally {
    $("read").disabled = $("open").disabled = false;
  }
};
$("backup").onclick = () => finishDownload(new Blob([packSongs(slots)], { type: "application/json" }), "lion-kids-songs.lionkids");
$("open").onchange = async event => {
  const [file] = event.target.files;
  if (!file) return;
  try {
    if (file.size > 70_000) throw Error("This file is too large to be a three-slot song backup.");
    updateSongs(unpackSongs(await file.text()));
    $("song-status").textContent = "Song backup opened.";
  } catch (error) {
    $("song-status").textContent = error.message;
  } finally { event.target.value = ""; }
};

function recordControls() {
  $("record").disabled = !!recording || !$("source").value;
  $("stop").disabled = !recording?.node;
  $("inputs").disabled = !!recording;
  $("source").disabled = !!recording || $("source").options.length < 2;
}

async function audioInputs() {
  if (!navigator.mediaDevices?.getUserMedia) throw Error("Audio capture requires a secure Chrome or Edge tab.");
  const permission = await navigator.mediaDevices.getUserMedia({ audio: true });
  let devices;
  try { devices = await navigator.mediaDevices.enumerateDevices(); }
  finally { permission.getTracks().forEach(track => track.stop()); }
  $("source").replaceChildren(new Option("Choose an input", ""));
  for (const { label, deviceId, kind } of devices) {
    if (kind !== "audioinput" || deviceId === "default" || deviceId === "communications") continue;
    $("source").add(new Option(label || "Audio input", deviceId));
  }
  const fm1 = [...$("source").options].find(option => /lion kids|fm[- ]?1|felucca/i.test(option.text));
  if (fm1) $("source").value = fm1.value;
  $("live-status").textContent = fm1 ? "FM‑1 audio input ready." : "Plug in the FM‑1 and choose its audio input.";
  recordControls();
}
$("inputs").onclick = async () => {
  $("inputs").disabled = true;
  try { await audioInputs(); }
  catch (error) { $("live-status").textContent = error.message; }
  finally { recordControls(); }
};
$("source").onchange = recordControls;

async function closeRecorder(state) {
  state.stream?.getTracks().forEach(track => track.stop());
  if (state.context && state.context.state !== "closed") await state.context.close();
  if (recording === state) recording = null;
  recordControls();
}

$("record").onclick = async () => {
  if (recording || !$("source").value) return;
  const state = recording = { chunks: [] };
  const deviceId = $("source").value;
  recordControls();
  try {
    state.context = new AudioContext();
    await state.context.resume();
    state.stream = await navigator.mediaDevices.getUserMedia({ audio: {
      deviceId: { exact: deviceId }, channelCount: { exact: 2 },
      echoCancellation: false, noiseSuppression: false, autoGainControl: false,
    } });
    const [track] = state.stream.getAudioTracks();
    await state.context.audioWorklet.addModule("./record-worklet.mjs");
    state.node = new AudioWorkletNode(state.context, "lion-stereo-capture", {
      channelCount: 2, channelCountMode: "explicit", outputChannelCount: [2],
    });
    state.node.port.onmessage = async ({ data }) => {
      if (data.pcm) state.chunks.push(data.pcm);
      if (!data.done) return;
      try {
        if (!state.chunks.length) throw Error("The performance did not contain any recorded audio.");
        if (recordedUrl) URL.revokeObjectURL(recordedUrl);
        recordedUrl = URL.createObjectURL(stereoWav(state.chunks, state.context.sampleRate));
        const link = $("recording");
        link.href = recordedUrl;
        link.download = "lion-kids-live.wav";
        link.hidden = false;
        $("live-status").textContent = "Recording ready to download.";
      } catch (error) { $("live-status").textContent = error.message; }
      finally { await closeRecorder(state); }
    };
    state.node.onprocessorerror = () => {
      $("live-status").textContent = "Recording stopped because the audio processor failed.";
      void closeRecorder(state);
    };
    track.onended = () => {
      if (recording === state) $("stop").click();
    };
    // The processor outputs silence. Monitor the instrument itself.
    state.context.createMediaStreamSource(state.stream).connect(state.node).connect(state.context.destination);
    $("live-status").textContent = "Recording…";
    recordControls();
  } catch (error) {
    $("live-status").textContent = error.name === "OverconstrainedError" ?
      "This input does not offer stereo. Choose the FM‑1 audio input." : error.message;
    await closeRecorder(state);
  }
};
$("stop").onclick = () => {
  if (!recording?.node) return;
  $("stop").disabled = true;
  $("live-status").textContent = "Finishing recording…";
  recording.node.port.postMessage({ stop: true });
};
