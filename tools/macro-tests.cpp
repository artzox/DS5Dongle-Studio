//
// macro-tests.cpp - host-side regression tests for src/macro.cpp.
//
// Compiles the REAL engine (see run-macro-tests.sh, which copies src/macro.cpp
// into stubs/ unmodified so its quoted includes resolve to the stubs) against
// ~60 lines of fakes for TinyUSB, flash and time, plus a 4 MB fake flash array
// so macro_load/macro_commit exercise the true storage path. The crc32 in
// stubs/utils.h is lifted verbatim from src/utils.h.
//
// This exists because the stuck-key bug below was invisible to code reading and
// obvious in one run.
//

#include <cstdio>
#include <cstring>
#include <cstdint>

#include "tusb.h"
#include "hardware/flash.h"
#include "macro.h"
#include "input_buttons.h"
#include "config.h"
#include "stubs/utils.h"   // the stub, not src/utils.h: -I$SRC precedes -Istubs
#include "flash_map.h"
#include "wake.h"

// ---------------------------------------------------------------- stub state
FakeKbdReport g_sent[64];
int  g_sent_n = 0;
bool g_ep_ready = true;
unsigned char g_fake_flash[PICO_FLASH_SIZE_BYTES];
uint32_t g_now_ms = 1000;

static Config_body g_cfg;
Config_body &get_config() { return g_cfg; }

static bool g_suspended_stub = false;
static bool g_wake_owns_stub = false;
bool wake_host_is_suspended(void) { return g_suspended_stub; }
bool wake_owns_keyboard(void)     { return g_wake_owns_stub; }

// ---------------------------------------------------------------- harness
static int g_fail = 0;
static void ok(bool cond, const char *what) {
    printf("  %s  %s\n", cond ? "ok  " : "FAIL", what);
    if (!cond) g_fail++;
}

static uint8_t rpt[64];
static void reset_report() {
    memset(rpt, 0, sizeof(rpt));
    rpt[RPT_BTN0] = 0x08;  // hat idle
    rpt[32] = 0x80;        // finger 1 up
    rpt[36] = 0x80;        // finger 2 up
}
static void btn_r3(bool on)  { if (on) rpt[RPT_BTN1] |= 0x80; else rpt[RPT_BTN1] &= ~0x80; }
static void btn_up(bool on)  { rpt[RPT_BTN0] = (uint8_t)((rpt[RPT_BTN0] & 0xF0) | (on ? 0x00 : 0x08)); }
static void touch(bool down, uint16_t x, uint16_t y) {
    rpt[32] = down ? 0x00 : 0x80;
    rpt[33] = (uint8_t)(x & 0xFF);
    rpt[34] = (uint8_t)(((x >> 8) & 0x0F) | ((y & 0x0F) << 4));
    rpt[35] = (uint8_t)(y >> 4);
}
static void step(uint32_t dt) { g_now_ms += dt; macro_on_input(rpt, sizeof(rpt)); macro_task(); }
static void settle()          { for (int i = 0; i < 12; i++) step(20); }

static bool last_report_blank() {
    if (g_sent_n == 0) return true;
    const FakeKbdReport &r = g_sent[g_sent_n - 1];
    if (r.mods) return false;
    for (int k = 0; k < 6; k++) if (r.keys[k]) return false;
    return true;
}
static bool saw_key(uint8_t mods, uint8_t key) {
    for (int i = 0; i < g_sent_n; i++) {
        if (g_sent[i].mods != mods) continue;
        for (int k = 0; k < 6; k++) if (g_sent[i].keys[k] == key) return true;
    }
    return false;
}

static void fresh_device() {
    memset(g_fake_flash, 0xFF, sizeof(g_fake_flash));
    memset(&g_cfg, 0, sizeof(g_cfg));
    g_cfg.macro_disable = MACRO_NONE_ENABLED;
    g_suspended_stub = false;
    g_wake_owns_stub = false;
    g_sent_n = 0;
    g_ep_ready = true;
    reset_report();
    macro_load();
    macro_reset();
    g_sent_n = 0;
}

static MacroRecord chord_macro(uint32_t chord, uint8_t k0, uint8_t k1,
                               uint8_t rel, bool longpress, uint8_t hold_cs = 0) {
    MacroRecord r{};
    r.entry.chord     = chord;
    r.entry.gesture   = GESTURE_NONE;
    r.entry.flags     = longpress ? MACRO_FLAG_LONG_PRESS : 0;
    r.entry.hold_cs   = hold_cs;
    r.entry.keys[0]   = k0;
    r.entry.keys[1]   = k1;
    r.entry.rel_order = rel;
    return r;
}
static void enable_only(uint32_t bits) { g_cfg.macro_disable = ~bits; }

