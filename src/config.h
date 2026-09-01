//
// Created by awalol on 2026/5/4.
//

#ifndef DS5_BRIDGE_CONFIG_H
#define DS5_BRIDGE_CONFIG_H

#include <cstdint>

struct __attribute__((packed)) Config_body {
    uint8_t config_version; // Config Version
    float haptics_gain; // [1.0,2.0]
    uint8_t speaker_volume; // [0,127]
    uint8_t headset_volume; // [0,127] // max 0x7f
    uint8_t sync_spk_headset_volume; // bool: 0 disable,1 enable
    uint8_t speaker_gain; // [0,7]
    uint8_t inactive_time; // [5,60] min
    uint8_t disable_inactive_disconnect; // bool: 0 disable,1 enable
    uint8_t disable_pico_led; // bool
    uint8_t polling_rate_mode; // 0: 250Hz, 1: 500Hz, 2: real-time
    uint8_t audio_buffer_length; // [16,128]
    uint8_t controller_mode; // 0: DS5, 1: DSE, 2: Auto
    uint8_t lock_volume; // 0: disable,1: enable
    uint8_t disable_usb_sn; // 0: disable,1: enable
    uint8_t ps_shortcut_enabled; // 0: disabled, 1: enabled (Xbox Game Bar via HID keyboard)
    uint8_t disable_mic; // bool: 0 enable (default), 1 disable controller mic
    uint8_t disable_speaker; // bool: 0 enable (default), 1 disable speaker/headset
    uint8_t enable_wake; // bool: 0 disabled (default), 1 wake host on PS press (USB remote wakeup)
    uint8_t auto_haptics_enable;
    uint8_t auto_haptics_gain;
    uint16_t auto_haptics_lowpass_hz;
    uint8_t auto_mute_replace;
    uint8_t auto_mute_mix;
    uint8_t auto_haptics_gate; // noise gate threshold [0-100], 0=off; suppresses dialog/quiet content
    uint8_t auto_haptics_slope; // LP filter slope in dB/oct: 6, 12, or 24
    uint8_t lightbar_off; // bool: force the RGB lightbar off (e.g. blue glow in Xbox360/DS4 mode)
    uint8_t auto_haptics_smooth; // 0-100: response smoothing (release/decay), higher=smoother
    uint16_t bt_flush_timeout; // ACL automatic flush timeout in 0.625ms units; 0=off (infinite)
    uint16_t bt_qos_latency_us; // QoS requested link latency in microseconds; 0=off
    uint8_t rumble_haptic_strength; // [0-100] strength of converted DS4Windows rumble blended in Mix mode
    uint8_t effect_leak_volume; // [0-100] volume of high-passed effect leak through speaker when auto-muted; 0=off
    uint16_t effect_leak_hp_hz; // high-pass cutoff (Hz) for the effect leak detection band
    uint8_t effect_leak_sensitivity; // [0-100] transient detection sensitivity; higher=more eager (more leaks through)
    uint8_t effect_leak_decay; // [0-100] fade-out length after a transient; higher=longer/more gradual tail
    uint8_t effect_leak_attack; // [0-100] gate open speed; higher=more immediate (less delay)
    uint16_t effect_leak_output_hp_hz; // output high-pass cutoff (Hz) — protects speaker from low-freq popping
    // Rumble-to-trigger: express the game's rumble as trigger Vibration (effect 0x26).
    uint8_t r2t_mode;       // 0=off, 1=left trigger only, 2=right trigger only, 3=both
    uint8_t r2t_on_press;   // bool: 0=vibrate regardless of trigger position, 1=only when trigger pressed
    uint8_t r2t_strength;   // [0-100] amplitude multiplier applied to the rumble value
    uint8_t r2t_frequency;  // vibration frequency parameter for the 0x26 effect [1-255], ~tactile buzz
    // Adaptive triggers Stage 1: L2-gated R2 resistance. When L2 is pressed past the
    // threshold (aiming), R2 gets a constant resistance (Feedback effect 0x21).
    uint8_t at_mode;        // 0=off, 1=L2 gates R2 resistance
    uint8_t at_strength;    // [0-100] resistance intensity (mapped to 0-8 effect strength)
    uint8_t at_threshold;   // [0-255] how far L2 must be pressed to arm R2 resistance
    uint8_t at_start_pos;   // [0-9] trigger position where R2 resistance begins
    // Gyro -> right-stick aiming: adds controller angular velocity onto the right
    // stick in the input report, so ANY game gets gyro aim with no PC software.
    uint8_t gyro_mode;      // 0=off, 1=L2-held, 2=always, 3=touchpad-touch enables, 4=always but touch pauses (ratchet)
    uint8_t gyro_sens;      // [1-100] sensitivity (50 = raw/40 per report)
    uint8_t gyro_axis;      // horizontal source: 0=yaw (turn), 1=roll (tilt sideways),
                            // 2=player space (uses gravity; see config_valid clamp)
    uint8_t gyro_invert;    // bit0 = invert X, bit1 = invert Y
    uint8_t haptics_aa;     // native-haptics smoothing: 1=off (raw/gritty), 2=light 1-pole ~2.4kHz (default), 3=strong 2-pole ~1.3kHz
    uint8_t synth_force;    // 0=yield to game trigger effects (default), 1=force r2t/at even if a game/app sends effects
    // Adaptive triggers Stage 2: push-back kick (recoil). While resistance is
    // engaged, the vibration envelope momentarily raises the resistance strength,
    // pressing the trigger back against the finger - recoil on top of resistance.
    uint8_t at_pushback;     // [0-100] kick strength; 0=off (Stage 1 behavior unchanged)
    uint8_t at_pushback_src; // envelope source: 0=rumble only, 1=audio haptics only, 2=both (max)
    uint8_t at_pushback_freq;// [10-200] vibration frequency of the kick thump; lower = heavier knock (default 35)
    // Effect-leak band-pass window + gate hold (v1.2.0). The output high-pass
    // (effect_leak_output_hp_hz) forms the LOW wall of the window; this low-pass
    // is the HIGH wall. Both are 12 dB/oct. Sound outside the window never leaks,
    // which is what makes the leak selective instead of "thin treble = crackle".
    uint16_t effect_leak_lp_hz; // output low-pass cutoff (Hz); default 3500
    uint8_t  effect_leak_hold;  // [0-100] min gate-open hold after a transient (x5 = 0-500 ms); stops flutter ("missing and poppy")
    // Per-trigger adaptive-trigger modes (v1.2.1). Each trigger has its own mode;
    // strength/threshold/start-position/kick parameters are shared. "Gated" means
    // armed by the OPPOSITE trigger passing at_threshold (R2 gated = L2 arms it,
    // L2 gated = R2 arms it), with the same hysteresis as before.
    // Fully independent per-trigger adaptive triggers (v1.3.1). The at_* fields
    // above (mode/strength/threshold/start/pushback/freq) are R2's; the at_l2_*
    // fields below are L2's own complete set. Only at_pushback_src (the kick
    // envelope source) is shared — it's one signal. Per-trigger kick strength 0
    // simply disables the kick on that trigger.
    uint8_t  at_kick_style;      // R2 kick delivery: 0=vibration thump (0x26), 1=bow snap (0x22) — the
                                 // bow's snap force physically presses the trigger back (sharper; feel
                                 // varies with hold depth).
    uint8_t  at_l2_mode;         // L2: 0=off (default), 1=gated (R2 arms), 2=always on
    uint8_t  at_l2_strength;     // L2 resistance strength [0-100]
    uint8_t  at_l2_threshold;    // R2 press depth that arms L2 in gated mode [1-255]
    uint8_t  at_l2_start_pos;    // L2 resistance start zone [0-9]
    uint8_t  at_l2_pushback;     // L2 kick strength [0-100]; 0 = no kick on L2
    uint8_t  at_l2_pushback_freq;// L2 kick thump frequency [10-200]
    uint8_t  at_l2_kick_style;   // L2 kick delivery: 0=thump, 1=bow snap
    // Auto-haptics frequency split (v1.5.0). 0 = OFF (default): the single-band
    // path is byte-identical to previous firmware. When set (30-200 Hz), the
    // haptics band is divided at the crossover: LOW band (below - impacts,
    // explosions, engine weight) and HIGH band (crossover..LP cutoff - where
    // music bass lines and voice fundamentals live), each enveloped separately
    // and weighted by its own gain before the shared gate + carrier. Typical
    // use: keep low at 100, drop high to tame music/dialog-driven buzz.
    uint16_t ah_xover_hz;   // 0=off, else 30-200 Hz crossover
    uint8_t  ah_low_gain;   // [0-100] weight of the low band (default 100)
    uint8_t  ah_high_gain;  // [0-100] weight of the high band (default 100)
    // Adaptive-trigger resistance SHAPES (v1.7.0). The DualSense feedback effect
    // (0x21) supports 10 independent travel zones with 3-bit strength each - the
    // controller evaluates trigger position in hardware. Shapes program that
    // zone table:
    //   0 = Constant: start_pos..9 at Strength A (pre-1.7.0 behavior)
    //   1 = Ramp: linear A -> B across start_pos..9 (racing: light->heavy gas,
    //       or heavy->light brake bite - direction is just A vs B)
    //   2 = Two-stage detent: base Strength A with a WALL of Strength B at the
    //       detent zone - a tactile bump marking half-press (fire) from
    //       full-press (alt-fire)
    uint8_t  at_shape;          // R2 shape 0-2
    uint8_t  at_strength_b;     // R2 strength B [0-100] (ramp end / detent wall)
    uint8_t  at_detent_pos;     // R2 detent zone 0-9 (shape 2)
    uint8_t  at_l2_shape;       // L2 shape 0-2
    uint8_t  at_l2_strength_b;  // L2 strength B [0-100]
    uint8_t  at_l2_detent_pos;  // L2 detent zone 0-9 (shape 2)
    // Trigger activation dead zone (v1.8.0): below the configured zone the HOST
    // sees the trigger untouched (analog 0, digital bit cleared) - the game's
    // action registers only once the pull reaches the zone, aligning early-firing
    // games with the resistance/detent/bow feel. Internal effects (gating, kick,
    // shapes) always see the RAW trigger. 0 = off, 1-9 = first registered zone.
    uint8_t  at_deadzone;       // R2: 0=off, 1-9 first zone the host sees
    uint8_t  at_l2_deadzone;    // L2: 0=off, 1-9 first zone the host sees
    // Mix-mode native passthrough level (v1.10.0). In Mix, ch3/4 pass through to
    // the actuators UNSCALED by Intensity/split/gate - correct for real native
    // haptics, but with ds5audio's `duplicate` mapping ch3/4 carry a copy of the
    // game audio, drowning the adjustable derived part. This fader scales the
    // passthrough per profile: 100 = classic native Mix, 0 = derived+rumble only
    // (auto-haptics own the actuators; the ds5audio --map choice stops mattering).
    uint8_t  mix_native_level;  // [0-100] ch3/4 contribution in Mix (default 100)
    // Effect leak MAX BURST (v1.12.0): cap on how long one gate opening may last
    // (x5 ms, 0 = off/unlimited). Transients (gunshots, impacts) end within the
    // cap naturally; SUSTAINED content (dialogue, music) used to hold the gate
    // open and duplicate the room audio - now it gets cut at the cap and the
    // gate stays closed (refractory) until the signal genuinely falls, so one
    // sustained sound = one short accent, not a stream. Turns the leak into a
    // percussion layer that punctuates instead of duplicating.
    uint8_t  effect_leak_max_burst; // x5 ms, 0=off, e.g. 30 = 150 ms bursts
    // Custom captured-effect action (v1.14.0): plays raw 11-byte trigger effects
    // captured from a game, with NO fidelity loss (stored verbatim, not decoded
    // into sliders - the game's force curves don't round-trip through our fields).
    // An "action" is up to 2 effect STATES (A and B) that the firmware cycles
    // while the trigger is engaged - reproducing how games deliver adaptive-trigger
    // effects (a rapid A<->B alternation, each state's trigger position encoded in
    // its own bytes 1-2). Independent per trigger. When enabled it takes priority
    // on that trigger and bypasses the sliders. condition: 0=while-held, 1=on-press
    // (fire once crossing threshold upward), 2=on-release (fire once crossing down).
    // rate: A<->B toggle rate for 2-state actions (mainly vibration). thresh: the
    // trigger zone gate (dead-zone-compatible). state_count: 1 or 2.
    uint8_t  ce_r2_enable;        // 0=off, 1=on
    uint8_t  ce_r2_condition;     // 0=hold, 1=press, 2=release
    uint8_t  ce_r2_thresh;        // trigger zone 0-9 gate
    uint8_t  ce_r2_rate;          // A<->B toggle rate [1-100]
    uint8_t  ce_r2_state_count;   // 0..5 states in the action
    // Up to 6 raw captured states + per-state duration. TWO replay modes:
    //  - dt all zero (assigned from the history monitor): rate-based A<->B cycling
    //    (states[0]/[1]) - the mode that "stacks" mechanical pairs nicely.
    //  - dt present (assigned from a TIMELINE recording): step the timeline -
    //    hold each state for its recorded duration, loop. Reproduces asymmetric
    //    patterns (e.g. GoW switch-then-HOLD) verbatim; no rate dial-in needed.
    uint8_t  ce_r2_states[5][11]; // raw captured effect states, played verbatim
    uint16_t ce_r2_dt[5];         // per-state hold duration in ms (0 = rate mode)
    uint8_t  ce_l2_enable;
    uint8_t  ce_l2_condition;
    uint8_t  ce_l2_thresh;
    uint8_t  ce_l2_rate;
    uint8_t  ce_l2_state_count;
    uint8_t  ce_l2_states[5][11];
    uint16_t ce_l2_dt[5];
    // Gate hand-off between the custom effect and the slider adaptive-trigger,
    // per trigger. The gate is the SLIDER path's own engagement (at_mode: L2-gated,
    // L1-gated or always), so its threshold and hysteresis are reused as-is.
    //   0 = off      - custom effect owns the trigger whenever it is enabled (default)
    //   1 = sliders when gated  - gate engaged -> synthesized effect; otherwise custom
    //   2 = custom when gated   - gate engaged -> custom effect; otherwise sliders
    uint8_t  ce_r2_yield;
    uint8_t  ce_l2_yield;
    // Mix-mode native passthrough FILTER (v1.18.14). The ch3/4 native
    // contribution in Mix has always been low-passed at the auto-haptics LP
    // cutoff. That exists for VoiceMeeter-style 4ch setups where ch3/4 mirror
    // the full-band stereo and would leak dialogue to the actuators - but it
    // also strips genuine native haptics, whose content usually sits well
    // above an 80 Hz cutoff, so a native game mixed with auto-haptics lost
    // exactly the effects the passthrough was meant to preserve.
    //   1 = filtered (default, previous behaviour)
    //   0 = raw      - pass ch3/4 through untouched, derived haptics still filtered
    uint8_t  mix_native_filter;
    // Auto-haptics DSP SOURCE (v1.18.15). Which channel pair the auto-haptics
    // DSP listens to. The speaker and the effect leak always stay on ch0/1, so
    // moving the DSP to ch2/3 separates the two audio sources that previously
    // had to share ch0/1: run ds5audio with --map rear (its capture goes to
    // ch2/3 only) and ch0/1 then carries nothing but the game's own native
    // speaker output. Auto-haptics is derived from the script feed while the
    // speaker - and anything the effect leak passes - is purely the game's
    // native effects. Set Native Passthrough to 0 in this configuration, since
    // ch2/3 now carries the raw script audio rather than native haptics.
    //   0 = ch0/1 (default, previous behaviour)
    //   1 = ch2/3 (requires a 4-channel stream; falls back to ch0/1 on stereo)
    uint8_t  ah_dsp_source;
    // Right-stick inversion (v1.18.21). Inverts the PHYSICAL right stick axes in
    // the input report the host sees, independent of gyro aiming (which has its
    // own gyro_invert). Applied before the gyro delta is added, so the two
    // compose: invert the stick here, and align gyro separately if used. Same
    // bit layout as gyro_invert. Contributed by AppendinoCom (PR #4).
    uint8_t  rstick_invert; // bit0 = invert X (horizontal), bit1 = invert Y (vertical)
    // Macro mask (v1.19.0). One bit per entry in the DEVICE-GLOBAL macro table.
    // The table itself lives in its own flash sector (MACRO_FLASH_OFFSET) and is
    // NOT per-slot - only this mask is, so a slot selects which subset of the
    // shared macros is live. That is what lets Playnite switch macro sets per
    // game without duplicating definitions into all 32 slots.
    //
    // STORED INVERTED - a SET bit means macro N is DISABLED - and this is not a
    // style choice. Slots written by older firmware are read back 0xFF-filled
    // past their recorded body_len, and config_valid() turns that fill into a
    // safe default by RANGE-CLAMPING each field. A bitmap has no invalid range:
    // 0xFFFFFFFF is the perfectly legal "all 32 enabled". Stored the obvious way
    // round, every pre-existing slot would load with every macro switched on and
    // would drag a spurious USB re-enumeration along with it. Inverted, the
    // 0xFF fill reads as "all disabled", which is exactly the wanted default and
    // needs no clamp at all.
    //
    // Keep the inversion at the STORAGE layer only. Firmware, wire format and
    // portal state all speak macro_disable; the single place it flips is the
    // checkbox's checked attribute at render time.
    //
    // Enumeration-critical at its all-disabled boundary: the wake keyboard
    // interface is present iff at least one macro is enabled (see
    // usb_descriptors.cpp), so crossing MACRO_NONE_ENABLED changes the USB
    // descriptor. Flipping WHICH macros are on does not.
    uint32_t macro_disable;

