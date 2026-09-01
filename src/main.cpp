//
// Created by awalol on 2026/3/4.
//

#include <cstdio>
#include <cmath>
#include "bsp/board_api.h"
#include "bt.h"
#include "button_functions.h"
#include "utils.h"
#include "resample.h"
#include "audio.h"
#include "wake.h"
#ifdef ENABLE_WAKE_HID
#include "ps_shortcut.h"
#include "macro.h"
#include "input_buttons.h"   // BTN_* logical masks used by macro_apply_buttons()
#endif
#include "hardware/clocks.h"
#include "hardware/vreg.h"
#include "hardware/watchdog.h"
#include "pico/cyw43_arch.h"
#include "state_mgr.h"
#if ENABLE_SERIAL
#include "pico/stdio_usb.h"
#endif
#include "config.h"
#include "battery_notify.h"
#include "cmd.h"
#include "dse.h"
#if ENABLE_BATT_LED
#include "battery_led.h"
#endif

// Pico SDK speciifically for waiting on conditions
#include "pico/critical_section.h"

int reportSeqCounter = 0;
uint8_t packetCounter = 0;
bool spk_active = false;

uint8_t interrupt_in_data[63] = {
    0x7f, 0x7d, 0x7f, 0x7e, 0x00, 0x00, 0xa7,
    0x08, 0x00, 0x00, 0x00, 0x52, 0x43, 0x30, 0x41,
    0x01, 0x00, 0x0e, 0x00, 0xef, 0xff, 0x03, 0x03,
    0x7b, 0x1b, 0x18, 0xf0, 0xcc, 0x9c, 0x60, 0x00,
    0xfc, 0x80, 0x00, 0x00, 0x00, 0x80, 0x00, 0x00,
    0x00, 0x00, 0x09, 0x09, 0x00, 0x00, 0x00, 0x00,
    0x00, 0xa7, 0xad, 0x60, 0x00, 0x29, 0x18, 0x00,
    0x53, 0x9f, 0x28, 0x35, 0xa5, 0xa8, 0x0c, 0x8b
};

critical_section_t report_cs;
volatile bool report_dirty = false;

// Trigger activation dead zone (v1.8.0): mask what the HOST sees until the pull
// reaches the configured zone - analog forced to 0 and the digital press bit
// cleared, so games that fire on a hair-trigger register the action exactly where
// the resistance/detent/bow feel says they should. Applied ONLY to the outbound
// report copy: every internal consumer (AT gating, kick, shapes, gyro) keeps
// reading the raw trigger. Report body: [4]=L2 analog, [5]=R2 analog,
// [8] bit2=L2 pressed, bit3=R2 pressed. Zone N starts at N*25.5 counts.
// Stage-2 output button -> report bit. Report offsets: 7 = hat + face buttons,
// 8 = shoulders / sticks / system (same layout input_buttons.h decodes).
static inline void t2_press(uint8_t *r, uint8_t btn) {
    switch (btn) {
        case T2BTN_SQUARE:   r[7] |= 0x10u; break;
        case T2BTN_CROSS:    r[7] |= 0x20u; break;
        case T2BTN_CIRCLE:   r[7] |= 0x40u; break;
        case T2BTN_TRIANGLE: r[7] |= 0x80u; break;
        case T2BTN_L1:       r[8] |= 0x01u; break;
        case T2BTN_R1:       r[8] |= 0x02u; break;
        case T2BTN_L3:       r[8] |= 0x40u; break;
        case T2BTN_R3:       r[8] |= 0x80u; break;
        // Trigger targets have to move the ANALOG AXIS as well as the click bit.
        // Games read L2/R2 as axes - the digital bits in byte 8 are barely used -
        // so setting the bit alone was invisible, which is why picking L2 or R2
        // appeared to do nothing at all. Take the max so a real physical pull on
        // that trigger is never reduced by the synthetic press.
        case T2BTN_L2:       r[8] |= 0x04u; if (r[4] < 255u) r[4] = 255u; break;
        case T2BTN_R2:       r[8] |= 0x08u; if (r[5] < 255u) r[5] = 255u; break;
        case T2BTN_CREATE:   r[8] |= 0x10u; break;
        case T2BTN_OPTIONS:  r[8] |= 0x20u; break;
        case T2BTN_TOUCHPAD: r[9] |= 0x02u; break;
        // D-pad handled by the hat merge in macro_apply_buttons(), not here.
        default: break;
    }
}

// Release hysteresis, in raw trigger counts (~3% of travel). Resting a finger
// exactly on the boundary makes the raw value dither by a few counts, so a bare
// threshold pressed and released the stage-2 button at report rate. In a game
// that reacts to the button - nitro engaging and disengaging over and over -
// that reads as a buzz right at the wall. Engage at the boundary, release below
// it, so the two edges cannot chase each other.
constexpr uint8_t T2_HYST = 8;

// Per-trigger latch state. RAM only, never Config_body: this is transient, not
// a setting, and it must not travel into a profile or a slot. The boundary and
// mode are kept alongside so a slot activation that retunes them while the
// trigger is held cannot strand the latch in its old state.
struct T2State { bool latched; uint8_t pos; uint8_t mode; };

// Dead zone + two-stage handling for ONE trigger. Everything is computed from
// the ORIGINAL axis value captured on entry: the dead zone rewrites r[axis_off]
// and the rescale would otherwise read its own output back.
// Returns true when this trigger's second stage is latched. The PRESS itself is
// applied by the caller after BOTH triggers have been processed - see
// apply_trigger_output.
static inline bool apply_trigger_out_one(uint8_t *r, uint8_t axis_off, uint8_t dig_mask,
                                         uint8_t dz, uint8_t mode, uint8_t pos, uint8_t btn,
                                         T2State &st) {
    const uint8_t raw = r[axis_off];
    if (st.pos != pos || st.mode != mode) { st.latched = false; st.pos = pos; st.mode = mode; }
    const uint8_t dz_thr = dz ? (uint8_t) (((uint16_t) dz * 51u) / 2u) : 0u;

    if (dz && raw < dz_thr) { r[axis_off] = 0; r[8] &= (uint8_t) ~dig_mask; }

    if ((mode & T2_AXIS_MASK) == T2_AXIS_OFF || pos == 0) { st.latched = false; return false; }

    // Rescale stretches [dead zone .. boundary] over the full 0-255, so the
    // shortened travel keeps full analog authority instead of being clipped.
    // Position-only: it does not depend on the latch.
    if ((mode & T2_AXIS_MASK) == T2_AXIS_RESCALE && pos > dz_thr) {
        const uint32_t v   = raw <= dz_thr ? 0u : (uint32_t) (raw - dz_thr);
        const uint32_t out = (v * 255u) / (uint32_t) (pos - dz_thr);
        r[axis_off] = (uint8_t) (out > 255u ? 255u : out);
    }

    // A boundary at or below the hysteresis would leave the release threshold at
    // 0 and the latch could never clear, so it falls back to "fully released".
    const uint8_t rel = (pos > T2_HYST) ? (uint8_t) (pos - T2_HYST) : 1u;
    if (st.latched) { if (raw < rel) st.latched = false; }
    else            { if (raw >= pos) st.latched = true;  }
    if (!st.latched) return false;

    if (mode & T2_RELEASE_STAGE1) r[8] &= (uint8_t) ~dig_mask;
    return true;   // caller presses, once both triggers have been processed
}


