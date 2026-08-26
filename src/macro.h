//
// macro.h - controller chord / touchpad gesture -> keyboard combo macros.
//
// Generalises ps_shortcut.cpp, which was a single hardcoded macro (PS short ->
// Win+G, PS long -> Win+Tab) and already proved the whole shape: edge detection
// on the BT input path, short-vs-long discrimination, and a deferred key
// release on HID instance 1.
//
// STORAGE SPLIT, and it is the important design decision:
//   - The macro TABLE is DEVICE-GLOBAL, in its own flash sector. Definitions
//     are not duplicated per slot and survive a browser reset or a different PC.
//   - Config_body carries only macro_enable, a 32-bit mask. Slots therefore
//     select which subset of the shared table is live, which is what makes
//     per-game macro sets work through the existing Playnite slot automation.
// The definitions CANNOT live only in the portal: the dongle runs standalone
// with nothing attached, so it must hold what each enabled bit means.
//

#ifndef DS5_BRIDGE_MACRO_H
#define DS5_BRIDGE_MACRO_H

#include <cstdint>

#include "config.h"

constexpr uint8_t  MACRO_COUNT     = 32; // matches the 32 bits of macro_disable

// All-disabled sentinel for Config_body::macro_disable. The mask is stored
// INVERTED so an old slot's 0xFF fill defaults to "no macros"; see config.h for
// why a range clamp cannot do that job for a bitmap.
constexpr uint32_t MACRO_NONE_ENABLED = 0xFFFFFFFFu;
static inline bool macro_is_enabled(uint32_t disable_mask, uint8_t idx) {
    return idx < MACRO_COUNT && (disable_mask & (1u << idx)) == 0u;
}
static inline bool macro_any_enabled(uint32_t disable_mask) {
    return disable_mask != MACRO_NONE_ENABLED;
}

// Is the wake keyboard INTERFACE present in the configuration descriptor?
//
// SINGLE SOURCE OF TRUTH, and it has to be: usb_descriptors.cpp decides the
// interface count from this, and slot_activate/ENUM_FIELDS decide whether a
// change needs a USB re-enumeration. Testing enable_wake, ps_shortcut_enabled
// and the macro mask INDEPENDENTLY - which is what those sites used to do - is
// wrong in both directions. With wake already on, enabling a macro changes
// nothing about the descriptor, yet it forced a reconnect; and that fires on
// every Playnite slot switch between two wake-on profiles with different macro
// sets.
//
// NOTE enable_wake still needs its own re-enumeration test elsewhere: beyond
// this interface it also sets bcdUSB 2.1, the BOS descriptor and the
// REMOTE_WAKEUP attribute bit.
// Is the gyro-to-mouse HID interface present?
// Does any macro ENABLED by this mask carry a mouse output? Reads the COMMITTED
// table, not whatever the portal is editing, so the interface appears when a
// mouse macro is actually saved to the device rather than while one is being
// typed - editing must not re-enumerate the controller under the user.
bool macro_any_mouse_output(uint32_t disable_mask);

static inline bool usb_mouse_iface_needed(const Config_body &c) {
    return c.gyro_output >= 1              // 1 = mouse, 2 = mouse + flick stick
        || c.stick_mouse >= 1              // 1 = right stick, 2 = left stick
        || macro_any_mouse_output(c.macro_disable);
}

// Is the keyboard interface present?
//
// Note the last clause: enabling the MOUSE also brings the keyboard up, even if
// nothing wants to type. The configuration descriptor is a fixed array that is
// trimmed by shortening wTotalLength, so an interface can only be dropped from
// the END. The order has to be [base][keyboard][mouse] - the mouse must come
// after the keyboard, because wake.cpp, macro.cpp and ps_shortcut.cpp all
// address the keyboard as a literal instance 1 and anything that let the mouse
// take that index would point them at the wrong device (the v1.18.9 failure).
// With that order fixed, "mouse but no keyboard" would mean cutting a block out
// of the MIDDLE, so instead the mouse implies the keyboard. The cost is one idle
// interface; the benefit is that the mouse is always instance 2 and the trimming
// stays a simple suffix.
static inline bool usb_kbd_iface_needed(const Config_body &c) {
    return c.enable_wake || c.ps_shortcut_enabled || macro_any_enabled(c.macro_disable)
           || usb_mouse_iface_needed(c);
}

// The mouse is the last HID interface and the keyboard is always present with
// it, so this is a constant - but it is still expressed as a function so there
// is one place to change if the ordering ever moves.
static inline uint8_t usb_mouse_instance(const Config_body &c) {
    (void) c;
    return 2u;
}
constexpr uint8_t  MACRO_KEYS      = 4;  // keys per combo, excluding nothing - modifiers count
constexpr uint8_t  MACRO_LABEL_LEN = 16; // portal display name, stored on device

