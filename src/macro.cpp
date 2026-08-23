//
// macro.cpp - chord / gesture -> keyboard combo engine.
//
// Generalises ps_shortcut.cpp. See docs/MACRO-ARCHITECTURE.md for the storage
// split and the reasoning behind the inverted enable mask.
//

#include "macro.h"

#include <cstdio>
#include <cstring>

#include "config.h"
#include "flash_map.h"
#include "input_buttons.h"
#include "utils.h"
#include "wake.h"

#include "hardware/flash.h"
#include "hardware/sync.h"
#include "pico/flash.h"
#include "pico/time.h"
#include "tusb.h"

// Instance 1 is the wake keyboard. It is a CONSTANT in both identities - the
// interface layout is byte-identical whether or not the idle USB identity is
// active, which is the invariant that makes the v1.18.9 report-storm
// arithmetically impossible. Never derive this from a runtime flag.
#define MACRO_KBD_INSTANCE 1

// A bit that drops out of one BT report is not a release. ps_shortcut.cpp uses
// the same 50 ms hold for exactly this reason.
constexpr uint32_t MACRO_DEBOUNCE_MS = 50;

// Swipe classification thresholds. Pad is 1920 x 1080.
constexpr uint32_t GEST_MIN_MS = 40;
constexpr uint32_t GEST_MAX_MS = 600;
constexpr int32_t  GEST_MIN_DX = 300;
constexpr int32_t  GEST_MIN_DY = 200;

// Gyro offsets in the BT input report, same base main.cpp uses (data + 3).
// BYTE 17 IS THE HORIZONTAL TURN, NOT BYTE 19. The DualSense field names say
// otherwise; main.cpp carries the hardware-verified correction and a recogniser
// written from the names responds to the wrong axis and merely looks flaky.
constexpr uint16_t RPT_GYRO_PITCH = 15;
constexpr uint16_t RPT_GYRO_YAW   = 17;
constexpr uint16_t RPT_GYRO_MIN_LEN = RPT_GYRO_YAW + 2;

// Motion recogniser. Every one of these came out of hardware captures; see the
// portal's matching block and tools/portal-motion-test.js, which drives the
// same traces through the JavaScript twin.
constexpr int32_t MOT_NOISE   = 24; // per-sample deadzone; without it resting
                                    // BIAS sums to a stroke over a long hold
constexpr int32_t MOT_TURN    = 2;  // perpendicular turn must win by this much
constexpr int32_t MOT_REVERSE = 1;  // a reversal commits sooner: drift happens
                                    // constantly, nobody reverses by accident

// Flash image is page-granular; sizeof(MacroTable) is 1068.
constexpr uint32_t MACRO_IMAGE_BYTES =
    ((sizeof(MacroTable) + FLASH_PAGE_SIZE - 1) / FLASH_PAGE_SIZE) * FLASH_PAGE_SIZE;
static_assert(MACRO_IMAGE_BYTES <= FLASH_SECTOR_SIZE);

// ============================== state ======================================

static MacroTable g_table;          // RAM working image
static bool       g_loaded    = false;
static bool       g_suspended = false;

// Chord tracking
static uint32_t g_stable       = 0;      // debounced held mask
static uint32_t g_off_since[32] = {0};   // per-bit fall time, 0 = not falling
static uint32_t g_chord        = 0;      // armed chord value, 0 = none
static uint32_t g_chord_start  = 0;
static bool     g_chord_fired  = false;  // long fired, or short fired on press

// Touch tracking
static bool     g_touch_down = false;
static bool     g_touch_two  = false;
static uint16_t g_touch_x0   = 0;
static uint16_t g_touch_y0   = 0;
static uint16_t g_touch_lx   = 0;   // last sample while STILL DOWN
static uint16_t g_touch_ly   = 0;
static uint32_t g_touch_t0   = 0;

// Motion tracking. The portal buffers raw samples so the step can be retuned
// after the fact; the firmware knows the step up front and quantises STREAMING,
// which is equivalent because the quantiser is a single forward pass with no
// lookahead.
static bool     g_mot_active = false;
static uint32_t g_mot_gate   = 0;      // gate mask being held, 0 = idle
static int32_t  g_mot_cx     = 0;
static int32_t  g_mot_cy     = 0;
static int8_t   g_mot_cur    = -1;     // committed stroke direction, -1 = none
static int32_t  g_mot_step   = MACRO_MOTION_STEP_DEFAULT;
static uint8_t  g_mot_codes[MACRO_MOTION_MAX];
static uint8_t  g_mot_n      = 0;
static bool     g_mot_moved  = false;

// --- HOLD state --------------------------------------------------------------
// A held row asserts its output for as long as its input is active, so this is
// a STATE recomputed every input report, not a queued sequence. Playback and
// hold share one keyboard interface: playback wins while it is running, and the
// hold set is re-sent when it next changes, so a burst cannot be cut in half by
// a hold row updating underneath it.
static uint8_t  g_hold_keys[MACRO_KEYS * 4];
static uint8_t  g_hold_n       = 0;
static uint8_t  g_hold_sent[MACRO_KEYS * 4];
static uint8_t  g_hold_sent_n  = 0;
static bool     g_hold_pending = false;
static uint32_t g_suppress     = 0;   // logical buttons hidden from the host
static uint32_t g_inject       = 0;   // logical buttons asserted for the host
static bool     g_sup_lstick   = false;
static bool     g_sup_rstick   = false;
// Per-row stick direction latches, so a stick resting on a threshold cannot
// chatter the key. Same reason the two-stage trigger needed hysteresis.
static uint8_t  g_stick_latch[MACRO_COUNT];

uint32_t macro_suppress_mask() { return g_suppress; }
uint32_t macro_inject_mask()   { return g_inject; }
bool     macro_suppress_stick(bool right) { return right ? g_sup_rstick : g_sup_lstick; }