    // --- Two-stage triggers (v1.21) ------------------------------------------
    // One physical pull, two signals, with an adaptive-trigger wall marking the
    // boundary so the stage change is FELT rather than hunted for. Applied only
    // to the OUTBOUND report in main.cpp; every internal consumer (AT gating,
    // custom effects, gyro, macros) still reads the raw trigger position.
    //
    // t2_mode: bits 0-1 = what happens to the ANALOG axis below the boundary
    //   0 = off       - feature disabled
    //   1 = additive  - axis untouched, stage 2 just adds its button
    //   2 = rescale   - [deadzone..t2_pos] is stretched to the full 0..255, so
    //                   full throttle authority survives the shortened travel
    //                   and everything above t2_pos is a dedicated stage-2 zone
    // bit 2 (0x04)   = RELEASE STAGE 1 above the boundary: clears the trigger's
    //   DIGITAL button bit so stage 1 stops. Racing wants this OFF (throttle
    //   must stay held through nitro); FPS alt-fire wants it ON, because
    //   otherwise the primary fire bit stays set and you keep firing through
    //   the alt-fire - which is the objection that sank the first design.
    // Rescale distorts the throttle curve a game sees, which is right for an
    // arcade racer and wrong for a sim: prefer additive where the analog value
    // is being modulated deliberately.
    uint8_t t2_mode;      // R2. 0 = off (default)
    uint8_t t2_pos;       // R2 boundary, 0-255 raw trigger counts
    uint8_t t2_button;    // R2 stage-2 button, T2Button enum, 0 = none
    uint8_t t2_l2_mode;   // L2, same encoding
    uint8_t t2_l2_pos;
    uint8_t t2_l2_button;
    // Gyro output target. 0 = right stick (as before), 1 = mouse,
    // 2 = mouse + Flick Stick on the right stick.
    // A mouse is a DELTA device, which is what a gyro natively produces; the
    // stick is a velocity input with a dead zone and a limited range, so it
    // clips a fast turn and rounds away a slow one however good the maths is.
    //
    // ENUMERATION-CRITICAL: selecting mouse adds a HID interface, so crossing
    // between the two re-enumerates. Per-profile like everything else, so a slot
    // can pick mouse for a game where the pad is hidden and stick for a native
    // DualSense title.
    uint8_t gyro_output;
    // Flick Stick calibration: mouse counts for a full 360 turn IN THIS GAME.
    // Only meaningful when gyro_output == 2. Jibb Smart's spec is explicit that
    // faking flick stick with mouse movement needs this and an in-game
    // implementation would not - we are converting an ANGLE to a displacement,
    // so without the game's own mouse-to-yaw ratio a 90 degree flick lands
    // wherever it happens to land.
    uint16_t flick_counts_360;
    // Vertical gyro sensitivity, 0 = follow gyro_sens (one knob, as before).
    // Separate axes are worth having because the vertical aiming range in a game
    // is far smaller than the horizontal one, so the same gain that feels right
    // for turning is usually too fast for looking up and down.
    uint8_t  gyro_sens_y;

