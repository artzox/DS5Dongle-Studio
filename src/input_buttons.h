//
// input_buttons.h - logical button mask decoded from a DualSense input report.
//
// SINGLE SOURCE OF TRUTH for "which buttons are held". The macro engine matches
// chords against this mask and the portal DISPLAYS chords from the same mask, so
// the two must agree bit for bit. The portal carries a JS transcription of this
// file in ds5-config-portal.html (search BTN_ / decodeButtons); tools/portal-buttons-test.js
// cross-checks the two against a shared vector table. Change one, change all three.
//
// Input is the report as main.cpp already hands it around: the BT 0x31 payload
// AFTER the `data + 3` skip, which is byte-identical to what the host receives
// in interrupt_in_data. Offsets below are indices into that buffer.
//
//   byte 7  low nibble  D-pad HAT VALUE (see below), high nibble face buttons
//   byte 8              L1 R1 L2click R2click Create Options L3 R3
//   byte 9  bit0 PS, bit1 touchpad click, bit2 mute
//   byte 32 finger 1: bit7 set = NOT touching, bits 0-6 touch id
//   byte 36 finger 2: same
//
// THE D-PAD IS AN ENUM, NOT A BITMASK. This is the one thing a naive
// 24-bit-mask-over-bytes-7/8/9 implementation gets wrong: "Up" is hat value 0,
// so it is indistinguishable from "no button", and the idle value 8 reads as a
// phantom press. Every consumer must go through button_mask() rather than
// touching byte 7 directly.
//

#ifndef DS5_BRIDGE_INPUT_BUTTONS_H
#define DS5_BRIDGE_INPUT_BUTTONS_H

#include <cstdint>

// Byte offsets into the post-`data + 3` report.
constexpr uint8_t RPT_BTN0    = 7;   // hat + face
constexpr uint8_t RPT_BTN1    = 8;   // shoulders / sticks / system
constexpr uint8_t RPT_BTN2    = 9;   // PS / touchpad / mute
constexpr uint8_t RPT_L2_AXIS = 4;   // analog trigger travel, 0-255
constexpr uint8_t RPT_R2_AXIS = 5;
constexpr uint8_t RPT_TOUCH0  = 32;  // finger 1, 4 bytes
constexpr uint8_t RPT_TOUCH1  = 36;  // finger 2, 4 bytes
constexpr uint8_t RPT_MIN_LEN = 40;  // shortest report we can fully decode

// Touch coordinate ranges. Declared before button_mask(), which qualifies a
// touchpad click by which half of the pad the finger is on.
constexpr uint16_t TOUCH_X_MAX = 1919;
constexpr uint16_t TOUCH_Y_MAX = 1079;

// Logical button bits. APPEND ONLY - these values are persisted inside every
// stored macro chord, so renumbering silently rebinds every macro a user saved.
enum : uint32_t {
    BTN_DPAD_UP    = 1u << 0,
    BTN_DPAD_DOWN  = 1u << 1,
    BTN_DPAD_LEFT  = 1u << 2,
    BTN_DPAD_RIGHT = 1u << 3,
    BTN_SQUARE     = 1u << 4,
    BTN_CROSS      = 1u << 5,
    BTN_CIRCLE     = 1u << 6,
    BTN_TRIANGLE   = 1u << 7,
    BTN_L1         = 1u << 8,
    BTN_R1         = 1u << 9,
    BTN_L2         = 1u << 10, // digital click, not the analog axis
    BTN_R2         = 1u << 11,
    BTN_CREATE     = 1u << 12,
    BTN_OPTIONS    = 1u << 13,
    BTN_L3         = 1u << 14,
    BTN_R3         = 1u << 15,
    BTN_PS         = 1u << 16,
    BTN_TOUCHPAD   = 1u << 17, // touchpad CLICK, not a touch
    BTN_MUTE       = 1u << 18,
    // DualSense Edge only. Report byte 9 bits 4-7, already documented in
    // utils.h and never decoded until now. APPENDED, like every bit before
    // them: these values are persisted inside every stored macro chord, so
    // renumbering would silently rebind existing rows.
    //
    // The two Fn buttons are the useful half. Sony's own app claims Fn + a FACE
    // button for switching the controller's on-board profiles, so building
    // chords there fights it - but Fn + D-pad is unclaimed and nothing else on
    // the pad uses it, so a chord built there cannot collide with normal play.
    BTN_LEFT_FN    = 1u << 19,
    BTN_RIGHT_FN   = 1u << 20,
    BTN_LEFT_PAD   = 1u << 21,
    BTN_RIGHT_PAD  = 1u << 22,
    // Touchpad click, qualified by WHICH HALF the finger was on. The pad is one
    // physical switch, so BTN_TOUCHPAD alone cannot tell a left click from a
    // right one - but the finger position is in the same report, so these are
    // derived from it. BTN_TOUCHPAD is still set on every click: a chord
    // recorded before these existed keeps matching any click, while a chord
    // that includes a half only matches clicks on that half (and wins over the
    // generic one, since the longest fully-held chord wins).
    //
    // Set only when a finger is actually reported down. Clicking with a knuckle
    // or the pad's edge can register the switch with no touch point, and
    // guessing a half there would fire the wrong macro - a generic
    // BTN_TOUCHPAD chord still catches those.
    BTN_PAD_CLICK_LEFT  = 1u << 23,
    BTN_PAD_CLICK_RIGHT = 1u << 24,
    BTN_ALL        = 0x01FFFFFFu,
};