static void hold_add_key(uint8_t usage) {
    if (usage == 0 || g_hold_n >= sizeof(g_hold_keys)) return;
    for (uint8_t i = 0; i < g_hold_n; i++) if (g_hold_keys[i] == usage) return;
    g_hold_keys[g_hold_n++] = usage;
}

// Playback walk
static uint8_t  g_pb_keys[MACRO_KEYS];
static uint8_t  g_pb_rel  = 0;   // release permutation
static uint8_t  g_pb_n    = 0;   // key count
static uint8_t  g_pb_step = 0;   // 0 .. 2n, 2n means "done after this"
static bool     g_pb_busy = false;
static uint32_t g_pb_next = 0;

static inline uint32_t now_ms() { return to_ms_since_boot(get_absolute_time()); }

// ============================== storage ====================================

static uint32_t table_crc(const MacroTable &t) {
    return crc32(reinterpret_cast<const uint8_t *>(t.rec), sizeof(t.rec));
}

static void table_defaults(MacroTable &t) {
    memset(&t, 0, sizeof(t));
    t.magic   = MACRO_MAGIC;
    t.format  = MACRO_FORMAT;
    t.count   = MACRO_COUNT;
    t.rec_len = sizeof(MacroRecord);
    t.crc32   = table_crc(t);
}

void macro_load() {
    const auto *flashed =
        reinterpret_cast<const MacroTable *>(XIP_BASE + MACRO_FLASH_OFFSET);

    // Virgin flash on every existing device: no magic, so fall to an empty
    // table. No migration and no flash_nuke on upgrade.
    const uint8_t *raw = reinterpret_cast<const uint8_t *>(XIP_BASE + MACRO_FLASH_OFFSET);
    uint16_t rec_len = 0;
    memcpy(&rec_len, raw + 6, sizeof(rec_len));

    if (flashed->magic != MACRO_MAGIC || flashed->format != MACRO_FORMAT ||
        rec_len == 0 || rec_len > sizeof(MacroRecord) || flashed->count > MACRO_COUNT) {
        table_defaults(g_table);
        g_loaded = true;
        printf("[Macro] no valid table, starting empty\n");
        return;
    }

    // SHORTER records are migrated, not rejected. macro.h has always promised
    // that rec_len makes the record self-describing so a later firmware reads
    // today's tables - but the loader required an exact match, so growing the
    // record by one byte would have silently wiped every user's macros on
    // upgrade. The claim was in the comment and not in the code.
    const uint8_t *recs = raw + 8;
    uint32_t stored_crc = 0;
    memcpy(&stored_crc, recs + (size_t) MACRO_COUNT * rec_len, sizeof(stored_crc));
    if (stored_crc != crc32(recs, (size_t) MACRO_COUNT * rec_len)) {
        printf("[Macro] table CRC mismatch, starting empty\n");
        table_defaults(g_table);
        g_loaded = true;
        return;
    }

    table_defaults(g_table);
    g_table.count = flashed->count;

    // A flat copy of rec_len bytes is NOT enough once a field is appended to
    // MacroEntry. `label` lives AFTER the entry inside MacroRecord, so growing
    // the entry from 12 to 17 moved the label from offset 12 to offset 17 - and
    // copying the old 28 bytes straight in lands the label five bytes early,
    // over motion[] and motion_len. Verified before this fix: a macro named
    // "rivatuner" reloaded as label "uner" with motion_len = 118, which is 'v'.
    //
    // Migration therefore has to know the old LAYOUT, not just the old total
    // size. Each historical record length gets its own entry/label split here.
    // EVERY historical record length needs its own entry/label split. The label
    // lives AFTER MacroEntry inside MacroRecord, so each time the entry grows the
    // label moves and a flat rec_len-byte copy silently shifts it. Listing only
    // some lengths is worse than listing none: the unknown branch below discards
    // the table, so omitting 33 wiped every macro on every device running 1.20.0
    // through 1.24.2.
    constexpr uint16_t REC_LEN_V1  = 28;  // 1.19.x:          12-byte entry + label
    constexpr uint8_t  ENTRY_V1    = 12;
    constexpr uint16_t REC_LEN_V2  = 33;  // 1.20.0-1.24.2:   17-byte entry + label
    constexpr uint8_t  ENTRY_V2    = 17;
    for (uint8_t i = 0; i < MACRO_COUNT; i++) {
        const uint8_t *src = recs + (size_t) i * rec_len;
        if (rec_len == sizeof(MacroRecord)) {
            memcpy(&g_table.rec[i], src, rec_len);          // current layout
        } else if (rec_len == REC_LEN_V2) {
            memcpy(&g_table.rec[i].entry, src, ENTRY_V2);
            memcpy(g_table.rec[i].label, src + ENTRY_V2, MACRO_LABEL_LEN);
            // out_btn / stick_thresh keep table_defaults()' zeroes, which read
            // as "keyboard output, default stick threshold" - correct for a row
            // written before either field existed.
        } else if (rec_len == REC_LEN_V1) {
            memcpy(&g_table.rec[i].entry, src, ENTRY_V1);   // entry prefix
            memcpy(g_table.rec[i].label, src + ENTRY_V1, MACRO_LABEL_LEN);
            // Appended fields keep the zeroes table_defaults() left: no motion.
        } else {
            // Unknown historical length - refuse rather than guess at a split
            // and hand back records whose fields are silently misaligned.
            printf("[Macro] unknown rec_len %u, starting empty\n", (unsigned) rec_len);
            table_defaults(g_table);
            g_loaded = true;
            return;
        }
    }
    if (rec_len != sizeof(MacroRecord)) {
        printf("[Macro] migrated table from rec_len %u to %u\n",
               (unsigned) rec_len, (unsigned) sizeof(MacroRecord));
    }
    g_table.crc32 = table_crc(g_table);
    g_loaded = true;
}

