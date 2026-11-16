//
// Created by awalol on 2026/4/30.
//

#include "wake.h"

#ifdef ENABLE_WAKE_HID

#include <cstdio>
#include <cstring>
#include "bt.h"
#include "usb.h"
#include "tusb.h"
#include "device/dcd.h"
#include "pico/sync.h"
#include "hardware/structs/usb.h"
#include "hardware/address_mapped.h"
#include "pico/time.h"
#include "ps_shortcut.h"
#include "macro.h"
#include "config.h"


// Always 1. The interface layout is identical in both identities, so this can
// never shift underneath a sender — which is what made the previous approach
// produce garbage keystrokes.
#define WAKE_KBD_INSTANCE     1
#define WAKE_KEYCODE_F15      0x68
// Post-resume timings tuned for "wake-and-resleep" Windows behavior: the host
// resumes USB, but if no HID input is consumed during the brief wake window
// the system can re-suspend within ~1 s. Bigger settles + a second F15 give
// Windows multiple polling cycles to pick the keystroke up.
#define WAKE_SETTLE_US        150000   // 150 ms — let host finish USB re-init
#define WAKE_KEY_HOLD_US       80000   // 80 ms keydown -> keyup gap
#define WAKE_KEY_UP_SETTLE_US 200000   // 200 ms between attempts (or before DONE)
#define WAKE_REQUEST_TIMEOUT_US 5000000
#define WAKE_KEY_ATTEMPTS     2
#define WAKE_POWEROFF_DEBOUNCE_US 3000000 // 3s: only power off the controller after a
                                          // sustained suspend (real sleep); ignore brief
                                          // hub-induced suspends while the host is awake.
#define WAKE_RECONNECT_GRACE_US   5000000 // 5s: after a deliberate USB reconnect, ignore the
                                          // suspend it causes (it is not a host sleep).
                                          // Cleared early when the device re-mounts.

#ifdef WAKE_DEBUG
#  define WAKE_DBG(fmt, ...) printf("[wake] " fmt "\n", ##__VA_ARGS__)
static const char *wake_state_name(int s) {
    switch (s) {
    case 0: return "IDLE";
    case 1: return "PENDING_PRESS";
    case 2: return "REQUESTED";
    case 3: return "KEY_DOWN";
    case 4: return "KEY_UP_SENT";
    case 5: return "DONE";
    default: return "?";
    }
}
#else
#  define WAKE_DBG(fmt, ...) ((void)0)
#endif

typedef enum {
    WAKE_IDLE,
    WAKE_PENDING_PRESS,
    WAKE_REQUESTED,
    WAKE_KEY_DOWN,
    WAKE_KEY_UP_SENT,
    WAKE_DONE,
} wake_state_t;

static critical_section_t wake_cs;
static volatile bool host_suspended = false;

// ---- Wake diagnostics -------------------------------------------------------
// This failure is intermittent and cannot be reproduced on the bench, so record
// what the device actually observed and let the portal read it back after a bad
// sleep. Everything here is RAM only.
wake_diag_t g_wake_diag{};

// Cumulative counters cannot say WHICH sleep failed - "last wake refused" is
// usually just a button press after the user gave up and woke the PC by hand.
// So snapshot each suspend->resume cycle and keep the most recent one.
wake_cycle_t g_wake_cycle{};
static wake_cycle_t cur{};

// USB resume signalling. dcd_remote_wakeup() on RP2 SETS SIE_CTRL.RESUME and
// nothing in TinyUSB ever clears it, so the device keeps driving resume K
// indefinitely. The USB spec allows 1-15 ms; drive it past that and a host may
// disregard it entirely - and because the bit is already set, every LATER
// attempt writes a 1 over a 1 and produces no fresh edge, so once a wake is
// missed the port can never be woken again until something resets it. Terminate
// the drive after RESUME_DRIVE_US and the signal becomes spec-legal AND
// re-armable, which is what makes the retry below possible.
#define RESUME_DRIVE_US     10000     // 10 ms, inside the spec's 1-15 ms window
#define RESUME_RETRY_US    800000     // re-issue every 800 ms while still asleep
#define RESUME_MAX_TRIES        6
#define WAKE_HID_WAIT_US   3000000     // resumed but keyboard not ready: bounded wait
static uint64_t resume_drive_until = 0;
static uint64_t resume_last_try    = 0;
static uint8_t  resume_tries       = 0;
static volatile bool poll_recovered_pending = false;
// bt.h declares bt_is_connected() but nothing defines it, so track the link here
// from the connect/disconnect callbacks we already receive.
static volatile bool bt_link_up = false;