// Hat value -> direction bits. Index 8 is the idle/neutral position; anything
// above 8 (some pads emit 0x0F for "centred") clamps to it.
//   0 N   1 NE   2 E   3 SE   4 S   5 SW   6 W   7 NW   8 neutral
constexpr uint32_t HAT_TO_DPAD[9] = {
    BTN_DPAD_UP,
    BTN_DPAD_UP   | BTN_DPAD_RIGHT,
    BTN_DPAD_RIGHT,
    BTN_DPAD_RIGHT| BTN_DPAD_DOWN,
    BTN_DPAD_DOWN,
    BTN_DPAD_DOWN | BTN_DPAD_LEFT,
    BTN_DPAD_LEFT,
    BTN_DPAD_LEFT | BTN_DPAD_UP,
    0u,
};

// Decode the held-button set. Returns 0 for a report too short to trust rather
// than decoding garbage - a truncated report must never look like a chord.
static inline uint32_t button_mask(const uint8_t *r, uint16_t len) {
    if (r == nullptr || len < 10) return 0u;

    const uint8_t b0 = r[RPT_BTN0];
    const uint8_t b1 = r[RPT_BTN1];
    const uint8_t b2 = r[RPT_BTN2];

    uint8_t hat = (uint8_t) (b0 & 0x0Fu);
    if (hat > 8u) hat = 8u;
    uint32_t m = HAT_TO_DPAD[hat];

    if (b0 & 0x10u) m |= BTN_SQUARE;
    if (b0 & 0x20u) m |= BTN_CROSS;
    if (b0 & 0x40u) m |= BTN_CIRCLE;
    if (b0 & 0x80u) m |= BTN_TRIANGLE;

    if (b1 & 0x01u) m |= BTN_L1;
    if (b1 & 0x02u) m |= BTN_R1;
    if (b1 & 0x04u) m |= BTN_L2;
    if (b1 & 0x08u) m |= BTN_R2;
    if (b1 & 0x10u) m |= BTN_CREATE;
    if (b1 & 0x20u) m |= BTN_OPTIONS;
    if (b1 & 0x40u) m |= BTN_L3;
    if (b1 & 0x80u) m |= BTN_R3;

    if (b2 & 0x01u) m |= BTN_PS;
    if (b2 & 0x02u) m |= BTN_TOUCHPAD;
    if (b2 & 0x04u) m |= BTN_MUTE;
    // Edge-only bits. A standard DualSense leaves them clear, so decoding them
    // unconditionally costs nothing and needs no is_dse test here.
    if (b2 & 0x10u) m |= BTN_LEFT_FN;
    if (b2 & 0x20u) m |= BTN_RIGHT_FN;
    if (b2 & 0x40u) m |= BTN_LEFT_PAD;
    if (b2 & 0x80u) m |= BTN_RIGHT_PAD;

    // Qualify the pad click with the half the finger is on. Needs the touch
    // block, which lives further into the report than the 10 bytes checked
    // above - a shorter report simply keeps the unqualified click.
    if ((m & BTN_TOUCHPAD) && len >= (uint16_t) (RPT_TOUCH0 + 4)) {
        const uint8_t *t = r + RPT_TOUCH0;
        if ((t[0] & 0x80u) == 0u) {                       // bit7 SET = lifted
            const uint16_t x = (uint16_t) (t[1] | ((uint16_t) (t[2] & 0x0Fu) << 8));
            m |= (x > (uint16_t) (TOUCH_X_MAX / 2)) ? BTN_PAD_CLICK_RIGHT
                                                    : BTN_PAD_CLICK_LEFT;
        }
    }

    return m;
}

// --- Touchpad ---------------------------------------------------------------
// A DualSense touch point is 4 bytes: bit7 of byte 0 SET means the finger is
// LIFTED (this inversion is already relied on by gyro modes 3/4 in main.cpp).
// X is 0..1919, Y is 0..1079.
struct TouchPoint {
    bool     down;
    uint16_t x;
    uint16_t y;
};

static inline TouchPoint touch_point(const uint8_t *r, uint16_t len, uint8_t which) {
    TouchPoint t{false, 0, 0};
    const uint8_t off = which ? RPT_TOUCH1 : RPT_TOUCH0;
    if (r == nullptr || len < (uint16_t) (off + 4)) return t;
    const uint8_t *b = r + off;
    t.down = (b[0] & 0x80u) == 0u;
    t.x = (uint16_t) (b[1] | ((uint16_t) (b[2] & 0x0Fu) << 8));
    t.y = (uint16_t) ((b[2] >> 4) | ((uint16_t) b[3] << 4));
    return t;
}

#endif // DS5_BRIDGE_INPUT_BUTTONS_H