// Runs with core1 parked and interrupts off, exactly like config_save_flash_op.
// Without the core1 park this races the audio core and corrupts audio.
static void macro_flash_op(void *param) {
    const uint8_t *img = static_cast<const uint8_t *>(param);
    const uint32_t interrupts = save_and_disable_interrupts();
    flash_range_erase(MACRO_FLASH_OFFSET, FLASH_SECTOR_SIZE);
    flash_range_program(MACRO_FLASH_OFFSET, img, MACRO_IMAGE_BYTES);
    restore_interrupts(interrupts);
}

bool macro_commit() {
    if (!g_loaded) macro_load();
    g_table.magic   = MACRO_MAGIC;
    g_table.format  = MACRO_FORMAT;
    g_table.count   = MACRO_COUNT;
    g_table.rec_len = sizeof(MacroRecord);
    g_table.crc32   = table_crc(g_table);

    alignas(4) static uint8_t image[MACRO_IMAGE_BYTES];
    memset(image, 0xff, sizeof(image));
    memcpy(image, &g_table, sizeof(MacroTable));

    const int rc = flash_safe_execute(macro_flash_op, image, 1000);
    if (rc != PICO_OK) {
        printf("[Macro] commit flash_safe_execute failed: %d\n", rc);
        return false;
    }
    const auto *v = reinterpret_cast<const MacroTable *>(XIP_BASE + MACRO_FLASH_OFFSET);
    if (v->crc32 != g_table.crc32) {
        printf("[Macro] commit verify failed\n");
        return false;
    }
    return true;
}

bool macro_get(uint8_t idx, MacroRecord &out) {
    if (idx >= MACRO_COUNT) return false;
    if (!g_loaded) macro_load();
    out = g_table.rec[idx];
    return true;
}

bool macro_set_entry(uint8_t idx, const MacroRecord &rec) {
    if (idx >= MACRO_COUNT) return false;
    if (!g_loaded) macro_load();
    g_table.rec[idx] = rec;
    // motion_len indexes motion[] two bits at a time, so a value past
    // MACRO_MOTION_MAX reads off the end of a 2-byte array and into the
    // neighbouring record. It arrives straight off the wire in cmd 0x18, so
    // clamp it here - the single point every record enters the table - rather
    // than trusting each reader to bound it.
    if (g_table.rec[idx].entry.motion_len > MACRO_MOTION_MAX) {
        g_table.rec[idx].entry.motion_len = 0;   // not a motion macro
    }
    // Editing an entry invalidates any chord armed against the old definition.
    g_chord = 0;
    return true;
}

// ============================== playback ===================================

// Send the keyboard state for a set of currently-pressed key SLOTS. Modifiers
// are ordinary usages 0xE0-0xE7 in keys[]; they fold into the modifier byte
// here, which is why the format needs no separate modifier field and why press
// order between a modifier and a key is unambiguous.
static bool send_usages(const uint8_t *usages, uint8_t n, uint32_t pressed_slots) {
    if (!tud_hid_n_ready(MACRO_KBD_INSTANCE)) return false;
    uint8_t mods = 0;
    uint8_t keys[6] = {0};
    uint8_t kn = 0;
    for (uint8_t i = 0; i < n; i++) {
        if (!(pressed_slots & (1u << i))) continue;
        const uint8_t u = usages[i];
        if (u >= HID_USAGE_MOD_FIRST && u <= HID_USAGE_MOD_LAST) {
            mods |= (uint8_t) (1u << (u - HID_USAGE_MOD_FIRST));
        } else if (u != 0 && kn < 6) {
            keys[kn++] = u;
        }
    }
    return tud_hid_n_keyboard_report(MACRO_KBD_INSTANCE, 0, mods, kn ? keys : nullptr);
}

static bool send_state(uint32_t pressed_slots) {
    return send_usages(g_pb_keys, g_pb_n, pressed_slots);
}

// Which slot is released at release-position p.
static int slot_at_release_pos(uint8_t p) {
    for (uint8_t i = 0; i < g_pb_n; i++) {
        if (((g_pb_rel >> (2u * i)) & 3u) == p) return i;
    }
    return -1;
}

static uint32_t pressed_for_step(uint8_t step) {
    const uint32_t all = (g_pb_n >= 32) ? 0xFFFFFFFFu : ((1u << g_pb_n) - 1u);
    // Belt and braces: whatever the permutation says, the LAST step releases
    // everything. The guard in playback_start should make this unreachable, but
    // a stuck modifier at the host is bad enough to warrant two independent
    // defences rather than one.
    if (step >= (uint8_t) (2u * g_pb_n - 1u)) return 0;
    if (step < g_pb_n) {
        // Press phase: keys 0..step are down.
        return (step >= 31) ? all : ((1u << (step + 1)) - 1u);
    }
    // Release phase: positions 0..(step - n) have been released.
    uint32_t pressed = all;
    for (uint8_t p = 0; p <= (uint8_t) (step - g_pb_n); p++) {
        const int s = slot_at_release_pos(p);
        if (s >= 0) pressed &= ~(1u << s);
    }
    return pressed;
}

// rel_order must be a genuine permutation of 0..n-1: every release position
// claimed exactly once. If it is not, slot_at_release_pos() returns -1 for the
// unclaimed positions and those keys are NEVER released - playback ends with
// them still down and the host autorepeats them forever. rel_order == 0 is the
// default in a zeroed table and in anything a portal sends without setting it,
// and with n > 1 it makes every slot claim position 0, so the default value is
// itself the failure case. Verified on a host harness before this guard existed:
// Ctrl+J ended on [mods=00 keys=0D], i.e. J held down.
static bool rel_order_is_permutation(uint8_t rel, uint8_t n) {
    uint8_t seen = 0;
    for (uint8_t i = 0; i < n; i++) {
        const uint8_t p = (uint8_t) ((rel >> (2u * i)) & 3u);
        if (p >= n) return false;
        if (seen & (uint8_t) (1u << p)) return false;
        seen |= (uint8_t) (1u << p);
    }
    return true;
}