// --- Output encoding ---------------------------------------------------------
// Keys are HID USAGE CODES throughout, including modifiers (0xE0 LeftCtrl ...
// 0xE7 RightGUI). One uniform namespace means no separate modifier field and no
// ambiguity about ordering between a modifier and a key: macro_play() folds any
// 0xE0-0xE7 usage into the boot keyboard's modifier byte as it walks the list.
//
// keys[] is in PRESS order. rel_order is the RELEASE order as a permutation,
// 2 bits per slot (slot i released at position ((rel_order >> (2*i)) & 3)).
// That is what distinguishes Alt+Tab (release Tab, then Alt) from a combo where
// the modifier lifts first.
//
// Reverse-press order is the overwhelmingly common case; the recorder should
// fall back to it when a capture produces an ambiguous or implausible ordering.
constexpr uint8_t HID_USAGE_MOD_FIRST = 0xE0;
constexpr uint8_t HID_USAGE_MOD_LAST  = 0xE7;

// Milliseconds between successive keyboard reports during playback. Long enough
// that a host processes each transition, short enough to feel instant.
constexpr uint8_t MACRO_STEP_MS = 15;

// --- Trigger encoding --------------------------------------------------------
// gesture == GESTURE_NONE means this is a chord macro and `chord` is a logical
// button mask from input_buttons.h. Otherwise `chord` is unused and the entry
// fires on a touchpad swipe.
constexpr uint8_t GESTURE_NONE = 0;
// Packed: bits 0-1 direction, bit 2 start zone, bit 3 finger count.
enum : uint8_t {
    GEST_DIR_UP     = 0u << 0,
    GEST_DIR_DOWN   = 1u << 0,
    GEST_DIR_LEFT   = 2u << 0,
    GEST_DIR_RIGHT  = 3u << 0,
    GEST_DIR_MASK   = 3u << 0,
    GEST_ZONE_RIGHT = 1u << 2, // swipe STARTED on the right half of the pad
    GEST_TWO_FINGER = 1u << 3,
    GEST_MOTION     = 1u << 4, // MOTION gesture: motion[] holds the template
    GEST_STICK      = 1u << 5, // STICK axes: keys[] are direction-indexed
    GEST_STICK_RIGHT= 1u << 6, // right stick instead of left
    GEST_VALID      = 1u << 7, // set on every real gesture so the byte is non-zero
};

// --- Motion gestures ---------------------------------------------------------
// A THIRD trigger kind: hold a gate button, move the controller, release. The
// gate is what removes start/end detection - without it an always-on recogniser
// competes with gyro aiming and fires during normal play.
//
// `chord` carries the GATE mask for a motion entry. It is otherwise unused on a
// non-chord entry, and reusing it means find_entry()/best_chord() already skip
// these records (they skip anything with gesture != GESTURE_NONE), so the gate
// button cannot be stolen by chord matching.
//
// The template is a sequence of STROKE directions, 4-way, 2 bits each. Eight
// directions were tried first and failed on hardware: at 22.5 degrees per
// sector a hand cannot hold an axis, and one up-flick quantised as
// up/up-left/up/up-left to the length ceiling.
constexpr uint8_t MACRO_MOTION_MAX   = 8;  // codes; a real gesture is 1-4
constexpr uint8_t MACRO_MOTION_BYTES = 2;  // 8 codes x 2 bits
enum : uint8_t {
    MOTION_RIGHT = 0,
    MOTION_UP    = 1,
    MOTION_LEFT  = 2,
    MOTION_DOWN  = 3,
};
// Default step threshold in raw gyro counts. Per-entry because it is CALIBRATED
// from the user's own motion - a constant chosen without hardware produced a
// code storm, which is what sent the portal prototype back twice.
constexpr uint16_t MACRO_MOTION_STEP_DEFAULT = 1800;

// --- Stick as an input -------------------------------------------------------
// keys[] stops being a press SEQUENCE on a stick row and becomes
// direction-indexed: one output per cardinal. Per-AXIS thresholds, not
// direction quantisation - diagonals then fall out for free (up-left presses
// both up and left) and none of the boundary dithering that cost four rounds on
// motion gestures can happen, because the axes never compete.
constexpr uint8_t MACRO_STICK_UP = 0, MACRO_STICK_RIGHT = 1,
                  MACRO_STICK_DOWN = 2, MACRO_STICK_LEFT = 3;
constexpr uint8_t MACRO_STICK_THRESH = 48;   // deflection from centre, of 127
constexpr uint8_t MACRO_STICK_HYST   = 10;   // release margin, same units