static void resume_drive_start(void) {
    dcd_remote_wakeup(0);
    resume_drive_until = time_us_64() + RESUME_DRIVE_US;
    resume_last_try    = time_us_64();
    if (resume_tries < 255) resume_tries++;
}
static void resume_drive_service(uint64_t now) {
    if (resume_drive_until && now >= resume_drive_until) {
        hw_clear_alias(usb_hw)->sie_ctrl = USB_SIE_CTRL_RESUME_BITS;  // end the K-state
        resume_drive_until = 0;
    }
}
// Read-only query for other subsystems: while the host is suspended there is
// nothing to synthesize for, and extra BT output traffic competes with the
// input reports wake-on-PS must observe.
bool wake_host_is_suspended(void) { return host_suspended; }
static volatile bool host_resumed_event = false;
static wake_state_t state = WAKE_IDLE;
static uint64_t state_entered_us = 0;
static uint8_t key_attempts = 0;
// Last-seen DualSense button bytes. Idle defaults: byte 7 = 0x08 (D-pad
// released), bytes 8 / 9 = 0 (no shoulders, no PS / touchpad / mute).
static uint8_t prev_b7 = 0x08;
static uint8_t prev_b8 = 0x00;
static uint8_t prev_b9 = 0x00;
// Debounced controller power-off: time of the pending suspend (0 = none pending).
static volatile uint64_t suspend_at_us = 0;
// During a deliberate USB reconnect, ignore the suspend it triggers until this time.
static volatile uint64_t reconnect_until_us = 0;

static void enter_state(wake_state_t s) {
    state = s;
    state_entered_us = time_us_64();
}

static void request_host_wake(const char *reason) {
    resume_tries = 0;                       // new wake episode
    bool ok = tud_remote_wakeup();
    if (ok) { resume_drive_until = time_us_64() + RESUME_DRIVE_US;
              resume_last_try = time_us_64(); resume_tries = 1; }
    g_wake_diag.wake_attempts++;
    if (host_suspended && cur.requests < 255) cur.requests++;
    if (host_suspended && ok && cur.accepted < 255) cur.accepted++;
    g_wake_diag.last_wake_tud_ok = ok ? 1 : 0;
    g_wake_diag.last_wake_host_suspended = host_suspended ? 1 : 0;

    // Linux quirk: Sometimes Linux fails to set the REMOTE_WAKEUP feature
    // flag before the second suspend, causing TinyUSB to refuse to wake.
    // If we are suspended but ok is false, we force the wake signal.
    if (!ok && host_suspended) {
        WAKE_DBG("%s: tud_remote_wakeup()=0 but suspended. Forcing DCD wake.", reason);
        resume_drive_start();
        if (cur.dcd < 255) cur.dcd++;
        g_wake_diag.dcd_forced++;
        ok = true;
    }

    if (ok) {
        critical_section_enter_blocking(&wake_cs);
        state = WAKE_REQUESTED;
        state_entered_us = time_us_64();
        critical_section_exit(&wake_cs);
        WAKE_DBG("%s -> REQUESTED", reason);
    }
#ifdef WAKE_DEBUG
    else {
        static uint64_t last_log = 0;
        const uint64_t now = time_us_64();
        if (now - last_log > 5000000) {
            WAKE_DBG("%s, tud_remote_wakeup()=0 (USB bus not in suspend) -- 5s heartbeat", reason);
            last_log = now;
        }
    }
#endif
}

void wake_init(void) {
    critical_section_init(&wake_cs);
}

// Called right before a deliberate USB reconnect (FUNC_RECONNECT): arm a grace window so the
// suspend the reconnect causes is ignored, and drop any already-pending debounced power-off.
void wake_note_usb_reconnect(void) {
    reconnect_until_us = time_us_64() + WAKE_RECONNECT_GRACE_US;
    suspend_at_us = 0;
}

