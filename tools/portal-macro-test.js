// The macro panel is drawn by macroRender() into #macrobox at runtime, so
// portal-attr-test.js - which only inspects render()'s output into #app - never
// sees a single one of its generated handlers. This covers that gap with the
// same checks, plus the encoding the picker produces.
//
// The bug class being guarded: a quote inside a generated on*="..." attribute
// closes it early and silently truncates the handler. That broke all 37 selects
// in 1.14.7 and was invisible until a dropdown was clicked.
const fs = require('fs');
let js = fs.readFileSync('/tmp/portal.js', 'utf8');
const stop = js.indexOf('\nrender();\nportalSelfCheck();');
js = js.slice(0, stop > 0 ? stop : js.length);

global.setInterval = () => 0; global.setTimeout = () => 0; global.requestAnimationFrame = () => 0;
global.window = { setInterval: () => 0, setTimeout: () => 0 };
let appHtml = '', boxHtml = '';
const appEl = { set innerHTML(v){ appHtml = v; }, get innerHTML(){ return appHtml; } };
const boxEl = { set innerHTML(v){ boxHtml = v; }, get innerHTML(){ return boxHtml; },
                set textContent(v){ boxHtml = v; }, get textContent(){ return boxHtml; } };
global.document = {
  getElementById: id => id === 'app' ? appEl : (id === 'macrobox' ? boxEl : null),
  createElement: () => ({ style: {}, classList: { add(){} } }),
  body: { prepend(){} }, addEventListener(){}, querySelector: () => null
};
global.navigator = { hid: { getDevices: async () => [], addEventListener(){} } };

const fn = new Function('window', 'document', 'navigator', 'location', js + `
  return { macroRender, macroPickApply, macroPickOpen, macroPickMod, macroPickKey,
           macroChordName, macroGestureName, macroComboName, MACRO_COUNT,
           setRows: r => { window._macroRows = r; },
           getRows: () => window._macroRows,
           setPick: p => { window._macroPick = p; },
           setSnap: x => { savedSnapshot = x; },
           MOUT_FIRST, MOUT_LAST, GOUT_FIRST, GOUT_LAST,
           MACRO_OUT_BTNS, macroIsMouseOut, macroIsGyroOut, macroOutKind,
           macroOutBtnValues, macroOutMouseValues, macroAnyMouseEnabled,
           macroSetBit, getCfg: () => config,
           setCfg: c => { config = c; },
           setDev: d => { device = d; } };`);
const M = fn(global.window, global.document, global.navigator, global.location);

let bad = 0, checked = 0, tests = 0;
function ok(cond, what){ tests++; console.log('  ' + (cond ? 'ok  ' : 'FAIL') + '  ' + what); if (!cond) bad++; }

function scanHandlers(html, label){
  for (const m of html.matchAll(/\son(change|click|input)="([^"]*)"/g)){
    checked++;
    const body = m[2];
    if (body.includes('"')){ console.log('  QUOTE IN HANDLER (' + label + '):', body.slice(0,70)); bad++; }
    const par = (body.match(/\(/g)||[]).length - (body.match(/\)/g)||[]).length;
    if (par !== 0){ console.log('  UNBALANCED (' + label + '):', body.slice(0,70)); bad++; }
  }
  for (const m of html.matchAll(/\son(change|click|input)="([^"]*)$/gm)){
    console.log('  UNTERMINATED (' + label + '):', m[2].slice(0,60)); bad++;
  }
}

const blank = () => ({chord:0,gesture:0,flags:0,hold_cs:0,keys:[0,0,0,0],rel_order:0,label:'',present:false});
M.setDev({ productId: 0x0ce6, opened: true });
M.setCfg({ macro_disable: 0xFFFFFFFE >>> 0 });

// A row carrying hostile text in the NAME - the field a user types into freely,
// and the only place attacker-ish characters can reach an attribute.
const rows = [];
for (let i = 0; i < 32; i++) rows.push(blank());
rows[0] = {chord: (1<<15)|(1<<0), gesture:0, flags:1, hold_cs:75,
           keys:[0xE0,0x0D,0,0], rel_order:1, label:'he said "hi" & <b>', present:true};
rows[1] = {chord:0, gesture:0x80|0x03, flags:0, hold_cs:0,
           keys:[0xE3,0x0A,0,0], rel_order:1, label:"O'Brien", present:true};
M.setRows(rows);

M.macroRender();
scanHandlers(boxHtml, 'rows');
ok(boxHtml.indexOf('he said "hi"') < 0, 'a quote in a macro name is escaped, not emitted raw');
ok(/Ctrl \+ J/.test(boxHtml), 'combo renders from keys[]');
ok(/hold 0\.75s/.test(boxHtml), 'long-press threshold shown');

// Picker open: the panel with its checkboxes and the whole key list.
M.setPick({ row: 1, mods: [0xE3], key: 0x0A });
M.macroRender();
scanHandlers(boxHtml, 'picker');
ok(/macroPickApply\(\)/.test(boxHtml), 'picker renders a Set action');
ok((boxHtml.match(/<option /g) || []).length > 60, 'key list is populated');

