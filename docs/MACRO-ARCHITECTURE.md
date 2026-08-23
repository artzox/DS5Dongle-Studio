# Macro subsystem — architecture

Controller button presses and touchpad swipes trigger keyboard combos over the HID
keyboard interface the wake feature already provides.

Status: **shipped in 1.19.0** — firmware engine, portal UI and both test harnesses,
verified on hardware (Pico 2 W).

This document records *why* the subsystem is shaped the way it is. Several of the
decisions below look wrong at a glance and are not — the inverted enable mask and
the slot-sector reservation in particular. Read those before changing them.

---

## 1. Why this is mostly already built

`src/ps_shortcut.cpp` is a single hardcoded macro — PS short-press → Win+G, PS
long-press (750 ms) → Win+Tab. It established every mechanism the general case
needs:

| Mechanism | Where it already exists |
|---|---|
| Hook on every BT input report | `main.cpp:204`, `ps_shortcut_tick(data + 3, len - 3)` |
| Writing to the keyboard | `tud_hid_n_keyboard_report(PS_KBD_INSTANCE=1, …)` |
| Short vs long discrimination | `long_press_fired` + a 750 ms threshold |
| Debounce against BT dropouts | 50 ms `last_high_time` hold |
| Deferred key release | `key_release_pending` / `key_release_time` |
| Flush on controller loss | `ps_shortcut_reset()` from `wake_on_bt_disconnect()` |
| Keyboard interface gating | `usb_descriptors.cpp:521`, `wake \|\| ps_shortcut_enabled` |

The macro engine generalises this file rather than sitting beside it. The PS
shortcut becomes two default table entries and `ps_shortcut.cpp` is deleted.

---

## 2. Storage split

**The macro table is device-global. Only the enable mask is per-slot.**

The definitions cannot live in the portal. The dongle runs standalone with
nothing attached — it must hold what each enabled bit means. Keeping the table on
the device also means the macro list survives a browser reset, a reinstall or a
different PC, which matches the portal's existing invariant: it uses **no** browser
storage at all and treats the device as the only source of truth.

| | Lives in | Size |
|---|---|---|
| 32 macro definitions + labels | Own flash sector, `MACRO_FLASH_OFFSET` | 908 B of 4096 |
| Enable mask | `Config_body.macro_disable` | 4 B |

A slot therefore selects *which subset* of the shared table is live. Per-game
macro sets work through the existing Playnite slot automation with no new moving
parts and no duplication of definitions across 24 slots.

### 2.1 The mask is stored inverted

`macro_disable` — a **set bit means disabled**. This is forced, not stylistic.

Slots written by older firmware are read back 0xFF-filled past their recorded
`body_len`, and `config_valid()` converts that fill into a sane default by
range-clamping each field. A bitmap has no invalid range: `0xFFFFFFFF` is the
perfectly legal "all 32 enabled". Stored the obvious way round, every
pre-existing slot would load with every macro switched on *and* drag a spurious
USB re-enumeration along with it. Inverted, the 0xFF fill reads as "all
disabled" — exactly the wanted default, and it needs no clamp at all.

The inversion stays at the storage layer. Firmware, wire format and portal state
all speak `macro_disable`; the one place it flips is the checkbox's `checked`
attribute at render time.

### 2.2 Flash map and the slot reservation

Slots grow **downward** from −4, so anything parked immediately below the last
slot sector is overrun the moment `SLOT_COUNT` is raised. At 24 slots the last
sector is −6; at 32 it would be −7. Placing macros at −7 would have made a
future 32-slot bump silently eat the table.

```
-1   legacy config / btstack TLV
-2   legacy slots  / btstack TLV
-3   ACTIVE CONFIG
-4 … -19   SLOT RESERVATION  (SLOT_SECTORS_RESERVED = 16 → up to 128 slots)
-20  MACRO TABLE            (grows downward if it ever needs more)
```

Enforced in `config.cpp`, not documented in prose:

```c
static_assert(SLOT_COUNT / SLOTS_PER_SECTOR <= SLOT_SECTORS_RESERVED, …);
static_assert(MACRO_FLASH_OFFSET < slot_sector_offset(SLOT_SECTORS_RESERVED - 1), …);
```