// Logical button mask -> report bits. The INVERSE of button_mask() in
// input_buttons.h; the two lists must stay in step, so they are cross
// referenced in both directions. Bytes 7/8/9 are b0/b1/b2 there.
static inline void macro_apply_buttons(uint8_t *r) {
    const uint32_t sup = macro_suppress_mask();
    const uint32_t inj = macro_inject_mask();
    if (sup) {
        if (sup & BTN_SQUARE)   r[7] &= (uint8_t) ~0x10u;
        if (sup & BTN_CROSS)    r[7] &= (uint8_t) ~0x20u;
        if (sup & BTN_CIRCLE)   r[7] &= (uint8_t) ~0x40u;
        if (sup & BTN_TRIANGLE) r[7] &= (uint8_t) ~0x80u;
        if (sup & BTN_L1)       r[8] &= (uint8_t) ~0x01u;
        if (sup & BTN_R1)       r[8] &= (uint8_t) ~0x02u;
        // Triggers need the AXIS zeroed as well as the click bit. Games read
        // L2/R2 as analog axes and barely use the digital bits, so clearing the
        // bit alone left the full pull visible and "hide input from game" did
        // nothing for a trigger while working for every other button. Same
        // lesson as the two-stage trigger's L2/R2 output, in reverse.
        if (sup & BTN_L2)     { r[8] &= (uint8_t) ~0x04u; r[4] = 0; }
        if (sup & BTN_R2)     { r[8] &= (uint8_t) ~0x08u; r[5] = 0; }
        if (sup & BTN_CREATE)   r[8] &= (uint8_t) ~0x10u;
        if (sup & BTN_OPTIONS)  r[8] &= (uint8_t) ~0x20u;
        if (sup & BTN_L3)       r[8] &= (uint8_t) ~0x40u;
        if (sup & BTN_R3)       r[8] &= (uint8_t) ~0x80u;
        if (sup & BTN_PS)       r[9] &= (uint8_t) ~0x01u;
        if (sup & BTN_TOUCHPAD) r[9] &= (uint8_t) ~0x02u;
        if (sup & BTN_MUTE)     r[9] &= (uint8_t) ~0x04u;
        // DualSense Edge Fn buttons and paddles. Without these a REPLACE macro
        // bound to one of them fired but could not hide the press, so the game
        // still saw whatever the paddle was mapped to.
        if (sup & BTN_LEFT_FN)   r[9] &= (uint8_t) ~0x10u;
        if (sup & BTN_RIGHT_FN)  r[9] &= (uint8_t) ~0x20u;
        if (sup & BTN_LEFT_PAD)  r[9] &= (uint8_t) ~0x40u;
        if (sup & BTN_RIGHT_PAD) r[9] &= (uint8_t) ~0x80u;
        // A qualified touchpad click has no report bit of its own - it IS the
        // click bit plus a finger position - so suppressing one clears the
        // click. Only ever engaged while that half is actually being pressed,
        // so this cannot hide a click on the other half. The touch coordinates
        // are left alone: a finger resting on the pad without a click is a
        // normal thing for a game to see.
        if (sup & (BTN_PAD_CLICK_LEFT | BTN_PAD_CLICK_RIGHT)) r[9] &= (uint8_t) ~0x02u;
        // A suppressed D-pad direction has to rewrite the HAT NIBBLE, which is
        // an enum (0-7 plus 8 = centred), not a bitfield. Only the exact
        // direction is cleared; a diagonal keeps its other half.
        constexpr uint32_t DPAD_ALL = BTN_DPAD_UP | BTN_DPAD_RIGHT |
                                      BTN_DPAD_DOWN | BTN_DPAD_LEFT;
        const uint8_t hat = (uint8_t) (r[7] & 0x0Fu);
        if (hat <= 7 && (sup & DPAD_ALL)) {
            // Reuses input_buttons.h's own table rather than restating it, so
            // there is no second copy of the hat encoding to drift.
            const uint32_t d = HAT_TO_DPAD[hat] & ~(sup & DPAD_ALL);
            uint8_t out = 8;                       // centred
            for (uint8_t i = 0; i < 8; i++) if (HAT_TO_DPAD[i] == d) { out = i; break; }
            r[7] = (uint8_t) ((r[7] & 0xF0u) | out);
        }
    }
    if (inj) {
        // D-pad outputs FIRST, because the hat is an enum: the four directions
        // share one nibble, so they cannot be OR-ed in the way every other
        // button can. Collect the injected directions, merge them with whatever
        // the player is physically holding, and re-encode once. An injected
        // direction WINS over a held one on the same axis - a macro that says
        // "press Up" should press Up even if the player is leaning down, rather
        // than cancelling to neutral. Opposite injected directions cancel,
        // since the hat has no way to express both.
        constexpr uint32_t DP_UP    = 1u << (T2BTN_DPAD_UP    - 1);
        constexpr uint32_t DP_DOWN  = 1u << (T2BTN_DPAD_DOWN  - 1);
        constexpr uint32_t DP_LEFT  = 1u << (T2BTN_DPAD_LEFT  - 1);
        constexpr uint32_t DP_RIGHT = 1u << (T2BTN_DPAD_RIGHT - 1);
        if (inj & (DP_UP | DP_DOWN | DP_LEFT | DP_RIGHT)) {
            const uint8_t hat = (uint8_t) (r[7] & 0x0Fu);
            uint32_t dirs = HAT_TO_DPAD[hat > 8 ? 8 : hat];   // what is held now
            if (inj & (DP_UP | DP_DOWN)) {                    // injected axis wins
                dirs &= ~(BTN_DPAD_UP | BTN_DPAD_DOWN);
                if (inj & DP_UP)   dirs |= BTN_DPAD_UP;
                if (inj & DP_DOWN) dirs |= BTN_DPAD_DOWN;
                if ((inj & DP_UP) && (inj & DP_DOWN)) dirs &= ~(BTN_DPAD_UP | BTN_DPAD_DOWN);
            }
            if (inj & (DP_LEFT | DP_RIGHT)) {
                dirs &= ~(BTN_DPAD_LEFT | BTN_DPAD_RIGHT);
                if (inj & DP_LEFT)  dirs |= BTN_DPAD_LEFT;
                if (inj & DP_RIGHT) dirs |= BTN_DPAD_RIGHT;
                if ((inj & DP_LEFT) && (inj & DP_RIGHT)) dirs &= ~(BTN_DPAD_LEFT | BTN_DPAD_RIGHT);
            }
            uint8_t out = 8;                                   // centred
            for (uint8_t i = 0; i < 8; i++) if (HAT_TO_DPAD[i] == dirs) { out = i; break; }
            r[7] = (uint8_t) ((r[7] & 0xF0u) | out);
        }
        // inj is indexed by the T2Button enum, shared with the two-stage
        // trigger so there is exactly one place that knows these bit positions.
        for (uint8_t b = 1; b < T2BTN_COUNT; b++) {
            if (!(inj & (1u << (b - 1)))) continue;
            if (macro_is_mouse_out(b)) continue;   // 11-15 are mouse, not buttons
            // A trigger output carries ANALOG travel when a trigger drove it, so
            // L2 -> R2 stays variable instead of collapsing to an on/off switch.
            // macro_analog_out() returns 255 for a button-driven trigger, which
            // is exactly what t2_press would have done anyway.
            if (b == T2BTN_L2 || b == T2BTN_R2) {
                const bool right = (b == T2BTN_R2);
                const uint8_t v  = macro_analog_out(right);
                const uint8_t ax = right ? 5u : 4u;
                r[8] |= right ? 0x08u : 0x04u;
                if (v && r[ax] < v) r[ax] = v;
                continue;
            }
            t2_press(r, b);
        }
    }
    if (macro_suppress_stick(false)) { r[0] = 128; r[1] = 128; }
    if (macro_suppress_stick(true))  { r[2] = 128; r[3] = 128; }
}

static inline void apply_trigger_output(uint8_t *r) {
    const auto &c = get_config();
    macro_apply_buttons(r);
    static T2State r2_st{}, l2_st{};
    // Both passes read the PHYSICAL trigger values and decide their own latch
    // first. Only then are the stage-2 presses applied. Doing it inline meant
    // R2's press landed before L2's pass, which then cleared the very click bit
    // R2 had just set (its dead-zone branch clears that same mask) - and a
    // synthetic full-scale value on L2 would have been read by L2's own latch as
    // a real pull, so a press on one trigger could trip the other's second stage.
    const bool r2_fire = apply_trigger_out_one(r, 5, 0x08u, c.at_deadzone,    c.t2_mode,    c.t2_pos,    c.t2_button,    r2_st);
    const bool l2_fire = apply_trigger_out_one(r, 4, 0x04u, c.at_l2_deadzone, c.t2_l2_mode, c.t2_l2_pos, c.t2_l2_button, l2_st);
    if (r2_fire) t2_press(r, c.t2_button);
    if (l2_fire) t2_press(r, c.t2_l2_button);
}

// Cheap gate so the untouched-report fast path survives for everyone not using
// either feature.
static inline bool trigger_output_active(const Config_body &c) {
    return c.at_deadzone || c.at_l2_deadzone ||
           (c.t2_mode & T2_AXIS_MASK) != T2_AXIS_OFF ||
           (c.t2_l2_mode & T2_AXIS_MASK) != T2_AXIS_OFF;
}

// Does the report need rewriting at all? This decides whether the input report
// is passed through untouched or copied and edited first.
//
// It used to ask only about TRIGGER settings, but apply_trigger_output() also
// runs macro_apply_buttons(), which is what "hide input from game" and macro
// button injection depend on. At any polling rate other than real-time, a
// controller with no trigger features configured therefore took the untouched
// path and every macro suppression and injection was silently dropped - so a
// Replace macro on L3 or R3 fired its keys AND still sent the stick click, and
// the same for every other button. Real-time mode was unaffected, which is why
// this looked like it worked for some people and not others.
static inline bool report_needs_rewrite(const Config_body &c) {
    return trigger_output_active(c) || macro_report_active();
}

void __not_in_flash_func(interrupt_loop)() {
    if (!tud_hid_ready()) return;

    // TODO: Refactor for better code reuse
    if (get_config().polling_rate_mode != 2) {
        const auto &cdz = get_config();
        if (report_needs_rewrite(cdz)) {
            static uint8_t dz_report[63];
            memcpy(dz_report, interrupt_in_data, 63);
            apply_trigger_output(dz_report);
            if (!tud_hid_report(0x01, dz_report, 63)) {
                printf("[USBHID] tud_hid_report error\n");
            }
        } else if (!tud_hid_report(0x01, interrupt_in_data, 63)) {
            printf("[USBHID] tud_hid_report error\n");
        }
        return;
    }

    bool should_send = false;
    // Local buffer to hold the report data while we prepare it to send. 
    uint8_t safe_report[63];


    critical_section_enter_blocking(&report_cs);
    if (report_dirty) {
        memcpy(safe_report, interrupt_in_data, 63);
        report_dirty = false;
        should_send = true;
    }
    critical_section_exit(&report_cs);

    // Only send to TinyUSB if we actually grabbed fresh data
    if (should_send) {
        apply_trigger_output(safe_report); // no-op when dead zones and stages are off
        if (!tud_hid_report(0x01, safe_report, 63)) {
            printf("[USBHID] tud_hid_report error\n");

            // If the report failed to queue, restore the dirty flag 
            // so we try again on the next loop iteration.
            critical_section_enter_blocking(&report_cs);
            report_dirty = true;
            critical_section_exit(&report_cs);
        }
    }
}