// The picker must encode combos the RECORDER cannot capture at all.
function pickEncode(mods, key){
  M.setPick({ row: 0, mods: mods.slice(), key });
  M.macroPickApply();
  return M.getRows()[0];
}
let r = pickEncode([0xE3], 0x0A);                      // Win+G
ok(r.keys[0] === 0xE3 && r.keys[1] === 0x0A, 'Win+G stored in press order');
ok(r.rel_order === 1, 'Win+G releases G before Win');
r = pickEncode([0xE2], 0x2B);                          // Alt+Tab
ok(r.keys[0] === 0xE2 && r.keys[1] === 0x2B && r.rel_order === 1, 'Alt+Tab encoded correctly');
r = pickEncode([0xE0, 0xE1], 0x0D);                    // Ctrl+Shift+J
ok(r.keys[2] === 0x0D && r.rel_order === ((2)|(1<<2)|(0<<4)), 'three-key combo reverses on release');

// Every stored rel_order must be a real permutation, or macro.cpp strands a key.
function isPerm(rel, n){
  const seen = new Set();
  for (let i = 0; i < n; i++){ const p = (rel >> (2*i)) & 3; if (p >= n || seen.has(p)) return false; seen.add(p); }
  return true;
}
let allPerm = true;
for (const mods of [[],[0xE0],[0xE0,0xE1],[0xE0,0xE1,0xE2]]){
  const e = pickEncode(mods, 0x04);
  if (!isPerm(e.rel_order, mods.length + 1)) allPerm = false;
}
ok(allPerm, 'picker always emits a valid release permutation');

// The dirty banner is drawn from savedSnapshot. saveAll()'s common path refreshes
// the snapshot but deliberately does NOT render, so the panel must repaint itself
// after a save or it keeps claiming unsaved changes.
(function bannerClears(){
  console.log('dirty banner tracks the saved snapshot');
  const rows2 = []; for (let i=0;i<32;i++) rows2.push(blank());
  rows2[0] = {chord:1<<15, gesture:0, flags:0, hold_cs:0, keys:[0xE0,0x0D,0,0],
              rel_order:1, label:'t', present:true};
  M.setRows(rows2);
  M.setPick(null);
  M.setCfg({macro_disable: 0xFFFFFFFF});
  M.setSnap({macro_disable: 0xFFFFFFFF});
  M.macroRender();
  ok(!/Enable state changed/.test(boxHtml), 'hidden when config matches the snapshot');
  M.macroSetBit(0, true);
  ok(/Enable state changed/.test(boxHtml), 'shown after a checkbox change');
  M.setSnap({...M.getCfg()});          // what a successful saveAll() does
  M.macroRender();                     // the repaint macroSaveTable performs
  ok(!/Enable state changed/.test(boxHtml), 'cleared once the snapshot is refreshed');
})();


// Every row KIND must render. A stick row exercised a code path no fixture
// touched, so macroRender() called a helper that did not exist and the whole
// suite still passed - rendering only rows of one shape proves only that shape.
(function rowKindsRender(){
  console.log('every row kind renders without throwing');
  const kinds = [
    ['chord',  {chord:1<<15, gesture:0, flags:0, keys:[0xE0,0x0D,0,0], rel_order:1}],
    ['hold remap to key',    {chord:1<<5, gesture:0, flags:0x02, keys:[0x2C,0,0,0]}],
    ['hold remap to button', {chord:1<<5, gesture:0, flags:0x06, keys:[0,0,0,0], out_btn:2}],
    ['left stick',  {chord:0, gesture:0x80|0x20, flags:0x02, keys:[0x1A,0x07,0x16,0x04], stick_thresh:48}],
    ['right stick', {chord:0, gesture:0x80|0x20|0x40, flags:0x06, keys:[0x1A,0x07,0x16,0x04], stick_thresh:0}],
    ['motion',      {chord:1<<10, gesture:0x80|0x10, flags:0, keys:[0x2B,0,0,0], motion:[1,0], motion_len:1}],
  ];
  for (const [name, def] of kinds){
    const rows = []; for (let i=0;i<32;i++) rows.push(blank());
    rows[0] = {hold_cs:0, rel_order:0, out_btn:0, stick_thresh:0, motion:[0,0],
               motion_len:0, motion_step:0, label:name, present:true, ...def};
    M.setRows(rows); M.setPick(null);
    M.setCfg({macro_disable: 0}); M.setSnap({macro_disable: 0});
    let threw = null;
    try { M.macroRender(); } catch (e) { threw = e; }
    ok(!threw, name + ' renders' + (threw ? ' (' + threw.message + ')' : ''));
    ok(boxHtml.length > 0, name + ' produced markup');
  }
})();