The reservation is free: the firmware image is ~723 KB (the 1.48 MB `.uf2` is the
UF2 container, 256 payload bytes per 512-byte block), leaving ~837 spare sectors
on the 4 MB Pico 2 W and far more on the 16 MB Waveshare. 64 KB held back costs
nothing measurable.

**Do not** anchor the macro sector to the *current* slot count
(`slot_sector_offset(SLOT_COUNT / SLOTS_PER_SECTOR)`). It looks self-maintaining
and is the worst option available: raising `SLOT_COUNT` moves the macro sector,
orphaning its old contents *and* letting slot writes land on top of them. Same
failure class as the v1.17.1 automation cap — a capability constant raised in one
place quietly shifting a layout somewhere else.

### 2.3 Upgrade path

−20 is virgin flash on every existing device, so the loader finds no valid magic
and defaults to an empty table. No migration, no `flash_nuke`. `MacroTable.rec_len`
makes the record self-describing the way `SlotRecordV2.body_len` does, so a later
firmware with a larger record still reads today's tables.

---

## 3. Data model

> Terminology: the docs say **button press** / **combo**; the code calls the field
> `chord` (`MacroEntry.chord`, `best_chord()`, `macroChordName()`). The identifiers
> are left alone deliberately — renaming them would decouple this document from
> the source it describes.


`src/macro.h`. 12-byte entry, 28-byte record with its label.

```c
struct MacroEntry {          // 12 bytes
    uint32_t chord;          // logical button mask; 0 if this is a gesture
    uint8_t  gesture;        // GESTURE_NONE or GEST_* bits
    uint8_t  flags;          // MACRO_FLAG_LONG_PRESS
    uint8_t  hold_cs;        // long-press threshold, centiseconds (0 → 75)
    uint8_t  keys[4];        // HID usages in PRESS order, 0 = unused
    uint8_t  rel_order;      // release permutation, 2 bits per slot
};
```

### 3.1 Output: one uniform usage namespace

Modifiers are **not** a separate field. They are HID usages `0xE0`–`0xE7`
(LeftCtrl … RightGUI) sitting in `keys[]` like anything else, and `macro_play()`
folds any usage in that range into the boot keyboard's modifier byte as it walks
the list. One namespace means no ambiguity about ordering between a modifier and
a key — which is the whole point, because ordering is what the user asked for.

`keys[]` is press order; `rel_order` is the release order as a 2-bit-per-slot
permutation. That is what distinguishes Alt+Tab (release Tab, then Alt) from a
combo where the modifier lifts first. Playback is N boot-keyboard reports
`MACRO_STEP_MS` apart: `Alt` → `Alt+Tab` → `Alt` → empty.

Reverse-press order is the overwhelmingly common case. The recorder should fall
back to it whenever a capture yields an ambiguous or implausible ordering.

### 3.2 Browser capture limits — the picker is not optional

The output recorder is `keydown`/`keyup` with `event.code`, giving physical key
identity independent of layout. But a browser **cannot** capture:

- anything with the **Win key** — it goes to Windows and never reaches the page
- **Alt+Tab** — Windows intercepts it and the page loses focus mid-capture
- Ctrl+W, Ctrl+T, Ctrl+N, F11 — swallowed or acted on regardless of `preventDefault()`

Note that this covers *both* of the existing PS shortcuts. So the manual picker
(modifier checkboxes + key list) is a required half of the output field, not a
fallback, and the recorder must detect "you pressed something I could not see"
and point at it. `navigator.keyboard.lock()` recovers a few of these but needs
fullscreen, is Chromium-only, and still never gets the Win key — not worth it.

---

## 4. Logical button decode

`src/input_buttons.h` is the single source of truth. The portal carries a JS
transcription; `tools/portal-buttons-test.js` cross-checks both, deriving the
expected bits from the header text itself rather than a hand-copied table, and
asserting that every bit the firmware decodes is reachable from the portal's
capture path. That last check exists because it failed in practice: 1.29.0
added the Edge bits to the header and to the portal's button table, but the
portal's live decoder still stopped at bit 18, so the firmware could match a
chord recording could never produce.

**The D-pad is an enum, not a bitmask.** This is the one thing a naive
24-bit-mask-over-bytes-7/8/9 implementation gets wrong: "Up" is hat value 0, so
it is indistinguishable from "no button", and the idle value 8 reads as a
phantom press. Everything goes through `button_mask()`.