// --- Gyro -> right-stick aiming ---------------------------------------------
// Adds the controller's angular velocity onto the right stick in the input
// report the PC sees, so ANY game gets gyro aiming with zero PC software
// (DSX needs its app running for this; here it lives in the dongle).
// Integer-only so it is safe inside the report critical section.
// Report offsets: RightStickX=2, RightStickY=3, TriggerLeft=4,
// AngularVelocityX(pitch)=15, Z(roll)=17, Y(yaw)=19 (int16 LE).
volatile uint16_t g_diag_gyro = 0; // |horizontal gyro raw|, field 0x35
// Raw accelerometer, for the portal. Exposed so the axis mapping and the sign
// of player-space aim can be checked against a real controller instead of
// trusting the wiki - which was already wrong once about which byte is yaw.
volatile int16_t g_diag_ax = 0, g_diag_ay = 0, g_diag_az = 0;
// Tilt steering, instrumented. Three numbers answer the whole chain: is the
// block running at all (ran), what roll did it compute (deg), and what did it
// actually add to the stick (add). Shipping another guess without these was the
// wrong call twice over.
volatile int16_t g_diag_tilt_deg = 0;
volatile int16_t g_diag_tilt_add = 0;
volatile int16_t g_diag_tilt_ydeg = 0;
volatile int16_t g_diag_tilt_yadd = 0;
volatile uint8_t g_diag_tilt_ran = 0;   // 0 off, 1 running, 2 gravity rejected

// --- gyro as a mouse -------------------------------------------------------
//
// A mouse takes DELTAS, which is what a gyro produces natively. The stick path
// clamps into a 0-255 absolute range, so a fast turn pegs and stops.
//
// The accumulator is the whole trick. `raw * sens / 200` truncates, and the loss
// is per REPORT - so the slower you turn, the more reports the same rotation
// takes and the more of it is thrown away. Simulated over a 90-degree turn at
// sens 50: fast (30 reports) travels the full 1500 counts, slow (600 reports)
// only 1200. A 20% shortfall that appears only when aiming carefully, which is
// exactly when it is least wanted. Keeping the remainder in integer form makes
// every speed land on the same total. No floats: this runs in the report path.
struct __attribute__((packed)) GyroMouseReport {
    uint8_t buttons;   // bit0 left, bit1 right, bit2 middle - from macro rows
    int16_t x;
    int16_t y;
    int8_t  wheel;     // scroll ticks from macro rows
};
static_assert(sizeof(GyroMouseReport) == 6, "must match desc_hid_report_mouse");

static int32_t g_gm_acc_x = 0, g_gm_acc_y = 0;   // leftover numerator, not counts
static volatile int32_t g_gm_pend_x = 0, g_gm_pend_y = 0; // computed in the report
                                                          // path, sent from the loop

// Latest position of whichever stick drives the mouse, 0-255 per axis, 128 at
// rest. Written by the report path, consumed by gyro_mouse_task().
static volatile uint8_t g_sm_stick_x = 128, g_sm_stick_y = 128;
// Touchpad to mouse. The report path publishes the finger; gyro_mouse_task()
// turns it into movement. Written as a position plus a down flag rather than a
// delta so a dropped report cannot lose motion - the next tick simply sees a
// larger gap.
static volatile uint16_t g_tm_x = 0, g_tm_y = 0;
static volatile bool     g_tm_down = false;
// Sub-count remainder, so slow stick movement still moves the pointer: the same
// reason the gyro path carries one.
static float g_sm_rem_x = 0.0f, g_sm_rem_y = 0.0f;
static uint32_t g_sm_last_ms = 0;
constexpr float STICK_MOUSE_SENS_DEFAULT = 600.0f;   // counts/second at full tilt

void gyro_mouse_reset() { g_gm_acc_x = 0; g_gm_acc_y = 0; g_gm_pend_x = 0; g_gm_pend_y = 0;
                          g_sm_rem_x = 0.0f; g_sm_rem_y = 0.0f; g_sm_last_ms = 0; }

// A mouse is a DELTA device and the stick is an ABSOLUTE one, so they need
// completely different scales. A stick deflection of dx is a steady turn RATE
// for as long as it is held; a mouse delta of dx moves the pointer dx counts on
// EVERY report and then needs another one. Reusing the stick's /200 meant sens 5
// produced roughly 40 counts per report - about 40,000 counts per second at
// 1 kHz, which is why it flew off the screen. /256 then overshot the other way;
// /64 puts a 90-degree turn at roughly 5,700 counts at sens 50, with the 1-100
// range spanning very slow to very fast.
constexpr int32_t GYRO_MOUSE_DIV = 200 * 64;

// Reports arrive at the polling rate, so a delta-per-report scale would make the
// mouse four times slower at 250 Hz than at 1 kHz for the same wrist movement.
// Multiply back up so the FEEL is the same at every polling rate.
//   mode 0 = 250 Hz (bInterval 4), 1 = 500 Hz (2), 2 = 1 kHz (1)
static inline int32_t gyro_rate_mul(uint8_t polling_rate_mode) {
    switch (polling_rate_mode) {
        case 0:  return 4;
        case 1:  return 2;
        default: return 1;
    }
}

// Computed in the report path, SENT from the main loop.
//
// This function runs inside on_bt_data(), which the comment above calls the
// report critical section. Calling into TinyUSB from here re-enters the USB
// stack from the BT callback: the first version did exactly that and the portal
// started failing to read config fields, including the version registers, so it
// reported "pre-1.0.5". Only arithmetic belongs here.
// --- Natural (real-world) sensitivity ---------------------------------------
// counts = degrees_rotated * (counts_per_360 / 360) * multiplier
//
// The gyro reports ANGULAR VELOCITY in raw LSB. The DualSense runs its gyro at
// +/-2000 degrees/s over a full int16, so one LSB is 2000/32768 deg/s, and one
// report covers 1 ms of that at 1 kHz - which is exactly what gyro_rate_mul()
// already normalises for lower polling rates. Putting those together:
//
//   counts = raw * rate_mul * (2000/32768) / 1000 * (counts_360/360) * (x10/10)
//          = raw * rate_mul * counts_360 * x10 / 58982400
//
// The divisor is exact (32768 * 3600000 / 2000), so this is integer maths with
// no drift, and the remainder is carried the same way the arbitrary path does -
// without that, slow movement is truncated to nothing and fast movement loses
// a fraction of a count per report, always in the same direction.
//
// int64 is deliberate: counts_360 reaches 50000 and raw reaches 32767, so the
// numerator overflows int32 easily. It runs once per report, not per sample.
// Nominal scale: +/-2000 deg/s over a full int16, x100 for the trim.
//   counts = raw * rate_mul * counts_360 * x10 * trim / (58982400 * 100)
// The trim is what absorbs any error in that rating, per controller. Do not
// bake a correction in here on the strength of hand-turned measurements: an
// error factor that is not a clean ratio is as likely to be the human judging
// the angle as the sensor reporting it.
// counts = raw * dt_us * (2000/32768) deg/s / 1e6 * (counts_360/360) * (x10/10) * (trim/100)
//        = raw * dt_us * counts_360 * x10 * trim / (32768 * 1e6 * 360 * 10 * 100 / 2000)
constexpr int64_t GYRO_NATURAL_DIV = 32768LL * 1000000LL * 360LL * 10LL * 100LL / 2000LL;
static int64_t g_nat_acc_x = 0, g_nat_acc_y = 0;

// Degrees rotated since the last diagnostics read, x10, so the constant above
// can be CHECKED rather than trusted: rotate the controller through a known
// angle and compare. Wrong by a fixed ratio would mean 1.0x is not truly 1:1.
// Accumulate RAW readings and convert only when read. Converting per report
// truncated each one toward zero, so a slow turn lost most of its travel and a
// fast one lost a fraction every millisecond - the measurement has to be more
// trustworthy than the thing it is checking.
static volatile int64_t g_nat_raw_sum = 0;
static volatile uint32_t g_nat_samples = 0;   // gyro reports since the last read
static volatile uint64_t g_nat_span_us = 0;   // time they covered
// Samples per second observed over the last measurement window - exposes the
// report rate instead of assuming it.
uint16_t gyro_natural_rate_hz_read(void) {
    const uint64_t span = g_nat_span_us;
    const uint32_t n    = g_nat_samples;
    g_nat_samples = 0; g_nat_span_us = 0;
    if (span == 0) return 0;
    const uint64_t hz = (uint64_t) n * 1000000ULL / span;
    return (uint16_t) (hz > 65535 ? 65535 : hz);
}

uint32_t gyro_natural_degrees_x10_read(void) {
    const int64_t s = g_nat_raw_sum;
    g_nat_raw_sum = 0;
    // degrees x10 = raw_sum * (2000/32768) deg/s / 1000 reports/s * 10
    // degrees x10 = sum(raw * dt_us) * (2000/32768) / 1e6 * 10
    const int64_t d = s * 20000LL / 32768LL / 1000000LL;
    return (uint32_t) (d < 0 ? 0 : (d > 0xFFFFFFFFLL ? 0xFFFFFFFFLL : d));
}