// Reverse press order: last pressed is first released. The correct answer for
// essentially every real combo, and the only sane fallback.
static uint8_t reverse_press_order(uint8_t n) {
    uint8_t rel = 0;
    for (uint8_t i = 0; i < n; i++) {
        rel |= (uint8_t) ((uint8_t) (n - 1u - i) << (2u * i));
    }
    return rel;
}

static void playback_start(const MacroEntry &e) {
    g_pb_n = 0;
    for (uint8_t i = 0; i < MACRO_KEYS; i++) {
        if (e.keys[i] == 0) break;
        g_pb_keys[g_pb_n++] = e.keys[i];
    }
    if (g_pb_n == 0) return;
    g_pb_rel  = rel_order_is_permutation(e.rel_order, g_pb_n)
                    ? e.rel_order
                    : reverse_press_order(g_pb_n);
    g_pb_step = 0;
    g_pb_next = now_ms();
    g_pb_busy = true;
}

static void playback_task() {
    if (!g_pb_busy) return;
    const uint32_t now = now_ms();
    if ((int32_t) (now - g_pb_next) < 0) return;

    // If the endpoint is not ready, retry next pass rather than dropping a
    // transition - a lost release would leave a modifier latched at the host.
    if (!send_state(pressed_for_step(g_pb_step))) return;

    g_pb_step++;
    g_pb_next = now + MACRO_STEP_MS;
    if (g_pb_step >= (uint8_t) (2u * g_pb_n)) {
        g_pb_busy = false;
        g_pb_n    = 0;
    }
}

// ============================== matching ===================================

static inline uint8_t popcount32(uint32_t v) {
    uint8_t n = 0;
    while (v) { v &= (v - 1u); n++; }
    return n;
}

static int find_entry(uint32_t chord, bool want_long, uint32_t disable) {
    for (uint8_t i = 0; i < MACRO_COUNT; i++) {
        const MacroEntry &e = g_table.rec[i].entry;
        if (e.gesture != GESTURE_NONE || e.chord != chord || e.chord == 0) continue;
        if (!macro_is_enabled(disable, i)) continue;
        if (((e.flags & MACRO_FLAG_LONG_PRESS) != 0) != want_long) continue;
        return i;
    }
    return -1;
}

// Longest bound chord that is FULLY HELD wins. Note what this does and does not
// give you: a chord only matches once every one of its buttons is down, so with
// only R3+Up bound, holding R3 alone matches nothing and the macro fires the
// instant Up lands, however long R3 was held first.
//
// It does NOT suppress a bound SUBSET. Bind R3 as well and pressing R3 first
// arms and fires the R3 macro; the held branch then returns early and never
// re-evaluates, so R3+Up becomes unreachable. That is inherent - at the moment
// R3 goes down the firmware cannot know whether you meant "R3" or "the first
// half of R3+Up". The portal warns when one bound chord contains another rather
// than the firmware guessing with an arm delay.
static uint32_t best_chord(uint32_t mask, uint32_t disable) {
    uint32_t best = 0;
    uint8_t  best_bits = 0;
    for (uint8_t i = 0; i < MACRO_COUNT; i++) {
        const MacroEntry &e = g_table.rec[i].entry;
        if (e.gesture != GESTURE_NONE || e.chord == 0) continue;
        if (!macro_is_enabled(disable, i)) continue;
        if ((mask & e.chord) != e.chord) continue;
        // Rank by how SPECIFIC the chord is, which is its bit count plus one
        // for naming a touchpad half. Every click reports the generic bit AND
        // the half, so a one-button "click (left)" chord would otherwise tie
        // with a one-button generic "click" chord and lose on table order -
        // the more specific binding must win, exactly as a longer chord does.
        uint8_t bits = popcount32(e.chord);
        if (e.chord & (BTN_PAD_CLICK_LEFT | BTN_PAD_CLICK_RIGHT)) bits++;
        if (bits > best_bits) { best_bits = bits; best = e.chord; }
    }
    return best;
}


// --- hold / remap / stick evaluation -----------------------------------------
// Runs every input report and rebuilds the whole output state from scratch. A
// row is "active" when its input is currently true, which is a different
// question from the edge detection the burst rows use - that is exactly why
// HOLD needs its own pass rather than another branch inside find_entry().
static int16_t stick_axis(const uint8_t *r, uint8_t idx) {
    return (int16_t) ((int16_t) r[idx] - 128);      // 0..255 -> -128..127
}

// One cardinal of one stick, with hysteresis: engage at thresh, release at
// thresh - MACRO_STICK_HYST. Without it a stick parked on the threshold streams
// key up/down at report rate.
static bool stick_dir_active(int16_t v, bool positive, uint8_t thresh, bool latched) {
    const int16_t on  = (int16_t) thresh;
    const int16_t off = (int16_t) (thresh > MACRO_STICK_HYST
                                   ? thresh - MACRO_STICK_HYST : 1);
    const int16_t mag = positive ? v : (int16_t) -v;
    return latched ? (mag >= off) : (mag >= on);
}

// Analog travel to apply to an L2/R2 controller output, 0 when that output is
// not active. Set only when a TRIGGER drives a trigger; a button driving one
// leaves 255, a full press.
static uint8_t g_analog_l2 = 0, g_analog_r2 = 0;
uint8_t macro_analog_out(bool right) { return right ? g_analog_r2 : g_analog_l2; }

// Mouse output. Clicks are a STATE - held while the input is held, so a
// click-and-drag works. Scroll is an EVENT: one tick per press, latched per row
// so holding the button does not spin the wheel at report rate.
static uint8_t  g_mouse_btns   = 0;
static int8_t   g_mouse_scroll = 0;
static uint32_t g_scroll_latch = 0;
uint8_t macro_mouse_buttons() { return g_mouse_btns; }
int8_t  macro_mouse_peek_scroll() { return g_mouse_scroll; }
int8_t  macro_mouse_take_scroll() { const int8_t v = g_mouse_scroll; g_mouse_scroll = 0; return v; }