    // --- Stick to mouse (v1.30.0) ---------------------------------------------
    // Drive the mouse from a STICK, the way gyro_output drives it from motion.
    // Both feed the same accumulator in gyro_mouse_task(), so a game can be
    // played with the stick doing the large turns and the gyro the fine aim -
    // which is the usual reason to want this at all.
    //   0 = off (default), 1 = right stick, 2 = left stick
    // The chosen stick is CENTRED in the report the game sees, exactly as Flick
    // Stick does, so the game does not also turn from it. Mutually exclusive
    // with Flick Stick (gyro_output 2), which claims the right stick for itself;
    // config_valid() enforces that rather than leaving two owners fighting.
    uint8_t  stick_mouse;
    // Counts per second at full deflection, stored DIRECTLY (not /10) so the
    // ceiling is a real limit rather than an artifact of byte width: a byte
    // capped this at 2550/s, which is short for a fast-turning game. 0 uses
    // STICK_MOUSE_SENS_DEFAULT.
    uint16_t stick_mouse_sens;
    // Radial deadzone, percent of full deflection. Sticks rest a little off
    // centre and a mouse never stops moving, so without this the view creeps.
    uint8_t  stick_mouse_deadzone;
    // Response curve exponent x10 (10 = linear, 20 = squared). A linear stick
    // is twitchy at the centre and slow at the edge; the curve is what makes
    // this feel like a mouse rather than a joystick.
    uint8_t  stick_mouse_curve;
    // Invert: bit0 = X, bit1 = Y.
    uint8_t  stick_mouse_invert;
    // Vertical speed, 0 = follow stick_mouse_sens (one knob, as before). Same
    // convention and the same reason as gyro_sens_y: the vertical aiming range
    // in a game is far smaller than the horizontal one, so a gain that feels
    // right for turning is usually too fast for looking up and down.
    uint16_t stick_mouse_sens_y;