extern "C" void tud_suspend_cb(bool remote_wakeup_en) {
    WAKE_DBG("tud_suspend_cb remote_wakeup_en=%d prev_state=%s",
             (int)remote_wakeup_en, wake_state_name(state));
    g_wake_diag.suspend_cb_count++;
    poll_recovered_pending = false;   // callback arrived: the poll merely beat it
    cur = wake_cycle_t{};             // new sleep: start a fresh record
    g_wake_diag.last_remote_wakeup_en = remote_wakeup_en ? 1 : 0;
    // A deliberate Reconnect USB (FUNC_RECONNECT) tears the bus down and back up, which looks
    // like a suspend but is not a host sleep -- ignore it so it cannot power off the controller.
    // See wake_note_usb_reconnect().
    if (time_us_64() < reconnect_until_us) {
        WAKE_DBG("suspend during reconnect grace -> ignored");
        return;
    }
    // The power-off runs on every genuine suspend, independent of enable_wake (battery-save for
    // a real sleep/shutdown). A spurious hub suspend is filtered by the debounce below, not a
    // gate -- it resumes and tud_resume_cb / tud_mount_cb cancel the pending power-off first.
    // Do NOT gate this on enable_wake, or the controller stops powering off on shutdown.
    suspend_at_us = time_us_64();
    host_suspended = true;
    host_resumed_event = false;

    // Everything below is the wake-UP path (press a key to wake the host) -- enable_wake only.
    if (!get_config().enable_wake) return;

    // Unconditionally re-arm on suspend. If a previous wake attempt hung
    // (e.g. Linux ignored a keystroke and left the endpoint busy forever),
    // we must abort and reset so the NEXT wake attempt can trigger.
    state = WAKE_PENDING_PRESS;
    state_entered_us = time_us_64();
    prev_b7 = 0x08; prev_b8 = 0x00; prev_b9 = 0x00;
    key_attempts = 0;
    WAKE_DBG("-> PENDING_PRESS");
}

void wake_on_bt_connect(void) {
    bt_link_up = true;                     // track regardless of the wake setting
    if (!get_config().enable_wake) return;
    critical_section_enter_blocking(&wake_cs);
    const bool should_wake = host_suspended &&
        (state == WAKE_IDLE || state == WAKE_DONE || state == WAKE_PENDING_PRESS);
    critical_section_exit(&wake_cs);

    if (should_wake) {
        request_host_wake("BT reconnect while suspended");
    }
}

extern "C" void tud_resume_cb(void) {
    if (poll_recovered_pending) { g_wake_diag.recovered_suspends++; poll_recovered_pending = false; }
    cur.resumed = 1;
    cur.end_state = (uint8_t)state;
    g_wake_cycle = cur;               // close the record for this sleep
    WAKE_DBG("tud_resume_cb state=%s", wake_state_name(state));
    host_suspended = false;
    host_resumed_event = true;
    suspend_at_us = 0; // resumed before the debounce elapsed -> cancel the power-off
}

extern "C" void tud_mount_cb(void) {
    WAKE_DBG("tud_mount_cb state=%s", wake_state_name(state));
    // This is the ONLY hook that fires on every enumeration, which is why the
    // audio-control reset hangs off a wake callback rather than living in
    // audio.cpp. A host that has just enumerated us has set no controls yet, so
    // anything still in mute[] belongs to an endpoint that no longer exists.
    usb_audio_controls_reset();
    host_suspended = false;
    host_resumed_event = true;
    suspend_at_us = 0;
    reconnect_until_us = 0; // reconnect finished re-enumerating; end the grace early
}