// Whether the COMMITTED table has a mouse output among the enabled rows. Drives
// whether the mouse interface exists at all, so it must read flash rather than
// any in-progress edit.
bool macro_any_mouse_output(uint32_t disable_mask) {
    if (!g_loaded) macro_load();
    for (uint8_t i = 0; i < MACRO_COUNT; i++) {
        if (!macro_is_enabled(disable_mask, i)) continue;
        if (macro_is_mouse_out(g_table.rec[i].entry.out_btn)) return true;
    }
    return false;
}

static void hold_task(const uint8_t *r, uint16_t len, uint32_t mask, uint32_t disable) {
    g_hold_n     = 0;
    g_suppress   = 0;
    g_inject     = 0;
    g_sup_lstick = false;
    g_sup_rstick = false;
    g_analog_l2  = 0;
    g_analog_r2  = 0;
    g_mouse_btns = 0;   // scroll is NOT cleared here: it is a pending tick that
                        // main.cpp consumes, not a per-report state
    if (len < RPT_MIN_LEN) return;

    for (uint8_t i = 0; i < MACRO_COUNT; i++) {
        const MacroEntry &e = g_table.rec[i].entry;
        if (!macro_is_enabled(disable, i)) { g_stick_latch[i] = 0; continue; }

        if (macro_is_stick(e)) {
            const bool right = (e.gesture & GEST_STICK_RIGHT) != 0;
            const uint8_t th = e.stick_thresh ? e.stick_thresh : MACRO_STICK_THRESH;
            const int16_t x  = stick_axis(r, right ? 2 : 0);
            const int16_t y  = stick_axis(r, right ? 3 : 1);
            uint8_t latch = g_stick_latch[i], next = 0;
            // Screen sense: the report's Y grows DOWNWARD, so "up" is negative.
            const bool dirs[4] = {
                stick_dir_active(y, false, th, latch & (1u << MACRO_STICK_UP)),
                stick_dir_active(x, true,  th, latch & (1u << MACRO_STICK_RIGHT)),
                stick_dir_active(y, true,  th, latch & (1u << MACRO_STICK_DOWN)),
                stick_dir_active(x, false, th, latch & (1u << MACRO_STICK_LEFT)),
            };
            for (uint8_t d = 0; d < 4; d++) {
                if (!dirs[d]) continue;
                next |= (uint8_t) (1u << d);
                if (e.out_btn == 0) hold_add_key(e.keys[d]);
            }
            g_stick_latch[i] = next;
            if (next && (e.flags & MACRO_FLAG_REPLACE)) {
                if (right) g_sup_rstick = true; else g_sup_lstick = true;
            }
            continue;
        }

        g_stick_latch[i] = 0;
        // Button/combo rows only. A swipe or motion gesture is an event that
        // completes; there is no "while active" for it to hold.
        if (!macro_is_hold(e) || e.gesture != GESTURE_NONE || e.chord == 0) continue;
        if ((mask & e.chord) != e.chord) { g_scroll_latch &= ~(1u << i); continue; }

        // Mouse outputs never reach g_inject: that mask is indexed by the
        // T2Button enum and main.cpp walks it up to T2BTN_COUNT, so a value of
        // 11+ would either be ignored or, worse, alias a controller button.
        if (macro_is_mouse_out(e.out_btn)) {
            if (e.out_btn <= MOUT_MIDDLE) {
                g_mouse_btns |= (uint8_t) (1u << (e.out_btn - MOUT_LEFT));
            } else if (!(g_scroll_latch & (1u << i))) {
                g_scroll_latch |= (1u << i);
                const int8_t tick = (e.out_btn == MOUT_SCROLL_UP) ? 1 : -1;
                if (g_mouse_scroll < 100 && g_mouse_scroll > -100) g_mouse_scroll += tick;
            }
            if (e.flags & MACRO_FLAG_REPLACE) g_suppress |= e.chord;
            continue;
        }

        // g_inject is indexed by the T2Button enum (config.h) - NOT by the
        // logical button masks from input_buttons.h that g_suppress uses. Two
        // conventions in one file, with the only note at the CONSUMER
        // (macro_apply_buttons in main.cpp). Verified end to end by
        // t_out_btn_maps_to_the_chosen_button in tools/macro-tests.
        if (e.out_btn != 0) {
            g_inject |= (1u << (e.out_btn - 1));
            // ANALOG PASSTHROUGH. Remapping one trigger onto the other is the
            // one remap where a binary press throws information away: L2 -> R2
            // as a full press makes a variable throttle into an on/off switch.
            // When BOTH sides are triggers, carry the source travel across so
            // the target is pulled exactly as far as the finger pulled it.
            // Anything else - a face button driving R2 - has no travel to copy
            // and stays a full press.
            if (e.out_btn == T2BTN_L2 || e.out_btn == T2BTN_R2) {
                uint8_t v = 255;
                if (e.chord == BTN_L2)      v = r[RPT_L2_AXIS];
                else if (e.chord == BTN_R2) v = r[RPT_R2_AXIS];
                if (v == 0) v = 1;   // held past the digital point, so never zero
                if (e.out_btn == T2BTN_L2) g_analog_l2 = v; else g_analog_r2 = v;
            }
        }
        else for (uint8_t k = 0; k < MACRO_KEYS; k++) hold_add_key(e.keys[k]);
        if (e.flags & MACRO_FLAG_REPLACE) g_suppress |= e.chord;
    }

    if (g_hold_n != g_hold_sent_n || memcmp(g_hold_keys, g_hold_sent, g_hold_n) != 0) {
        g_hold_pending = true;
    }
}

// Push the hold set to the host. Deferred out of the input path so it never
// runs inside the report critical section, and skipped while a burst is
// playing so the two cannot interleave on one interface.
static void hold_flush() {
    if (!g_hold_pending || g_pb_busy || g_suspended || wake_owns_keyboard()) return;
    const uint32_t all = (g_hold_n >= 32) ? 0xFFFFFFFFu : ((1u << g_hold_n) - 1u);
    if (!send_usages(g_hold_keys, g_hold_n, all)) return;
    memcpy(g_hold_sent, g_hold_keys, g_hold_n);
    g_hold_sent_n = g_hold_n;
    g_hold_pending = false;
}