static inline void __not_in_flash_func(gyro_emit_mouse)(int32_t horiz, int32_t pitch,
                                                        const Config_body &cfg) {
    // Feed the angle check in EVERY mode: you need to calibrate before switching
    // to natural, not after.
    // Diagnostic integrates over real time too, and also counts how many gyro
    // samples arrive per second - if that is not what anyone expects, it is the
    // answer to why an angle looked wrong.
    {
        static uint64_t d_last_us = 0;
        const uint64_t now_us = time_us_64();
        const uint64_t dt_us  = (d_last_us == 0) ? 0 : (now_us - d_last_us);
        d_last_us = now_us;
        if (dt_us > 0 && dt_us <= 100000) {
            g_nat_raw_sum += (int64_t) (horiz < 0 ? -horiz : horiz) * (int64_t) dt_us;
            g_nat_samples++;
            g_nat_span_us += dt_us;
        }
    }
    if (cfg.gyro_sens_mode == 1 && cfg.flick_counts_360 > 0) {
        // INTEGRATE OVER REAL TIME. The gyro reports angular velocity, so
        // turning it into an angle needs the interval each reading covers. That
        // interval was assumed to be 1 ms scaled by the USB polling rate - but
        // these samples arrive over BLUETOOTH, whose rate is set by the
        // controller and its link, not by how often the host polls USB. The
        // assumption made a measured 90-degree turn read 1.4x high and a
        // 360-degree turn 2.3x LOW, which no scale error can do: a fast turn
        // packs its rotation into fewer reports, a slow one into more, so the
        // error moved with how fast the turn was. Using the actual elapsed
        // microseconds removes the guess entirely.
        static uint64_t s_last_us = 0;
        const uint64_t now_us = time_us_64();
        const uint64_t dt_us  = (s_last_us == 0) ? 0 : (now_us - s_last_us);
        s_last_us = now_us;
        // A gap this large means the stream stopped (disconnect, sleep); one
        // sample cannot represent it, so drop it rather than lurch the view.
        if (dt_us == 0 || dt_us > 100000) { return; }
        const int64_t c360 = cfg.flick_counts_360;
        // Scale trim, x100 (100 = the nominal +/-2000 deg/s rating). Measuring a
        // known rotation is the only way to confirm that rating on real
        // hardware; if it is off, this corrects it once and every multiplier
        // stays honest afterwards.
        const int64_t trim = cfg.gyro_scale_trim_x100 ? cfg.gyro_scale_trim_x100 : 100;
        const int64_t mx   = (cfg.gyro_natural_x10 ? cfg.gyro_natural_x10 : 10) * trim;
        const int64_t my   = (cfg.gyro_natural_y_x10 ? cfg.gyro_natural_y_x10 : (cfg.gyro_natural_x10 ? cfg.gyro_natural_x10 : 10)) * trim;
        const int64_t nx = (int64_t) -horiz * (int64_t) dt_us * c360 * mx + g_nat_acc_x;
        const int64_t ny = (int64_t) -pitch * (int64_t) dt_us * c360 * my + g_nat_acc_y;
        int32_t dx = (int32_t) (nx / GYRO_NATURAL_DIV);
        int32_t dy = (int32_t) (ny / GYRO_NATURAL_DIV);
        g_nat_acc_x = nx - (int64_t) dx * GYRO_NATURAL_DIV;
        g_nat_acc_y = ny - (int64_t) dy * GYRO_NATURAL_DIV;

        if (cfg.gyro_invert & 1) dx = -dx;
        if (cfg.gyro_invert & 2) dy = -dy;
        g_gm_pend_x += dx;
        g_gm_pend_y += dy;
        return;
    }
    const int32_t s   = cfg.gyro_sens;
    const int32_t sy  = cfg.gyro_sens_y ? cfg.gyro_sens_y : s;
    const int32_t mul = gyro_rate_mul(cfg.polling_rate_mode);
    int32_t nx = -horiz * s  * mul + g_gm_acc_x;
    int32_t ny = -pitch * sy * mul + g_gm_acc_y;
    int32_t dx = nx / GYRO_MOUSE_DIV;        // truncates toward zero
    int32_t dy = ny / GYRO_MOUSE_DIV;
    g_gm_acc_x = nx - dx * GYRO_MOUSE_DIV;   // exact remainder, sign follows nx
    g_gm_acc_y = ny - dy * GYRO_MOUSE_DIV;
    if (cfg.gyro_invert & 1) dx = -dx;
    if (cfg.gyro_invert & 2) dy = -dy;
    g_gm_pend_x += dx;
    g_gm_pend_y += dy;
}

// --- Flick Stick -----------------------------------------------------------
//
// Implemented to Jibb Smart's specification (gyrowiki.jibbsmart.com, "Good Gyro
// Controls Part 2"). He invented it, and the constants below are the shipped
// JoyShockMapper defaults rather than numbers chosen here.
//
// The stick angle maps to the SAME in-game angle. Two behaviours:
//   flick  - crossing the threshold from centre turns by the stick's angle
//   turn   - rotating a held stick adds the angle change directly
//
// Crucially it adds a RELATIVE yaw change and never an absolute heading, so the
// gyro can still adjust during and after a flick. That is the whole point of
// pairing it with gyro aim.
//
// Runs in the main loop, not the report path: it needs atan2 and a time delta,
// and the report path is documented integer-only.
constexpr float FLICK_THRESHOLD       = 0.9f;   // huge deadzone: a flick is deliberate
constexpr float FLICK_TIME_S          = 0.10f;  // 0.2s "feels slow enough that I feel
                                                // like I'm waiting for it"
constexpr float TURN_SMOOTH_THRESHOLD = 0.1f;   // only SMALL movements get smoothed
constexpr int   TURN_SMOOTH_SAMPLES   = 8;

static volatile uint8_t g_fs_stick_x = 128, g_fs_stick_y = 128; // captured raw
static float g_fs_last_x = 0.0f, g_fs_last_y = 0.0f;
static float g_fs_progress = FLICK_TIME_S;   // finished
static float g_fs_size = 0.0f;               // radians still to deliver
static float g_fs_buf[TURN_SMOOTH_SAMPLES];
static int   g_fs_buf_i = 0;
static uint32_t g_fs_last_ms = 0;
static float g_fs_count_rem = 0.0f;   // sub-count remainder, carried between ticks

static void flick_zero_smoothing() {
    for (int i = 0; i < TURN_SMOOTH_SAMPLES; i++) g_fs_buf[i] = 0.0f;
}
void flick_stick_reset() {
    g_fs_last_x = g_fs_last_y = 0.0f;
    g_fs_progress = FLICK_TIME_S;
    g_fs_size = 0.0f;
    g_fs_last_ms = 0;
    g_fs_count_rem = 0.0f;
    flick_zero_smoothing();
}

// Ease OUT only. Jibb Smart: easing in is deliberately skipped so the flick
// feels responsive rather than animated.
static inline float flick_warp(float t) {
    const float f = 1.0f - t;
    return 1.0f - f * f;
}
static inline float wrap_pi(float a) {
    while (a >  3.14159265f) a -= 6.28318531f;
    while (a < -3.14159265f) a += 6.28318531f;
    return a;
}
static inline float flick_smoothed(float in) {
    g_fs_buf_i = (g_fs_buf_i + 1) % TURN_SMOOTH_SAMPLES;
    g_fs_buf[g_fs_buf_i] = in;
    float sum = 0.0f;
    for (int i = 0; i < TURN_SMOOTH_SAMPLES; i++) sum += g_fs_buf[i];
    return sum / (float) TURN_SMOOTH_SAMPLES;
}
// Soft tiered smoothing: no smoothing at all above a small threshold, and the
// blend is weighted so total displacement is unchanged either way.
static inline float flick_tiered(float in) {
    const float t1 = TURN_SMOOTH_THRESHOLD * 0.5f, t2 = TURN_SMOOTH_THRESHOLD;
    float w = (fabsf(in) - t1) / (t2 - t1);
    if (w < 0.0f) w = 0.0f; else if (w > 1.0f) w = 1.0f;
    return in * w + flick_smoothed(in * (1.0f - w));
}

// Returns the yaw change in radians for this tick.
static float flick_stick_step(float sx, float sy, float dt) {
    float result = 0.0f;
    const float len     = sqrtf(sx * sx + sy * sy);
    const float lastLen = sqrtf(g_fs_last_x * g_fs_last_x + g_fs_last_y * g_fs_last_y);

    if (len >= FLICK_THRESHOLD) {
        if (lastLen < FLICK_THRESHOLD) {
            g_fs_progress = 0.0f;                    // flick start
            g_fs_size = atan2f(-sx, sy);             // angle from forward
        } else {
            const float a  = atan2f(-sx, sy);
            const float la = atan2f(-g_fs_last_x, g_fs_last_y);
            result += flick_tiered(wrap_pi(a - la));  // turn
        }
    } else if (lastLen >= FLICK_THRESHOLD) {
        flick_zero_smoothing();                      // released: drop the tail
    }

    if (g_fs_progress < FLICK_TIME_S) {              // continue an in-flight flick
        const float last = g_fs_progress;
        g_fs_progress += dt;
        if (g_fs_progress > FLICK_TIME_S) g_fs_progress = FLICK_TIME_S;
        result += (flick_warp(g_fs_progress / FLICK_TIME_S)
                 - flick_warp(last / FLICK_TIME_S)) * g_fs_size;
    }

    g_fs_last_x = sx; g_fs_last_y = sy;
    return result;
}

// --- Counts-per-360 calibration burst ---------------------------------------
// Emits an EXACT number of mouse counts on request. Point: counts-per-360 is a
// property of the GAME's mouse sensitivity, so it cannot be derived from the
// controller - but it CAN be measured. Send a known number of counts, see how
// far the game turned, and the true value follows:
//   counts_360 = counts_sent * 360 / degrees_observed
// That replaces the guess-and-correct loop behind the default of 6500, and
// calibrates Flick Stick at the same time since both read the same field.
//
// Metered out over many reports rather than in one: a single huge delta gets
// clamped by some games, and a smooth sweep is far easier to judge by eye than
// an instant snap.
static volatile int32_t g_cal_remaining = 0;
// Counts per report. This was 40, which delivers ~10,000 counts/second - fast
// enough that a game sampling the mouse once a frame, or applying any
// smoothing, drops part of the sweep. Lost counts make the view turn LESS than
// it should, which inflates the calculated counts-per-360 rather than showing
// up as an obvious failure. 8 per report is ~2,000/second: slow enough for any
// game to see every count, and still only a few seconds for a full sweep.
constexpr int32_t CAL_STEP = 8;

void gyro_cal_emit(int32_t counts) { g_cal_remaining = counts; }
bool gyro_cal_busy(void)           { return g_cal_remaining != 0; }