25 bits defined: 4 D-pad directions expanded from the hat, 4 face, 4
shoulder/trigger-click, Create/Options/L3/R3, PS/touchpad-click/mute (bits
0-18), the DualSense Edge Fn buttons and paddles (19-22), and the touchpad
click qualified by half (23-24). **Append only** — these values are persisted
inside every stored `chord`, so renumbering silently rebinds every macro a user
saved.

**Bits 23/24 are derived, not reported.** The pad has one physical switch; the
half comes from finger 1's X in the same report. `BTN_TOUCHPAD` is still set on
every click, so pre-existing generic bindings keep matching, and a click with no
finger reported (knuckle, pad edge) stays unqualified rather than guessing a
side. Because a click reports the generic bit *and* the half, `best_chord()`
ranks a chord naming a half as one more than its bit count — otherwise a
one-button "click (left)" would tie with a one-button generic "click" and lose
on table order.

Verified behaviour: idle → `0x00000`; hat 0 → `UP`; hat 7 → `UP|LEFT`; hat `0x0F`
clamps to idle; R3+Up → `0x08001`; a short report → `0` rather than garbage.

Touch points decode at bytes 32 (finger 1) and 36 (finger 2), `bit7 set = lifted`
— the inversion `main.cpp:143` already relies on for gyro modes 3/4.

---

## 5. Engine

### 5.1 Chord matching

- **Longest button press wins**, and a matched one suppresses any bound subset of
  itself. Otherwise binding both R3 and R3+Up fires both.
- Capture the **peak simultaneous set**, not the union over the recording window.
  Press R3, then Up a moment later, and a union also catches anything brushed in
  between.
- Reuse the 50 ms debounce hold from `ps_shortcut.cpp`; BT reports drop bits.

### 5.2 Firing rules

The recording duration itself sets the mode — tap the buttons while recording and
it stores short, hold it and it stores long with the captured threshold. No extra
UI control in the row, and two rows can share one button press with different modes,
which is exactly how PS → Win+G / Win+Tab generalises.

| Chord has | Fires |
|---|---|
| only a short macro | on **press** — snappy |
| both short and long | short fires on **release** (you cannot know it is short until then) |
| only a long macro | at the threshold, buttons still held |

That latency difference is real and belongs in the UI, not in a user's
discovery process.

### 5.3 Gestures

Touch-down records `(x, y, t)`; touch-up classifies if `dt` is 40–600 ms and
travel exceeds threshold. 4 directions × start zone (left/right half) × finger
count = 16 gestures in one byte. Gestures have no release, so they always fire a
pulse — hold semantics apply to button presses only.

### 5.4 Every output is a pulse

No macro ever holds a key across time. That removes the stuck-key-across-sleep
hazard that hold-to-hold would have created — the keyboard-side twin of the
v1.15.1 latched-actuator bug. `macro_reset()` still flushes on the suspend edge
for a combo caught mid-playback.

---

## 6. Sharing the keyboard with wake

Both engines own HID instance 1. Four rules:

1. **Gate on `wake_host_is_suspended()`.** Non-negotiable per the wake rule —
   macro traffic while suspended competes with the BT input path wake depends on.
2. **Flush on the suspend edge** via `macro_reset()`, beside the existing
   `state_release_for_suspend()`.
3. **Add `wake_owns_keyboard()`** and refuse to write while the wake FSM is
   between `WAKE_REQUESTED` and `WAKE_KEY_UP_SENT`. Nothing arbitrates today.
   Symmetrically, wake checks `macro_busy()` before its F15.
4. There are already three `tud_hid_n_ready(WAKE_KBD_INSTANCE)` sites — patch by
   line index, not pattern.

The boot keyboard is 8 bytes / 6-key rollover / modifier byte. Media and
consumer-control keys need a descriptor change, which touches the
byte-identical-layout invariant from v1.18.11 — **out of scope, deliberately.**

---

## 7. Enumeration

The keyboard interface becomes:

```c
const bool kbd = wake || get_config().ps_shortcut_enabled
                      || macro_any_enabled(get_config().macro_disable);
```

Only the **all-disabled crossing** is enumeration-critical. Flipping *which*
macros are on must not cost a reconnect.

### 7.1 Which list covers what