// A stick row's four direction outputs must actually appear, or the row is
// unconfigurable however cleanly it renders.
(function stickShowsDirections(){
  console.log('a stick row exposes all four directions');
  const rows = []; for (let i=0;i<32;i++) rows.push(blank());
  rows[0] = {chord:0, gesture:0x80|0x20, flags:0x02, hold_cs:0,
             keys:[0x1A,0x07,0x16,0x04], rel_order:0, out_btn:0, stick_thresh:48,
             motion:[0,0], motion_len:0, motion_step:0, label:'wasd', present:true};
  M.setRows(rows); M.setPick(null);
  M.setCfg({macro_disable: 0}); M.setSnap({macro_disable: 0});
  M.macroRender();
  for (const arrow of ['\u2191','\u2192','\u2193','\u2190']){
    ok(boxHtml.indexOf(arrow) >= 0, 'direction ' + arrow + ' is shown');
  }
  ok(/macroSetStickKey\(0, ?0/.test(boxHtml), 'each direction has its own key selector');
  ok(/Left stick/.test(boxHtml), 'the input column names the stick');
})();

// The output numbering is PERSISTED in every saved macro and is split across
// three ranges (controller 1-10 and 16-22, mouse 11-15, gyro 23-25). The portal
// and src/macro.h / src/config.h each hold their own copy of that split, and
// they have drifted twice: the flick outputs appeared in the controller picker
// in 1.40.x, and the inject-mask readout named the wrong buttons. So check the
// two copies against each other rather than trusting either.
(function outputRangesMatchTheHeaders(){
  console.log('output numbering agrees with the firmware headers');
  const mh = fs.readFileSync('src/macro.h', 'utf8');
  const ch = fs.readFileSync('src/config.h', 'utf8');
  const num = (src, name) => {
    const m = new RegExp('\\b' + name + '\\s*=\\s*(\\d+)').exec(src);
    return m ? +m[1] : null;
  };
  const hMoutFirst = num(mh, 'MOUT_FIRST'), hMoutLast = num(mh, 'MOUT_LAST');
  const hGoutFirst = num(mh, 'GOUT_FIRST'), hGoutLast = num(mh, 'GOUT_LAST');
  const hT2Count   = num(ch, 'T2BTN_COUNT');
  ok(hMoutFirst === M.MOUT_FIRST, 'MOUT_FIRST matches macro.h (' + hMoutFirst + ')');
  ok(hMoutLast  === M.MOUT_LAST,  'MOUT_LAST matches macro.h (' + hMoutLast + ')');
  ok(hGoutFirst === M.GOUT_FIRST, 'GOUT_FIRST matches macro.h (' + hGoutFirst + ')');
  ok(hGoutLast  === M.GOUT_LAST,  'GOUT_LAST matches macro.h (' + hGoutLast + ')');
  ok(hGoutFirst >= hT2Count, 'the gyro block starts above T2BTN_COUNT (' + hT2Count + ')');
  // Every value in the table has to be reachable from exactly one picker, and
  // no value may appear in both.
  const btns = M.macroOutBtnValues(), mice = M.macroOutMouseValues();
  ok(M.MACRO_OUT_BTNS.length === hGoutLast + 1,
     'the name table covers every value up to GOUT_LAST and no further');
  for (const v of btns){
    ok(!M.macroIsMouseOut(v) && !M.macroIsGyroOut(v),
       'controller picker value ' + v + ' (' + M.MACRO_OUT_BTNS[v] + ') is a controller button');
  }
  // The mouse picker's two ranges, NOT the span between them: 16-22 are
  // controller buttons that sit between the mouse block and the gyro block.
  const mouseRanges = [[hMoutFirst, hMoutLast], [hGoutFirst, hGoutLast]];
  for (const [lo, hi] of mouseRanges) for (let v = lo; v <= hi; v++){
    ok(mice.indexOf(v) >= 0, 'mouse picker offers ' + v + ' (' + M.MACRO_OUT_BTNS[v] + ')');
    ok(btns.indexOf(v) < 0,  'and the controller picker does not');
  }
  for (let v = 1; v < hMoutFirst; v++) ok(btns.indexOf(v) >= 0, 'controller picker offers ' + v);
  for (let v = hMoutLast + 1; v < hGoutFirst; v++) ok(btns.indexOf(v) >= 0, 'controller picker offers ' + v);
  // A gyro row must come back as the MOUSE kind, or re-opening a saved profile
  // shows it as an unrecognised controller button.
  for (let v = hGoutFirst; v <= hGoutLast; v++){
    ok(M.macroOutKind(0, {out_btn: v}) === 2, 'output ' + v + ' reads back as the mouse kind');
  }
  // ...and must NOT count towards the mouse interface, which is what decides
  // whether saving the table re-enumerates the device.
  const rows = []; for (let i=0;i<32;i++) rows.push(blank());
  rows[0] = Object.assign(blank(), {chord: 0x20, out_btn: M.GOUT_FIRST, present: true});
  M.setRows(rows); M.setCfg({macro_disable: 0}); M.setSnap({macro_disable: 0});
  ok(M.macroAnyMouseEnabled() === false, 'a gyro row does not ask for the mouse interface');
  rows[0].out_btn = M.MOUT_FIRST;
  ok(M.macroAnyMouseEnabled() === true, 'but a real mouse row does');
})();

console.log('\nmacro panel handlers checked:', checked, '| problems:', bad);
console.log(bad ? 'MACRO PANEL TEST FAILED' : 'MACRO PANEL TEST OK');
process.exit(bad ? 1 : 0);