static void fire(int idx) {
    if (idx < 0 || g_suspended || g_pb_busy) return;
    playback_start(g_table.rec[idx].entry);
}

static uint32_t hold_ms_of(int idx) {
    if (idx < 0) return (uint32_t) MACRO_HOLD_CS_DEFAULT * 10u;
    const uint8_t cs = g_table.rec[idx].entry.hold_cs;
    return (uint32_t) (cs ? cs : MACRO_HOLD_CS_DEFAULT) * 10u;
}

// ============================== debounce ===================================

static uint32_t debounce(uint32_t raw, uint32_t now) {
    uint32_t out = raw;
    for (uint8_t i = 0; i < 32; i++) {
        const uint32_t b = 1u << i;
        if (raw & b) { g_off_since[i] = 0; continue; }
        if (!(g_stable & b)) { g_off_since[i] = 0; continue; }
        if (g_off_since[i] == 0) g_off_since[i] = now ? now : 1u;
        if ((now - g_off_since[i]) < MACRO_DEBOUNCE_MS) out |= b;   // still holding
        else g_off_since[i] = 0;                                    // real release
    }
    g_stable = out;
    return out;
}

// ============================== gestures ===================================

static void gesture_fire(uint8_t gesture, uint32_t disable) {
    for (uint8_t i = 0; i < MACRO_COUNT; i++) {
        const MacroEntry &e = g_table.rec[i].entry;
        // Explicit, not incidental: a swipe byte can never carry GEST_MOTION
        // so equality could not collide today, but relying on that is exactly
        // how the v1.14.5 bow ended up unclassified in one of two lists.
        if (e.gesture == GESTURE_NONE || (e.gesture & GEST_MOTION)) continue;
        if (e.gesture != gesture) continue;
        if (!macro_is_enabled(disable, i)) continue;
        fire(i);
        return;
    }
}

// ============================== motion =====================================
//
// The JavaScript twin lives in ds5-config-portal.html (motQuantise/motMatch) and
// tools/portal-motion-test.js drives the same traces through it. Both are
// INTEGER ONLY and single-pass, so the two must agree stroke for stroke; if one
// is changed the other has to move with it.

static inline int16_t rpt_i16(const uint8_t *r, uint16_t off) {
    return (int16_t) ((uint16_t) r[off] | ((uint16_t) r[off + 1] << 8));
}

// Four directions, not eight. See macro.h for why eight failed on hardware.
static uint8_t motion_dir(int32_t dx, int32_t dy) {
    const int32_t adx = dx < 0 ? -dx : dx;
    const int32_t ady = dy < 0 ? -dy : dy;
    if (adx >= ady) return dx > 0 ? MOTION_RIGHT : MOTION_LEFT;
    return dy > 0 ? MOTION_UP : MOTION_DOWN;
}

static void motion_clear() {
    g_mot_active = false;
    g_mot_gate   = 0;
    g_mot_cx     = 0;
    g_mot_cy     = 0;
    g_mot_cur    = -1;
    g_mot_n      = 0;
    g_mot_moved  = false;
}

// One sample into the stroke segmenter. A stroke is EXTENDED by continued
// motion and only a COMMITTED change of direction starts a new one, so wobble
// inside a stroke costs nothing.
static void motion_feed(int32_t gx, int32_t gy) {
    const int32_t mag = (gx < 0 ? -gx : gx) + (gy < 0 ? -gy : gy);
    if (mag < MOT_NOISE) return;
    g_mot_moved = true;
    g_mot_cx += gx;
    g_mot_cy += gy;

    if (g_mot_cur < 0) {
        const int32_t a = (g_mot_cx < 0 ? -g_mot_cx : g_mot_cx) +
                          (g_mot_cy < 0 ? -g_mot_cy : g_mot_cy);
        if (a < g_mot_step) return;
        g_mot_cur = (int8_t) motion_dir(g_mot_cx, g_mot_cy);
        g_mot_cx  = 0;
        g_mot_cy  = 0;
        if (g_mot_n < MACRO_MOTION_MAX) g_mot_codes[g_mot_n++] = (uint8_t) g_mot_cur;
        return;
    }

    // g_mot_cur is axis-aligned, so parallel and perpendicular are just the two
    // axes. fwd is SIGNED along the stroke: a reversal shows up as a large
    // negative fwd with perp near zero, so testing only |parallel| swallows it.
    const bool    horiz = (g_mot_cur == MOTION_RIGHT || g_mot_cur == MOTION_LEFT);
    const int32_t fwd   = (g_mot_cur == MOTION_RIGHT) ? g_mot_cx :
                          (g_mot_cur == MOTION_LEFT)  ? -g_mot_cx :
                          (g_mot_cur == MOTION_UP)    ? g_mot_cy : -g_mot_cy;
    const int32_t perp  = horiz ? g_mot_cy : g_mot_cx;
    const int32_t aperp = perp < 0 ? -perp : perp;

    if (fwd >= g_mot_step) { g_mot_cx = 0; g_mot_cy = 0; return; } // continues

    int8_t next = -1;
    if (fwd <= -g_mot_step * MOT_REVERSE) {
        next = (int8_t) ((g_mot_cur + 2) & 3);                     // reversal
    } else if (aperp >= g_mot_step * MOT_TURN) {
        next = horiz ? (int8_t) (perp > 0 ? MOTION_UP : MOTION_DOWN)
                     : (int8_t) (perp > 0 ? MOTION_RIGHT : MOTION_LEFT);
    }
    if (next < 0) return;                                          // not committed

    g_mot_cur = next;
    g_mot_cx  = 0;
    g_mot_cy  = 0;
    if (g_mot_n < MACRO_MOTION_MAX) g_mot_codes[g_mot_n++] = (uint8_t) next;
}

