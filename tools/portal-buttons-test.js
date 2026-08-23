// portal-buttons-test.js - cross-check the portal's button decoding against
// src/input_buttons.h, which input_buttons.h has always claimed exists.
//
// Three things must agree or a chord recorded in the portal will not be the
// chord the firmware matches:
//   1. the BTN table's bit values           vs the enum in the header
//   2. macroButtonMask()'s byte->bit decode vs button_mask() in the header
//   3. every bit the header decodes must be reachable by the portal decoder
//
// (3) is the one that mattered: 1.29.0 added the Edge Fn/paddle bits to both
// the header AND the BTN table, but macroButtonMask() still stopped at bit 18,
// so the firmware could match a chord the portal could never record.

const fs = require('fs');
const path = require('path');

const root = path.join(__dirname, '..');
const html = fs.readFileSync(path.join(root, 'ds5-config-portal.html'), 'utf8');
const hdr = fs.readFileSync(path.join(root, 'src', 'input_buttons.h'), 'utf8');

let fails = 0;
const ok = (cond, msg) => {
  console.log(`  ${cond ? 'ok   ' : 'FAIL '} ${msg}`);
  if (!cond) fails++;
};

// --- pull the enum out of the header ---------------------------------------
const hdrBits = {};
for (const m of hdr.matchAll(/BTN_([A-Z0-9_]+)\s*=\s*1u\s*<<\s*(\d+)/g)) {
  hdrBits[m[1]] = 1 << Number(m[2]);
}

// --- pull the BTN table out of the portal ----------------------------------
const tableSrc = html.match(/const BTN = \[([\s\S]*?)\n\];/);
if (!tableSrc) { console.error('BTN table not found in portal'); process.exit(1); }
const portalBits = {};
for (const m of tableSrc[1].matchAll(/\['([A-Z0-9_]+)',\s*1<<(\d+),\s*'([^']*)'\]/g)) {
  portalBits[m[1]] = { bit: 1 << Number(m[2]), label: m[3] };
}

console.log('BTN table vs src/input_buttons.h');
for (const [name, bit] of Object.entries(hdrBits)) {
  if (name === 'ALL') continue;
  const p = portalBits[name];
  ok(p && p.bit === bit, `${name} = bit ${Math.log2(bit)}${p && p.bit !== bit ? ` (portal says ${Math.log2(p.bit)})` : ''}`);
}
for (const name of Object.keys(portalBits)) {
  ok(hdrBits[name] !== undefined, `${name} exists in the header too`);
}

// --- evaluate macroButtonMask() against button_mask() ----------------------
// Both decoders are pure functions of bytes 7/8/9, so run the portal's real
// source over a vector table and compare with the header's rules re-derived
// from the header text itself (not hand-copied).
const fnSrc = html.match(/function macroButtonMask\(d\)\{[\s\S]*?\n\}/);
if (!fnSrc) { console.error('macroButtonMask not found'); process.exit(1); }
const hatSrc = html.match(/const HAT_TO_DPAD = \[[\s\S]*?\];/);
if (!hatSrc) { console.error('HAT_TO_DPAD not found'); process.exit(1); }
const xmaxSrc = html.match(/const TOUCH_X_MAX = \d+;/);
if (!xmaxSrc) { console.error('TOUCH_X_MAX not found'); process.exit(1); }
const portalDecode = new Function(`${xmaxSrc[0]}\n${hatSrc[0]}\n${fnSrc[0]}\nreturn macroButtonMask;`)();

// byte 9 bit -> name, straight from the header's own if-chain
const b2Rules = [...hdr.matchAll(/if \(b2 & (0x[0-9A-Fa-f]+)u\) m \|= BTN_([A-Z0-9_]+);/g)]
  .map(m => [Number(m[1]), m[2]]);