// Drained from the main loop, where touching TinyUSB is safe.
void gyro_mouse_task() {
    const auto &cfg = get_config();
    if (!usb_mouse_iface_needed(cfg)) return;
    // Macro mouse output shares this report. Clicks are a level, scroll a tick.
    const uint8_t mbtn = macro_mouse_buttons();
    static uint8_t s_mbtn_sent = 0;

    if (cfg.gyro_output == 2 && cfg.flick_counts_360 > 0) {
        const uint32_t now = to_ms_since_boot(get_absolute_time());
        if (g_fs_last_ms == 0) g_fs_last_ms = now;
        const uint32_t dms = now - g_fs_last_ms;
        if (dms > 0) {
            g_fs_last_ms = now;
            // Stick to -1..1, Y up-positive to match atan2f(-x, y) from forward.
            const float sx =  ((float) g_fs_stick_x - 128.0f) / 127.0f;
            const float sy = -((float) g_fs_stick_y - 128.0f) / 127.0f;
            const float yaw = flick_stick_step(sx, sy, (float) dms / 1000.0f);
            if (yaw != 0.0f) {
                // radians -> mouse counts using THIS GAME's calibration.
                //
                // NEGATED: the spec's atan2(-x, y) measures anticlockwise from
                // forward, so a flick to the RIGHT is -90 degrees while mouse +X
                // moves the view right.
                //
                // The REMAINDER is carried. A flick is delivered over ~100 ticks
                // and each one was truncated toward zero, so every flick lost
                // about half a count per tick - roughly 2.8 degrees on a 180 at
                // 6500 counts/360, always in the same direction, which showed up
                // as the view drifting after a few flicks. Ease-out makes it
                // worse: the tail ticks are fractions of a count and truncated
                // away entirely. Same fix as the gyro accumulator, which was
                // built for exactly this and which this conversion bypassed.
                const float counts = -(yaw / 6.28318531f) * (float) cfg.flick_counts_360
                                   + g_fs_count_rem;
                const int32_t whole = (int32_t) counts;
                g_fs_count_rem = counts - (float) whole;
                g_gm_pend_x += whole;
            }
        }
    }

    // --- Touchpad to mouse --------------------------------------------------
    // Relative, trackpad style: the pointer follows how far the finger MOVED
    // since the last tick. Deltas are computed from the finger POSITION rather
    // than accumulated in the report path, so a dropped report costs nothing -
    // the next tick just sees a bigger gap.
    if (cfg.touch_mouse >= 1) {
        static uint16_t s_px = 0, s_py = 0;
        static bool     s_had = false;
        static float    s_rx = 0.0f, s_ry = 0.0f;   // sub-count carry
        static float    s_vx = 0.0f, s_vy = 0.0f;   // trackball glide
        const bool down = g_tm_down;
        const uint16_t cx = g_tm_x, cy = g_tm_y;

        float dx = 0.0f, dy = 0.0f;
        if (down) {
            if (s_had) {
                dx = (float) ((int32_t) cx - (int32_t) s_px);
                dy = (float) ((int32_t) cy - (int32_t) s_py);
            }
            // A FRESH TOUCH must not move the pointer. Without this the first
            // tick of every touch jumps by the distance between where the last
            // finger left and where this one landed - which is the width of the
            // pad if you lift and reposition, exactly what a trackpad user does
            // constantly.
            s_px = cx; s_py = cy; s_had = true;
        } else {
            s_had = false;
        }

        // Jitter floor. A resting finger still wobbles a count or two, and
        // without this the pointer drifts while you are not touching anything.
        const float minmove = (float) cfg.touch_mouse_min;
        if (dx * dx + dy * dy < minmove * minmove) { dx = 0.0f; dy = 0.0f; }

        if (cfg.touch_mouse_invert & 1) dx = -dx;
        if (cfg.touch_mouse_invert & 2) dy = -dy;

        const float sens = (float) cfg.touch_mouse_sens / 100.0f;
        float mx = dx * sens, my = dy * sens;

        if (cfg.touch_mouse_trackball) {
            // TIME-BASED, not per-tick. This task runs every main-loop pass -
            // far more often than reports arrive - so decaying by the friction
            // once per tick killed the glide within milliseconds and the
            // pointer stopped dead on release. Speed is now counts per SECOND
            // and both the glide and its decay are scaled by real elapsed time.
            static uint32_t s_tick_ms = 0, s_move_ms = 0;
            const uint32_t now = to_ms_since_boot(get_absolute_time());
            if (s_tick_ms == 0) s_tick_ms = now;
            if (s_move_ms == 0) s_move_ms = now;
            uint32_t dms = now - s_tick_ms;
            if (dms > 100) dms = 100;              // a stall must not launch the pointer
            s_tick_ms = now;

            if (down) {
                if (dx != 0.0f || dy != 0.0f) {
                    // Speed of the last real movement, from the time between
                    // MOVEMENTS rather than between ticks - most ticks carry no
                    // new report and would read as infinite speed.
                    const uint32_t mdt = (now > s_move_ms) ? (now - s_move_ms) : 1;
                    s_vx = mx * 1000.0f / (float) mdt;
                    s_vy = my * 1000.0f / (float) mdt;
                    s_move_ms = now;
                }
            } else if (s_vx != 0.0f || s_vy != 0.0f) {
                // Coast, so a flick can cross a large screen in one gesture.
                mx = s_vx * (float) dms / 1000.0f;
                my = s_vy * (float) dms / 1000.0f;
                // Friction is the fraction of speed shed every 100 ms, so the
                // feel does not change with the loop rate.
                float keep = 1.0f - ((float) cfg.touch_mouse_friction / 100.0f)
                                    * ((float) dms / 100.0f);
                if (keep < 0.0f) keep = 0.0f;
                s_vx *= keep; s_vy *= keep;
                if (s_vx * s_vx + s_vy * s_vy < 4.0f) { s_vx = 0.0f; s_vy = 0.0f; }
                s_move_ms = now;
            }
        } else if (!down) {
            s_vx = 0.0f; s_vy = 0.0f;
        }

        // Same sub-count carry as the stick and gyro paths: slow movement is
        // fractions of a count per tick and truncating it away loses it.
        const float fx = mx + s_rx, fy = my + s_ry;
        const int32_t ix = (int32_t) fx, iy = (int32_t) fy;
        s_rx = fx - (float) ix; s_ry = fy - (float) iy;
        g_gm_pend_x += ix;
        g_gm_pend_y += iy;
    }

    // --- Stick to mouse -----------------------------------------------------
    // Runs every tick so movement is smooth and time-based rather than tied to
    // however often reports happen to arrive.
    if (cfg.stick_mouse >= 1) {
        const uint32_t now = to_ms_since_boot(get_absolute_time());
        if (g_sm_last_ms == 0) g_sm_last_ms = now;
        const uint32_t dms = now - g_sm_last_ms;
        if (dms > 0) {
            g_sm_last_ms = now;
            float sx = ((float) g_sm_stick_x - 128.0f) / 127.0f;
            float sy = ((float) g_sm_stick_y - 128.0f) / 127.0f;
            if (cfg.stick_mouse_invert & 1) sx = -sx;
            if (cfg.stick_mouse_invert & 2) sy = -sy;
            // RADIAL deadzone, not per-axis: a per-axis one squares off the
            // centre, so a diagonal push just past the threshold jumps.
            const float mag = sqrtf(sx * sx + sy * sy);
            const float dz = (float) cfg.stick_mouse_deadzone / 100.0f;
            if (mag > dz && mag > 0.0001f) {
                // Rescale past the deadzone so travel starts from zero rather
                // than jumping to the deadzone value, then apply the curve to
                // the MAGNITUDE and put the direction back. Curving each axis
                // separately would bend diagonals toward the axes.
                float t = (mag - dz) / (1.0f - dz);
                if (t > 1.0f) t = 1.0f;
                const float curve = (float) cfg.stick_mouse_curve / 10.0f;
                const float scaled = powf(t, curve);
                const float sens_x = (cfg.stick_mouse_sens ? (float) cfg.stick_mouse_sens
                                                           : STICK_MOUSE_SENS_DEFAULT);
                // 0 means "follow X", so one knob still works. The curve and the
                // deadzone stay on the MAGNITUDE - only the final speed is per
                // axis, so a diagonal keeps its direction and simply travels
                // further horizontally than vertically, which is the point.
                const float sens_y = (cfg.stick_mouse_sens_y ? (float) cfg.stick_mouse_sens_y
                                                             : sens_x);
                const float step = scaled * ((float) dms / 1000.0f);
                const float ux = sx / mag, uy = sy / mag;
                const float fx = ux * step * sens_x + g_sm_rem_x;
                const float fy = uy * step * sens_y + g_sm_rem_y;
                const int32_t ix = (int32_t) fx, iy = (int32_t) fy;
                g_sm_rem_x = fx - (float) ix;
                g_sm_rem_y = fy - (float) iy;
                // Y passes through unnegated: stick Y is 0 = up and screen Y
                // grows downward, so the two already agree - the same
                // convention the gyro path documents below.
                g_gm_pend_x += ix;
                g_gm_pend_y += iy;
            } else {
                g_sm_rem_x = 0.0f; g_sm_rem_y = 0.0f;   // inside the deadzone: no creep
            }
        }
    }

    // Feed the calibration burst into the same pending counters the gyro uses,
    // so it goes out through exactly the path being calibrated.
    if (g_cal_remaining != 0) {
        const int32_t step = (g_cal_remaining > 0)
                           ? (g_cal_remaining <  CAL_STEP ? g_cal_remaining :  CAL_STEP)
                           : (g_cal_remaining > -CAL_STEP ? g_cal_remaining : -CAL_STEP);
        g_gm_pend_x    += step;
        g_cal_remaining -= step;
    }

    int32_t dx = g_gm_pend_x, dy = g_gm_pend_y;
    const int8_t pending = macro_mouse_peek_scroll();
    // Send when anything changed: movement, a scroll tick, or a button going
    // down OR up. Returning early on "no movement" would strand a release and
    // leave the click held at the host.
    if (dx == 0 && dy == 0 && pending == 0 && mbtn == s_mbtn_sent) return;
    const uint8_t inst = usb_mouse_instance(cfg);
    if (!tud_hid_n_ready(inst)) return;      // busy: keep it pending, lose nothing
    // Consume the scroll only now that the report is definitely going out. Taking
    // it before the readiness check dropped the tick whenever the endpoint was
    // busy - a scroll that silently does nothing under load.
    const int8_t wheel = macro_mouse_take_scroll();
    g_gm_pend_x -= dx;
    g_gm_pend_y -= dy;
    if (dx >  32767) dx =  32767; if (dx < -32767) dx = -32767;
    if (dy >  32767) dy =  32767; if (dy < -32767) dy = -32767;
    // dy already carries the stick path's sign convention, where aim-up is
    // NEGATIVE (stick Y: 0 = up, 255 = down). Screen Y also grows downward, so
    // the two agree and dy passes through unchanged. Negating it here - which the
    // first version did, reasoning about screen coordinates alone - inverted the
    // vertical axis relative to stick mode and to the Invert gyro aim setting.
    GyroMouseReport r{mbtn, (int16_t) dx, (int16_t) dy, wheel};
    tud_hid_n_report(inst, 0, &r, sizeof(r));
    s_mbtn_sent = mbtn;
}