// The template must match the START of the capture. The gate window ends at
// RELEASE, so holding the button a beat longer sweeps up settling motion; the
// gesture is at the front by construction.
//
// One spurious code may be dropped from inside the matched prefix, but ONLY for
// templates of two strokes or more. That slack exists for lateral drift during
// a reversal (down-up captured as down-right-up); applied to a SINGLE stroke it
// means "any capture whose first two codes include this one", which made four
// orthogonal single-stroke templates mutually ambiguous on hardware.
static bool motion_prefix_eq(const uint8_t *codes, const MacroEntry &e,
                             uint8_t skip, uint8_t k) {
    uint8_t j = 0;
    for (uint8_t i = 0; i < k; i++) {
        if (i == skip) continue;
        if (codes[i] != macro_motion_code(e, j)) return false;
        j++;
    }
    return j == e.motion_len;
}

static bool motion_match(const uint8_t *codes, uint8_t n, const MacroEntry &e) {
    if (e.motion_len == 0 || n == 0) return false;
    const uint8_t slack = e.motion_len >= 2 ? 1u : 0u;
    for (uint8_t k = e.motion_len; k <= (uint8_t) (e.motion_len + slack); k++) {
        if (n < k) continue;
        if (k == e.motion_len) {
            if (motion_prefix_eq(codes, e, MACRO_MOTION_MAX, k)) return true;
            continue;
        }
        for (uint8_t skip = 0; skip < k; skip++) {
            if (motion_prefix_eq(codes, e, skip, k)) return true;
        }
    }
    return false;
}

// Longest matching template wins: with prefix matching a short template would
// otherwise shadow every longer one that starts the same way.
// Takes the captured path BY ARGUMENT, not from g_mot_codes. The window is
// cleared before firing - a macro may re-enter this file - and reading the
// module buffer here matched against an already-emptied path, so nothing ever
// fired even though the quantiser was producing the right strokes.
static void motion_fire(const uint8_t *codes, uint8_t n,
                        uint32_t gate, uint32_t disable) {
    int best_idx = -1;
    uint8_t best_len = 0;
    for (uint8_t i = 0; i < MACRO_COUNT; i++) {
        const MacroEntry &e = g_table.rec[i].entry;
        if (!macro_is_motion(e)) continue;
        if (!macro_is_enabled(disable, i)) continue;
        if (e.chord == 0 || (gate & e.chord) != e.chord) continue;
        if (!motion_match(codes, n, e)) continue;
        if (e.motion_len > best_len) { best_len = e.motion_len; best_idx = i; }
    }
    if (best_idx >= 0) fire(best_idx);
}

// Pick the gate to arm: the first enabled motion entry whose gate is fully
// held. Its step is used for the whole window.
static uint32_t motion_gate_for(uint32_t mask, uint32_t disable, int32_t &step_out) {
    for (uint8_t i = 0; i < MACRO_COUNT; i++) {
        const MacroEntry &e = g_table.rec[i].entry;
        if (!macro_is_motion(e)) continue;
        if (!macro_is_enabled(disable, i)) continue;
        if (e.chord == 0 || (mask & e.chord) != e.chord) continue;
        step_out = e.motion_step ? (int32_t) e.motion_step
                                 : (int32_t) MACRO_MOTION_STEP_DEFAULT;
        return e.chord;
    }
    return 0;
}

static void motion_task(const uint8_t *r, uint16_t len, uint32_t mask, uint32_t disable) {
    if (len < RPT_GYRO_MIN_LEN) return;

    if (!g_mot_active) {
        int32_t step = MACRO_MOTION_STEP_DEFAULT;
        const uint32_t gate = motion_gate_for(mask, disable, step);
        if (gate == 0) return;
        motion_clear();
        g_mot_active = true;
        g_mot_gate   = gate;
        g_mot_step   = step;
        return;                       // arm on the gate edge, sample from next
    }

    if ((mask & g_mot_gate) == g_mot_gate) {
        // Sign convention matches apply_gyro_stick, where horiz is negated so
        // turning right aims right. Negating both here makes the codes read in
        // screen-natural terms: up is up.
        motion_feed(-(int32_t) rpt_i16(r, RPT_GYRO_YAW),
                    -(int32_t) rpt_i16(r, RPT_GYRO_PITCH));
        return;
    }

    const uint32_t gate = g_mot_gate;
    uint8_t codes[MACRO_MOTION_MAX];
    const uint8_t n = g_mot_n;
    memcpy(codes, g_mot_codes, sizeof(codes));
    const bool moved = g_mot_moved && n > 0;
    motion_clear();
    if (moved) motion_fire(codes, n, gate, disable);
}

static void touch_task(const uint8_t *r, uint16_t len, uint32_t now, uint32_t disable) {
    const TouchPoint f1 = touch_point(r, len, 0);
    const TouchPoint f2 = touch_point(r, len, 1);

    if (f1.down && !g_touch_down) {
        g_touch_down = true;
        g_touch_two  = f2.down;
        g_touch_x0   = f1.x;
        g_touch_y0   = f1.y;
        g_touch_lx   = f1.x;
        g_touch_ly   = f1.y;
        g_touch_t0   = now;
        return;
    }
    if (f1.down) {
        if (f2.down) g_touch_two = true;   // second finger may land late
        g_touch_lx = f1.x;                 // end point comes from here, not the lift
        g_touch_ly = f1.y;
        return;
    }
    if (!g_touch_down) return;

    g_touch_down = false;
    const uint32_t dt = now - g_touch_t0;
    if (dt < GEST_MIN_MS || dt > GEST_MAX_MS) return;

    // The lifted sample's coordinates are stale - the DualSense stops updating
    // X/Y once bit7 goes high, so the report that tells us the finger left
    // carries wherever it was some time earlier. Classify from the last sample
    // taken while the finger was still down, or every swipe reads as noise.
    const int32_t dx = (int32_t) g_touch_lx - (int32_t) g_touch_x0;
    const int32_t dy = (int32_t) g_touch_ly - (int32_t) g_touch_y0;
    const int32_t adx = dx < 0 ? -dx : dx;
    const int32_t ady = dy < 0 ? -dy : dy;

    uint8_t dir;
    if (adx >= GEST_MIN_DX && adx > ady)      dir = dx > 0 ? GEST_DIR_RIGHT : GEST_DIR_LEFT;
    else if (ady >= GEST_MIN_DY && ady > adx) dir = dy > 0 ? GEST_DIR_DOWN  : GEST_DIR_UP;
    else return;

    uint8_t g = (uint8_t) (GEST_VALID | dir);
    if (g_touch_x0 > (TOUCH_X_MAX / 2)) g |= GEST_ZONE_RIGHT;
    if (g_touch_two)                    g |= GEST_TWO_FINGER;
    gesture_fire(g, disable);
}

