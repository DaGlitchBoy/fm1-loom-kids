// SPDX-License-Identifier: MPL-2.0
// Test the real page module's entry binding without requiring a WASM toolchain.
import { readFileSync } from 'node:fs';
import vm from 'node:vm';
import assert from 'node:assert/strict';
const page = readFileSync('app/web/emu/page.html', 'utf8');
assert.match(page, /id="kids-entry" disabled/);
assert.match(page, /<h1>LOOM Kids<\/h1>/);
const entry = { disabled: true }, calls = [];
const selectHit = { getAttribute: () => null, setAttribute() {} };
const context = {
  document: { getElementById: id => id === 'kids-entry' ? entry : null },
  node: {}, localStorage: { getItem: () => null },
  buttons: {}, knobs: { SELECT: { hit: selectHit } }, KEYMAP: {}, BTN: {},
  controls: { addEventListener() {} }, addEventListener() {},
  holdDown: (...args) => calls.push(args), letGo: id => calls.push(['release', id]),
};
vm.createContext(context);
vm.runInContext(readFileSync('app/web/emu/app.js', 'utf8').replace('export function attach', 'function attach'), context);
vm.runInContext('attach()', context);
assert.equal(entry.disabled, false);
assert.match(selectHit.title, /Kids: pattern/);
entry.onclick();
assert.deepEqual(calls, [['kids-entry', 'btn', 30], ['release', 'kids-entry']]);
calls.length = 0;
vm.runInContext('attach()', context); // reconnect must replace, not accumulate, click handlers
entry.onclick();
assert.equal(calls.length, 2);
calls.length = 0;
context.node = null;
entry.onclick();
assert.equal(calls.length, 0);
console.log('Kids entry: enabled after startup, one press/release per click, harmless without emulator');