    // --- Gyro natural sensitivity (v1.32.0) -----------------------------------
    // Express gyro-to-mouse aiming as a REAL-WORLD RATIO instead of an
    // arbitrary slider: 1.0x means rotating the controller 10 degrees turns the
    // in-game view 10 degrees. Set it once and it holds in every game that
    // shares the same mouse counts per 360, instead of being re-tuned per game.
    //   0 = arbitrary slider (default, gyro_sens as before)
    //   1 = natural, using gyro_natural_x10 and flick_counts_360
    // Only meaningful for gyro-to-MOUSE. Gyro-to-stick is a rate control - the
    // stick says "how fast to turn", not "how far" - so a 1:1 rotation ratio
    // has nothing to attach to there, and this is ignored in that mode.
    uint8_t  gyro_sens_mode;
    // Multiplier x10: 10 = 1.0x (true 1:1), 25 = 2.5x. Typical play is 2.5x-12x.
    uint8_t  gyro_natural_x10;
    // Vertical multiplier x10, 0 = follow the horizontal one.
    uint8_t  gyro_natural_y_x10;
    // Gyro scale trim, x100 (0 or 100 = nominal). The natural conversion assumes
    // the gyro is rated +/-2000 deg/s; if a controller reads high or low, the
    // angle check in the portal measures the error and this corrects it, so
    // 1.0x is genuinely 1:1 rather than nominally.
    uint16_t gyro_scale_trim_x100;
    // ---- Staged battery notification (appended 1.35.0) ----
    // Three independent stages. Each fires ONCE when the battery falls past its
    // level while discharging, blinks the controller's lightbar in its colour,
    // and then hands the lightbar back. It is a prompt to go and plug in, not a
    // running indicator - nothing keeps flashing until you do.
    //
    // The level is the DualSense's own PowerPercent nibble, 0-10, i.e. 10%
    // steps. It cannot be finer: that is the resolution the controller reports.
    // 0 disables the stage.
    uint8_t  batt_notify_enable;      // master on/off
    uint8_t  batt_stage_level[3];     // 1-10 = 10%-100%, 0 = stage off
    uint8_t  batt_stage_blinks[3];    // 1-20 blinks
    uint8_t  batt_stage_r[3];
    uint8_t  batt_stage_g[3];
    uint8_t  batt_stage_b[3];
    // Per-stage on/off, kept SEPARATE from the level so unticking a stage does
    // not throw away the level and colour it was set to.
    uint8_t  batt_stage_on[3];
    // ---- Touchpad to mouse (appended 1.37.0) ----
    // Relative, trackpad style: the pointer follows how far the finger MOVED,
    // not where it is. Clicks are deliberately absent - the touchpad-click
    // halves are already macro triggers and can output mouse buttons, so
    // binding left/right click there costs nothing and stays configurable.
    uint8_t  touch_mouse;            // 0 off, 1 on
    uint8_t  touch_mouse_sens;       // "slide" speed, 100 = 1:1-ish, 0 -> default
    uint8_t  touch_mouse_min;        // ignore movement smaller than this, in pad counts
    uint8_t  touch_mouse_invert;     // bit0 X, bit1 Y
    uint8_t  touch_mouse_trackball;  // 0 off, 1 = keep gliding after release
    uint8_t  touch_mouse_friction;   // how fast the glide decays, higher = stops sooner
};