constexpr uint8_t K_CTRL = 0xE0;
constexpr uint8_t K_ALT  = 0xE2;
constexpr uint8_t K_J    = 0x0D;
constexpr uint8_t K_TAB  = 0x2B;

// ---------------------------------------------------------------- tests

static void t_press_then_second_button() {
    printf("chord fires only when FULLY held, however long the first button waits\n");
    fresh_device();
    macro_set_entry(0, chord_macro(BTN_R3 | BTN_DPAD_UP, K_CTRL, K_J, 0, false));
    enable_only(1u);

    btn_r3(true);  step(10);
    step(200); step(200);
    ok(g_sent_n == 0, "R3 held 410ms alone fires nothing");

    btn_up(true);  step(10);
    ok(g_sent_n > 0, "fires the instant Up lands");
    settle();
    ok(saw_key(0x01, K_J), "Ctrl+J appears in the walk");
    ok(last_report_blank(), "walk ends on a blank report");
}

static void t_default_rel_order_no_stuck_key() {
    printf("rel_order = 0 (the default) must not strand a key\n");
    fresh_device();
    macro_set_entry(0, chord_macro(BTN_R3 | BTN_DPAD_UP, K_CTRL, K_J, 0, false));
    enable_only(1u);
    btn_r3(true); btn_up(true); step(10); settle();
    ok(last_report_blank(), "no key left down (the pre-fix bug ended on keys=0D)");
}

static void t_alt_tab_release_order() {
    printf("captured release order is honoured: Alt+Tab lifts Tab first\n");
    fresh_device();
    // slot0 = Alt released at position 1, slot1 = Tab released at position 0
    const uint8_t rel = (uint8_t)((1u << 0) | (0u << 2));
    macro_set_entry(0, chord_macro(BTN_R3, K_ALT, K_TAB, rel, false));
    enable_only(1u);
    btn_r3(true); step(10); settle();

    int i_alt_only_after_tab = -1;
    for (int i = 1; i < g_sent_n; i++) {
        if (g_sent[i].mods == 0x04 && g_sent[i].keys[0] == 0) { i_alt_only_after_tab = i; break; }
    }
    ok(saw_key(0x04, K_TAB), "Alt+Tab asserted");
    ok(i_alt_only_after_tab > 0, "Tab released while Alt still held");
    ok(last_report_blank(), "ends blank");
}

static void t_long_vs_short() {
    printf("short and long on one chord\n");
    fresh_device();
    macro_set_entry(0, chord_macro(BTN_R3, K_CTRL, K_J, 0, false));
    macro_set_entry(1, chord_macro(BTN_R3, K_ALT, K_TAB, 0, true, 75));
    enable_only(0x3u);

    btn_r3(true); step(10);
    ok(g_sent_n == 0, "short does NOT fire on press when a long shares the chord");
    step(400);
    ok(g_sent_n == 0, "still nothing at 410ms");
    step(400);
    ok(g_sent_n > 0, "long fires past 750ms");
    settle();   // the walk needs MACRO_STEP_MS between reports; TAB lands on step 1
    ok(saw_key(0x04, K_TAB), "and the combo reaches Alt+Tab");
    g_sent_n = 0;
    btn_r3(false); step(10); settle();
    ok(!saw_key(0x01, K_J), "short suppressed on release after long fired");

    // and the short path when the press is brief
    fresh_device();
    macro_set_entry(0, chord_macro(BTN_R3, K_CTRL, K_J, 0, false));
    macro_set_entry(1, chord_macro(BTN_R3, K_ALT, K_TAB, 0, true, 75));
    enable_only(0x3u);
    btn_r3(true); step(10); step(100);
    btn_r3(false); step(10); step(60); settle();
    ok(saw_key(0x01, K_J), "brief press fires the short macro on release");
}

static void t_only_short_fires_on_press() {
    printf("no long macro on the chord -> fire on press, not release\n");
    fresh_device();
    macro_set_entry(0, chord_macro(BTN_R3, K_CTRL, K_J, 0, false));
    enable_only(1u);
    btn_r3(true); step(10);
    ok(g_sent_n > 0, "fires immediately on press");
}

static void t_disabled_mask() {
    printf("the per-slot mask actually gates firing\n");
    fresh_device();
    macro_set_entry(0, chord_macro(BTN_R3, K_CTRL, K_J, 0, false));
    g_cfg.macro_disable = MACRO_NONE_ENABLED;   // all off
    btn_r3(true); step(10); settle();
    ok(g_sent_n == 0, "nothing fires with all macros disabled");
    ok(!macro_any_enabled(g_cfg.macro_disable), "macro_any_enabled false -> no kbd interface");
}