enum : uint8_t {
    MACRO_FLAG_LONG_PRESS = 1u << 0, // fire at hold_cs, not on release
    // HOLD turns the row from a one-shot BURST into a state: the output is
    // asserted while the input is active and released when it stops. It is what
    // makes remapping and stick-to-keys possible at all, and it is meaningless
    // on a swipe or a motion gesture - those are EVENTS that complete, with
    // nothing to hold. Enforced in both the portal and macro_task(); a flag
    // honoured in only one of two places is how the v1.14.5 bow went missing.
    MACRO_FLAG_HOLD    = 1u << 1,
    // REPLACE hides the input from the game: the source button is cleared, or
    // the source stick zeroed, in the OUTBOUND report. Without it a remap is
    // additive and the game sees both the original and the replacement.
    MACRO_FLAG_REPLACE = 1u << 2,
};

// Default long-press threshold, centiseconds. 750 ms, matching ps_shortcut.
constexpr uint8_t MACRO_HOLD_CS_DEFAULT = 75;

// --- Mouse outputs -----------------------------------------------------------
// out_btn values ABOVE T2BTN_COUNT are mouse actions rather than controller
// buttons. Continuing the same field keeps one output list in the portal, and
// the split is by VALUE RANGE so main.cpp's controller loop simply never sees
// them. Clicks are held while the input is held; scroll is a tick per press.
// 11, fixed. It was T2BTN_COUNT, which happened to be 11 when the mouse
// outputs were added - but the two stopped being the same number the moment
// controller buttons were appended past the mouse block, and these values are
// PERSISTED in every saved macro.
constexpr uint8_t MOUT_FIRST      = 11;
constexpr uint8_t MOUT_LEFT       = 11;
constexpr uint8_t MOUT_RIGHT      = 12;
constexpr uint8_t MOUT_MIDDLE     = 13;
constexpr uint8_t MOUT_SCROLL_UP  = 14;
constexpr uint8_t MOUT_SCROLL_DN  = 15;
constexpr uint8_t MOUT_LAST       = 15;
static inline bool macro_is_mouse_out(uint8_t out_btn) {
    return out_btn >= MOUT_FIRST && out_btn <= MOUT_LAST;
}

struct __attribute__((packed)) MacroEntry {
    uint32_t chord;       // logical button mask (input_buttons.h); 0 if gesture
    uint8_t  gesture;     // GESTURE_NONE or GEST_* bits
    uint8_t  flags;       // MACRO_FLAG_*
    uint8_t  hold_cs;     // long-press threshold, centiseconds (0 -> default)
    uint8_t  keys[MACRO_KEYS]; // HID usages in PRESS order, 0 = unused
    uint8_t  rel_order;   // release permutation, 2 bits per slot
    // --- appended for motion gestures; absent from rec_len 28 tables ---
    uint8_t  motion[MACRO_MOTION_BYTES]; // 2 bits per stroke, index 0 first
    uint8_t  motion_len;  // strokes used, 0 on every non-motion entry
    uint16_t motion_step; // raw gyro counts per stroke; 0 -> default
    // --- appended for remapping and stick-to-keys; absent from rec_len 33 ---
    // out_btn selects the OUTPUT kind: 0 sends keys[] over the keyboard
    // interface, non-zero asserts a controller button instead and keys[] is
    // ignored. Values use the T2Button numbering in config.h - the two are
    // deliberately the same list, so main.cpp has ONE mask-to-report mapping.
    // A controller-button output is far more reliable in-game than a keystroke:
    // a game that sees a DualSense is in controller mode, and many ignore the
    // keyboard entirely or flip every on-screen prompt when one arrives.
    uint8_t  out_btn;
    // Stick deflection needed before a direction counts, 0 -> MACRO_STICK_THRESH.
    uint8_t  stick_thresh;
    // No reserved padding: MacroTable.rec_len makes the record self-describing,
    // so a later firmware with a LARGER record still reads today's tables (the
    // same mechanism SlotRecordV2.body_len uses to survive Config_body growth).
};
static_assert(sizeof(MacroEntry) == 19, "MacroEntry size is part of the flash format");

static inline uint8_t macro_motion_code(const MacroEntry &e, uint8_t i) {
    if (i >= MACRO_MOTION_MAX) return 0;
    return (uint8_t) ((e.motion[i >> 2] >> ((i & 3u) * 2u)) & 3u);
}
static inline void macro_motion_set_code(MacroEntry &e, uint8_t i, uint8_t code) {
    if (i >= MACRO_MOTION_MAX) return;
    const uint8_t sh = (uint8_t) ((i & 3u) * 2u);
    e.motion[i >> 2] = (uint8_t) ((e.motion[i >> 2] & ~(3u << sh)) | ((code & 3u) << sh));
}
static inline bool macro_is_stick(const MacroEntry &e) {
    return (e.gesture & GEST_STICK) != 0;
}
static inline bool macro_is_hold(const MacroEntry &e) {
    return (e.flags & MACRO_FLAG_HOLD) != 0;
}
static inline bool macro_is_motion(const MacroEntry &e) {
    return (e.gesture & GEST_MOTION) != 0 && e.motion_len > 0;
}

