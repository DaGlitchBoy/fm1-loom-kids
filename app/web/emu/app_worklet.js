// SPDX-License-Identifier: MPL-2.0
// Relay song shelf I/O to the page. IndexedDB is owned by the page thread.
globalThis.fm1App = {
  process(fm1) {
    const ex = fm1.ex;
    if (!ex.web_kids_seed_pending) return;
    if (ex.web_kids_seed_pending() && !fm1.kidsSeed) {
      fm1.kidsSeed = true;
      fm1.port.postMessage({ type: "app", shelf: "read" });
    }
    const id = ex.web_kids_write_id() >>> 0;
    if (!id || id === fm1.kidsLastWrite) return;
    fm1.kidsLastWrite = id;
    const size = ex.web_kids_write_len() >>> 0;
    const bytes = size ? new Uint8Array(fm1.mem.buffer, ex.web_kids_write_data(), size).slice().buffer : null;
    fm1.port.postMessage({
      type: "app", shelf: "write", id, slot: ex.web_kids_write_slot(), rev: ex.web_kids_write_rev() >>> 0, bytes,
    }, bytes ? [bytes] : []);
  },
  message(fm1, msg) {
    const ex = fm1.ex;
    if (msg.shelf === "seed") {
      for (const [slot, record] of Object.entries(msg.slots)) {
        const bytes = record.bytes instanceof ArrayBuffer ? new Uint8Array(record.bytes) : null;
        const invalid = record.bytes !== null && (!bytes || !bytes.length || bytes.length > ex.web_kids_max());
        if (bytes && !invalid) new Uint8Array(fm1.mem.buffer, ex.web_kids_incoming(), bytes.length).set(bytes);
        ex.web_kids_seed(+slot, record.rev >>> 0, bytes && !invalid ? bytes.length : 0, +invalid);
      }
      ex.web_kids_ready(+msg.ok);
    } else if (msg.shelf === "pin") {
      ex.web_kids_pin(msg.token >>> 0);
    } else if (msg.shelf === "complete") {
      ex.web_kids_complete(msg.id, msg.status);
    }
  },
};
