//
// Staged battery notification on the CONTROLLER's RGB lightbar.
//
// Distinct from battery_led.cpp, which blinks the Pico's own onboard LED and
// keeps blinking for as long as the battery is low. This is a PROMPT: when the
// battery falls past a stage's level it blinks that stage's colour a set number
// of times and then stops, handing the lightbar back to whatever was driving
// it. Nothing keeps flashing until you plug in.
//
// The two are independent and can both be on. The Pico LED is the one to use at
// a desk, where the dongle is in view; the lightbar is the one that carries
// across a room.
//

#pragma once

#include <cstdint>

// Call once per main-loop iteration. Reads the battery nibble from the cached
// input report and advances any blink in progress.
void battery_notify_tick(void);

// Call from the BT disconnect handler. Cancels a blink in progress and re-arms
// every stage, so a fresh connection reports honestly rather than staying
// silent because a stage fired on the previous one.
void battery_notify_on_disconnect(void);

// Run a stage's notification NOW, whatever the battery is doing. Colours and
// blink counts are impossible to judge without seeing them, and the alternative
// is draining a controller to 20% to find out that the yellow is too dim.
// Ignores the master enable so a stage can be tried before committing to it.
void battery_notify_test(uint8_t stage);

// True while the dongle must keep COMPOSING reports for this - during a pulse,
// and for a short tail afterwards.
//
// The tail is not optional. The controller LATCHES whatever colour it was last
// sent; it does not revert on its own. Simply ceasing to override leaves the
// last pulse frame lit forever unless something else happens to send a report,
// and on an idle desktop nothing does - which is the same reason the pulse
// needed its own report in the first place. The tail sends reports WITHOUT the
// override so the real colour is written back explicitly.
bool battery_notify_wants_report(void);

// True while the lightbar should be overridden, with the colour to write.
// False during the tail, so the composed report carries the colour the state
// module already holds and the controller is actively restored.
bool battery_notify_override(uint8_t &r, uint8_t &g, uint8_t &b);