static inline void __not_in_flash_func(apply_gyro_stick)(uint8_t *d) {
    const auto &cfg = get_config();
    // Right-stick inversion (v1.18.21): flip the PHYSICAL stick axes first, so it
    // applies whether or not gyro is on (this runs before every gyro-mode early
    // return, and the call sites invoke it unconditionally). Center ~128; 255-v is
    // the standard stick flip (1-LSB center offset, imperceptible). The gyro delta,
    // with its own gyro_invert, is added on top below - the two stay independent.
    if (cfg.rstick_invert & 1) d[2] = (uint8_t)(255 - d[2]); // RightStickX
    if (cfg.rstick_invert & 2) d[3] = (uint8_t)(255 - d[3]); // RightStickY

    // Flick Stick is a STICK feature, not a gyro one, so it must run before every
    // gyro-mode early return below - exactly as the inversion above does. Placed
    // after them it did nothing whenever gyro was off or its activation gate was
    // shut (mode 1 needs L2 held, 3 needs the touchpad, and so on), which is why
    // the right stick appeared completely dead.
    if (cfg.gyro_output == 2) {
        g_fs_stick_x = d[2];
        g_fs_stick_y = d[3];
        d[2] = 128; d[3] = 128;   // the game must not also turn from the stick
    }

    // Stick to mouse. Same placement and reasoning as Flick Stick above: it runs
    // before every gyro-mode early return, because this is a STICK feature and
    // must work with gyro off. The chosen stick is centred in the report so the
    // game does not also turn from it, and the conversion happens in
    // gyro_mouse_task() where the mouse report and its remainder carry live.
    if (cfg.stick_mouse == 1) {
        g_sm_stick_x = d[2]; g_sm_stick_y = d[3];   // right
        d[2] = 128; d[3] = 128;
    } else if (cfg.stick_mouse == 2) {
        g_sm_stick_x = d[0]; g_sm_stick_y = d[1];   // left
        d[0] = 128; d[1] = 128;
    }

    // Touchpad to mouse. Finger 1 only: byte 33 bit 7 clear means down, and the
    // 12-bit X / 12-bit Y are packed across bytes 34-36. The touch data is NOT
    // stripped from the report - the pad-click halves are macro triggers and a
    // game may use the pad itself, so this only reads.
    if (cfg.touch_mouse >= 1) {
        // Use the SHARED decoder rather than unpacking the bytes again here.
        // touch_point() is what macro.cpp's gestures and pad-click halves
        // already read, so there is one definition of "where the finger is" and
        // a second hand-copied one cannot drift from it.
        const TouchPoint f = touch_point(d, 63, 0);
        g_tm_x    = f.x;
        g_tm_y    = f.y;
        g_tm_down = f.down;
    }

    // Accelerometer work runs BEFORE the gyro early-return below. Tilt steering
    // has nothing to do with gyro aiming and a racing profile is exactly where
    // gyro is switched off - leaving it down there meant the feature could never
    // run in the only situation it exists for.
    auto rd_i16 = [&](int off) -> int32_t {
        return (int16_t)((uint16_t)d[off] | ((uint16_t)d[off + 1] << 8));
    };
    // ACCELEROMETER: bytes 21/23/25, immediately after the three gyro axes.
    // At rest it reads gravity, which is what makes it useful - it tells us
    // which way is DOWN, something the gyro alone can never know.
    const int32_t ax = rd_i16(21), ay = rd_i16(23), az = rd_i16(25);
    { extern volatile int16_t g_diag_ax, g_diag_ay, g_diag_az;
      g_diag_ax = (int16_t) ax; g_diag_ay = (int16_t) ay; g_diag_az = (int16_t) az; }

    // Gravity, low-passed out of the accelerometer. Shared by player-space aim
    // and tilt steering - both need to know which way is down, and filtering it
    // twice would be two filters drifting apart.
    static float gvx = 0.0f, gvy = 0.0f, gvz = 0.0f;
    if (cfg.gyro_axis == 2 || cfg.tilt_steer) {
        constexpr float A = 0.02f;                 // ~1s settle at report rate
        gvx += A * ((float) ax - gvx);
        gvy += A * ((float) ay - gvy);
        gvz += A * ((float) az - gvz);
    }

    // --- Tilt steering ------------------------------------------------------
    // Roll the pad like a small wheel and it ADDS to the left stick's X. It does
    // not replace it: at full lock there is nothing left to add, and with the
    // stick centred the offset falls inside the game's own dead zone, so driving
    // straight does not wander. What is left is the middle of the range, which
    // is exactly where a stick is hardest to be precise with.
    if (cfg.tilt_steer) {
        extern volatile int16_t g_diag_tilt_deg, g_diag_tilt_add;
        extern volatile uint8_t g_diag_tilt_ran;
        const float mag = sqrtf(gvx * gvx + gvy * gvy + gvz * gvz);
        g_diag_tilt_ran = (mag > 1000.0f) ? 1 : 2;
        if (mag <= 1000.0f) { g_diag_tilt_deg = 0; g_diag_tilt_add = 0; }
        if (mag > 1000.0f) {
            // Roll from gravity. Flat reads 0; rolling sideways swings gravity
            // from the down axis into the lateral one. Absolute, from gravity,
            // so it never drifts and always returns to centre when levelled -
            // which integrating the gyro for an angle could not do.
            // Negated: hardware testing showed the raw sign steers the wrong
            // way, so the DEFAULT has to be the correct one and "Invert" has to
            // mean inverted. Shipping it the other way round makes every new
            // user tick a box to get the obvious behaviour.
            float deg = -atan2f(gvx, gvy) * 57.2957795f;
            if (cfg.tilt_steer_invert) deg = -deg;
            const float dz = (float) cfg.tilt_steer_deadzone;
            if (deg > dz)       deg -= dz;
            else if (deg < -dz) deg += dz;
            else                deg = 0.0f;
            const float range = (float) cfg.tilt_steer_range;
            float f = deg / (range > 1.0f ? range : 1.0f);
            if (f > 1.0f) f = 1.0f; else if (f < -1.0f) f = -1.0f;
            const int32_t add = (int32_t) (f * 127.0f * ((float) cfg.tilt_steer_amount / 100.0f));
            g_diag_tilt_deg = (int16_t) deg;
            g_diag_tilt_add = (int16_t) add;
            int32_t v = (int32_t) d[0] + add;
            if (v < 0) v = 0; else if (v > 255) v = 255;
            d[0] = (uint8_t) v;

            // VERTICAL tilt, its own switch. Leaning the pad forward and back
            // is what a bike wants for weight shift; a car does not, so this
            // cannot ride along with the horizontal axis. Range and dead zone
            // are shared - the wrist movement is the same size either way - but
            // the amount is separate, because how much lean a game wants is not
            // how much steering it wants.
            if (cfg.tilt_steer_y) {
                extern volatile int16_t g_diag_tilt_ydeg, g_diag_tilt_yadd;
                float ydeg = -atan2f(gvz, gvy) * 57.2957795f;
                if (cfg.tilt_steer_y_invert) ydeg = -ydeg;
                if (ydeg > dz)       ydeg -= dz;
                else if (ydeg < -dz) ydeg += dz;
                else                 ydeg = 0.0f;
                float yf = ydeg / (range > 1.0f ? range : 1.0f);
                if (yf > 1.0f) yf = 1.0f; else if (yf < -1.0f) yf = -1.0f;
                const int32_t yadd = (int32_t) (yf * 127.0f * ((float) cfg.tilt_steer_y_amount / 100.0f));
                g_diag_tilt_ydeg = (int16_t) ydeg;
                g_diag_tilt_yadd = (int16_t) yadd;
                int32_t vy = (int32_t) d[1] + yadd;
                if (vy < 0) vy = 0; else if (vy > 255) vy = 255;
                d[1] = (uint8_t) vy;
            }
        }
    }


    if (cfg.gyro_mode == 0) return;
    // Activation schemes (industry set: ADS-gated, always-on, touch-enable, ratchet):
    //   1 = only while L2 (aim) held past ~12%
    //   2 = always on
    //   3 = only while the touchpad is touched (Steam 'touch to enable' style)
    //   4 = always on, touching the touchpad PAUSES gyro (ratchet: re-center like
    //       lifting a mouse)
    const bool touch = !(d[32] & 0x80);            // touchpad finger 1 down
    if (cfg.gyro_mode == 1 && d[4] < 30) return;                 // L2 held (aim)
    if (cfg.gyro_mode == 3 && !touch)    return;
    if (cfg.gyro_mode == 4 && touch)     return;
    // v1.11.0: additional gates for games that don't aim on L2. Same 30-count
    // threshold for the R2 analog gate; shoulders are digital (bit0=L1, bit1=R1).
    if (cfg.gyro_mode == 5 && d[5] < 30)         return;         // R2 held
    if (cfg.gyro_mode == 6 && !(d[8] & 0x01))    return;         // L1 held
    if (cfg.gyro_mode == 7 && !(d[8] & 0x02))    return;         // R1 held
    // A motion macro's gate is held: the same wrist movement that draws the
    // gesture would otherwise also swing the aim. Same idea as gyro_mode 4
    // pausing while the touchpad is touched.
    if (macro_motion_capturing()) return;
    auto rd16 = [&](int off) -> int32_t {
        return (int16_t)((uint16_t)d[off] | ((uint16_t)d[off + 1] << 8));
    };
    int32_t pitch = rd16(15);
    // Hardware-verified axis mapping (v1.0.6): on the DualSense the horizontal
    // "turn the controller" motion shows up on the int16 at byte 17, NOT byte 19
    // as the wiki field names suggested — user testing showed 19 gives no
    // horizontal response while 17 tracks turning. So: yaw = 17, roll = 19.
    int32_t horiz;
    if (cfg.gyro_axis == 2) {
        // PLAYER SPACE. Yaw and roll both turn the view horizontally, and which
        // one does the work depends entirely on how far the pad is tilted -
        // which is why picking one by hand means holding the controller a fixed
        // way. Projecting the rotation onto the DOWN vector asks the question
        // that actually matters: how much did this movement turn the player
        // about the world's vertical axis? Flat or tilted up, it answers the
        // same, so the aim stops depending on wrist angle.
        //
        // Gravity is low-passed out of the accelerometer: a shake is transient,
        // gravity is not, and steering off raw acceleration would make the aim
        // jump every time the pad is knocked.
        const float gx = gvx, gy = gvy, gz = gvz;
        const float mag = sqrtf(gx * gx + gy * gy + gz * gz);
        if (mag > 1000.0f) {                       // sane gravity, not free-fall
            const float ux = gx / mag, uy = gy / mag, uz = gz / mag;
            // Dot the angular velocity with the down vector. Sign convention
            // follows the yaw axis so an upright pad behaves as it always has.
            const float dot = (float) rd16(15) * ux
                            + (float) rd16(17) * uy
                            + (float) rd16(19) * uz;
            horiz = (int32_t) dot;
            // BOTH axes have to be corrected, not just this one. Fixing only
            // the horizontal left the vertical reading the raw pitch byte, and
            // once the pad is tilted a horizontal turn also rotates about the
            // controller's own pitch axis - so a left-right sweep dragged the
            // cursor diagonally. Removing the world-vertical part of the
            // rotation leaves exactly the pitch that is NOT turning, which is
            // what the vertical axis should follow.
            pitch = (int32_t) ((float) rd16(15) - ux * dot);
        } else {
            horiz = rd16(17);                      // fall back to plain yaw
        }
    } else {
        horiz = (cfg.gyro_axis == 1) ? rd16(19) /* roll */ : rd16(17) /* yaw */;
    }
    // Live diagnostic (portal): |horiz| raw magnitude, pre-deadzone, whenever gyro
    // is enabled — lets sensitivity be calibrated against real numbers.
    { extern volatile uint16_t g_diag_gyro;
      int32_t ah = horiz < 0 ? -horiz : horiz;
      g_diag_gyro = (ah > 65535) ? 65535 : (uint16_t)ah; }
    // Small deadzone against sensor noise/bias at rest.
    if (horiz > -12 && horiz < 12) horiz = 0;
    if (pitch > -12 && pitch < 12) pitch = 0;
    if (horiz == 0 && pitch == 0) return;
    // Scale: sens 1-100, divisor 200 (v1.0.6: 10x more range after "100 felt too
    // low" on hardware — the old maximum now sits around slider value 10).
    const int32_t s  = cfg.gyro_sens;
    // 0 means "follow X", so a config written before this field existed - where
    // the tail is 0xFF and clamps to 0 - behaves exactly as it always did.
    const int32_t sy = cfg.gyro_sens_y ? cfg.gyro_sens_y : s;
    int32_t dx = -horiz * s  / 200;   // turn controller right -> aim right
    int32_t dy = -pitch * sy / 200;   // tilt up -> aim up (flip via invert if wrong)
    if (cfg.gyro_invert & 1) dx = -dx;
    if (cfg.gyro_invert & 2) dy = -dy;
    if (usb_mouse_iface_needed(cfg)) {
        gyro_emit_mouse(horiz, pitch, cfg);   // stick already captured above
        return;
    }
    int32_t rx = (int32_t)d[2] + dx;
    int32_t ry = (int32_t)d[3] + dy;
    d[2] = (uint8_t)(rx < 0 ? 0 : (rx > 255 ? 255 : rx));
    d[3] = (uint8_t)(ry < 0 ? 0 : (ry > 255 ? 255 : ry));
}