static void t_suspend_and_wake_arbitration() {
    printf("wake rules\n");
    fresh_device();
    macro_set_entry(0, chord_macro(BTN_R3, K_CTRL, K_J, 0, false));
    enable_only(1u);

    g_suspended_stub = true;
    btn_r3(true); step(10); settle();
    ok(g_sent_n == 0, "nothing transmits while the host is suspended");

    g_suspended_stub = false; btn_r3(false); step(10); settle(); g_sent_n = 0;
    g_wake_owns_stub = true;
    btn_r3(true); step(10);
    const int during = g_sent_n;
    settle();
    ok(during == g_sent_n, "playback stalls while the wake FSM owns the keyboard");
    g_wake_owns_stub = false;
    settle();
    ok(g_sent_n > during, "resumes once wake releases it");
    ok(last_report_blank(), "and still ends blank");
}

static void t_truncated_report() {
    printf("a truncated report is not decoded\n");
    fresh_device();
    macro_set_entry(0, chord_macro(BTN_R3, K_CTRL, K_J, 0, false));
    enable_only(1u);
    btn_r3(true);
    macro_on_input(rpt, 8);      // shorter than RPT_MIN_LEN
    macro_task();
    ok(g_sent_n == 0, "short report ignored rather than half-decoded");
}

static void t_gesture() {
    printf("touchpad swipe\n");
    fresh_device();
    MacroRecord g{};
    g.entry.gesture   = (uint8_t)(GEST_VALID | GEST_DIR_RIGHT);
    g.entry.keys[0]   = K_CTRL;
    g.entry.keys[1]   = K_J;
    g.entry.rel_order = 0;
    macro_set_entry(0, g);
    enable_only(1u);

    touch(true, 200, 500);  step(10);
    touch(true, 700, 520);  step(60);
    touch(false, 0, 0);     step(10);       // lift: coords go stale, engine must use last-down
    settle();
    ok(saw_key(0x01, K_J), "left-to-right swipe fires its macro");
    ok(last_report_blank(), "gesture playback ends blank");
}

static void t_persistence() {
    printf("flash round trip\n");
    fresh_device();
    macro_set_entry(0, chord_macro(BTN_R3 | BTN_DPAD_UP, K_CTRL, K_J, 0, false));
    ok(macro_commit(), "commit succeeds and verifies");
    macro_load();
    MacroRecord back{};
    ok(macro_get(0, back), "entry reads back");
    ok(back.entry.chord == (BTN_R3 | BTN_DPAD_UP) && back.entry.keys[1] == K_J,
       "contents survive the round trip");

    // corrupt one record, CRC must reject the whole table
    g_fake_flash[MACRO_FLASH_OFFSET + sizeof(uint32_t) * 2 + 4] ^= 0xFF;
    macro_load();
    macro_get(0, back);
    ok(back.entry.chord == 0, "a corrupted table falls back to empty, not garbage");
}

static void t_virgin_flash() {
    printf("upgrade from a device that has never had macros\n");
    memset(g_fake_flash, 0xFF, sizeof(g_fake_flash));
    memset(&g_cfg, 0, sizeof(g_cfg));
    g_cfg.macro_disable = MACRO_NONE_ENABLED;
    macro_load();
    MacroRecord r{};
    ok(macro_get(0, r) && r.entry.chord == 0 && r.entry.keys[0] == 0,
       "virgin sector yields an empty table, no migration needed");
}

static void t_known_subset_behaviour() {
    printf("KNOWN BEHAVIOUR: a bound subset shadows the longer chord\n");
    fresh_device();
    macro_set_entry(0, chord_macro(BTN_R3, K_CTRL, K_J, 0, false));               // subset
    macro_set_entry(1, chord_macro(BTN_R3 | BTN_DPAD_UP, K_ALT, K_TAB, 0, false));
    enable_only(0x3u);
    btn_r3(true); step(10); settle();
    btn_up(true); step(10); settle();
    ok(saw_key(0x01, K_J),   "R3 fired (pressed first)");
    ok(!saw_key(0x04, K_TAB),
       "R3+Up did NOT fire - documented, portal warns instead of an arm delay");
}