void wake_on_bt_input(const uint8_t *hid_input, uint16_t len) {
    if (!get_config().enable_wake) return;
    if (len < 10) return;
    // DualSense BT 0x31 input report layout (after main.cpp's `data + 3` skip):
    //   byte 7 low nibble: D-pad direction (0x08 idle); high nibble: face buttons
    //   byte 8: L1, R1, L2 click, R2 click, share, options, L3, R3
    //   byte 9: PS (bit 0), touchpad-click (bit 1), mute (bit 2)
    //
    // We trigger on ANY change in those three button bytes, not strictly on
    // the PS bit. Reasons:
    //   1. The DualSense's BT radio enters a low-power sniff mode after a
    //      period of inactivity. The PS button alone often does not wake
    //      the radio out of sniff -- shoulder buttons reliably do. So the
    //      first BT report after S3 is most likely whichever button the
    //      user happened to press to wake the radio. PS itself counts as
    //      "any button" too, so the single-press UX still works.
    //   2. We additionally call tud_remote_wakeup() speculatively even from
    //      WAKE_IDLE / WAKE_DONE state. TinyUSB returns true only when the
    //      host actually USB-suspended the bus; otherwise it's a no-op. This
    //      protects against the case where tud_suspend_cb didn't fire (e.g.
    //      a hub between the host and the dongle masking the suspend signal
    //      from downstream). On success the FSM transitions to REQUESTED and
    //      proceeds with the keystroke as normal.
    const uint8_t b7 = hid_input[7];
    const uint8_t b8 = hid_input[8];
    const uint8_t b9 = hid_input[9];

    critical_section_enter_blocking(&wake_cs);
    const bool changed = (b7 != prev_b7) || (b8 != prev_b8) || (b9 != prev_b9);
    const bool armable = (state == WAKE_IDLE || state == WAKE_DONE || state == WAKE_PENDING_PRESS);
    prev_b7 = b7; prev_b8 = b8; prev_b9 = b9;
    critical_section_exit(&wake_cs);

    if (changed && armable) {
        request_host_wake("button event");
    }
}

void wake_on_bt_disconnect(void) {
    bt_link_up = false;
    critical_section_enter_blocking(&wake_cs);
    state = WAKE_IDLE;
    prev_b7 = 0x08; prev_b8 = 0x00; prev_b9 = 0x00;
    critical_section_exit(&wake_cs);
    ps_shortcut_reset();
    macro_reset();   // never leave a modifier latched when the controller goes
}

// True while the wake FSM is mid-sequence on the shared keyboard instance:
// from the resume request until its F15 keyup has been sent. macro.cpp stands
// off during this window. Deliberately NOT gated on host_suspended - most of
// this sequence runs AFTER the bus resumes, which is exactly when a macro would
// otherwise be free to interleave.
bool wake_owns_keyboard(void) {
    return state == WAKE_REQUESTED || state == WAKE_KEY_DOWN || state == WAKE_KEY_UP_SENT;
}