void __not_in_flash_func(on_bt_data)(CHANNEL_TYPE channel, uint8_t *data, uint16_t len) {
    // printf("[Main] BT data callback: channel=%u len=%u\n", channel, len);
    if (channel == INTERRUPT && len > 2 && data[1] == 0x31) {
        // Mic audio: controller signals mic payload via bit1 of data[2];
        // the opus-encoded mic frame starts at data+4.
        if ((data[2] >> 1) & 1) {
            if (len >= 4) {
                mic_add_queue(data + 4, len - 4);
            }
            return;
        }
        if ((data[56] & 1) != (interrupt_in_data[53] & 1)) {
            set_headset(data[56] & 1);
        }

        // Wake-on-PS must observe every BT input report regardless of polling
        // mode: the wake feature has its own state to maintain (button-byte
        // diff for edge detection) and short-circuiting it on non-2 polling
        // modes silently breaks wake while the host is suspended.
        wake_on_bt_input(data + 3, len - 3);
        #ifdef ENABLE_WAKE_HID
        ps_shortcut_tick(data + 3, len - 3);
        macro_on_input(data + 3, len - 3);
        #endif

        if (get_config().polling_rate_mode != 2) {
            memcpy(interrupt_in_data, data + 3, 63);
            apply_gyro_stick(interrupt_in_data);
            { extern volatile uint8_t g_l2_pos, g_r2_pos, g_l1_btn, g_r1_btn; g_l2_pos = interrupt_in_data[4]; g_r2_pos = interrupt_in_data[5]; g_l1_btn = (interrupt_in_data[8] & 0x01) ? 1 : 0; g_r1_btn = (interrupt_in_data[8] & 0x02) ? 1 : 0; } // L2@4 R2@5 L1/R1@8
#if ENABLE_BATT_LED
            battery_led_note_report();
#endif
            return;
        }

        // We add the critical section here to avoid any race conditions when writing to the interrupt_in_data buffer,
        // which is shared between the main loop and this callback.
        // The critical section ensures that only one thread can access the buffer at a time,
        // preventing data corruption and ensuring thread safety.
        // We also set the report_dirty flag to true to indicate that new data is available
        //  and needs to be sent in the next interrupt report.
        critical_section_enter_blocking(&report_cs);
        memcpy(interrupt_in_data, data + 3, 63);
        apply_gyro_stick(interrupt_in_data);
        report_dirty = true;
        critical_section_exit(&report_cs);
        { extern volatile uint8_t g_l2_pos, g_r2_pos, g_l1_btn, g_r1_btn; g_l2_pos = data[3 + 4]; g_r2_pos = data[3 + 5]; g_l1_btn = (data[3 + 8] & 0x01) ? 1 : 0; g_r1_btn = (data[3 + 8] & 0x02) ? 1 : 0; } // L2@4 R2@5 L1/R1@8
#if ENABLE_BATT_LED
        battery_led_note_report();
#endif
    }
}

// Invoked when received GET_REPORT control request
// Application must fill buffer report's content and return its length.
// Return zero will cause the stack to STALL request
uint16_t tud_hid_get_report_cb(uint8_t itf, uint8_t report_id, hid_report_type_t report_type, uint8_t *buffer,
                               uint16_t reqlen) {
#ifdef ENABLE_WAKE_HID
    if (itf == 1) {
        if (reqlen >= 8) {
            memset(buffer, 0, 8);
            return 8;
        }
        return 0;
    }
#endif
    (void) itf;
    (void) report_id;
    (void) report_type;
    (void) buffer;
    (void) reqlen;

    // DSE profiles: while the unlock + prefetch is still in progress, return 0
    // (NAK) for profile reads so the PS app retries rather than caching an
    // empty snapshot. Still kick off the background BT fetch.
    if (dse_is_profile_report(report_id) && !dse_profiles_ready()) {
        get_feature_data(report_id, reqlen);
        return 0;
    }

    std::vector<uint8_t> feature_data = get_feature_data(report_id, reqlen);
    if (!feature_data.empty()) {
        // 0x81 (portal command replies) and 0x82 (slot-command replies, split off
        // to dodge the portal's 0x81 diagnostic poll) both carry a full 0x66-framed
        // reply that must be returned VERBATIM. Every other report id is a native
        // report whose stored leading byte is the report id and gets stripped.
        // CLAMP to reqlen in every path: TinyUSB sizes the transfer buffer from
        // the DESCRIPTOR-declared report size. Copying more than reqlen is a
        // buffer overflow in the USB stack (this is exactly how routing 63-byte
        // slot replies through 0x82 - declared as a 9-byte report - corrupted
        // reads and threw errors in WebHID). Slot replies live on 0x84, whose
        // declared size is the full 63 bytes.
        if ((report_id == 0x81 || report_id == 0x84) && feature_data[0] == 0x66) {
            const uint16_t n = (uint16_t)((feature_data.size() < reqlen) ? feature_data.size() : reqlen);
            memcpy(buffer, feature_data.data(), n);
            return n;
        }
        const uint16_t n = (uint16_t)(((feature_data.size() - 1) < reqlen) ? (feature_data.size() - 1) : reqlen);
        memcpy(buffer, feature_data.data() + 1, n);
        return n;
    }

    return 0;
}