// Upgrading from the 1.19.x on-disk layout must preserve BOTH halves of a record.
// MacroEntry grew 12 -> 17 for motion gestures, which moved `label` inside
// MacroRecord, so a flat rec_len-byte copy shifts the name over the appended
// fields. Caught exactly this way: "rivatuner" reloaded as "uner", motion_len 118.
// Every historical record length needs its own split, and omitting one is not a
// no-op: the unknown-length branch DISCARDS the table. rec_len 33 (1.20.0 to
// 1.24.2) was missing when the 35-byte record landed, so upgrading wiped every
// macro on every device in the field.
static void t_migrate_v2_layout() {
    printf("migrating a 1.20-1.24.2 table (rec_len 33) keeps entry, label and motion\n");
    #pragma pack(push,1)
    struct E33 { uint32_t chord; uint8_t gesture, flags, hold_cs, keys[4], rel_order;
                 uint8_t motion[2]; uint8_t motion_len; uint16_t motion_step; };
    struct R33 { E33 e; uint8_t label[16]; };
    #pragma pack(pop)
    static_assert(sizeof(R33) == 33, "the 1.20.0-1.24.2 record was 33 bytes");

    memset(g_fake_flash, 0xFF, sizeof(g_fake_flash));
    memset(&g_cfg, 0, sizeof(g_cfg));
    g_cfg.macro_disable = MACRO_NONE_ENABLED;
    uint8_t *p = g_fake_flash + MACRO_FLASH_OFFSET;
    memset(p, 0, 8 + (size_t) MACRO_COUNT * 33 + 4);
    const uint32_t magic = MACRO_MAGIC; memcpy(p, &magic, 4);
    p[4] = 1; p[5] = MACRO_COUNT;
    const uint16_t rl = 33; memcpy(p + 6, &rl, 2);

    R33 recs[MACRO_COUNT]; memset(recs, 0, sizeof(recs));
    recs[0].e.chord = BTN_R3 | BTN_DPAD_UP;
    recs[0].e.keys[0] = 0xE0; recs[0].e.keys[1] = 0x0D; recs[0].e.rel_order = 1;
    memcpy(recs[0].label, "rivatuner", 9);
    recs[1].e.gesture = (uint8_t)(GEST_VALID | GEST_MOTION);
    recs[1].e.motion_len = 2; recs[1].e.motion[0] = 0x0D; recs[1].e.motion_step = 2100;
    memcpy(recs[1].label, "down-up", 7);
    memcpy(p + 8, recs, sizeof(recs));
    const uint32_t c = crc32(p + 8, (size_t) MACRO_COUNT * 33);
    memcpy(p + 8 + (size_t) MACRO_COUNT * 33, &c, 4);

    macro_load();
    MacroRecord a{}, b{};
    ok(macro_get(0, a), "a rec_len 33 table still loads");
    ok(a.entry.chord == (uint32_t)(BTN_R3 | BTN_DPAD_UP), "chord survives");
    ok(strncmp((const char *) a.label, "rivatuner", 9) == 0, "label survives intact");
    ok(a.entry.out_btn == 0 && a.entry.stick_thresh == 0,
       "the appended fields default to keyboard output and default threshold");
    macro_get(1, b);
    ok(b.entry.motion_len == 2 && b.entry.motion_step == 2100,
       "a motion gesture keeps its strokes and its calibrated step");
    ok(strncmp((const char *) b.label, "down-up", 7) == 0, "and its label");
}

static void t_migrate_v1_layout() {
    printf("migrating a 1.19.x table preserves entry AND label\n");
    #pragma pack(push,1)
    struct OldEntry { uint32_t chord; uint8_t gesture, flags, hold_cs, keys[4], rel_order; };
    struct OldRecord { OldEntry e; uint8_t label[16]; };
    #pragma pack(pop)
    static_assert(sizeof(OldRecord) == 28, "the 1.19.x record was 28 bytes");

    memset(g_fake_flash, 0xFF, sizeof(g_fake_flash));
    memset(&g_cfg, 0, sizeof(g_cfg));
    g_cfg.macro_disable = MACRO_NONE_ENABLED;

    uint8_t *p = g_fake_flash + MACRO_FLASH_OFFSET;
    memset(p, 0, 8 + (size_t) MACRO_COUNT * 28 + 4);
    const uint32_t magic = MACRO_MAGIC; memcpy(p, &magic, 4);
    p[4] = 1; p[5] = MACRO_COUNT;
    const uint16_t rl = 28; memcpy(p + 6, &rl, 2);

    OldRecord recs[MACRO_COUNT]; memset(recs, 0, sizeof(recs));
    recs[0].e.chord = BTN_R3 | BTN_DPAD_UP;
    recs[0].e.keys[0] = 0xE0; recs[0].e.keys[1] = 0x0D; recs[0].e.rel_order = 1;
    memcpy(recs[0].label, "rivatuner", 9);
    memcpy(p + 8, recs, sizeof(recs));
    const uint32_t c = crc32(p + 8, (size_t) MACRO_COUNT * 28);
    memcpy(p + 8 + (size_t) MACRO_COUNT * 28, &c, 4);

    macro_load();
    MacroRecord out{};
    ok(macro_get(0, out), "old table loads");
    ok(out.entry.chord == (uint32_t)(BTN_R3 | BTN_DPAD_UP), "chord survives");
    ok(out.entry.keys[0] == 0xE0 && out.entry.keys[1] == 0x0D, "keys survive");
    ok(strncmp((const char *) out.label, "rivatuner", 9) == 0, "label survives intact");
    ok(out.entry.motion_len == 0, "appended motion_len defaults to 0");
    ok(out.entry.motion[0] == 0 && out.entry.motion[1] == 0, "appended motion bytes default to 0");
}