void wake_task(void) {
    const uint64_t now = time_us_64();
    resume_drive_service(now);

    // ---- Restore the full device once the host is awake again ----
    // If the controller reconnected while the host was asleep we deliberately did
    // NOT re-enumerate (that would have disturbed a sleeping host), so we can be
    // left keyboard-only with a controller attached and no gamepad. Put the full
    // device back as soon as the host is up.
    // Repair BOTH forms of "host is showing the wrong identity":
    //   - intent still idle (the reconnect happened while the host slept and
    //     was deliberately not acted on), and
    //   - intent already full but the host was never made to re-read it,
    //     which is the state a missed re-enumeration leaves behind.
    // The second case is what kept the idle product id on screen after a
    // wake until the controller was power-cycled. usb_served_idle_pid() is
    // latched at GET_DESCRIPTOR, so it reflects what Windows actually has.
    static uint64_t last_restore_us = 0;
    const bool identity_wrong = usb_is_idle_pid() || usb_served_idle_pid();
    const bool cooled_down    = (last_restore_us == 0) || (now - last_restore_us > 3000000);
    if (!host_suspended && identity_wrong && cooled_down && bt_link_up && !tud_suspended()) {
        last_restore_us = now;
        usb_set_idle_pid(false);
        wake_note_usb_reconnect();
        tud_disconnect();
        sleep_ms(250);   // > the USB 100 ms port debounce, or the host may not see the disconnect at all
        tud_connect();
        WAKE_DBG("host awake + controller back -> restoring full USB device");
    }

    // ---- Backstop: never trust the suspend EDGE alone ----
    // Every part of the wake path is gated on host_suspended: the controller
    // disconnect, the wake request on BT reconnect, and the DCD fallback inside
    // request_host_wake(). So if tud_suspend_cb() is ever missed - a hub masking
    // the signal downstream, or the edge landing while interrupts are masked -
    // ALL THREE silently do nothing, which is exactly the reported failure: the
    // controller stays on at sleep AND a later press does not wake the host.
    // tud_suspended() is TinyUSB's own current state rather than an edge, so
    // polling it recovers a missed callback.
    {
        const bool tu_susp = tud_suspended();
        if (tu_susp && !host_suspended) {
            host_suspended = true;
            if (suspend_at_us == 0) suspend_at_us = now;
            // Not necessarily a miss: TinyUSB sets its suspended flag in the
            // INTERRUPT handler but dispatches tud_suspend_cb() later from
            // tud_task(), so polling can legitimately win that race by a few
            // hundred microseconds - and a busy main loop (a custom effect
            // running its 8 ms tick) widens the window. Only count a genuine
            // miss: flag it now, and clear the flag if the callback does turn
            // up. Whatever is still flagged when the host resumes was real.
            poll_recovered_pending = true;
            WAKE_DBG("tud_suspended()=1 but no suspend callback -> recovered");
        } else if (!tu_susp && host_suspended) {
            // Symmetric case: a missed RESUME would otherwise leave us convinced
            // the host is asleep and keep disconnecting a controller in use.
            host_suspended = false;
            suspend_at_us = 0;
            if (poll_recovered_pending) { g_wake_diag.recovered_suspends++; poll_recovered_pending = false; }
            g_wake_diag.recovered_resumes++;
            WAKE_DBG("tud_suspended()=0 but still flagged suspended -> recovered");
        }
    }

    // Commit the deferred controller power-off once we have stayed suspended past the debounce
    // window (a genuine host sleep/shutdown). Runs regardless of enable_wake -- it is a
    // battery-save, not part of the wake-UP path. A transient hub suspend will already have
    // been cancelled by tud_resume_cb / tud_mount_cb before this fires.
    if (suspend_at_us != 0 && host_suspended &&
        now - suspend_at_us >= WAKE_POWEROFF_DEBOUNCE_US) {
        // Drop the LINK rather than asking the controller to power itself off.
        // bt_power_off_controller() queues a feature report that the DualSense has
        // to receive and obey; if it is not delivered, or is ignored, the
        // controller silently stays connected and awake - and with the link still
        // up the wake FSM never sees the disconnect it expects, so a later PS
        // press does not wake the host either. bt_disconnect() is an HCI command
        // to our OWN radio, so it cannot be refused. A disconnected DualSense
        // still powers down on its own idle timeout, so the battery saving is
        // kept; it just happens a few minutes later.
        // (Matches upstream's fix for the same intermittent failure.)
        const bool dis = bt_disconnect();
        g_wake_diag.disconnect_attempts++;
        g_wake_diag.last_disconnect_ok = dis ? 1 : 0;
        suspend_at_us = 0;
        WAKE_DBG("suspend debounce elapsed -> bt_disconnect()=%d", (int)dis);
    }

    // The wake-UP FSM below only runs when wake is enabled.
    if (!get_config().enable_wake) return;

    critical_section_enter_blocking(&wake_cs);
    const wake_state_t s = state;
    const uint64_t entered = state_entered_us;
    critical_section_exit(&wake_cs);

    switch (s) {
        case WAKE_IDLE:
        case WAKE_PENDING_PRESS:
        case WAKE_DONE:
            return;

        case WAKE_REQUESTED: {
            if (host_resumed_event || !host_suspended) {
                host_resumed_event = false;
                if (now - entered < WAKE_SETTLE_US) return;
                if (!tud_hid_n_ready(WAKE_KBD_INSTANCE)) {
                    // The bus resumed but the keyboard endpoint is not accepting reports.
                    // Without a bound here the machine waits in WAKE_REQUESTED forever —
                    // and that state is not "armable", so NO later button press can request
                    // another wake. A single failure would disable waking entirely until a
                    // reconnect. Wait a bounded time, then fall back to DONE so a press retries.
                    if (now - entered > WAKE_HID_WAIT_US) {
                        g_wake_cycle.hid_timeout = 1;
                        WAKE_DBG("REQUESTED: keyboard never ready -> DONE (retryable)");
                        critical_section_enter_blocking(&wake_cs);
                        enter_state(WAKE_DONE);
                        critical_section_exit(&wake_cs);
                        return;
                    }
                    g_wake_cycle.hid_waited = 1;
#ifdef WAKE_DEBUG
                    static uint64_t last_log = 0;
                    if (now - last_log > 1000000) {
                        WAKE_DBG("REQUESTED waiting: hid_n_ready=0 (heartbeat 1Hz)");
                        last_log = now;
                    }
#endif
                    return;
                }
                uint8_t rpt[8] = { 0, 0, WAKE_KEYCODE_F15, 0, 0, 0, 0, 0 };
                const bool sent = tud_hid_n_report(WAKE_KBD_INSTANCE, 0, rpt, sizeof(rpt));
                cur.key_sent = sent ? 1 : 0;
                WAKE_DBG("REQUESTED: sent keydown 0x%02X -> %d", WAKE_KEYCODE_F15, (int)sent);
                if (sent) {
                    critical_section_enter_blocking(&wake_cs);
                    enter_state(WAKE_KEY_DOWN);
                    critical_section_exit(&wake_cs);
                }
            } else if (host_suspended && resume_tries && resume_tries < RESUME_MAX_TRIES &&
                       now - resume_last_try >= RESUME_RETRY_US) {
                // Still asleep and the host has not resumed. Now that the drive
                // is terminated the bit can produce a fresh edge, so try again -
                // a single resume pulse is easy for a host to miss.
                resume_drive_start();
                g_wake_diag.resume_reissues++;
                if (cur.reissues < 255) cur.reissues++;
                WAKE_DBG("no resume yet -> re-issuing (try %u)", (unsigned)resume_tries);
            } else if (now - entered > WAKE_REQUEST_TIMEOUT_US) {
                WAKE_DBG("REQUESTED timeout 5s -> DONE (no resume signaling; may have already woken)");
                critical_section_enter_blocking(&wake_cs);
                enter_state(WAKE_DONE);
                critical_section_exit(&wake_cs);
            }
            return;
        }

        case WAKE_KEY_DOWN: {
            if (now - entered < WAKE_KEY_HOLD_US) return;
            if (!tud_hid_n_ready(WAKE_KBD_INSTANCE)) {
#ifdef WAKE_DEBUG
                static uint64_t last_log = 0;
                if (now - last_log > 1000000) {
                    WAKE_DBG("KEY_DOWN waiting: hid_n_ready=0 (heartbeat 1Hz)");
                    last_log = now;
                }
#endif
                return;
            }
            uint8_t up[8] = { 0 };
            const bool sent = tud_hid_n_report(WAKE_KBD_INSTANCE, 0, up, sizeof(up));
            WAKE_DBG("KEY_DOWN: sent keyup -> %d", (int)sent);
            if (sent) {
                critical_section_enter_blocking(&wake_cs);
                enter_state(WAKE_KEY_UP_SENT);
                critical_section_exit(&wake_cs);
            }
            return;
        }

        case WAKE_KEY_UP_SENT: {
            if (now - entered < WAKE_KEY_UP_SETTLE_US) return;
            key_attempts++;
            if (key_attempts < WAKE_KEY_ATTEMPTS) {
                // Retry: do NOT re-enter WAKE_REQUESTED (which gates on a
                // fresh tud_resume_cb event). We already established the
                // host woke once; just send another keydown directly. If the
                // host has dipped back into suspend, tud_hid_n_ready will be
                // false and we'll heartbeat from KEY_DOWN until it returns.
                if (!tud_hid_n_ready(WAKE_KBD_INSTANCE)) {
#ifdef WAKE_DEBUG
                    static uint64_t last_log = 0;
                    if (now - last_log > 1000000) {
                        WAKE_DBG("KEY_UP_SENT retry waiting: hid_n_ready=0 (heartbeat 1Hz)");
                        last_log = now;
                    }
#endif
                    return;
                }
                uint8_t rpt[8] = { 0, 0, WAKE_KEYCODE_F15, 0, 0, 0, 0, 0 };
                const bool sent = tud_hid_n_report(WAKE_KBD_INSTANCE, 0, rpt, sizeof(rpt));
                WAKE_DBG("KEY_UP_SENT: retrying F15 (attempt %d/%d) -> %d",
                         (int)key_attempts + 1, (int)WAKE_KEY_ATTEMPTS, (int)sent);
                if (sent) {
                    critical_section_enter_blocking(&wake_cs);
                    enter_state(WAKE_KEY_DOWN);
                    critical_section_exit(&wake_cs);
                }
            } else {
                WAKE_DBG("KEY_UP_SENT settle done -> DONE");
                critical_section_enter_blocking(&wake_cs);
                enter_state(WAKE_DONE);
                key_attempts = 0;
                critical_section_exit(&wake_cs);
            }
            return;
        }
    }
}

#endif // ENABLE_WAKE_HID