bool tud_audio_set_itf_cb(uint8_t rhport, tusb_control_request_t const *p_request) {
    (void) rhport;
    uint8_t const itf = tu_u16_low(p_request->wIndex); // wInterface
    uint8_t const alt = tu_u16_low(p_request->wValue); // bAlternateSetting

    if (itf == 1) {
        printf("[AUDIO] Set interface Speaker to alternate setting %d\n", alt);
        spk_active = alt;
    }
    if (itf == 2) { // ITF_NUM_AUDIO_STREAMING_IN (microphone)
        printf("[AUDIO] Set interface Microphone to alternate setting %d\n", alt);
        set_mic_active(alt);
    }

    return true;
}

// Invoked when received SET_REPORT control request or
// received data on OUT endpoint ( Report ID = 0, Type = 0 )
void tud_hid_set_report_cb(uint8_t itf, uint8_t report_id, hid_report_type_t report_type, uint8_t const *buffer,
                           uint16_t bufsize) {
#ifdef ENABLE_WAKE_HID
    if (itf == 1) {
        // Drop keyboard SET_REPORT (host LED state).
        return;
    }
#endif
    (void) itf;
    (void) report_id;
    (void) report_type;
    (void) buffer;
    (void) bufsize;

    // INTERRUPT OUT
    if (report_id == 0) {
        switch (buffer[0]) {
            case 0x02: {
                bool changed = state_update(buffer + 1, bufsize - 1);
                if (spk_active && !changed) {
                    break;
                }
                uint8_t outputData[78]{};
                outputData[0] = 0x31;
                outputData[1] = reportSeqCounter << 4;
                if (++reportSeqCounter == 256) {
                    reportSeqCounter = 0;
                }
                outputData[2] = 0x10;
                // memcpy(outputData + 3, buffer + 1, bufsize - 1);
                state_set(outputData + 3, sizeof(SetStateData));
                bt_write(outputData, sizeof(outputData));
                break;
            }
        }
    }
    if (report_id == 0x80 && bufsize >= 2 && buffer[0] == 0x66) {
#if ENABLE_VERBOSE
        printf("[HID] Receive 0x66 setting config, funcid:0x%02X\n", buffer[1]);
#endif

        // 0x80 0x66 cmd_id payload...
        pico_cmd_set(buffer[1], buffer + 2, bufsize - 2);
        return;
    }
    if (report_id == 0x80 ||
        // DSE: Write Profile Block
        report_id == 0x60 ||
        report_id == 0x62 ||
        report_id == 0x61) {
        set_feature_data(report_id, const_cast<uint8_t *>(buffer), bufsize);
    }
}

int main() {
#if SYS_CLOCK_KHZ != 150000
    vreg_set_voltage(VREG_VOLTAGE_1_20);
    sleep_ms(1000);
    set_sys_clock_khz(SYS_CLOCK_KHZ, true);
#endif

    board_init();
    tusb_rhport_init_t dev_init = {
        .role = TUSB_ROLE_DEVICE,
        .speed = TUSB_SPEED_FULL
    };
    tusb_init(BOARD_TUD_RHPORT, &dev_init);
#if !ENABLE_SERIAL
    sleep_ms(150);
    tud_disconnect();
#endif
    board_init_after_tusb();
#if ENABLE_SERIAL
    stdio_usb_init();
#endif

    if (cyw43_arch_init()) {
        printf("Failed to initialize CYW43\n");
        return 1;
    }
    cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, false);

    // SMPS coil-whine fix: at light load the on-board buck regulator drops into PFM
    // (power-save) mode, and its pulse-skipping repetition rate falls into the
    // audible band -> the board whines at idle. Driving the CYW43 SMPS power-save
    // control pin (WL_GPIO1 on the Pico 2 W / Pico W) HIGH forces continuous PWM,
    // which has lower 3V3 ripple at light load and silences the whine. No-op on
    // boards without the pin. (From awalol PR #207, independent of Wake-on-LAN.)
#ifndef CYW43_WL_GPIO_SMPS_PIN
#define CYW43_WL_GPIO_SMPS_PIN 1   // WL_GPIO1 on Pico W / Pico 2 W
#endif
    cyw43_arch_gpio_put(CYW43_WL_GPIO_SMPS_PIN, true);

#if ENABLE_BATT_LED
    battery_led_init();
#endif

#if !ENABLE_SERIAL
    if (watchdog_caused_reboot()) {
        printf("Rebooted by Watchdog!\n");
        // 当崩溃重启以后，闪三下灯
        for (int i = 0; i < 6; i++) {
            if (i % 2 == 0) {
                cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, true);
            } else {
                cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, false);
            }
            sleep_ms(500);
        }
    } else {
        printf("Clean boot\n");
    }
#endif

    // Initialize the critical section for the report buffer
    critical_section_init(&report_cs);
    wake_init();

    config_load();

    bt_init();
    bt_register_data_callback(on_bt_data);

    audio_init();
    state_init();

#if !ENABLE_SERIAL
    watchdog_enable(1000, true);
#endif

    while (1) {
#if !ENABLE_SERIAL
        watchdog_update();
#endif
        // Synth tick: with the host quiet, gated adaptive triggers must still
        // engage/release from live trigger movement, and releases must actually
        // reach the controller (fixes triggers stuck in resistance after rapid
        // R2/L2 play in games that only send reports when rumble changes).
        // Host just went to sleep: actively release the triggers ONCE before
        // standing down, so nothing stays latched on the controller through the
        // sleep and across the deferred power-off the wake path relies on.
        {
            static bool was_suspended = false;
            const bool susp = wake_host_is_suspended();
            if (susp && !was_suspended && state_release_for_suspend()) {
                uint8_t outputData[78]{};
                outputData[0] = 0x31;
                outputData[1] = reportSeqCounter << 4;
                if (++reportSeqCounter == 256) reportSeqCounter = 0;
                outputData[2] = 0x10;
                state_set(outputData + 3, sizeof(SetStateData));
                bt_write(outputData, sizeof(outputData));
            }
            #ifdef ENABLE_WAKE_HID
            // Keyboard-side twin of state_release_for_suspend(). Doing this
            // lazily on the next input report is not enough: if the controller
            // goes quiet no report arrives, and a combo caught mid-playback
            // leaves a modifier latched at the host across the whole sleep.
            if (susp && !was_suspended) macro_reset();
            #endif
            was_suspended = susp;
        }
        {
            static uint32_t last_synth_tick_ms = 0;
            const uint32_t now = to_ms_since_boot(get_absolute_time());
            // 8 ms, not 50: a custom-effect stage sequence latches on trigger
            // POSITION, and a pull takes ~100-200 ms, so a 50 ms cadence gave only
            // 2-4 samples per pull and routinely skipped a stage's arming window
            // ("sometimes I get the wall, sometimes I don't"). The call is cheap -
            // it early-returns unless the host has gone quiet, and only pushes a
            // report when the composed state actually changes.
            // While the host is SUSPENDED there is nothing to synthesize for, and
            // the extra BT output traffic competes with the input reports that
            // wake-on-PS has to observe - so stand down completely until resume.
            // (Raising this cadence from 50 ms without that guard is what made
            // wake less reliable than 1.13.3.)
            // Interval is re-evaluated EVERY pass (cheap), so trigger movement
            // restores the fast cadence immediately; only the tick is rate-limited.
            if (!wake_host_is_suspended() &&
                now - last_synth_tick_ms >= state_synth_interval_ms()) {
                last_synth_tick_ms = now;
                // ...or when a battery notification is running. state_synth_tick()
                // returns false until the HOST has sent an output report, so on an
                // idle desktop with no game driving the controller nothing ever
                // composed a report - the notification fired in firmware and never
                // reached the lightbar. state_set() builds from the state module's
                // own struct, not from the host cache, so it is safe to compose
                // one for this alone.
                if (state_synth_tick() || battery_notify_wants_report()) {
                    uint8_t outputData[78]{};
                    outputData[0] = 0x31;
                    outputData[1] = reportSeqCounter << 4;
                    if (++reportSeqCounter == 256) reportSeqCounter = 0;
                    outputData[2] = 0x10;
                    state_set(outputData + 3, sizeof(SetStateData));
                    bt_write(outputData, sizeof(outputData));
                }
            }
        }
        cyw43_arch_poll();
        tud_task();
        wake_task();
        // Drives the ordered playback walk and the long-press threshold. Without
        // this the engine would assert the first key of a combo and never
        // advance to release it - macro_on_input only STARTS a macro.
        #ifdef ENABLE_WAKE_HID
        macro_task();
        gyro_mouse_task();   // USB send happens HERE, never in the BT callback
        #endif
        audio_loop();
        interrupt_loop();
#if ENABLE_BATT_LED
        battery_led_tick();
#endif
        // Lightbar notification. Deliberately OUTSIDE ENABLE_BATT_LED: that flag
        // governs the Pico's onboard LED, and this drives the controller's
        // lightbar. They are separate indicators for separate distances and
        // either may be used without the other.
        battery_notify_tick();
        button_check();
        bt_inquiring_led();
        dse_task();
    }
}