// ------------------------------------------------- hold / remap / stick
constexpr uint8_t K_SPACE = 0x2C, K_W = 0x1A, K_A = 0x04, K_S = 0x16, K_D = 0x07;

static void btn_cross(bool on) { if (on) rpt[RPT_BTN0] |= 0x20; else rpt[RPT_BTN0] &= ~0x20; }
static void btn_l2(bool on)    { if (on) rpt[RPT_BTN1] |= 0x04; else rpt[RPT_BTN1] &= ~0x04; }
static void lstick(int x, int y) { rpt[0] = (uint8_t)(128 + x); rpt[1] = (uint8_t)(128 + y); }

static bool key_down_now(uint8_t key) {
    if (g_sent_n == 0) return false;
    const FakeKbdReport &r = g_sent[g_sent_n - 1];
    for (int k = 0; k < 6; k++) if (r.keys[k] == key) return true;
    return false;
}
static MacroRecord hold_macro(uint32_t chord, uint8_t key, bool replace) {
    MacroRecord r{};
    r.entry.chord = chord;
    r.entry.gesture = GESTURE_NONE;
    r.entry.flags = MACRO_FLAG_HOLD | (replace ? MACRO_FLAG_REPLACE : 0);
    r.entry.keys[0] = key;
    return r;
}
static MacroRecord stick_macro(bool right, uint8_t thresh, bool replace) {
    MacroRecord r{};
    r.entry.gesture = (uint8_t)(GEST_VALID | GEST_STICK | (right ? GEST_STICK_RIGHT : 0));
    r.entry.flags = MACRO_FLAG_HOLD | (replace ? MACRO_FLAG_REPLACE : 0);
    r.entry.stick_thresh = thresh;
    r.entry.keys[MACRO_STICK_UP] = K_W;
    r.entry.keys[MACRO_STICK_RIGHT] = K_D;
    r.entry.keys[MACRO_STICK_DOWN] = K_S;
    r.entry.keys[MACRO_STICK_LEFT] = K_A;
    return r;
}

static void t_hold_remap_key() {
    printf("hold: a remap holds its key for as long as the button is held\n");
    fresh_device();
    macro_set_entry(0, hold_macro(BTN_CROSS, K_SPACE, true));
    enable_only(1u << 0);
    btn_cross(true); settle();
    ok(key_down_now(K_SPACE), "Cross down holds Space");
    for (int i = 0; i < 5; i++) step(50);
    ok(key_down_now(K_SPACE), "Space is STILL held 250ms later, not a one-shot");
    ok(macro_suppress_mask() & BTN_CROSS, "REPLACE hides Cross from the game");
    btn_cross(false); settle();
    ok(!key_down_now(K_SPACE), "releasing Cross releases Space");
    ok(macro_suppress_mask() == 0, "and stops suppressing it");
}

static void t_hold_does_not_double_fire() {
    printf("hold: a held row must not ALSO fire as a burst on release\n");
    fresh_device();
    macro_set_entry(0, hold_macro(BTN_CROSS, K_SPACE, false));
    enable_only(1u << 0);
    btn_cross(true); settle();
    g_sent_n = 0;
    btn_cross(false); settle();
    // After release the only traffic should be the release itself: no second
    // press of Space from the burst path.
    ok(!saw_key(0, K_SPACE), "no burst press after the hold ends");
}