struct __attribute__((packed)) MacroRecord {
    MacroEntry entry;
    uint8_t    label[MACRO_LABEL_LEN]; // NUL-padded, portal display only
};
static_assert(sizeof(MacroRecord) == 35);

// Whole table + header, rewritten as one sector image like the slot sectors.
constexpr uint32_t MACRO_MAGIC   = 0x4D355344; // "DS5M"
constexpr uint8_t  MACRO_FORMAT  = 1;

struct __attribute__((packed)) MacroTable {
    uint32_t    magic;
    uint8_t     format;    // MACRO_FORMAT
    uint8_t     count;     // MACRO_COUNT at write time; extra entries default
    uint16_t    rec_len;   // sizeof(MacroRecord) at write time
    MacroRecord rec[MACRO_COUNT];
    uint32_t    crc32;     // over rec[0..count) using rec_len bytes each
};
static_assert(sizeof(MacroTable) <= 4096, "macro table must fit one flash sector");

// --- API ---------------------------------------------------------------------

// Load the table from flash into the RAM working image. Missing/invalid sector
// (virgin flash on every existing device) yields an all-empty table - no
// migration and no flash_nuke on upgrade.
void macro_load();

// Edit the RAM image, then commit once. Per-entry writes with a single explicit
// commit avoid erasing the sector 32 times while the portal saves a list.
bool macro_get(uint8_t idx, MacroRecord &out);
bool macro_set_entry(uint8_t idx, const MacroRecord &rec);
bool macro_commit();

// Called from main.cpp's on_bt_data for every input report, with the same
// `data + 3` pointer ps_shortcut_tick already receives.
void macro_on_input(const uint8_t *report, uint16_t len);

// Service deferred work: long-press thresholds that expire with no new report,
// and the multi-step playback walk. Call from the main loop.
void macro_task();

// Drop all pending state and release any keys still down. Called on controller
// disconnect (beside ps_shortcut_reset) and on the host-suspend edge, so a
// combo caught mid-playback cannot leave a modifier latched at the host across
// sleep - the keyboard-side analogue of state_release_for_suspend().
void macro_reset();

// Portal record mode. While suspended, chords are still decoded and reported
// but never fire, so capturing a chord that matches an already-enabled macro
// cannot type into the portal while the user is recording it.
void macro_suspend(bool on);

// True while a motion gate is held and strokes are being accumulated. main.cpp
// suppresses gyro-to-stick aiming while this is set, so performing a gesture
// does not also swing the aim - the same idea as gyro_mode 4 pausing on a
// touchpad touch.
bool macro_motion_capturing();

// Outbound report edits requested by REPLACE rows and controller-button
// outputs. main.cpp applies these in apply_trigger_output(), the one place that
// already rewrites the report on its way to the host - the macro engine never
// touches the shared buffer itself.
uint32_t macro_suppress_mask();   // logical buttons to CLEAR
uint32_t macro_inject_mask();     // logical buttons to SET
bool     macro_suppress_stick(bool right);
// True when the macro engine needs the outgoing report rewritten this tick.
bool     macro_report_active();
// Live keyboard/mouse output, for diagnostics (see macro_output_state).
void     macro_output_state(uint8_t &keys_held, uint8_t &first_key, uint8_t &mouse_btns);

// Analog travel for an L2/R2 controller output, or 0 when that output is not
// active. Non-zero only when a TRIGGER is driving a trigger: remapping L2 to R2
// as a binary press would turn a variable throttle into an on/off switch.
uint8_t  macro_analog_out(bool right);

// Mouse output state, merged into the gyro mouse report by main.cpp.
// Buttons are a bitmask: bit0 left, bit1 right, bit2 middle - the same order as
// the HID report's button byte. Scroll is consumed on read: it is a tick per
// press, not a level, so reading it twice must not send it twice.
uint8_t  macro_mouse_buttons();
int8_t   macro_mouse_peek_scroll();   // without consuming
int8_t   macro_mouse_take_scroll();   // consumes: only call once the report will be sent

// True while the engine holds keys down or has queued playback steps. wake.cpp
// uses it to avoid interleaving its F15 keystroke with a macro on the shared
// keyboard instance.
bool macro_busy();

// Touchpad-click diagnostics for the portal: X where the finger last landed
// (0-1919), how many click presses have been seen, and which half the last one
// resolved to (1 = left, 2 = right, 0 = unqualified). Exists because this is
// not something that can be reasoned about from the code - the pad's behaviour
// at press time has to be measured on real hardware.
void macro_pad_debug(uint16_t &x, uint8_t &presses, uint8_t &last_half);

#endif // DS5_BRIDGE_MACRO_H