console.log('\nmacroButtonMask() vs button_mask()');
const mkReport = (b0, b1, b2) => {
  const buf = new Uint8Array(40);
  buf[7] = b0; buf[8] = b1; buf[9] = b2;
  buf[32] = 0x80; buf[36] = 0x80;   // both fingers LIFTED - a zeroed touch
  return new DataView(buf.buffer);  // block would read as a finger at x=0
};
for (const [mask, name] of b2Rules) {
  const got = portalDecode(mkReport(8, 0, mask));   // hat 8 = neutral
  ok(got === hdrBits[name], `byte9 ${('0x' + mask.toString(16)).padEnd(4)} decodes to ${name}`);
}

// face + shoulder bytes, and the hat enum trap
console.log('\nother bytes');
ok(portalDecode(mkReport(8, 0, 0)) === 0, 'hat 8 (neutral) is NOT a phantom press');
ok(portalDecode(mkReport(0, 0, 0)) === hdrBits['DPAD_UP'], 'hat 0 is D-pad Up, not "nothing"');
ok(portalDecode(mkReport(0x18, 0, 0)) === (hdrBits['SQUARE']), 'face bit 4 = Square');
ok(portalDecode(mkReport(8, 0xFF, 0)) === 0x0000FF00, 'byte 8 fills bits 8-15');
ok(portalDecode(mkReport(8, 0, 0xF7)) === (hdrBits['PS'] | hdrBits['TOUCHPAD'] | hdrBits['MUTE'] |
     hdrBits['LEFT_FN'] | hdrBits['RIGHT_FN'] | hdrBits['LEFT_PAD'] | hdrBits['RIGHT_PAD']),
   'all byte 9 bits together');

// --- touchpad click halves --------------------------------------------------
// One physical switch, qualified by finger position. Both decoders must agree
// on the boundary AND on leaving the click unqualified when no finger is down.
console.log('\ntouchpad click halves');
const mkTouch = (clicked, down, x) => {
  const buf = new Uint8Array(40);
  buf[7] = 8;                                  // hat neutral
  buf[9] = clicked ? 0x02 : 0;
  buf[32] = down ? 0x00 : 0x80;                // bit7 SET = lifted
  buf[33] = x & 0xFF;
  buf[34] = (x >> 8) & 0x0F;
  return new DataView(buf.buffer);
};
const T = hdrBits['TOUCHPAD'], L = hdrBits['PAD_CLICK_LEFT'], R = hdrBits['PAD_CLICK_RIGHT'];
ok(portalDecode(mkTouch(true, true, 100)) === (T | L), 'click at x=100 is a LEFT click');
ok(portalDecode(mkTouch(true, true, 1800)) === (T | R), 'click at x=1800 is a RIGHT click');
// Boundary. C++ computes TOUCH_X_MAX/2 as 959 (integer), JS as 959.5 - the
// comparison is > in both, so 959 is left and 960 is right either way. This
// pins that the two languages have not drifted apart at the midpoint.
ok(portalDecode(mkTouch(true, true, 959)) === (T | L), 'x = 959 is the last LEFT column');
ok(portalDecode(mkTouch(true, true, 960)) === (T | R), 'x = 960 is the first RIGHT column');
ok(portalDecode(mkTouch(true, false, 100)) === T, 'click with no finger down stays unqualified');
ok(portalDecode(mkTouch(false, true, 100)) === 0, 'touching without clicking is not a button');
ok((portalDecode(mkTouch(true, true, 100)) & T) !== 0, 'a qualified click still sets TOUCHPAD, so old chords keep matching');

// --- reachability: nothing the header decodes may be undecodable here ------
console.log('\nreachability');
for (const [, name] of b2Rules) {
  const reachable = (portalDecode(mkReport(8, 0, 0xFF)) & hdrBits[name]) !== 0;
  ok(reachable, `${name} can actually be recorded`);
}

console.log(`\nbutton decode problems: ${fails}`);
console.log(fails ? 'PORTAL BUTTONS TEST FAILED' : 'PORTAL BUTTONS TEST OK');
process.exit(fails ? 1 : 0);