static void t_remap_to_controller_button() {
    printf("hold: remap to another CONTROLLER button, no keyboard involved\n");
    fresh_device();
    MacroRecord r = hold_macro(BTN_CROSS, 0, true);
    r.entry.out_btn = 2;                       // T2BTN_CIRCLE
    macro_set_entry(0, r);
    enable_only(1u << 0);
    btn_cross(true); settle();
    ok(macro_inject_mask() == (1u << (T2BTN_CIRCLE - 1)), "Cross injects Circle");
    ok(macro_suppress_mask() & BTN_CROSS, "and Cross itself is hidden");
    ok(g_sent_n == 0 || last_report_blank(), "nothing was sent to the keyboard");
    btn_cross(false); settle();
    ok(macro_inject_mask() == 0, "release clears the injection");
}

// The inject mask is indexed by the T2Button enum - the same numbering the
// two-stage trigger uses - and NOT by the logical button masks that the suppress
// mask uses. Both conventions live in this file. Assert the end-to-end identity
// so the difference is pinned down by a test rather than by a comment in a
// different file.
static void t_out_btn_maps_to_the_chosen_button() {
    printf("every controller-button output injects its own T2Button bit\n");
    const uint8_t all[] = { T2BTN_CROSS, T2BTN_CIRCLE, T2BTN_SQUARE, T2BTN_TRIANGLE,
                            T2BTN_L1, T2BTN_R1, T2BTN_L3, T2BTN_R3, T2BTN_L2, T2BTN_R2 };
    const char *names[] = { "Cross","Circle","Square","Triangle","L1","R1","L3","R3","L2","R2" };
    for (unsigned k = 0; k < sizeof(all); k++) {
        fresh_device();
        MacroRecord r = hold_macro(BTN_R3, 0, true);
        r.entry.out_btn = all[k];
        macro_set_entry(0, r);
        enable_only(1u << 0);
        btn_r3(true); settle();
        ok(macro_inject_mask() == (1u << (all[k] - 1)), names[k]);
        btn_r3(false); settle();
    }
}

// Remapping a trigger onto the other trigger must stay ANALOG. A binary press
// turns a variable throttle into an on/off switch, which is the one remap where
// the difference is the whole point.
static void t_trigger_to_trigger_is_analog() {
    printf("trigger -> trigger remap carries the analog travel\n");
    fresh_device();
    MacroRecord r = hold_macro(BTN_L2, 0, true);
    r.entry.out_btn = T2BTN_R2;
    macro_set_entry(0, r);
    enable_only(1u << 0);

    rpt[RPT_L2_AXIS] = 90;                 // a partial pull
    btn_l2(true); settle();
    ok(macro_inject_mask() == (1u << (T2BTN_R2 - 1)), "R2 is the injected output");
    ok(macro_analog_out(true) == 90, "and R2 carries the source travel, not 255");

    rpt[RPT_L2_AXIS] = 200;                // pull further
    settle();
    ok(macro_analog_out(true) == 200, "the value tracks the trigger as it moves");

    btn_l2(false); rpt[RPT_L2_AXIS] = 0; settle();
    ok(macro_analog_out(true) == 0, "release clears it");

    // A BUTTON driving a trigger has no travel to copy: full press.
    fresh_device();
    MacroRecord b = hold_macro(BTN_CROSS, 0, true);
    b.entry.out_btn = T2BTN_R2;
    macro_set_entry(0, b);
    enable_only(1u << 0);
    btn_cross(true); settle();
    ok(macro_analog_out(true) == 255, "a face button driving R2 is a full press");
    btn_cross(false); settle();
}

