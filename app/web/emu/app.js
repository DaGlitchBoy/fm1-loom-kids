// SPDX-License-Identifier: MPL-2.0
// LOOM-8's own part of fm1-base's emulator page (base/web/emu/index.html; fm1-base's build loads it as app_own.js
// beside the emulator's parts, when the emulator is switched on). It works through the page's own names (KEYMAP,
// holdDown, turn, knobs, buttons, shortcuts, controls, node): the projects saved by the earlier page, what each
// control does on hover, and LOOM-8's computer keys: Space PLAY, Enter REC, / SEQ, as everywhere on the page, not
// only on the panel. A control reached with Tab keeps Space and Enter for itself (it presses that control); one
// clicked with the mouse or a finger does not, so Space stays PLAY after playing the panel with the mouse.

// the projects the page kept before it was fm1-base's (its own key), moved to fm1-base's page's key once
const OLD_STORE = "xy-emu-store", STORE = "loom-emu-store";
function moveStore() {
  try {
    const old = localStorage.getItem(OLD_STORE);
    if (old === null) return;
    if (localStorage.getItem(STORE) === null) localStorage.setItem(STORE, old);
    localStorage.removeItem(OLD_STORE);
  } catch { /* private window: nothing kept */ }
}

// what each control does, on hover and for a screen reader (the Buttons list below the panel says the same)
const TIP = { FX: "Reverb for this track", SEL: "Delay for this track", ENV: "Envelope for this track",
  LFO: "Modulation graph, signed depth and synced rate",
  EDIT: "Toggle the selected step in Steps, open songs in GLO, or open Quick FX",
  GLO: "Tempo, metronome, swing and song slots. Hold for five seconds to clear a slot.",
  HOME: "Stage. Hold and turn K1 to undo or redo", SAVE: "Save the loaded song", ARP: "Arpeggiator for this track",
  SEQ: "Open the last sequencer view or alternate between Performance and Steps", PLAY: "Start or stop playback",
  REC: "Record live notes on the selected track", "OCT-": "Octave down", "OCT+": "Octave up",
  SELECT: "Kids: track", ALGORITHM: "Sound preset", PRESETS: "Sound category", MASTER: "Master volume" };
function tips() {
  for (const [n, b] of Object.entries(buttons)) if (TIP[n]) { b.hit.title = TIP[n]; b.hit.setAttribute("aria-description", TIP[n]); }
  for (const [n, k] of Object.entries(knobs)) {
    if (!TIP[n] || n === "MASTER") continue;
    const how = k.hit.getAttribute("aria-description");
    k.hit.title = TIP[n];
    k.hit.setAttribute("aria-description", how ? `${TIP[n]}. ${how}` : TIP[n]);
  }
}

// the computer's keys: fm1-base's map (white keys Q-I, A-K; black keys 1-0, -; Esc HOME; Z / X octave) and LOOM-8's
const KNOB_KEYS = new Set(["ArrowUp", "ArrowDown", "ArrowLeft", "ArrowRight", "PageUp", "PageDown", "Home", "End"]);
function keyboard() {
  const map = {
    ...KEYMAP, Space: { btn: BTN.PLAY }, Enter: { btn: BTN.REC }, Slash: { btn: BTN.SEQ },
    KeyO: { btn: buttons.GLO.bit }, KeyN: { btn: buttons.EDIT.bit }, KeyV: { btn: buttons.SAVE.bit },
  };
  let clicked = null;                    // the control the mouse or a finger last gave the focus to
  controls.addEventListener("pointerdown", (e) => { clicked = e.target.closest(".hit"); }, true);
  addEventListener("keydown", (e) => { if (e.key === "Tab") clicked = null; }, true);
  addEventListener("keydown", (e) => {
    if (!node || !shortcuts.checked || e.metaKey || e.ctrlKey || e.altKey) return;
    const t = e.target;
    if (t !== document.body && t !== document.documentElement && !controls.contains(t)) return;   // the page's fields
    const hit = t.closest ? t.closest(".hit") : null;
    const knob = hit && hit.classList.contains("knob");
    if (knob && KNOB_KEYS.has(e.key)) return;   // a knob turns with its keys
    if (hit && !knob && hit !== clicked && (e.key === " " || e.key === "Enter")) return;
    if (e.code === "ArrowUp" || e.code === "ArrowDown") {
      turn(knobs.SELECT, e.code === "ArrowUp" ? 1 : -1);
      e.preventDefault(); e.stopPropagation();
      return;
    }
    const m = map[e.code];
    if (!m) return;
    e.preventDefault(); e.stopPropagation();
    if (!e.repeat) holdDown("k" + e.code, m.key !== undefined ? "key" : "btn", m.key !== undefined ? m.key : m.btn);
  }, true);
}

let keyboardAttached = false;

export function attach(context) {
  shelfNode = context?.node;
  moveStore();
  tips();
  if (!keyboardAttached) {
    keyboard();
    keyboardAttached = true;
  }
}

// A transaction compares the slot generation and commits one record. Keeping
// a generation even for an empty slot makes deletion and stale undo detectable.
const SHELF_DB = "loom-kids-song-shelf";
let shelfDb, shelfNode;

function openShelf() {
  if (!shelfDb) shelfDb = new Promise((resolve, reject) => {
    const request = indexedDB.open(SHELF_DB, 1);
    request.onupgradeneeded = () => request.result.createObjectStore("songs", { keyPath: "slot" });
    request.onsuccess = () => resolve(request.result);
    request.onerror = request.onblocked = () => reject(request.error);
  });
  return shelfDb;
}

async function readShelf() {
  const db = await openShelf();
  return new Promise((resolve, reject) => {
    const tx = db.transaction("songs", "readonly");
    const req = tx.objectStore("songs").getAll();
    tx.oncomplete = () => resolve(req.result);
    tx.onabort = () => reject(tx.error);
  });
}

async function writeShelf(msg) {
  const db = await openShelf();
  return new Promise((resolve) => {
    const tx = db.transaction("songs", "readwrite");
    const songs = tx.objectStore("songs");
    let status = -1;
    tx.oncomplete = () => resolve(0);
    tx.onabort = () => resolve(status);
    const req = songs.get(msg.slot);
    req.onsuccess = () => {
      if ((req.result?.rev ?? 0) !== msg.rev) {
        status = -2;
        tx.abort();
        return;
      }
      songs.put({ slot: msg.slot, rev: msg.rev + 1, bytes: msg.bytes });
    };
  });
}

export async function message(msg) {
  if (msg.shelf === "read") {
    try {
      const records = await readShelf();
      const slots = {};
      for (const record of records) {
        if (Number.isInteger(record.slot) && record.slot >= 0 && record.slot < 5) slots[record.slot] = record;
      }
      const transfer = Object.values(slots).map((record) => record.bytes).filter((bytes) => bytes instanceof ArrayBuffer);
      shelfNode.port.postMessage({ type: "app", shelf: "seed", ok: true, slots }, transfer);
    } catch {
      shelfNode.port.postMessage({ type: "app", shelf: "seed", ok: false, slots: {} });
    }
  } else if (msg.shelf === "write") {
    let status = -1;
    try {
      if (Number.isInteger(msg.slot) && msg.slot >= 0 && msg.slot < 5) status = await writeShelf(msg);
    } catch { /* send a failed acknowledgement so the working song and history stay put */ }
    shelfNode.port.postMessage({ type: "app", shelf: "complete", id: msg.id, status });
  }
}