`slot_activate()`'s `needs_reenum` and the portal's `ENUM_FIELDS` are deliberately
different sizes. The firmware list also carries `ps_shortcut_enabled` and
`disable_usb_sn`, because a **slot body** can carry any field value. The portal
list only needs the fields the portal can actually change, and neither of those
two is exposed in its UI — so they can never differ between two portal saves.

If either is ever exposed in the portal, it must be added to `ENUM_FIELDS` at the
same time, and `kbdIfaceNeeded()` must actually read `ps_shortcut_enabled` rather
than seeing `undefined`.

## 8. Command map

Config field, `0x6c` (0x68–0x6b are the auto-haptics diagnostics, 0x7d–0x7f the
version):

| ID | Field |
|---|---|
| `0x6c` | `macro_disable` (uint32, read/write) |

`0x84`-framed sub-commands — `0x01`–`0x16` are taken, so:

| ID | Purpose |
|---|---|
| `0x17` | read macro entry `idx` → record + label |
| `0x18` | write macro entry `idx` into the RAM image |
| `0x19` | commit the RAM image to flash (one erase per save, not 32) |
| `0x1a` | suspend/resume macro firing (portal record mode) |

`0x1a` matters more than it looks: without it, recording a combo that matches an
already-enabled macro types into the portal while the user is capturing it.

A fifth command for reading the live button state was designed and then dropped:
the portal subscribes to the gamepad's own `inputreport` stream instead, which is
full-rate and needs no round trip. (Note `0x1b` is already `bt_flush_timeout` as a
config field id — the two namespaces are separate, but do not reuse it here.)

---

## 9. Portal

New `macros` tab, new `SECTION_TAB` entry. The row is:

```
[✓] [Name] [Record input] [input field] [Record output] [combo field] [+] [−]
```

- Input capture needs `device.addEventListener('inputreport', …)`. The portal
  opens the device at five sites today but **never subscribes to input reports** —
  adding that gives the full 63-byte report at polling rate, the same bytes the
  firmware sees at the same offsets. No firmware round-trip, full resolution.
- Only `macro_disable` joins `FIELDS` (88 → 89 — update the counts in
  `portal-coverage-test.js` and `portal-render-test.js`). The macro list itself
  is bespoke UI on its own command path, like Slots and the CE builder.
- `render()` rebuilds dynamic sub-panels to their placeholder while the backing
  data survives — the macro list must be repopulated after render, same as
  `ceb_list` / `tlbox` / `effmon`.
- Gate the table sweep on the Macros tab being visible (the v1.17.0 lesson: an
  unconditional `refreshSlots()` ran on every tab switch).

### 9.1 Export gap — close it on day one

Macros live outside `Config_body`, so a slot backup restores the mask but not the
table. That is byte-for-byte the v1.16.0 bug where slot backups silently lost
every custom effect. Wire macros into `exportProfile`, `exportHtml` and
`backupAllSlots` in the **same commit** that adds the storage, not afterwards.

---

## 10. Staging

| Stage | Scope | State |
|---|---|---|
| 0 | Slot reservation, macro sector anchor, `macro_disable` | shipped |
| 1 | `input_buttons.h` + JS transcription | shipped |
| 2 | `macro.cpp`: table load/commit, button-press FSM, playback walk | shipped |
| 3 | Gesture detector feeding the same table | shipped |
| 4 | Portal tab, recorders, manual picker, harness | shipped |
| 5 | Retire `ps_shortcut.cpp` into two default entries | **not started** |

---

## 11. Open items

- **New-field rule:** `macro_disable` must be re-saved into existing slots or a
  later profile apply reverts it. Tell users on release.
- Should a matched combo be **suppressed from the game**? Deferred — you cannot
  know R3 is part of a combo until the second button arrives, so suppression
  costs latency on the modifier button. Pick combos games do not use for now.
- Bulk table read (15 × 60-byte pages) if the 32-entry per-entry sweep feels slow.
  Do not pre-optimise it.
- Config page is at **245/256, 11 bytes headroom.**
- **Export/backup gap still open.** The macro table lives outside `Config_body`,
  so a slot backup captures the enable mask but not the definitions. Same shape as
  the v1.16.0 bug where slot backups silently lost custom effects.
- **`ps_shortcut.cpp` still runs in parallel.** It writes the same keyboard
  instance as the macro engine with no arbitration between them. Retiring it into
  two default table entries is stage 5.