// Mouse outputs. Clicks are a STATE held while the input is held; scroll is an
// EVENT, one tick per press - holding the button must not spin the wheel at
// report rate.
static void t_mouse_outputs() {
    printf("mouse outputs: clicks hold, scroll ticks once per press\n");
    fresh_device();
    MacroRecord r = hold_macro(BTN_CROSS, 0, true);
    r.entry.out_btn = MOUT_LEFT;
    macro_set_entry(0, r);
    enable_only(1u << 0);

    ok(macro_mouse_buttons() == 0, "nothing held at rest");
    btn_cross(true); settle();
    ok(macro_mouse_buttons() == 0x01, "Cross holds the left mouse button");
    ok(macro_inject_mask() == 0, "and no controller button is injected");
    btn_cross(false); settle();
    ok(macro_mouse_buttons() == 0, "release lets it go");

    // Scroll: one tick however long the button is held.
    fresh_device();
    MacroRecord sc = hold_macro(BTN_CROSS, 0, true);
    sc.entry.out_btn = MOUT_SCROLL_UP;
    macro_set_entry(0, sc);
    enable_only(1u << 0);
    btn_cross(true); settle(); settle();
    ok(macro_mouse_peek_scroll() == 1, "a held scroll row ticks exactly once");
    ok(macro_mouse_take_scroll() == 1, "taking it returns the tick");
    ok(macro_mouse_peek_scroll() == 0, "and consumes it");
    settle();
    ok(macro_mouse_peek_scroll() == 0, "still held: no further ticks");
    btn_cross(false); settle();
    btn_cross(true); settle();
    ok(macro_mouse_peek_scroll() == 1, "releasing and pressing again ticks once more");

    // Scroll down is the other direction.
    fresh_device();
    MacroRecord dn = hold_macro(BTN_CROSS, 0, true);
    dn.entry.out_btn = MOUT_SCROLL_DN;
    macro_set_entry(0, dn);
    enable_only(1u << 0);
    btn_cross(true); settle();
    ok(macro_mouse_take_scroll() == -1, "scroll down is negative");

    // The interface must only appear when a mouse row is ENABLED.
    fresh_device();
    MacroRecord mr = hold_macro(BTN_CROSS, 0, true);
    mr.entry.out_btn = MOUT_RIGHT;
    macro_set_entry(0, mr);
    enable_only(0);
    ok(!macro_any_mouse_output(g_cfg.macro_disable), "a disabled mouse row needs no interface");
    enable_only(1u << 0);
    ok(macro_any_mouse_output(g_cfg.macro_disable), "an enabled one does");
}

static void t_stick_to_keys() {
    printf("stick: one row drives all four directions\n");
    fresh_device();
    macro_set_entry(0, stick_macro(false, 0, true));
    enable_only(1u << 0);
    lstick(0, 0); settle();
    ok(!key_down_now(K_W) && !key_down_now(K_A), "centred stick holds nothing");

    lstick(0, -100); settle();
    ok(key_down_now(K_W), "up holds W");
    ok(!key_down_now(K_S), "and not S");
    ok(macro_suppress_stick(false), "REPLACE zeroes the left stick for the game");
    ok(!macro_suppress_stick(true), "the right stick is untouched");

    // Diagonals must press BOTH, which is the whole reason for per-axis
    // thresholds instead of direction quantisation.
    lstick(-100, -100); settle();
    ok(key_down_now(K_W) && key_down_now(K_A), "up-left holds W and A together");

    lstick(100, 100); settle();
    ok(key_down_now(K_S) && key_down_now(K_D), "down-right holds S and D");
    ok(!key_down_now(K_W) && !key_down_now(K_A), "and drops the opposite pair");

    lstick(0, 0); settle();
    ok(!key_down_now(K_W) && !key_down_now(K_S) &&
       !key_down_now(K_A) && !key_down_now(K_D), "recentring releases everything");
}

// The resting-jitter case. Without STICK_ALWAYS the stick is only centred past
// the threshold, so at rest the pad's own noise reaches the game as a stream of
// changing axis values - which is what flips a game's button prompts back to
// controller glyphs while the macro's keys say keyboard.
static void t_stick_always_centres() {
    printf("stick: centre-always keeps the stick zeroed at rest\n");
    fresh_device();
    MacroRecord r = stick_macro(false, 0, true);
    r.entry.flags |= MACRO_FLAG_STICK_ALWAYS;
    macro_set_entry(0, r);
    enable_only(1u << 0);

    // At rest, and with a few counts of jitter - both must still be suppressed.
    lstick(0, 0); settle();
    ok(macro_suppress_stick(false), "centred stick is suppressed at rest");
    lstick(3, -2); settle();
    ok(macro_suppress_stick(false), "sub-threshold jitter is suppressed too");
    ok(!key_down_now(K_W) && !key_down_now(K_A),
       "...without firing any key below the threshold");
    ok(!macro_suppress_stick(true), "the other stick is left alone");

    // Past the threshold it behaves exactly as before.
    lstick(0, -100); settle();
    ok(key_down_now(K_W), "a real deflection still fires its key");
    ok(macro_suppress_stick(false), "and is still suppressed");

    // The flag must not leak to rows that did not ask for it.
    fresh_device();
    macro_set_entry(0, stick_macro(false, 0, true));
    enable_only(1u << 0);
    lstick(0, 0); settle();
    ok(!macro_suppress_stick(false),
       "without the flag a resting stick is NOT suppressed (old behaviour kept)");

    // REPLACE is still required: the flag alone must not zero a stick, or a
    // plain stick-to-keys row would silently lose its analog output.
    fresh_device();
    MacroRecord r2 = stick_macro(false, 0, false);
    r2.entry.flags |= MACRO_FLAG_STICK_ALWAYS;
    macro_set_entry(0, r2);
    enable_only(1u << 0);
    lstick(0, 0); settle();
    ok(!macro_suppress_stick(false), "the flag alone, without REPLACE, suppresses nothing");
    lstick(0, -100); settle();
    ok(!macro_suppress_stick(false), "...even when deflected");
    ok(key_down_now(K_W), "...and the key still fires");

    // A row disabled on this profile must never kill a stick.
    fresh_device();
    MacroRecord r3 = stick_macro(false, 0, true);
    r3.entry.flags |= MACRO_FLAG_STICK_ALWAYS;
    macro_set_entry(0, r3);
    enable_only(1u << 0);
    g_cfg.macro_disable = MACRO_NONE_ENABLED;   // row 0 off on this profile
    lstick(0, 0); settle();
    ok(!macro_suppress_stick(false), "a row disabled on this profile does not centre the stick");
    enable_only(1u << 0);

    // The rewrite gate must see stick-only work, or main.cpp skips the whole
    // rewrite and the suppression never reaches the outgoing report (v1.32.8).
    fresh_device();
    MacroRecord r4 = stick_macro(false, 0, true);
    r4.entry.flags |= MACRO_FLAG_STICK_ALWAYS;
    macro_set_entry(0, r4);
    enable_only(1u << 0);
    lstick(0, 0); settle();
    ok(macro_report_active(),
       "a resting suppressed stick still marks the report as needing a rewrite");
}

