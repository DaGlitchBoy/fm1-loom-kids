// SPDX-License-Identifier: MPL-2.0
import { readFileSync } from "node:fs";
import vm from "node:vm";
import assert from "node:assert/strict";

const page = readFileSync("app/web/emu/page.html", "utf8");
assert.match(page, /<!-- emu-page: start -->\s*Start LION KIDS/);
assert.match(page, /<h1>LION KIDS<\/h1>/);
assert.doesNotMatch(page, /id="kids-entry"/);

const body = { closest: () => null }, calls = [], keydown = [];
const hit = { getAttribute: () => null, setAttribute() {} };
const context = {
  document: { body }, node: {}, localStorage: { getItem: () => null },
  buttons: { GLO: { bit: 5, hit }, EDIT: { bit: 4, hit }, SAVE: { bit: 7, hit } },
  knobs: { SELECT: { hit } }, KEYMAP: { KeyQ: { key: 0 } }, BTN: { PLAY: 10, REC: 11, SEQ: 9 },
  shortcuts: { checked: true },
  controls: { addEventListener() {}, contains: () => true },
  addEventListener: (name, fn) => { if (name === "keydown") keydown.push(fn); },
  holdDown: (...args) => calls.push(args),
};
vm.createContext(context);
vm.runInContext(readFileSync("app/web/emu/app.js", "utf8").replaceAll(/^export /gm, ""), context);
vm.runInContext("attach(); attach()", context);
for (const code of ["KeyQ", "KeyO", "KeyN", "KeyV", "Enter"])
  for (const fn of keydown)
    fn({ code, target: body, preventDefault() {}, stopPropagation() {} });
const focused = knob => ({ closest: () => ({ classList: { contains: name => knob && name === "knob" } }) });
for (const fn of keydown) {
  fn({ code: "Space", key: " ", target: focused(true), preventDefault() {}, stopPropagation() {} });
  fn({ code: "Enter", key: "Enter", target: focused(false), preventDefault() {}, stopPropagation() {} });
}
assert.deepEqual(calls, [
  ["kKeyQ", "key", 0], ["kKeyO", "btn", 5], ["kKeyN", "btn", 4], ["kKeyV", "btn", 7], ["kEnter", "btn", 11], ["kSpace", "btn", 10],
]);
console.log("Single Kids entry and Mac keyboard shortcuts verified");
