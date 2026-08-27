//
// Staged battery notification. See battery_notify.h.
//

#include "battery_notify.h"

#include "config.h"
#include "pico/time.h"

extern uint8_t interrupt_in_data[63];

namespace {

// A PULSE, not a blink: brightness fades up and back down. DS4Windows' low
// battery indicator does this and it reads as a gentle breath rather than a
// fault light, which is what a "you might want to charge soon" prompt should
// look like. A hard on/off at this size looks like something is wrong.
//
// One pulse is a fade up and a fade back down. Reports are composed about every
// 50 ms, so a 1.6 s pulse is ~32 brightness steps - smooth enough that the
// steps are not visible.
constexpr uint64_t PULSE_US = 1'600'000;
// Written back for this long after the last pulse. The controller latches its
// colour, so the restore has to be SENT; several reports go out in this window
// in case one is lost.
constexpr uint64_t TAIL_US  = 300'000;

// No fresh report for this long and the controller is treated as gone. Matches
// battery_led.cpp, which reads the same byte.
constexpr uint64_t REPORT_STALE_US = 2'000'000;

constexpr uint8_t POWER_STATE_DISCHARGING = 0x0;

// A stage that has fired stays fired until the battery climbs back ABOVE its
// level (a charge) - otherwise a reading sitting exactly on the boundary would
// re-announce itself every time it wobbled.
bool     s_fired[3]   = {false, false, false};
int8_t   s_stage      = -1;      // stage being shown, -1 = idle
uint8_t  s_left       = 0;       // blinks remaining
uint64_t s_phase_us   = 0;       // start of the current pulse
uint64_t s_tail_us    = 0;       // 0 = not in the restore tail
uint8_t  s_last_level = 0xFF;

}  // namespace

void battery_notify_on_disconnect(void) {
    s_stage  = -1;
    s_left   = 0;
    s_tail_us = 0;
    for (int i = 0; i < 3; i++) s_fired[i] = false;
    s_last_level = 0xFF;
}

bool battery_notify_wants_report(void) {
    if (s_stage >= 0) return true;
    if (s_tail_us == 0) return false;
    if ((time_us_64() - s_tail_us) >= TAIL_US) { s_tail_us = 0; return false; }
    return true;
}

// Triangular fade, 0..255 across the pulse. Cheap and, at 20 updates a second,
// indistinguishable from a smoother curve.
static uint8_t pulse_level(uint64_t elapsed) {
    if (elapsed >= PULSE_US) return 0;
    const uint64_t half = PULSE_US / 2;
    const uint64_t up   = (elapsed < half) ? elapsed : (PULSE_US - elapsed);
    return (uint8_t) ((up * 255u) / half);
}

bool battery_notify_override(uint8_t &r, uint8_t &g, uint8_t &b) {
    if (s_stage < 0) return false;             // tail: do not override
    const Config_body &c = get_config();
    const uint8_t lvl = pulse_level(time_us_64() - s_phase_us);
    r = (uint8_t) ((c.batt_stage_r[s_stage] * lvl) / 255);
    g = (uint8_t) ((c.batt_stage_g[s_stage] * lvl) / 255);
    b = (uint8_t) ((c.batt_stage_b[s_stage] * lvl) / 255);
    return true;
}

void battery_notify_test(uint8_t stage) {
    if (stage > 2) return;
    const Config_body &c = get_config();
    s_stage    = (int8_t) stage;
    s_left     = c.batt_stage_blinks[stage] ? c.batt_stage_blinks[stage] : 1;
    s_phase_us = time_us_64();
    s_tail_us  = 0;
    // A test must not consume the real notification: leave s_fired alone so the
    // stage still announces itself when the battery actually gets there.
}

void battery_notify_tick(void) {
    const Config_body &c = get_config();
    const uint64_t now = time_us_64();

    // A blink already running is allowed to finish even with the feature off,
    // so the Test button works before the master switch is ticked - which is
    // exactly when someone is choosing colours.
    if (!c.batt_notify_enable && s_stage < 0) return;

    // Advance a blink already in progress FIRST, so a battery reading that
    // changes mid-notification cannot cut it short.
    if (s_stage >= 0) {
        if ((now - s_phase_us) >= PULSE_US) {
            s_phase_us = now;
            if (s_left > 0) s_left--;
            if (s_left == 0) {
                s_stage   = -1;
                // Hand back deliberately: reports keep going out for the tail,
                // now WITHOUT the override, so the controller is written back to
                // the real colour instead of being left on the last frame.
                s_tail_us = now;
            }
        }
        return;
    }

    const uint8_t byte  = interrupt_in_data[52];
    const uint8_t level = byte & 0x0F;          // PowerPercent, 0-10
    const uint8_t state = (byte >> 4) & 0x0F;

    // Only while actually running down. Charging or complete says the user is
    // already doing the thing the notification would ask for.
    if (state != POWER_STATE_DISCHARGING) {
        for (int i = 0; i < 3; i++) s_fired[i] = false;
        s_last_level = level;
        return;
    }
    if (level > 10) return;                     // not a level we can read

    // Re-arm any stage the battery has climbed back above.
    for (int i = 0; i < 3; i++) {
        const uint8_t lv = c.batt_stage_level[i];
        if (lv != 0 && level > lv) s_fired[i] = false;
    }

    // First reading of a session only arms; it must not fire. Plugging in a
    // controller that is already at 20% should not immediately flash - the
    // notification marks a CROSSING, and there has been none yet.
    if (s_last_level == 0xFF) { s_last_level = level; return; }

    // Fire the LOWEST unfired stage the battery has reached. Lowest first so a
    // drop past two stages at once announces the more urgent one.
    int pick = -1;
    for (int i = 0; i < 3; i++) {
        const uint8_t lv = c.batt_stage_level[i];
        if (!c.batt_stage_on[i] || lv == 0 || s_fired[i] || level > lv) continue;
        if (pick < 0 || c.batt_stage_level[i] < c.batt_stage_level[pick]) pick = i;
    }
    s_last_level = level;
    if (pick < 0) return;

    s_fired[pick] = true;
    s_stage       = (int8_t) pick;
    s_left        = c.batt_stage_blinks[pick];
    s_phase_us    = now;
    s_tail_us     = 0;
}