static void t_stick_hysteresis() {
    printf("stick: resting on the threshold must not chatter\n");
    fresh_device();
    macro_set_entry(0, stick_macro(false, 48, false));
    enable_only(1u << 0);
    lstick(0, 0); settle();
    g_sent_n = 0;
    // Dither either side of the 48 threshold. Hysteresis releases at 38, so a
    // few counts of wobble must not produce a stream of key events.
    const int dither[] = { -49, -47, -50, -46, -48, -45, -51, -47 };
    for (unsigned i = 0; i < sizeof(dither)/sizeof(dither[0]); i++) { lstick(0, dither[i]); step(20); }
    ok(g_sent_n <= 1, "dithering on the threshold sends at most one report");
    ok(key_down_now(K_W), "and W stays held throughout");
    lstick(0, -20); settle();
    ok(!key_down_now(K_W), "well below the release point it lets go");
}

static void t_hold_released_on_suspend() {
    printf("hold: a suspend must RELEASE held keys, not just forget them\n");
    fresh_device();
    macro_set_entry(0, hold_macro(BTN_CROSS, K_SPACE, false));
    enable_only(1u << 0);
    btn_cross(true); settle();
    ok(key_down_now(K_SPACE), "Space held before suspend");
    macro_reset();
    ok(last_report_blank(), "macro_reset sends a blank report, so nothing sticks");
    ok(macro_suppress_mask() == 0, "and clears the suppression mask");
}

static void t_disabled_hold_row_is_inert() {
    printf("hold: the enable mask still governs held rows\n");
    fresh_device();
    macro_set_entry(0, hold_macro(BTN_CROSS, K_SPACE, true));
    g_cfg.macro_disable = MACRO_NONE_ENABLED;
    btn_cross(true); settle();
    ok(!key_down_now(K_SPACE), "a disabled hold row holds nothing");
    ok(macro_suppress_mask() == 0, "and suppresses nothing");
}

int main() {
    printf("=== macro engine tests ===\n");
    t_press_then_second_button();
    t_default_rel_order_no_stuck_key();
    t_alt_tab_release_order();
    t_long_vs_short();
    t_only_short_fires_on_press();
    t_disabled_mask();
    t_suspend_and_wake_arbitration();
    t_truncated_report();
    t_hold_remap_key();
    t_hold_does_not_double_fire();
    t_remap_to_controller_button();
    t_out_btn_maps_to_the_chosen_button();
    t_trigger_to_trigger_is_analog();
    t_mouse_outputs();
    t_stick_to_keys();
    t_stick_always_centres();
    t_stick_hysteresis();
    t_hold_released_on_suspend();
    t_disabled_hold_row_is_inert();
    t_gesture();
    t_persistence();
    t_virgin_flash();
    t_migrate_v1_layout();
    t_migrate_v2_layout();
    t_known_subset_behaviour();
    printf("\n%s (%d failure%s)\n", g_fail ? "MACRO TESTS FAILED" : "MACRO TESTS OK",
           g_fail, g_fail == 1 ? "" : "s");
    return g_fail ? 1 : 0;
}