// ============================== public =====================================

void macro_on_input(const uint8_t *report, uint16_t len) {
    // A truncated report must never be decoded: touch_point() would report
    // "finger up" for a report that simply ended early, and touch_task() would
    // classify a gesture out of stale coordinates.
    if (report == nullptr || len < RPT_MIN_LEN) return;
    if (!g_loaded) macro_load();

    const uint32_t disable = get_config().macro_disable;
    if (!macro_any_enabled(disable)) { g_chord = 0; g_stable = 0; return; }

    // Every wake rule applies: nothing may transmit while the host is
    // suspended, and nothing may be left latched when it goes down.
    if (wake_host_is_suspended()) { macro_reset(); return; }

    const uint32_t now  = now_ms();
    const uint32_t mask = debounce(button_mask(report, len), now);

    motion_task(report, len, mask, disable);
    touch_task(report, len, now, disable);
    hold_task(report, len, mask, disable);

    if (g_chord != 0) {
        const int long_idx_r  = find_entry(g_chord, true,  disable);
        const int short_idx_r = find_entry(g_chord, false, disable);
        // A HOLD row is driven entirely by hold_task(); letting it ALSO fire a
        // burst on release would send the combo twice, once as a state and once
        // as a one-shot.
        const int long_idx  = (long_idx_r  >= 0 && macro_is_hold(g_table.rec[long_idx_r].entry))  ? -1 : long_idx_r;
        const int short_idx = (short_idx_r >= 0 && macro_is_hold(g_table.rec[short_idx_r].entry)) ? -1 : short_idx_r;

        if ((mask & g_chord) == g_chord) {
            if (!g_chord_fired && long_idx >= 0 &&
                (now - g_chord_start) >= hold_ms_of(long_idx)) {
                fire(long_idx);
                g_chord_fired = true;
            }
            return;
        }
        // Released. A short macro sharing the chord with a long one can only
        // fire here, because "short" is not knowable until the release.
        if (!g_chord_fired && short_idx >= 0) fire(short_idx);
        g_chord = 0;
        return;
    }

    const uint32_t chord = best_chord(mask, disable);
    if (chord == 0) return;

    g_chord       = chord;
    g_chord_start = now;
    g_chord_fired = false;

    // With no long macro on this chord there is nothing to wait for, so fire on
    // PRESS. Waiting for the release would add avoidable latency.
    if (find_entry(chord, true, disable) < 0) {
        const int short_idx = find_entry(chord, false, disable);
        if (short_idx >= 0) { fire(short_idx); g_chord_fired = true; }
    }
}

void macro_task() {
    hold_flush();
    if (wake_host_is_suspended()) return;
    // The wake FSM owns the shared keyboard instance from the moment it asks for
    // a resume until its F15 keyup has gone out. That sequence straddles the
    // resume, so host_suspended is already false for most of it - and wake is
    // triggered BY a button press, so the user is holding buttons exactly then.
    // Interleaving a macro there corrupts both sequences.
    if (wake_owns_keyboard()) return;
    playback_task();

    // A long press must still fire when the chord is held perfectly still and
    // the pad stops sending fresh reports.
    if (g_chord == 0 || g_chord_fired) return;
    const uint32_t disable = get_config().macro_disable;
    const int long_idx = find_entry(g_chord, true, disable);
    if (long_idx < 0) return;
    if ((now_ms() - g_chord_start) >= hold_ms_of(long_idx)) {
        fire(long_idx);
        g_chord_fired = true;
    }
}

void macro_reset() {
    motion_clear();
    // Everything held must be released, not merely forgotten: a suspend with a
    // key still down leaves it stuck at the host.
    g_hold_n = 0;
    g_suppress = g_inject = 0;
    g_sup_lstick = g_sup_rstick = false;
    memset(g_stick_latch, 0, sizeof(g_stick_latch));
    // Mouse output has to be dropped for the same reason as the keys: a click
    // still held at a suspend stays latched at the host, and a pending scroll
    // tick would fire on the far side of a reconnect with nothing pressed.
    g_mouse_btns   = 0;
    g_mouse_scroll = 0;
    g_scroll_latch = 0;
    if (g_hold_sent_n) { send_usages(g_hold_keys, 0, 0); g_hold_sent_n = 0; }
    g_hold_pending = false;
    g_chord      = 0;
    g_stable     = 0;
    g_touch_down = false;
    memset(g_off_since, 0, sizeof(g_off_since));
    if (g_pb_busy || g_pb_n) {
        // Blank report: never leave a modifier latched at the host. This is the
        // keyboard-side twin of state_release_for_suspend().
        if (tud_hid_n_ready(MACRO_KBD_INSTANCE)) {
            tud_hid_n_keyboard_report(MACRO_KBD_INSTANCE, 0, 0, nullptr);
        }
        g_pb_busy = false;
        g_pb_n    = 0;
        g_pb_step = 0;
    }
}

void macro_suspend(bool on) {
    g_suspended = on;
    if (on) macro_reset();
}

bool macro_motion_capturing() { return g_mot_active; }

bool macro_busy() { return g_pb_busy; }