// Stage-2 output buttons. Values are PERSISTED in every profile and slot, so
// this list is APPEND ONLY - renumbering silently rebinds saved profiles.
enum : uint8_t {
    T2BTN_NONE     = 0,
    T2BTN_CROSS    = 1,
    T2BTN_CIRCLE   = 2,
    T2BTN_SQUARE   = 3,
    T2BTN_TRIANGLE = 4,
    T2BTN_L1       = 5,
    T2BTN_R1       = 6,
    T2BTN_L3       = 7,
    T2BTN_R3       = 8,
    // The trigger CLICK bits. A trigger can drive the other trigger's digital
    // button - R2's second stage pressing L2 is a normal ask - but never its own,
    // which the portal filters per trigger rather than by splitting the enum.
    T2BTN_L2       = 9,
    T2BTN_R2       = 10,
    // Appended for macro outputs (v1.31.0). The list above was scoped for
    // two-stage triggers; macros reused it and inherited the gap, so a macro
    // could TRIGGER on Create, Options or a D-pad direction but never OUTPUT
    // one. Appending keeps every saved profile's values meaning what they did.
    // Values 11-15 are RESERVED: they are the mouse outputs (MOUT_* in
    // macro.h), which were carved out of this same numbering. Saved macros
    // already store 11-15 meaning "left click" and so on, so the controller
    // buttons appended below must start ABOVE them - numbering these from 11
    // would silently turn every saved mouse output into a gamepad button.
    T2BTN_CREATE   = 16,
    T2BTN_OPTIONS  = 17,
    T2BTN_TOUCHPAD = 18,   // touchpad CLICK
    // The D-pad is a hat ENUM in the report, not four bits, so these cannot be
    // OR-ed in like the rest; see the merge in macro_apply_buttons().
    T2BTN_DPAD_UP    = 19,
    T2BTN_DPAD_DOWN  = 20,
    T2BTN_DPAD_LEFT  = 21,
    T2BTN_DPAD_RIGHT = 22,
    T2BTN_COUNT      = 23,
};
enum : uint8_t {
    T2_AXIS_OFF      = 0,
    T2_AXIS_ADDITIVE = 1,
    T2_AXIS_RESCALE  = 2,
    T2_AXIS_MASK     = 0x03,
    T2_RELEASE_STAGE1 = 0x04,
};

struct __attribute__((packed)) Config {
    uint32_t magic;
    uint32_t crc32; // Config_body crc32, only calc and verify when save
    uint16_t size;  // Config_body size
    Config_body body;
};

void config_default();
void config_load();
bool config_save();

// --- Profile slots -----------------------------------------------------------
// Named copies of Config_body stored in their own flash sector (the sector
// below the active-config sector). Saving a slot is a rare manual portal
// action; activating one at game launch is a single atomic command instead of
// a 30-field write.
constexpr uint8_t SLOT_COUNT = 32;       // v1.22.0: 32 (was 24) - 8 per flash sector, 4 sectors
constexpr uint8_t SLOTS_PER_SECTOR = 8;  // 512-byte stride in a 4 KB sector
constexpr uint8_t SLOT_NAME_LEN = 16;

// The REAL ceiling on Config_body, and the one worth knowing when adding a field.
// It is not the 256-byte flash page - the config sector is erased whole and the
// write length follows the struct. It is the profile SLOT: every slot stores a
// Config_body, eight to a 4 KB sector, so the body must fit the 512-byte stride
// minus the record's own header (4 magic + 2 body_len + 16 name + 4 crc = 26).
// Past this, SLOTS_PER_SECTOR has to drop to 4, which doubles the sectors slots
// need - affordable inside the 16-sector reservation, but a real step.
constexpr uint32_t SLOT_STRIDE_BYTES = 4096u / SLOTS_PER_SECTOR;          // 512
constexpr uint32_t SLOT_MAX_BODY_LEN = SLOT_STRIDE_BYTES - (4 + 2 + SLOT_NAME_LEN + 4);
bool slot_save(uint8_t idx, const uint8_t *name, uint8_t name_len); // current config.body -> slot
// slot -> active config + flash. Returns 0 = failed (out param stage: 1 bad
// idx, 2 slot unreadable, 3 flash persist failed even after retry), 1 = fully
// activated + persisted, 2 = ACTIVATED (settings live in RAM) but persistence
// failed - treat as success at game launch; the config only reverts on power
// loss and any later save re-persists it.
uint8_t slot_activate(uint8_t idx, bool &needs_reenum, uint8_t &fail_stage);
bool slot_info(uint8_t idx, uint8_t name_out[SLOT_NAME_LEN], uint8_t &valid, uint8_t &cfg_version);
bool slot_load_body(uint8_t idx, Config_body &out);
// Currently-loaded profile (RAM only): slot_out=0xFF + returns false when nothing
// is tracked; otherwise fills the source slot name and sets edited when the live
// config has diverged from the slot as loaded.
bool active_profile_get(uint8_t &slot_out, bool &edited_out, uint8_t name_out[SLOT_NAME_LEN]);
Config_body& get_config();
void set_config(const uint8_t *new_config, const uint16_t len);
void config_valid();
void set_config(const Config_body &new_config);
void set_gain(uint8_t value);
extern bool is_dse;

#endif //DS5_BRIDGE_CONFIG_H
