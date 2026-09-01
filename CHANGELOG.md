# Changelog

All notable changes to this project are documented here.

## [1.38.2] — 2026-08-29

### Added

- **Player space gyro aiming.** *Horizontal source* on the Gyro tab gains a third
  option beside Yaw and Roll. Yaw and Roll each assume you hold the controller a
  particular way: turning the pad drives the aim only while it is roughly flat,
  and tilting it sideways only while it is roughly upright. Hold it any other way
  and part of your movement lands on the wrong axis — a level sweep starts
  dragging the cursor diagonally, and at 90° it barely turns the view at all.

  Player space uses the **accelerometer** to work out which way is down, then
  asks the question that actually matters: how much did that movement turn you
  about the world's vertical axis? Flat, tilted, rolled onto its edge — the aim
  behaves the same, and there is no axis to choose and then hold the controller
  to suit.

  Both axes are corrected, so vertical aim stays vertical when the pad is tilted
  rather than picking up part of a horizontal turn. Gravity is low-passed, so a
  knock does not throw the aim, and it falls back to plain yaw if the reading is
  not sane gravity — during a hard shake or free fall.

  Yaw and Roll are unchanged and remain the default.

- **Live accelerometer readout** in the Device tab diagnostics: raw X, Y and Z.
  At rest one axis sits near ±8000, which is gravity. It is what player space
  reads, and it is the fastest way to confirm the sensor is being decoded
  correctly on a given controller.

## [1.37.2] — 2026-08-28

### Added

- **Touchpad as Mouse.** Relative, trackpad style: the pointer follows how far
  your finger *moves*, not where it is, so you can lift and reposition the way
  you would on a laptop touchpad. Settings are **Touchpad Speed**, a **jitter
  filter** so a resting finger does not make the pointer drift, invert per axis,
  and an optional **trackball** mode that keeps the pointer gliding after you
  lift off, decaying at a **friction** you set — enough to cross a large screen
  from a single flick.

  **Clicks are deliberately not part of it.** The touchpad-click halves are
  already macro triggers and can output mouse buttons, so left and right click
  are one macro row each and you choose which half is which — more flexible than
  a fixed corner, and it already exists.

  It shares the pointer accumulator with gyro aiming and Stick to Mouse, so the
  three add rather than fight, and it carries sub-count movement between ticks so
  slow drags still register. Turning it on adds the mouse HID interface, so the
  device re-enumerates once.

  There is no on/off chord: enablement is per profile, and profiles load per
  game, so a desktop profile can have it on while every game profile leaves it
  off.

  The portal warns if **Touchpad as Mouse** is on while gyro activation is set to
  one of the two touchpad-based schemes, since both want the same finger: *only
  while the touchpad is touched* adds wrist movement to every drag, and *ratchet*
  pauses the gyro exactly when you are moving the pointer. It is a warning rather
  than a block — taking turns may be what you want on a desktop profile — and the
  other activation schemes gate on triggers or shoulders and compose cleanly.

### Fixed

- **A short override entry captured longer game names.** `profile-overrides.txt`
  matches a fragment anywhere in the title and the first hit won, so `God of War`
  also matched `God of War Ragnarök` and loaded the 2018 game's audio-mix slot —
  overriding a correct native classification, and making the result depend on
  line order in a file that says nothing about ordering. The **longest** matching
  fragment now wins, so both games can be listed in any order, and the start log
  names any shorter entry it passed over.

### Changed

- `utils.h` names two previously unknown fields in the controller output
  report — `AllowEdgeProfileSwitchControl` (byte 38 bit 6) and
  `EdgeProfileSwitchMode` (byte 40), identified by SundayMoments/DS5_Bridge and
  not verified here. Documentation only; the firmware does not set them. The old
  note guessing byte 40 was `HapticLowPassFilter` off by one was wrong — that is
  at 39.0.

## [1.36.0] — 2026-08-27

### Changed

- **Lightbar Off now applies in every haptics mode**, and has moved from the
  Haptics section to **Device & Connection**. It was gated on Replace mode for a
  reason: if you are using Replace you are probably not running DS4Windows, which
  is what would otherwise let you set the lightbar to black. Outside Replace the
  assumption was that DS4Windows already had it covered.

  That assumption no longer holds for everyone, and which haptics mode is running
  has nothing to do with whether you want the light on.

  **This turns the lightbar off in previously saved profiles that had the setting
  ticked**, which for an audio-mix profile is what the setting always said it
  would do. The case that changes the wrong way is a **native** profile carrying
  the same ticked setting, where the light should stay on.
  `native-off.autoapply.html` has been corrected; **check your own saved profiles
  and slots** — any native profile with *Lightbar Off* ticked needs it unticked
  and re-saved, or the lightbar will go dark in the games you least want it to.

  A battery notification still overrides the setting for the seconds it runs.

## [1.35.2] — 2026-08-27

### Fixed

- **The colour stayed lit after a notification.** The controller latches
  whatever colour it was last sent and does not revert on its own, so ceasing to
  override left the last frame lit indefinitely — including after a Test, and
  regardless of whether the stage or the whole feature was switched off. The
  earlier claim that it "restores itself" was wrong: it only restored if
  something else happened to send a report, and on an idle desktop nothing does.
  Reports now keep going out for a short tail after the last pulse, *without*
  the override, which actively writes the real colour back.

### Changed

- **It is a pulse, not a blink.** Brightness fades up and back down over about
  1.6 seconds rather than switching hard on and off — the same gentle breath
  DS4Windows uses for its low-battery indicator. A hard flash at this size reads
  as a fault light rather than a prompt to charge.

## [1.35.1] — 2026-08-27

### Fixed

- **The battery notification never reached the lightbar.** Reports to the
  controller are only composed once the *host* has sent an output report, so on
  an idle desktop with no game driving the controller nothing composed one at
  all — the notification ran in the firmware and was never carried anywhere. It
  now composes a report of its own while a notification is running. This is why
  the Test button appeared to do nothing, and why turning *Lightbar Off in
  Replace Mode* off changed nothing: the fault was upstream of the lightbar
  override entirely.

- The stage rows are drawn inside the **Battery Notification** card, under the
  switch they belong to, instead of after every other card at the foot of the
  page with the Save button in between.

## [1.35.0] — 2026-08-27

### Added

- **Staged battery notification on the controller's lightbar.** Three stages,
  each with its own battery level, blink count and colour — 5 amber blinks at
  50%, 10 red at 10%, or whatever suits you. A stage fires **once** when the
  battery falls past its level and then stops. It is a prompt to go and plug in,
  not an indicator that flashes until you do.

  Levels are in **10% steps**, because that is the resolution the DualSense
  reports its own charge at. A blink is one second lit and one second dark, slow
  enough to read from a sofa; the count is what you set, so 20 blinks runs for
  about 40 seconds.

  A stage re-arms when the battery climbs back above its level or the controller
  goes on charge, so a reading hovering on a boundary cannot re-announce itself.
  Switching a controller on reports its charge straight away: every stage it is
  already at or below pulses in turn, so you know where you stand before you
  start rather than when it dies mid-session.

  While a notification runs it takes the lightbar over, then hands it straight
  back: the colour bytes are simply left alone again, so whatever the game was
  driving returns on the next report with nothing to restore.

  Each stage is **one line** on the Device tab: a tick to enable it, the level,
  the blink count, a **colour picker** showing the colour it will use, and a
  **Test** button that runs that stage immediately — colours and counts are
  impossible to judge without seeing them, and the alternative is draining a
  controller to 20% to find out the amber is too dim. A test does not use up the
  real notification. The dongle blinks its *saved* settings, so save first.

  The per-stage tick is stored separately from the level, so turning a stage off
  keeps the level and colour you set for when you turn it back on.

  This is **additive to the Pico LED indicator**, which is unchanged and still
  blinks continuously below 10%. The two suit different distances — the Pico LED
  when the dongle is on the desk in front of you, the lightbar from across the
  room — and most people will want one or the other rather than both.

## [1.34.0] — 2026-08-27

### Added

- **Double tap** — a macro row can fire on two presses of its trigger within
  250 ms, adapted from upstream's button-shortcut work. Tick *double tap* on a
  button row; it is not offered on hold rows (driven by the held set) or long
  presses (resolved by duration), and the portal clears the others if you tick
  it.

  **It costs nothing unless you use it.** A single tap can only be resolved late
  if a double-tap row exists on the *same* trigger — until the window closes
  there is no way to know which was meant. So the wait is applied only to a
  trigger that has both: every other row still fires the instant the button goes
  down, and a table with no double-tap rows behaves exactly as it did before.
  A double-tap row on a different trigger does not slow this one down either.

  A double-tap row can output a **controller or mouse button** as well as keys.
  Those outputs are a state in the report the game reads rather than a keyboard
  sequence, so a one-shot now presses the button for 80 ms and releases it —
  long enough for a game polling once a frame to see it, far too short to read
  as a deliberate hold. It layers over whatever hold rows are injecting at the
  time, so a pulse and a held remap coexist. Before this, a one-shot row with a
  controller output sent an empty keyboard burst and did nothing at all while
  looking correctly configured.

  Selecting a controller output still switches *hold while held* on, since a
  remap almost always wants it — but no longer when *double tap* is already
  ticked, which would have cleared the choice just made.

## [1.33.1] — 2026-08-27

### Fixed

- **A held key was dropped for two reports whenever a hold macro was pressed.**
  A hold row is driven entirely by the hold set, and a guard exists to stop one
  also firing a one-shot burst — but the guard was applied only on the release
  path, not when the chord is first pressed. A burst writes *only* its own keys,
  so it overwrote everything else being held: with a stick row holding A,
  pressing the button emitted `[dodge]`, then a blank release, then `[A, dodge]`.
  At the instant the game saw the dodge key go down, the direction was gone, so
  it used its default direction; the direction was back two reports later, which
  is why every repeat after the first was correct and why a real keyboard —
  which never releases the direction — was right every time.

- **Unticking the last macro left the stick centred until a reconnect.** The
  engine short-circuits when no macro is enabled, and that path skipped the only
  code that clears the stick suppression, so the flag kept its last value. With
  *centre stick always* that value is permanently on, and the firmware went on
  centring the stick for the game until `macro_reset()` ran on reconnect.
  Unticking *centre stick always* before disabling the macro avoided it, because
  the flag was already clear by then. Keys held by a row that is disabled while
  they are down are now released rather than left stuck.

## [1.33.0] — 2026-08-26

### Added

- **Centre stick always** — a new option on a stick macro row, beside *hide
  input from game*. With *hide* alone the stick is only centred for the game
  while a direction is past the threshold, so at rest the pad's own jitter still
  reaches the game as a stream of small, constantly changing axis values. A
  game's dead zone stops that from moving the character, but prompt detection
  usually reads raw deltas before the dead zone — so the on-screen button prompts
  flip back to controller glyphs while the macro's keys say keyboard, over and
  over. Nothing in the game's settings can fix it; the values have to stop
  leaving the dongle. Turn this on and the stick is centred for as long as the
  row is enabled.

  It is **opt-in**, and rows saved before this release are unchanged. The old
  behaviour is a legitimate hybrid — fine pressure walking the character as
  analog while a hard push fires a key — and switching it on by default would
  break those rows silently.

- **Live test panel on the Macros tab.** The portal reads the same interface the
  game does, so it can show the report *after* the firmware has rewritten it:
  buttons, sticks, triggers, touchpad and the Edge Fn buttons and paddles, with
  the observed report rate. Alongside it, the macro engine's own view of what it
  is hiding and injecting this tick — moved here from the Device tab, where it
  sat one tab away from the rows it describes. Together they separate "the rule
  never fired" from "the rewrite never reached the report".

  The stick threshold is drawn as a ring, the two-stage boundary as a line on
  the trigger bar, and the touchpad's neutral band is shaded with the last
  press's landing point marked — the position that decides which half fires.

### Fixed

- `tools/t2-tests` had not compiled since 1.32.8, when the rewrite gate began
  calling `macro_report_active()` that the harness does not stub. It reported
  nothing from then on. Both missing stubs added; the suite passes again.

### Tools

- `tools/run-portal-tests.sh` now runs `tools/portal-buttons-test.js`, which had
  shipped since 1.29.1 without ever being invoked by the release gate — the
  second time a test file has been added without being wired in, after
  `portal-motion-test.js` in 1.20.0 (and `t2-tests` above makes three ways a
  suite can go quiet). Every `portal-*.js` in `tools/` is now invoked, and the
  runner's stale "three regression harnesses" comment is corrected.

## [1.32.11] — 2026-08-02

### Fixed
- **Apply did nothing in the manual trigger picker.** It called a helper that
  does not exist, so the handler threw before closing the panel: the panel
  stayed open and the button looked dead. The pick had already been written to
  the row, which is why pressing Cancel afterwards appeared to apply it - Cancel
  closed the panel and the re-render showed the new chord. It now clears its
  state and re-renders, the same way the keyboard picker always has, so Apply
  and Cancel both close the panel immediately.

### Changed
- **The macro output readout now reports keys and mouse buttons too.** The
  "injecting" figure only ever covered CONTROLLER buttons, because that is what
  the inject mask holds - keys go out on the keyboard interface and mouse
  actions on the mouse one. A keyboard remap therefore read as "injecting
  nothing" while working perfectly. The line now reads "sending" and lists
  controller buttons, how many keys are held, and mouse buttons.

## [1.32.10] — 2026-08-02

### Fixed
- **The macro output readout ignored stick remaps.** A stick macro does not
  touch the button suppress mask - it centres its stick through separate flags -
  so a stick mapped to keys showed as "nothing hidden" in the diagnostic even
  while it was hiding the stick correctly. The readout now reports centred
  sticks alongside hidden buttons, names injected buttons instead of printing a
  bit pattern, and shows whether the report is being rewritten at all this tick.

## [1.32.9] — 2026-08-02

### Added
- **Live macro output readout** in the Device tab diagnostics: which buttons the
  macro engine is hiding and injecting at this instant. Hold the button that
  refuses to hide and read it - if the button is not listed as hidden, the rule
  never fired; if it IS listed and the game still sees the press, the report is
  being rewritten and something downstream is re-adding it. That splits a
  "hiding does not work" report into two very different faults instead of
  guessing which one it is.

## [1.32.8] — 2026-08-02

### Fixed
- **"Hide input from game" was skipped entirely unless a trigger feature was
  also configured.** Reported as "L3 and R3 will not hide", which is how it
  presents in practice even though the cause is not specific to those buttons.

  **What was wrong.** Before sending each report the firmware decides whether
  the report needs rewriting. That decision asked only about TRIGGER settings -
  dead zones and two-stage modes - but the very same path is what applies macro
  suppression, macro button injection, analog trigger passthrough and centred
  sticks. On a profile with no trigger feature enabled, the report took the
  untouched fast path and every macro rewrite was discarded: the macro's keys
  were still sent, because those go out on the keyboard interface which is not
  affected, while the original button went to the game as well. Two inputs, one
  press.

  **Why it looked specific to L3 and R3.** The defect hides nothing selectively -
  when it triggers, no button is hidden. What differs is whether you NOTICE:
  L3 and R3 are usually bound to sprint and melee, so an unhidden press does
  something obvious on screen, while a stray Square or D-pad press in the same
  test often does nothing visible at all. Two further things make it look
  inconsistent between setups: real-time polling always rewrites the report and
  was never affected, and trigger dead zones are per profile - so the same
  macro row hides correctly on one profile and not on another, depending on
  settings that have nothing to do with macros.

  **The fix.** The check now also asks the macro engine whether it has anything
  to write this tick, so the report is rewritten whenever a macro needs it
  regardless of trigger settings or polling mode. If a button still reaches the
  game after this, the live macro readout added in 1.32.9/1.32.10 will show
  whether the firmware is hiding it, which separates a rule that never fired
  from something downstream re-adding the press.

## [1.32.7] — 2026-08-02

### Changed
- **The gyro angle check and counts-per-360 measurement moved to the Gyro tab**,
  into a *Gyro calibration* card, next to the settings they calibrate. They were
  on the Device tab with the wake diagnostics, which meant setting up gyro
  aiming involved hopping between tabs. Wake troubleshooting stays on Device.

## [1.32.6] — 2026-08-02

### Fixed
- **A second counts-per-360 measurement skipped the linearity check and reused
  the previous run's readings.** The recorded pair was never cleared, so once
  both amounts had been measured, every later measurement found them already
  present: it went straight to Apply and compared the new reading against a
  stale one. Sending the full amount now starts a fresh run, applying a result
  ends one, the readings recorded so far are shown, and there is a clear link.

## [1.32.5] — 2026-08-02

### Fixed
- **The counts-per-360 burst was sent too fast to measure with.** At 40 counts
  per report it delivered ~10,000 counts/second, and a game that samples the
  mouse once a frame - or applies any smoothing - drops part of that. Lost
  counts make the view turn LESS than it should, which quietly inflates the
  calculated value instead of failing visibly. The sweep now runs at ~2,000
  counts/second, slow enough for any game to see every count.

### Added
- **Linearity check** in the calibration panel: a second button sends HALF the
  counts, which must turn the view exactly half as far. If it does not, counts
  are being lost or scaled on the way in - by frame-sampled mouse reads, in-game
  smoothing, or Windows pointer acceleration - and no counts-per-360 measured
  through that is correct however carefully the angle is judged. This is the
  only way to tell that apart from a genuinely low in-game sensitivity, since
  both simply look like the view turning less than expected.

## [1.32.4] — 2026-08-02

### Added
- **Counts-per-360 measurement.** The dongle can emit an exact number of mouse
  counts on request; you report how far the view turned and the portal works out
  the value: counts_360 = sent x 360 / observed. That number belongs to the GAME
  rather than the controller, so it cannot be derived from the gyro - but it can
  be measured, which replaces the guess-and-correct loop behind the default of
  6500. It calibrates Flick Stick at the same time, since both read the field.
  The burst is metered out over many reports rather than sent as one delta:
  games clamp large jumps, and a smooth sweep is far easier to judge by eye.

## [1.32.3] — 2026-08-02

### Fixed
- **Natural sensitivity and the angle check integrated against an assumed
  report rate.** The gyro reports angular VELOCITY, so converting it to an angle
  needs the interval each reading covers. That interval was taken to be 1 ms
  scaled by the USB polling rate — but the samples arrive over BLUETOOTH, at a
  rate set by the controller and the link, not by how often the host polls USB.
  Measurements exposed it: a 90-degree turn read about 1.4x HIGH while a
  360-degree turn read about 2.3x LOW, which no scale error can produce. A fast
  turn packs its rotation into fewer reports and a slow one into more, so the
  error tracked the speed of the turn rather than the angle. Both paths now
  integrate over the real elapsed microseconds between samples, which removes
  the assumption; verified identical at 250 Hz and 1000 Hz sample rates.
- Samples separated by more than 100 ms are dropped rather than integrated, so a
  disconnect or a sleep cannot deliver one enormous jump.

### Added
- The angle check reports the **gyro sample rate it actually observed**, so the
  report interval is visible rather than assumed.

## [1.32.2] — 2026-08-02

### Changed
- **Natural is the default gyro scale on a fresh install**, and Arbitrary is
  renamed **Manual** — a clearer description of what it is (tune by feel, no
  calibration) now that the alternative is calibrated rather than nominal.
  Existing profiles keep whatever they stored.
- **The angle check measures a full 360 instead of 90 degrees.** Judging a
  quarter turn by eye is the least accurate part of the measurement, and a few
  degrees of human error is several percent of the answer. A full turn ends
  where it started, so the controller can be lined up against a desk edge and
  returned to it exactly, and the error is spread over four times the angle.

### Added
- **Gyro scale trim** (50-1000, default 100): corrects the sensor's assumed
  scale so 1.0x is genuinely 1:1 rather than nominally. The angle check reports
  the value to enter.

### Fixed
- **The angle diagnostic reported ten times the true angle** (a divisor of 100
  where it should have been 1000), and converted each report separately so slow
  rotation was truncated away. It now sums raw readings and converts once.
- The check feeds in **every** gyro mode, so the sensor can be calibrated before
  switching to Natural rather than after.

## [1.32.0] — 2026-08-02

### Added
- **Gyro natural sensitivity (real-world scale).** Gyro-to-mouse aiming can now
  be expressed as a ratio rather than an arbitrary slider: at **1.0x**, rotating
  the controller 10 degrees turns the in-game view 10 degrees. Set it once and
  it holds in every game sharing the same mouse counts per 360, instead of being
  re-tuned per game — the behaviour people expect from Steam Input and similar
  remappers. Typical play is 2.5x to 12x.
  - Uses the **counts per 360** value Flick Stick already needed, so games
    calibrated for one are calibrated for the other. That field is no longer
    labelled as Flick-Stick-only.
  - Separate vertical multiplier, 0 to follow the horizontal one.
  - The old arbitrary slider remains the default and is untouched, so existing
    profiles behave exactly as before.
  - Applies to gyro-to-MOUSE only. Gyro-to-stick is a rate control — the stick
    says how fast to turn, not how far — so a 1:1 rotation ratio has nothing to
    attach to, and the setting is ignored there.
  - The conversion is exact integer maths with the remainder carried, so slow
    movement is not truncated away and fast movement does not lose a fraction of
    a count per report. Verified against ideal values to within 0.06% across
    10-360 degree turns and 1x-12x, and identical at 250, 500 and 1000 Hz.
  - A **degrees-rotated diagnostic** is exposed so the 1:1 claim can be checked
    on hardware rather than trusted: rotate through a known angle and compare.

### Note
- Config field ids 0x01-0x7F are now fully allocated; new settings continue at
  0x80. The field-id space is a plain byte and separate from HID report ids.

## [1.31.1] — 2026-08-02

### Fixed
- **Recording a touchpad click could capture the opposite half.** 1.30.4 moved
  the firmware to decide the half from where the finger LANDED, but the portal
  was left deriving it from the position at CLICK time - the rule that was
  wrong in the first place. A recording made while the pressed-finger centre
  read on the other side of the middle produced a chord the firmware would
  never match, so the macro simply never fired. The portal now uses the
  touch-down position it already tracks for gestures, with the same neutral
  centre band, so what is recorded is what is matched.
-  checks the two rules against each other
  instead of the old click-time behaviour it was still asserting.

## [1.31.0] — 2026-08-02

### Added
- **Seven more macro output buttons: Create, Options, Touchpad click and the
  four D-pad directions.** The output list came from the two-stage trigger
  feature, which only ever needed face buttons, shoulders, sticks and trigger
  clicks; macros reused it and inherited the gap, so a macro could TRIGGER on
  Create or a D-pad direction but never SEND one. Mic and PS are deliberately
  still absent - PS collides with the PS-shortcut feature and Mute is a state
  toggle rather than a momentary button.
  - The D-pad needed real work rather than another bit: it is a hat ENUM in the
    report, so the four directions share one nibble. Injected directions are
    merged with whatever the player is physically holding and the nibble is
    re-encoded once. An injected direction WINS over a held one on the same
    axis - a macro that says "press Up" should press Up even if the player is
    leaning down - the other axis is preserved, and opposite injected
    directions cancel, since the hat cannot express both.
  - The new values are numbered from 16, ABOVE the mouse outputs at 11-15,
    because those numbers are already persisted in saved macros. Numbering the
    new buttons from 11 would have turned every saved "left click" into a
    gamepad button. `MOUT_FIRST` no longer derives from the button count for
    the same reason.

## [1.30.5] — 2026-08-02

### Added
- **Separate vertical speed for Stick to Mouse**, mirroring the gyro vertical
  sensitivity: 0 keeps it as one knob and follows the main Speed, any other
  value sets the vertical rate independently. Worth having for the same reason
  it is on the gyro — a game vertical aiming range is far smaller than its
  horizontal one, so the gain that feels right for turning is usually too fast
  for looking up and down. Half of Speed is a sensible starting point. The
  deadzone and response curve still act on the overall stick magnitude, so
  diagonals keep their direction and simply travel further horizontally than
  vertically.

## [1.30.4] — 2026-08-02

### Fixed
- **Touchpad click halves, properly this time.** 1.30.2 and 1.30.3 both took
  the half from the finger position AT CLICK TIME, which is the worst moment to
  sample it: the finger is flattened against the pad so its reported centre
  moves, and the switch chatters on top of that. No amount of latching or
  debouncing fixes a value that is wrong when it is read. The half now comes
  from where the finger LANDED - the position the gesture code already records
  at finger-down, milliseconds before the switch closes - and is frozen for the
  whole press. This is what DS4Windows does with touchpad zones, which is why it
  never had this problem.

### Added
- **Touchpad-click diagnostics** in the Device tab: the X where the finger last
  landed, how many clicks the firmware has seen, and which half the last one
  resolved to. Click each side a few times and the count should rise by exactly
  one per click with the half matching the side used - which turns "it fires
  both" into a measurement instead of a guess.

## [1.30.3] — 2026-08-02

### Fixed
- **A single touchpad click still fired both half-bindings, and a long-press
  fired its short action immediately.** 1.30.2 latched which half a click was on
  at the press edge, but it did so BEFORE the debounce, so it saw the switch
  chatter raw: the pad's contacts break and remake within a few milliseconds of
  a press, and because the finger's contact patch shifts slightly between
  bounces, the next edge could latch the OTHER half - one press fired both
  bindings, the second only briefly. The same chatter re-armed the chord on
  every bounce, resetting the long-press timer, so a macro set to hold fired its
  short action at once. The debounce now runs first and the latch sees one clean
  press, which is what DS4Windows does: decide the zone at finger-down, on a
  debounced press.
- **A half is latched only when it is unambiguous.** The debounce can briefly
  hold the previous half alongside the new one; latching that pair would fire
  whichever binding sorted first. When both or neither are present the click
  stays unqualified for another report, which costs nothing since a press lasts
  far longer than one report.

## [1.30.2] — 2026-08-02

### Fixed
- **Left and right touchpad clicks both fired from one press.** Which half a
  click was on was re-derived from the finger position on EVERY report, so a
  finger that drifted across the middle while the pad was held - or a click near
  the centre, where the reported X jitters - flipped the qualification mid-press
  and triggered both bindings. With quicksave on one half and quickload on the
  other, a save was reliably followed by a load. The half is now decided once,
  on the press edge, and held until release: a click is one gesture, and the
  half it started on is the half it is, however the finger wanders afterwards.
- **Clicks near the centre line are no longer a coin toss.** A neutral band of
  roughly 8% either side of the middle leaves a click unqualified, so a binding
  on a specific half does not fire and only a generic touchpad-click binding
  catches it. Doing nothing is the right outcome when the intent is genuinely
  not readable - especially when the two halves do opposite things.

## [1.30.1] — 2026-08-02

### Fixed
- **Stick-to-mouse Speed 255 was the SLOWEST setting, not the fastest.**
  Validation treated 0xFF as the "unset" fill and reset it to 0, which means
  "use the default" - so the top of the range silently became 600 counts/s. The
  sentinel was never needed: a slot saved before these fields existed has the
  feature itself clamped to off, so its speed value is never read.
- **Typed values above a setting's range wrapped instead of clamping.** The
  min/max on a number box only style the spinner; a typed value went through
  and was then masked to the field width, so entering 2000 sent 208. Values are
  now clamped to the declared range and the box is updated to show what was
  actually sent.

### Changed
- **Speed is now mouse counts per second directly, 0-20000** (was a byte
  holding tenths, capping the feature at 2550/s - short for a fast-turning
  game). **Existing values will read low after this update: multiply your old
  number by 10.**

## [1.30.0] — 2026-08-02

### Added
- **Stick to Mouse.** Drive the mouse pointer from the right or left stick, the
  way gyro aiming already can. Both feed the same accumulator, so the stick can
  do the large turns while the gyro does the fine aim - the usual reason to want
  this. The chosen stick is centred in the report the game sees, so the game
  does not also turn from it.
  - **Speed** in counts per second at full tilt (stored /10; 0 = 600/s).
  - **Deadzone** as a percent of full tilt, applied radially rather than
    per-axis so a diagonal push just past the threshold does not jump. Without
    it a resting stick makes the view creep, because a mouse never stops.
  - **Response curve** as an exponent (10 = linear, 18 = default). A linear
    stick is twitchy at the centre and slow at the edge; the curve is what makes
    this feel like a mouse rather than a joystick. It is applied to the
    magnitude, not per axis, so diagonals are not bent toward the axes.
  - **Invert** per axis.
  - Sub-count movement is carried between ticks, so slow stick pressure still
    moves the pointer instead of being truncated to nothing.
  - Mutually exclusive with Flick Stick, which claims the right stick for
    itself; selecting one clears the other rather than leaving two owners
    writing the same report.
  - Turning it on or off adds or removes the mouse HID interface, so it
    re-enumerates - the same as switching gyro output to mouse.

## [1.29.3] — 2026-08-02

### Fixed
- **Every slot activation re-enumerated the device.** The check for whether a
  slot changes something enumeration-critical compared the LIVE config, which
  `config_valid()` has normalised, against the slot's RAW stored bytes. Any byte
  a slot holds unclamped therefore differed forever - and a slot saved before a
  field existed carries 0xFF there. `gyro_output` is the clearest case: 0xFF
  reads as "mouse interface needed" while the live clamped value is 0, so the
  activation looked like an interface change every time, including
  re-activating the slot that was already loaded. Both sides are now clamped
  before the comparison, so the device only reconnects when something really
  changed.
- **Macros and slots came back blank after a reconnect.** When an activation
  did legitimately re-enumerate, the portal kept the macro table it had read
  from the handle that just disappeared, so the panels rendered stale state
  until the portal was reopened. The caches are dropped before reacquiring and
  the macro table is re-read on the new handle.

### Note
- Saving the FIRST macro (or removing the last one) changes which HID
  interfaces the device exposes, so it re-enumerates by design. Windows and
  Sony's app briefly see the controller disappear and return with a different
  shape; the app in particular may need to be restarted, or another profile
  loaded, before it lists the controller again. Adding further macros after the
  first does not re-enumerate.

## [1.29.2] — 2026-08-02

### Fixed
- **"Hide input from game" now works for the Edge buttons and pad-click halves.**
  The suppression table in `main.cpp` stopped at Mute, so a Replace macro bound
  to an Fn button, a paddle or a qualified touchpad click fired correctly but
  could not hide the press - the game still saw it. All bits the decoder
  produces can now be suppressed. A suppressed pad-click half clears the click
  bit only (never the other half, since suppression is only engaged while that
  half is held) and leaves the touch coordinates alone.

## [1.29.1] — 2026-08-02

Macro input fixes on top of 1.29.0.

### Fixed
- **Edge Fn buttons and paddles were never captured when recording a macro.**
  The firmware decoder and the portal's button table both listed them, but the
  portal's live capture decoder still stopped at bit 18 - so the firmware could
  match an Fn or paddle chord that recording could never produce.
- **The touchpad click could not be recorded as a trigger at all.** The click
  bit was stripped from every committed chord, not just from the test that
  decides swipe-versus-chord, so clicking the pad with no movement recorded
  nothing: no gesture, and an empty chord. The bit is now kept in the chord and
  only the swipe decision ignores it, so the click works alone, in a chord, and
  with hold.
- **A half-specific click records as the half alone.** Recording a left click
  produced the chord "Touchpad click + Touchpad click (left)" - two buttons for
  one press - because the controller reports the generic bit alongside the
  qualified one. Recording now drops the generic bit whenever a half is
  present, and the manual picker treats the generic click and the two halves as
  mutually exclusive. Matching is unaffected: the controller still reports all
  the bits, so a chord naming only the half matches, and firmware ranking now
  counts a named half as extra specificity so a one-button half chord still
  beats a one-button generic chord instead of tying with it.
- **The touchpad click can now distinguish left from right.** The pad is one
  physical switch, but the finger position is in the same report, so a click is
  qualified as left or right by which half the finger was on (new logical bits
  23/24, appended). The generic click bit is still set on every click, so chords
  recorded before this keep matching either half, and a half-specific chord wins
  over a generic one. A click with no finger reported - knuckle, pad edge -
  stays unqualified rather than guessing. Firmware-side, so it arrives with the
  1.29.1 build.
- **Added a manual trigger picker** (*Pick* beside *Record input*): tick the
  trigger buttons by hand instead of holding them. Needed for anything the
  controller consumes before transmitting - an Edge paddle that still has an
  assignment in Sony's app is sent as whatever it was mapped to, so no amount of
  recording will see the paddle itself.
- **Added `tools/portal-buttons-test.js`**, the cross-check `input_buttons.h`
  has always referenced: it verifies the portal's bit table and live decoder
  against the header, and that every bit the firmware decodes is reachable from
  the portal. It fails on the pre-fix code with 9 errors.

Reflash both boards. Config version stays at 19 and no `flash_nuke` is needed.

### Added
- **DualSense Edge Fn buttons and paddles as macro inputs.** All four sit in
  report byte 9 and were never decoded; they now join the logical button mask as
  appended bits, so existing macros keep their meaning.
  - **Fn + D-pad** is the combination worth using. Sony's app claims Fn + a face
    button for switching the controller's on-board profiles, so chords built there
    compete with it, while Fn + a D-pad direction is unclaimed.
  - A paddle with an assignment in the Sony app still sends that assignment: the
    controller applies its own mapping before the report reaches the dongle.
  - A standard DualSense never sets these bits, so decoding them costs nothing.

## [1.29.0] — 2026-08-22

## [1.28.4] — 2026-08-22

Reflash both boards. Config version stays at 19 and no `flash_nuke` is needed.

Supersedes the 1.27.x and 1.28.0-1.28.3 development builds, which were not
released.

### Added
- **Browser audio bridge (test mode).** Feeds PC audio to the dongle from the
  portal itself, so auto-haptics can be tried without installing Python. Nothing
  is uploaded — the audio is routed inside the browser and out to the dongle, and
  the screen share is only how Chrome exposes system audio.
  - **Not a replacement for `ds5audio.py`.** The share dialog cannot be automated,
    so it cannot start with a game; the feed is stereo, so `--map rear`, the ch2/3
    DSP source and native passthrough on ch2/3 stay script-only; and it needs
    Chrome on Windows.
  - Requires the **hosted** portal. A locally saved copy has no persistent origin,
    so the browser forgets the audio permission immediately and never lists any
    output device.
  - Share the **entire screen** and tick **Share system audio** — it is off by
    default, and the capture is silent without it.
  - A live signal readout polls the dongle's own level, so it shows what actually
    arrived rather than what the browser sent.
- **Separate vertical gyro sensitivity.** Leave it at 0 and both axes use the
  existing Sensitivity setting. A lower value is the usual choice: a game's
  vertical aiming range is far smaller than its horizontal one.

### Fixed
- **Profile slots could show as empty after a re-enumeration.** Reacquiring the
  device took the first handle it found without checking it worked. The browser
  can hand back the pre-reconnect object, which reports itself as open while every
  read silently returns nothing — so the slot sweep saw empty slots and drew an
  empty list. The handle is now proven with a read before it is adopted.
- **The reconnect used by every profile switch went off the USB bus for only
  150 ms**, against a host port debounce of about 100 ms. The identity-change path
  has used 250 ms since v1.18.12 for exactly this reason; both now match. A missed
  disconnect leaves the host on the old descriptor, so a setting that adds or
  removes an interface appears not to take effect.
- **The configuration could not grow past one flash page.** The sector is erased
  whole, but only the first 256 bytes were ever programmed, so `Config_body` was
  capped far below what the storage allows. The write length now follows the
  struct, and the assert sits on the real ceiling — the profile slot stride.

### Known
- Switching slots while the browser audio bridge is running can produce a feedback
  howl. Set the profile first, then start the bridge.

## [1.26.3] — 2026-08-21

Reflash both boards. Config version stays at 19 and no `flash_nuke` is needed.
Existing macros are migrated in place on first boot — the on-flash record grows
from 33 to 35 bytes.

Supersedes the 1.24.3–1.26.0 development builds, which were not released.

### Added
- **Remapping.** A macro row can now replace an input rather than only adding a
  keystroke to it.
  - **hold while held** keeps the output asserted for as long as the input is
    held, instead of firing once. Without it a remap taps its target on release.
  - **hide input from game** removes the original input from the report, so the
    game sees only the replacement.
- **Controller buttons as an output** — any face, shoulder, stick or trigger
  button. More reliable in-game than a keystroke: a game that sees a DualSense is
  in controller mode, and many ignore the keyboard entirely or flip every
  on-screen prompt when one arrives.
- **Mouse as an output** — left, right and middle click, scroll up and scroll
  down. Clicks are held while the input is held so click-and-drag works; scroll
  sends one tick per press. Choosing a mouse output makes the dongle present a
  mouse, so the controller re-enumerates — but only on **save**, never while a
  macro is being edited.
- **Sticks as an input.** One row drives four outputs, one per direction.
  Diagonals press both, and each axis has its own threshold with hysteresis so a
  stick resting on the edge does not chatter.
- **Trigger-to-trigger remaps stay analog.** `L2 → R2` carries the travel across
  rather than collapsing a variable throttle into an on/off switch. Any other
  input driving a trigger is a full press, since there is no travel to copy.
- **Search** in the portal, covering both settings and whole panels — searching
  for *gesture*, *wasd* or *backup* now finds the feature, not just fields.

### Changed
- **Record and Pick work the same way for every output kind.** Choosing keyboard,
  controller or mouse decides what they capture; previously the keyboard had
  Record and Pick while the controller side had a dropdown, so one row asked the
  same question two different ways.
- Selecting a controller or mouse output turns **hold while held** on, since that
  is what a remap almost always means. The checkbox stays editable — a chord that
  taps a button once is still a legitimate thing to build.

### Fixed
- **A remap showed a long-press time it does not use.** The threshold only
  applies to a burst macro; a hold row is driven by a different path that reads
  neither the flag nor the value, so a recorded "(hold 1.18s)" on a remap
  claimed a delay that never happened. It is no longer shown there, and turning
  hold on clears it rather than leaving it to reappear later.
- **Importing a profile could not describe a remap.** The notice that names what
  each enabled macro would fire read the keyboard combo, which is empty on a
  controller or mouse output - so exactly the rows that press a button or click
  for you were listed as "?".
- **"hide input from game" did nothing for L2 and R2.** Suppression cleared the
  digital click bit but left the analog axis untouched, and games read the
  triggers as axes — so the trigger stayed fully visible while every other button
  hid correctly.
- **Upgrading from 1.20.0–1.24.2 would have discarded every macro.** The record
  grew to 35 bytes and the migration handled the 28-byte layout but not the
  33-byte one, which is what every device in that range holds; unrecognised
  lengths are refused rather than guessed at, so the table was dropped.
- **Switching gyro output straight to Mouse + Flick Stick did not re-enumerate.**
  The check tested for one specific value rather than the range that needs the
  mouse interface, so the interface appeared without the host being told.

## [1.24.2] — 2026-08-20

Reflash both boards. Config version stays at 19 and no `flash_nuke` is needed —
the new settings are appended at the tail of the struct, so existing settings and
every saved slot load untouched.

Supersedes the 1.23.x and 1.24.0–1.24.1 development builds, which were not
released.

### Added
- **Gyro output selector.** Motion aiming can now drive a **mouse** instead of the
  right stick. A mouse takes deltas, which is what a gyro natively produces, so it
  is finer at low speed and never pegs on a fast turn — where the stick is an
  absolute input with a dead zone and a limited range. Selecting it adds a HID
  interface, so the controller re-enumerates once, and most games need the pad
  hidden before they will read mouse input alongside it.
  - Sensitivity is matched across polling rates: the same wrist movement turns you
    the same amount at 250 Hz and at real-time.
- **Flick Stick**, implemented to Jibb Smart's specification (he invented it).
  Push the right stick in a direction and the view snaps to face that way; hold it
  and rotate to keep turning. Fine aim stays with the gyro, which is what the
  design intends. The stick's own output is removed from the report so the game
  does not turn from it as well.
  - Reference constants are the shipped JoyShockMapper defaults: 90% flick
    threshold, 0.1 s flick time, ease-out with no ease-in, and soft tiered turn
    smoothing.
  - Works with **Gyro Mode** off, since it is a stick feature — but the gyro is
    what makes it worth using.
  - **Calibration is per game.** Faking a flick with mouse movement means
    converting an angle into a number of counts, so the dongle needs to know how
    far a full turn is: `(cm per 360°) × (DPI ÷ 2.54)`. Defaults to 6500. Requires
    mouse acceleration off and raw input on.

### Changed
- **The portal and the slot-activation pages now identify the controller by its
  gamepad usage** rather than by vendor and product ID alone. The gyro mouse is a
  third HID collection sharing those IDs, so a plain match could open the mouse
  instead — every configuration read then failed, which showed as "FW: pre-1.0.5"
  and a device that could not be saved to or have slots activated. Selection falls
  back to the old behaviour for anything it does not recognise, so it is never
  stricter than before. Affects `ds5-config-portal.html`,
  `automation/profiles/slot-activate.html` and the copy embedded in
  `automation/ds5-setup.ps1` — update all three together.

## [1.22.0] — 2026-08-19

Reflash both boards. Config version stays at 19 and no `flash_nuke` is needed —
the new settings are appended at the tail of the struct, so existing settings and
every saved slot load untouched.

Supersedes the 1.21.x development builds, which were not released.

### Added
- **Two-stage triggers.** A second action part-way through the trigger's travel:
  pull to the boundary and the game sees the trigger as normal, push past it and a
  button press is added. Pair it with a weapon-break effect and the wall you feel
  *is* the boundary.
  - **Add** keeps the trigger held past the boundary; **Swap** releases it so only
    the button remains.
  - **Rescale** stretches the travel below the boundary over the full range, so a
    shortened first stage keeps its full analog resolution.
  - **Hysteresis** on the boundary: holding at the crossing point gives one press
    rather than a stream.
  - The second stage can press **the other trigger** — R2 can drive L2 and vice
    versa. A trigger is never offered as its own second-stage button.
  - The portal warns when the chosen button is also what gates that trigger's
    resistance, since pressing through the detent would open the gate arming the
    resistance being pressed against.
- **32 profile slots**, up from 24. Existing slots keep their addresses and
  survive the upgrade untouched — growth is downward into the sector reservation
  added in 1.19.0, so the macro table is unaffected. The portal's slot sweep grows
  from roughly 1.5 s to 2 s; activating a slot is unchanged.

### Fixed
- **A trigger chosen as the second-stage button did nothing.** Only the digital
  click bit in byte 8 was set, and games read L2/R2 as analog axes — the digital
  bits are barely used — so the press was invisible in both Add and Swap. The axis
  is now driven as well, taking the max so a real pull is never reduced.
- **A dead zone on the target trigger wiped the press.** Stage-2 presses were
  applied inside each trigger's own pass, so R2's press landed before L2's pass,
  whose dead-zone branch clears that same bit. Both triggers now decide their
  latch from the physical values first and the presses are applied afterwards —
  which also stops a synthetic full-scale value being read by the other trigger's
  latch as a real pull.
- **The automation slot bound was capped in a third place.** Raising the slot
  count needs four separate bounds updated, and the profile-restore path in
  `ds5-setup.ps1` was still limited to 1-16 — it had survived both the v1.17.1 and
  v1.18.18 fixes to the same class of bug. All five sites now agree, including the
  copy of `slot-activate.html` embedded in the setup script.

### Changed
- `automation/ds5-setup.ps1` and `automation/profiles/slot-activate.html` accept
  slots 1-32. Out-of-range values still log the valid range rather than falling
  through to a filename lookup.

## [1.20.0] — 2026-08-17

Reflash both boards. Config version stays at 19 and no `flash_nuke` is needed —
`Config_body` is unchanged. The macro table's on-flash record grows, and existing
macros are migrated in place on first boot.

### Added
- **Motion gestures.** Hold a button as a gate, flick your wrist with the controller,
  release — and a macro fires. One to four strokes of up / down / left / right.
  - **Recording calibrates to your own movement.** The portal measures how far you
    actually moved while flicking and stores a step size per macro, so a small
    flick and a broad sweep are each recognised as performed. A fixed threshold
    picked without hardware produced a storm of spurious strokes.
  - **Gyro aiming is suspended while a gate is held**, since the wrist movement
    that makes a gesture would otherwise swing the aim. It resumes on release.
  - Only the **start** of the capture window has to match the template. Holding
    the gate a beat after finishing turned a clean down-up into six strokes;
    trailing movement is treated as settling.
  - Single-stroke gestures match strictly, while longer ones tolerate one stray
    stroke inside the match. The tolerance is proportional to the template — flat
    slack made a drooping leftward flick match both *left* and *down*.

### Fixed
- **Upgrading from 1.19.x corrupted every macro name.** `MacroEntry` grew from 12
  to 17 bytes for the motion fields, which moved `label` inside `MacroRecord` from
  offset 12 to 17 — so migrating a record by copying its stored length flat landed
  the name over the new fields. A macro called `rivatuner` reloaded as `uner` with
  a `motion_len` of 118, which is the letter `v`. Migration now splits entry and
  label by the layout each record length actually had, and refuses an unrecognised
  length rather than guessing at the split.
- **`motion_len` was unbounded on the write path.** It indexes a two-byte array
  two bits at a time, so a value above the maximum of 8 read past the record into
  its neighbour. It arrives straight from the host in command `0x18`; it is now
  clamped where records enter the table rather than at each point of use.
- **The macro table loader refused shorter records instead of migrating them.**
  `macro.h` had always documented `rec_len` as making a record self-describing so
  that later firmware could read older tables — but the loader required an exact
  match, so the first time the record grew, every user's macros would have been
  discarded. The claim was in the comment and not in the code.
- **Exporting a macro dropped its gesture, and the result was worse than data
  loss.** The macro file format predates motion, so `motion`, `motion_len` and
  `motion_step` were not written or read. A motion macro survived a round trip as
  an ordinary *chord* macro — `macro_is_motion()` needs `GEST_MOTION` and a
  non-zero `motion_len` — so its gate button would have fired it on its own, with
  no gesture performed. Files written before 1.20.0 still import correctly, as
  non-motion macros.
- **`portal-motion-test.js` was not invoked by `run-portal-tests.sh`.** The file
  shipped and reported nothing while the suite reported success. Now wired in.


## [1.19.1] — 2026-08-14

**Portal only.** The firmware is unchanged from 1.19.0 — same `.uf2` files, same
config version 19. Replace `ds5-config-portal.html`; no reflashing, and nothing
to re-save.

### Added
- **Export macros / Import macros**, on the Macros tab. Macro *definitions* are
  device-global and shared by every profile, so they get their own file rather
  than riding along inside a profile.
  - Importing replaces every macro on the dongle, for all profiles, so it always
    confirms first. It loads into the editor and waits — nothing reaches the
    device until you press **Save macros to device**.
  - Row numbers are preserved through a round trip. The enable mask addresses
    rows by index, so compacting an export would silently rebind every profile.
- **Back up all slots / Restore** now ask whether to include macro definitions.
  Which macros each slot enables was already covered — that is an ordinary config
  field — but the definitions live outside `Config_body`, so without this a
  restore produced enable masks pointing at whatever table the target dongle
  happened to hold. Backup format v2 → v3; v2 files restore exactly as before.
- **Importing a profile now asks before changing your macro selection**, and lists
  what that selection would switch on, by name. A profile carries only the mask, so
  it enables *your* row 3 — which may be something entirely different from what the
  profile's author had there. Decline and your current macros are left alone while
  every other setting still imports.
- **The Playnite auto-apply page no longer carries macro information at all.** It
  runs unattended on every game launch with nobody to answer that question, and
  `macro_disable` is enumeration-critical at its all-disabled boundary — the same
  mid-apply interruption hazard already documented for `enable_wake` on the
  field-by-field `.html` path. Older exported pages that contain the field are
  ignored rather than applied. Per-game macro sets work through slot activation,
  which applies the whole configuration in one command.

- **Yes/No dialogs** for the macro questions, replacing the browser's
  OK/Cancel. Three of them ask about one *part* of an operation already under way
  — whether to include definitions in a backup, whether to take a profile's macro
  selection — where "Cancel" reads as "abort the whole thing". The buttons now say
  what they do: *Include* / *Slots only*, *Apply selection* / *Keep mine*.

### Fixed
- **"Enable state changed" stayed on screen after saving.** The save itself
  worked; the panel was drawn from a stale snapshot. `saveAll()` refreshes the
  snapshot and deliberately returns without re-rendering when nothing about the
  USB descriptor changed, so the macro panel now repaints itself after a save.
  Covered by a regression test in `tools/portal-macro-test.js`.

### Notes
- The Playnite auto-apply page still carries only the enable mask, never
  definitions, and this is deliberate: it runs unattended on every game launch,
  so one stale export could otherwise overwrite macros made since.

## [1.19.0] — 2026-08-14

Config version 19. **No `flash_nuke` needed.** One new config field is appended at
the tail of the struct, so existing settings and all 24 slots load untouched.

> **Re-save your slots.** `macro_disable` is a new config field. Any slot saved
> before 1.19.0 carries no value for it and will load as "no macros enabled", so a
> later profile apply reverts the macro set you just chose. Save each slot once
> after setting its macros.

### Added
- **Macros.** Bind a controller button press/combo or a touchpad swipe to a keyboard combo —
  `R3 + D-pad Up` sends `Ctrl+J`, a swipe sends whatever you assign. Up to 32,
  edited on a new **Macros** tab. The dongle sends the keystrokes itself over the
  HID keyboard interface the wake feature already provides; nothing runs on the PC.
  - **Recorded, not typed in.** *Record input* captures the actual buttons you
    hold or the swipe you make; *Record output* captures the combo you type on
    your real keyboard.
  - **Tap or hold is decided by the recording.** Tap the controller button and it fires on
    press; hold it for ~0.5 s or more and it becomes a long-press macro with the
    duration you held as the threshold. The same button press can carry both, the way
    the PS button does.
  - **Release order is captured**, so `Alt+Tab` replays with Tab released before
    Alt.
  - **Touchpad swipes** — four directions, distinguished by starting half of the
    pad and one or two fingers.
  - **Names** — up to 15 characters, stored on the dongle, so they survive a
    cleared browser or a different PC.
  - **Definitions are shared, enablement is per-profile.** The macro table lives
    in its own flash sector and is common to every slot; only the enable bitmap
    (`macro_disable`, field `0x6c`) is stored per slot. Define once, then pick per
    game which are live — the Playnite automation switches macro sets with no
    extra setup.
  - The portal warns when one bound button press contains another, since the shorter one
    then makes the longer unreachable.
- **Host-side macro engine tests** at `tools/macro-tests/` — compiles the real
  `src/macro.cpp` against small fakes for TinyUSB, flash and time, with a fake
  flash sector so the storage path is exercised. Run `run-macro-tests.sh` after
  any change to the engine, alongside the portal harness.

### Fixed
- **Bulk config reads truncated any field wider than one byte unless it had been
  hand-added to a list.** The bulk reader (`0x0c`) carried its own field-id →
  length table with a `default: len = 1`; the length is now derived from the
  actual C++ type of the value, so any future field of any width is correct on
  arrival with nothing to keep in sync.
- **Unnecessary re-enumeration on slot switches.** The wake keyboard interface is
  shared by wake, the PS shortcut and macros, and those three were tested
  independently when deciding whether a reconnect was required. With wake already
  on, changing the macro set left the descriptor identical yet still dropped the
  device — on every Playnite slot switch. All three sites now test whether the
  interface is *present*, through one shared function.
- **`readAll()` left its change-detection baseline stale** whenever it fell back
  from the bulk read to the per-field path, which is most likely right after a
  reconnect. Beyond a spurious "unsaved changes" warning, this meant the next save
  compared against the previous device state and could trigger a reconnect that
  was not needed, or skip one that was.

### Changed
- Slot sectors now have an explicit reservation (`SLOT_SECTORS_RESERVED = 16`,
  room for up to 128 slots) enforced by `static_assert`, with the macro table
  placed below it. Raising `SLOT_COUNT` stays a one-constant change and can no
  longer silently overwrite a neighbouring region.

## [1.18.25] — 2026-08-11

### Added
- **Trigger effect visualizer in the portal.** Every trigger effect now renders
  as a diagram — trigger travel (0–100%) along the x-axis, force 0–7 up the y —
  drawn live under the builder and under the Read L2/R2 output. Resistance shows
  as per-zone steps, weapon-break walls mark where the break fires, bow effects
  show their ramp and snap point, and vibration is drawn as a band over the
  window where it actually plays. Sequenced stages get dashed hand-off lines, and
  a stage that never reaches its wall or snap because the sequencer hands over
  first is faded and marked ✕ — so a truncated wall or an unreachable bow is
  visible at a glance instead of read off a warning. Because effects can't be
  conveyed in text, the diagram can be screenshotted and shared. It reuses the
  existing effect-decode helpers, so the picture and the prose description always
  agree. Portal-only.
- **DualSense battery readout** in the Device-tab diagnostics — level and charge
  state (on battery / charging / full), read from the controller's own input
  report. New read-only HID field `0x68`; no new config field.
- **Auto-haptics activity + level meters** on the Haptics tab, right under the
  auto-haptics settings, so tuning is visible without hopping to another tab. An
  "Audio bridge: active / not detected" line
  reports whether audio is actually arriving on the dongle's USB audio endpoint —
  the most common reason auto-haptics "does nothing" is that `ds5audio.py` isn't
  running, and this makes that obvious. Two bars show the audio coming in (ch0/1)
  against the haptic the DSP is deriving from it, so "audio present but nothing
  derived" (mode off, or cutoff/intensity too low) is visible too — change a
  setting and watch "Haptics out" respond. All three are
  peak-hold and cleared on read, like the rumble diagnostics, so the one-second
  poll can't miss a short burst. New read-only HID fields `0x69`–`0x6b`; no new
  config field.
- Portal now links to game profiles and trigger effects, which can be directly loaded.

### Fixed
- **Vibration positioning in the effect reader.** A sequenced vibration stage was
  treated as position-independent, so both the text description and the new
  diagram placed it wrong. The reader now mirrors the firmware sequencer: in a
  multi-stage effect a vibration is a full stage sorted by its start zone, and its
  active window begins at the hand-off from the previous stage — e.g. a vibration
  set to zones 6–8 behind a wall that ends at zone 4 buzzes from ~40% travel, not
  60%. A lone vibration with no other stages still plays the whole time it's armed.
- **Connect no longer stalls or occasionally reads back a stale version with
  default settings.** The 1-second background poll (loaded profile, battery,
  auto-haptics) reads from the same `0x81` GET buffer as the full config read on
  connect, so if it fired mid-read it clobbered a reply — slowing the connect, and
  on the silent auto-reconnect leaving a stale firmware version and default-looking
  values. The poll is now suspended for the whole connect / auto-reconnect /
  auto-apply read sequence, and the auto-reconnect path now re-reads the firmware
  version too.

### Notes
- Firmware change (reports 1.18.25): reflash `ds5-v1.18.25.uf2` (Pico 2 W) or
  `ds5-v1.18.25-waveshare.uf2` (Waveshare RP2350B-Plus-W). On older firmware the
  portal degrades gracefully — the battery line and the auto-haptics meters stay
  blank; the visualizer and the reader fix work regardless, being portal-side.

## [1.18.24] — 2026-08-09

### Added
- **The portal now shows which profile is currently loaded**, right under the
  Connected status bar — visible on every tab, not buried in the Device-tab
  diagnostics. The dongle tracks the active slot in RAM and records it whenever a
  slot is activated, by the portal *or* by the Playnite automation, so the
  readout reflects background profile switches too, not only ones made in the
  portal. It shows the slot's name (e.g. "Control"), appends "(edited)" once a
  setting is changed away from the loaded slot, and reads "not from a saved slot"
  when the live config didn't come from one — including after a power cycle, since
  the marker is RAM-only. A 1-second poll keeps it current without a reload, and a
  fresh connect or reconnect always re-reads it. The "edited" check deliberately
  ignores the volume/gain fields the firmware syncs from hardware, so a hardware
  volume change — or the audio re-sync the host performs after a USB
  re-enumeration — is not mistaken for a user edit. New read-only HID command
  `0x67`; no new config field, so slots and existing configs are untouched.

### Notes
- Firmware change (reports 1.18.24): reflash `ds5-v1.18.24.uf2` (Pico 2 W) or
  `ds5-v1.18.24-waveshare.uf2` (Waveshare). On older firmware the portal degrades
  gracefully — the loaded-profile line simply stays blank.

## [1.18.21] — 2026-08-09

### Added
- **Right-stick inversion.** A new "Right Stick" setting inverts the physical
  right stick's X axis, Y axis, or both by rewriting the stick values in the
  input report the PC sees — so it works in any game with no PC-side software,
  and independently of gyro aiming (it applies whether or not gyro is on).
  Useful for inverted-look setups, or games that only offer inversion on one
  axis. New config field `rstick_invert` (id 0x65), appended at the struct tail
  so existing configs and slots are untouched. Contributed by AppendinoCom
  (PR #4) — thank you!

### Changed
- **Gyro invert is now two checkboxes** ("Invert X" / "Invert Y") instead of a
  0–3 number field. It is the same stored byte and the same effect — both boxes
  ticked equals the old "3" — so existing profiles carry over unchanged; it is
  simply easier to set. The Gyro tab is renamed "Gyro / Stick" to cover both
  controls, and both invert settings now carry hover descriptions. Also from
  AppendinoCom's PR #4.

### Notes
- First firmware change since 1.18.17 (1.18.18–1.18.20 were portal-only), so the
  version the portal reports now matches the release number again. This release
  requires reflashing: `ds5-v1.18.21.uf2` (Pico 2 W) or
  `ds5-v1.18.21-waveshare.uf2` (Waveshare RP2350B-Plus-W).

## [1.18.20] — 2026-08-09

### Fixed
- **The configuration portal loaded slowly after waking the PC, and stayed
  slow until it was closed and reopened.** On connect the portal reads the
  whole configuration in one bulk transfer, falling back to reading every
  field one at a time if that fails. Right after a host wake the Bluetooth
  link between the controller and dongle is still settling, so the bulk read
  dropped a packet and returned nothing — and the portal took that transient
  miss as "this firmware has no bulk support" and disabled the fast path for
  the rest of the session, leaving every read on the slow per-field route.
  Closing and reopening reset the flag, and by then the link had settled,
  which is exactly why a reopen loaded quickly. The bulk read is now retried a
  few times with a short backoff so it lands once the link is ready, and a
  failed read no longer disables the fast path unless a full per-field pass
  shows the link is healthy and bulk genuinely unavailable (pre-1.4.0
  firmware). Connects after wake are fast on the first attempt; the healthy
  case is unchanged, since a bulk read that succeeds immediately adds no delay.

## [1.18.19] — 2026-08-09

### Added
- **Hover descriptions for every setting in the configuration portal.** Each
  setting now shows a small "i" marker; hovering it brings up a one-line,
  plain-language description of what that setting does, in the spirit of the
  README glossary. Every setting is covered, across all tabs, including the
  two-column Adaptive Triggers layout. It is implemented as plain markup and
  CSS with no per-field scripting, so it adds nothing to portal load time or
  runtime and there is nothing new to wire up.

## [1.18.18] — 2026-08-04
### Fixed
- slot-activate script fixed to load slots 17-24, was broken and only loaded 1-16 with the previous release,
  which reworked the automation scripts.

## [1.18.17] — 2026-08-02

### Fixed
- **Converted rumble in Mix was inherently capped at half strength.** The
  rumble was summed with the audio content and the result passed through the
  `m/(1+|m|)` soft clip, which maps 1.0 to 0.5 — so even at a full motor value
  and 100% strength it could never exceed half scale, and auto-haptics playing
  at the same time pushed it further down. The audio content is now limited
  first and the rumble added afterwards with a hard clamp, so it keeps its full
  amplitude; the clamp only trims where both are loud at once, and slight
  clipping of a rumble tone reads as extra grunt rather than distortion.
- **Both motor values were rendered on the same 90 Hz tone.** A game's two
  values are not interchangeable: left drives the heavy/low-frequency motor,
  right the light/high-frequency one. Converted rumble now uses 60 Hz for the
  heavy side and 160 Hz for the light side, which both restores the distinction
  and puts the energy where the actuators render it most convincingly — a large
  part of why converted rumble felt thin next to the controller's own rumble
  emulation.

### Changed
- **Converted Rumble Strength now goes to 200.** Values above 100 deliberately
  overdrive into the clamp for games whose motor values sit low.

### Automation
- **ds5audio waits for the dongle audio endpoint at startup** instead of
  exiting. Applying a profile that changes an enumeration-critical setting
  (wake, polling rate, audio buffer, mic/speaker) makes the dongle re-enumerate,
  so the endpoint is briefly absent - and the start script launches the capture
  immediately after applying a profile. It lost that race and exited with
  "couldn't find the dongle audio output", which looked like the script
  refusing to start for one particular profile while a manual run always
  worked. It now polls for up to 30 s (`--wait-device SEC`, 0 disables),
  re-creating its audio instance each attempt because the device list is
  snapshotted when the instance is created.
- **Per-game ds5audio arguments in `profile-overrides.txt`.** Anything after a
  further comma is passed to the capture script, so a profile can carry the
  channel mapping it needs (`Resident Evil 4 = slot 5, audio, --map front`).
  Previously the mapping could only be set globally, which made the per-game
  setups in the firmware README impossible to automate. Global `$AudioArgs`
  still applies to every game; these are appended to it.
- Documented that a game on `native-games.txt` needs an explicit `, audio` in
  its override line, since without a flag the native list decides the capture
  and excludes exactly those games.

## [1.18.16] — 2026-08-02

### Changed
- **Rumble diagnostics are now peak-hold and report what the game asked for.**
  The readout added in 1.18.14 showed the instantaneous motor values, but the
  portal polls once per second while a rumble burst can be over in 100 ms — so
  it missed most bursts and a zero reading proved nothing. It now latches the
  highest value seen since the last read (cleared on read), so any rumble in
  the interval registers, and it also shows which rumble flags the host
  requested (EnableRumbleEmulation / UseRumbleNotHaptics / Improved). That
  distinguishes a game that asks for rumble but sends no motor values from one
  that never asks at all.

## [1.18.15] — 2026-08-02

### Added
- **Auto-Haptics DSP Source** (Auto-Haptics section): choose whether the
  auto-haptics DSP listens to **ch0/1** (default, previous behaviour) or
  **ch2/3**. The speaker and the effect leak always read ch0/1, so until now
  the script feed that drives auto-haptics and the audio that reaches the
  speaker were forced to be the same signal — with a native game there was no
  way to hear only the game's own speaker effects while still deriving haptics
  from the script. Setting the DSP to ch2/3 and running `ds5audio --map rear`
  separates them: the script feed drives auto-haptics on ch2/3, ch0/1 carries
  nothing but the game's native speaker output, and the effect leak passes only
  those native effects. Set Native Passthrough to 0 in this configuration,
  since ch2/3 then carries raw script audio rather than native haptics.
  Falls back to ch0/1 automatically on a 2-channel stream.

## [1.18.14] — 2026-08-02

### Added
- **Filter Native Passthrough in Mix** (Auto-Haptics section). In Mix the ch3/4
  native contribution has always been low-passed at the auto-haptics cutoff, to
  stop VoiceMeeter-style 4-channel setups — where ch3/4 mirror the full-band
  stereo — from leaking dialogue into the actuators. For a game that renders
  its own DualSense haptics that filter is destructive: genuine haptic content
  sits well above an 80 Hz cutoff, so mixing auto-haptics into a native game
  removed the very effects the passthrough exists to preserve, with no way to
  get them back. Set this to **Raw** to pass ch3/4 through untouched while the
  derived haptics stay filtered. Default is Filtered, i.e. previous behaviour.
- **Live rumble readout in Diagnostics.** Shows the motor values arriving from
  the host right now, which distinguishes the two ways a game can deliver
  vibration: non-zero while the game vibrates means it uses rumble motor values
  (carried into Mix by Converted Rumble Strength), while zeroes mean the game
  sends vibration as haptic audio on ch3/4 (carried by Native Passthrough, and
  previously filtered away by the issue above).

## [1.18.13] — 2026-08-02

### Fixed
- **After waking the PC, the bridge kept showing its idle identity until the
  controller was switched off and on again.** When the controller reconnects
  while the bridge is already on the bus under the idle identity, it asked the
  USB stack to connect — but it was never disconnected, so that call does
  nothing: the pull-up is already asserted, the host sees no change and never
  re-reads the device descriptor. The identity therefore changed internally
  while the host went on showing the idle product id. Worse, the post-wake
  repair in the wake module is itself conditional on the identity still being
  idle internally, so flipping it also disarmed the one path that would have
  corrected this. The reconnect now performs a real detach first, with the same
  250 ms gap as the power-off direction, so the host always re-reads the
  descriptor.
- **Belt and braces for any remaining timing window.** The bridge now records
  which identity it actually handed the host at its last descriptor request,
  rather than only which one it intends to present. If the host is awake, a
  controller is attached and the host is still holding the idle identity, the
  full identity is restored — whatever sequence of events led there. This
  covers the race where a controller reconnects in the same instant the host
  resumes, which no ordering of the individual steps can rule out.

## [1.18.12] — 2026-07-28

### Fixed
- **The dongle switched to its idle identity and immediately switched back.** The
  wake module was never told the controller had gone — the notification exists but
  nothing has ever called it, in this fork or the code it is based on. So it still
  believed a controller was attached, saw the idle identity appear, concluded that
  was a mistake and restored the DualSense identity within a moment. That is why
  the device was seen to disappear and come straight back as a DualSense Edge
  despite the identity change being correct. It is now told, so the idle identity
  stays until a controller genuinely returns.
- The gap during a USB identity change was 60 ms, which is below the 100 ms a host
  waits before believing a device has really gone. It is now 250 ms, so the change
  is always noticed rather than sometimes being missed entirely.

## [1.18.11] — 2026-07-28

### Fixed
- **The idle state still appeared as a DualSense Edge.** 1.18.10 gave it its own
  product ID, but left everything else untouched — so it still carried Sony's
  vendor ID and still called itself "DualSense Edge Wireless Controller", which is
  enough for controller software to claim it whatever the product ID says. The
  idle identity now uses a different vendor ID as well (the Raspberry Pi one, this
  being an RP2350 board) and its own manufacturer and product names. Nothing about
  it says DualSense any more. The interface layout remains deliberately identical
  between the two identities, which is what keeps the phantom key presses
  impossible.

## [1.18.10] — 2026-07-28

### Changed
- **The controller-away state now uses a different product ID instead of a
  different set of interfaces.** 1.18.9 presented a keyboard-only device when the
  controller was switched off. That changed which interface each report channel
  referred to, so a report written while the device was changing over could land
  on the wrong one — a gamepad report arriving at a keyboard is read as
  keystrokes, which is where the burst of phantom key presses at wake came from.
  The bridge now keeps exactly the same interfaces in both states and changes only
  the product ID, so the gamepad channel is always the gamepad and the keyboard
  channel is always the keyboard. That failure is no longer possible rather than
  merely guarded against. DS4Windows matches on product ID, so no controller
  appears while none is attached, and the bridge stays present on USB so the PC
  can still be woken.

### Note
- The idle state is a distinct USB device to Windows, so it has its own entry
  under Device Manager with its own *Allow this device to wake the computer*
  setting. Windows usually grants this to keyboards automatically; if a wake from
  that state fails, the portal's wake diagnostics report whether the host
  permitted it.

## [1.18.9] — 2026-07-27

### Fixed
- **1.18.8 left a controller showing in DS4Windows after the controller was
  switched off.** Staying on the USB bus is what makes waking possible, but
  keeping the *whole* device present meant the host still saw a gamepad that
  wasn't there. With wake enabled, the bridge now re-appears as a **keyboard-only
  device** when the controller is switched off: the host keeps something it can be
  woken from, and no controller appears in DS4Windows or anywhere else. The full
  device returns as soon as the controller reconnects, or as soon as the PC is
  awake again if it reconnected during sleep. With wake off, the bridge leaves the
  bus entirely, exactly as before.

## [1.18.8] — 2026-07-27

### Fixed
- **Turning the controller off and then sleeping the PC made waking impossible.**
  When the controller disconnects while the PC is awake, the bridge removes itself
  from USB so the host and DS4Windows see a clean removal. But if the PC was then
  put to sleep, there was no longer any device on the bus to be suspended — so the
  bridge never learned the PC had slept, and every part of the wake path is
  conditional on knowing that. Pressing the controller's button reconnected it to
  the dongle, exactly as observed, while the PC stayed asleep. The bridge now stays
  on the bus when wake is enabled, and hides itself only when wake is off. This
  matches the behaviour OmniSense settled on independently.

## [1.18.7] — 2026-07-27

### Fixed
- **One failed wake could disable waking entirely until the controller
  reconnected.** If the PC's USB bus came back but its keyboard channel never
  opened, the bridge waited for it with no time limit. The five-second give-up
  applied only to the case where the PC never came back at all, so the wake logic
  parked in a state that ignores button presses — meaning the first failure was
  permanent rather than something a second press could retry. It now gives that
  wait three seconds and then returns to a state a further press can use.

### Added
- **The wake diagnostics now report the most recent sleep on its own**, instead of
  only running totals. Totals mix in every button press made while the PC was
  awake, so "last wake refused" was usually just a press after giving up and
  waking the machine by hand — which said nothing about the failure. The new block
  reports what happened during that one sleep and names the stage it reached: no
  wake requested, request refused, signal sent but the PC never came back, came
  back but the keyboard channel never opened, or the keypress was delivered.

## [1.18.6] — 2026-07-27

### Fixed
- **The wake diagnostics reported missed suspend notifications that had not been
  missed.** The USB stack raises its internal suspended flag inside the interrupt
  handler but delivers the notification later, when the main loop next services
  USB — so the bridge's safety poll can legitimately win that race by a fraction
  of a millisecond and then see the notification arrive normally. A busy main loop
  widens the window, which is why this showed up particularly after activating a
  profile with a custom trigger effect. A suspend is now only counted as missed if
  the notification never arrives at all, so the figure means what it says.

## [1.18.5] — 2026-07-27

### Fixed
- **The USB wake-up signal was never terminated, so it could only ever work
  once.** Sending a remote wake-up sets a hardware bit that makes the chip drive
  the wake signal on the USB bus, and nothing in the USB stack ever clears it
  again. The USB specification allows that signal to last between 1 and 15
  milliseconds; ours was left on indefinitely, which a host may disregard — and
  because the bit was already set, every later attempt wrote a 1 over a 1 and
  produced no fresh signal at all. Once a wake was missed, no further press could
  wake the machine. The bridge now ends the signal after 10 ms, which is both
  within spec and leaves it able to fire again.
- **A wake is now retried instead of attempted once.** A single wake pulse is easy
  for a host to miss, and previously the bridge sent one and waited five seconds
  for a resume that might never come. It now re-sends up to six times at 800 ms
  intervals while the host is still asleep, which only became possible once the
  signal was being terminated properly.

### Added
- The wake diagnostics report how many resume pulses had to be re-sent, which
  distinguishes the host ignoring a correctly-sent signal from the dongle failing
  to send one.

## [1.18.4] — 2026-07-27

### Fixed
- **A missed USB suspend notification disabled the entire wake path.** Everything
  that has to happen when the PC sleeps — disconnecting the controller, waking the
  host when it reconnects, and the low-level wake fallback — was gated on the
  suspend *callback* having fired. When that notification is missed, which a hub
  between the host and the dongle can cause, all three silently do nothing at
  once: the controller stays powered on at sleep **and** a later button press
  fails to wake the PC. That is one cause for both halves of the symptom. The
  bridge now also polls the USB stack's current suspend state rather than relying
  on the edge, so a missed notification is recovered within a cycle, and the
  mirror case (a missed resume leaving it convinced the host is still asleep) is
  recovered too.

### Added
- **Wake diagnostics**, in the portal's Device tab. Because this failure is
  intermittent, the firmware now records what it actually observed — how many
  suspends it saw, how many had to be recovered by polling, whether the host
  *permitted* remote wakeup, whether the controller disconnect found a live link,
  and what each wake attempt returned. Read it after a sleep that failed to wake
  and it will say which stage broke instead of leaving it to guesswork. It also
  flags the case where Windows has not been told the dongle may wake the machine.

## [1.18.3] — 2026-07-27

### Fixed
- **The controller sometimes stayed connected when the PC slept, and then would
  not wake it.** On suspend the bridge asked the controller to power itself off,
  which is a request the DualSense has to receive and obey — when it wasn't
  delivered or was ignored, the controller stayed connected and awake, and because
  the wake logic never saw the disconnect it expects, a later PS press failed to
  wake the host as well. The bridge now drops the Bluetooth link itself, which is
  a command to its own radio and cannot be refused. Battery saving is unaffected in
  practice: a disconnected DualSense still powers down on its own idle timeout.
  This matches the fix upstream made for the same intermittent failure.

## [1.18.2] — 2026-07-26

### Changed
- **The effect builder now shows which force level a percentage lands on.** Force
  is a 3-bit field — eight levels, not a hundred — so a typed value snaps to the
  nearest one: 10% and 21% both become level 1, which reads back as 14%. The
  Strength and Snap force fields now show `= level 1/7 (14%)` as you type, so the
  value the device stores is visible before you assign it rather than being a
  surprise when the effect is read back. Level 0 is additionally flagged as
  possibly imperceptible.

## [1.18.1] — 2026-07-26

### Fixed
- **The reader described a zero force field as "default force".** That was wrong:
  force is a 3-bit level from 0 to 7, written straight through with no offset, and
  the firmware refuses to engage a slider effect at all when its strength is 0 — so
  0 is the weakest setting, not a "use the hardware default" sentinel. The reader
  now reports the level itself (`force 3/7 (43%)`) and flags 0 as possibly
  imperceptible instead of implying the controller will substitute something.

## [1.18.0] — 2026-07-26

### Added
- **Read what is actually on a trigger.** The Build a Custom Effect panel can now
  decode the effect currently stored on L2 or R2 and show it in plain language —
  the same way the builder describes a stage you are authoring — instead of leaving
  captured or loaded effects as unreadable bytes. For each state it gives the type
  and parameters, the raw bytes, and the **position window that stage will actually
  get**, which is the part that cannot be worked out by eye. It also names the
  playback mode (single state, mechanical sequence, or time-based) and shows the
  trigger's condition, re-arm zone and gate setting alongside.
- The readout **warns when a stage is handed over before its own span finishes** —
  the case where a wall never reaches its break point or a bow never snaps, which
  until now could only be found by decoding the bytes by hand.

## [1.17.8] — 2026-07-26

### Fixed
- **The repeated-state check no longer touches vibration sets.** It compared only
  the effect bytes, so in an all-vibration timeline — the one case where recorded
  durations actually drive playback — the same vibration held for a different
  length was treated as a duplicate and dropped, quietly flattening the rhythm the
  recorder had captured. Repeats are now dropped only for mechanical states, whose
  durations the firmware discards anyway, so two identical ones really are
  interchangeable. Where a repeat is dropped, the first occurrence is kept.

## [1.17.7] — 2026-07-26

### Fixed
- **The repeated-state guard added in 1.17.6 only covered two of the four places
  effects are written.** Loading a saved effect file — the most likely way to hit
  it, since a file is just a capture someone ticked earlier — skipped the check
  entirely and still produced constant clicking. Restoring effects from a profile
  or slot backup was equally unguarded. All four write paths now share the same
  validation: exact repeats are dropped (and the stored state count corrected to
  match), and a set whose stages genuinely start at the same trigger position is
  refused with the zone named. The effect builder needs no check because it
  already refuses a stage whose start zone is taken.

## [1.17.6] — 2026-07-26

### Fixed
- **Assigning a repeated state from a capture caused constant clicking.** Games
  routinely return to a state they have already used (ready → fired → ready), so a
  timeline capture usually contains the same effect twice. Ticking both assigned
  two stages starting at the same trigger position; the sequencer requires
  distinct start positions, so it silently fell back to rapid A/B cycling — which,
  with a weapon break in the set, is felt as continuous clicking. Assigning from
  the monitor or a timeline now skips exact repeats automatically (saying so), and
  refuses a set whose stages genuinely start at the same position, naming the zone
  instead of assigning something that will misbehave.

## [1.17.5] — 2026-07-26

### Added
- **A prebuilt Waveshare RP2350B-Plus-W binary now ships** —
  `ds5-v1.17.5-waveshare.uf2`, alongside the Pico 2 W firmware. It is built
  against pico-sdk 2.2.0 as that board requires, so it no longer has to be built
  by hand.

### Fixed
- **`-Variant waveshare` was rejected before it ran.** The variant added in 1.17.3
  was documented and wired into the build switch, but never added to the
  `ValidateSet` on the script's `-Variant` parameter, so PowerShell refused the
  value at parameter binding and the option could not be used at all. Thanks to
  **@ishay3000** for catching and fixing it (#2).

## [1.17.4] — 2026-07-25

### Documentation
- **Wake guidance corrected.** The README told everyone to leave wake off. That
  advice only holds for games with native DualSense support, where the altered USB
  descriptor can break native recognition. For non-native games driven by an
  auto-haptics profile nothing depends on that recognition, so wake can stay on.
  Wake is an ordinary configuration field, so every profile and slot carries its
  own value and the automation switches it per game — on for auto-haptics
  profiles, off for native ones. The one real caveat is now stated precisely:
  changing wake forces a USB re-enumeration, which slot activation handles cleanly
  (a single command) but a field-by-field `.html` profile can be interrupted by.
- **Stale references swept.** Section names in the configuration reference now
  match the portal's renamed sections; instructions name the tab a panel lives on
  now that the portal is tabbed; the gate hand-off setting was added to the
  per-trigger settings table; and five "new in ..." notes that had been dragged
  along by version bumps now name the release each feature actually arrived in
  (custom effects 1.14.0, effect-carrying backups 1.16.0) rather than the current
  one.

## [1.17.3] — 2026-07-25

### Fixed
- **Building for the Waveshare RP2350B-Plus-W is now one command, and can no
  longer produce a silently broken binary.** A user reported that
  `boards/build_waveshare_rp2350b_plus_w.sh` was hard to use and that even after
  working through its errors the firmware "still didn't work correctly", while
  passing the board flag to the Windows builder worked flawlessly. The cause: the
  script only checked that `PICO_SDK_PATH` was *set*, not what it pointed at. This
  target compiles cleanly against SDK 2.1.1 — the version the README's own setup
  recipe produces for the Pico 2 W build — but the board's RM2 wireless needs
  2.2.0, so a 2.1.1 checkout yields firmware that builds and then misbehaves. The
  script now verifies the SDK version and refuses, naming the easier route.

### Added
- **A supported `waveshare` build variant** in both one-command builders, so the
  board no longer needs anyone to hand-edit CMake arguments:
  `tools/build-windows.ps1 -Variant waveshare` and
  `tools/build-macos.sh --waveshare`. Each fetches the correct SDK and TinyUSB,
  builds into its own directory, and produces `ds5-bridge-waveshare.uf2`.

## [1.17.2] — 2026-07-25

### Fixed
- Exported profiles no longer tell you to apply them with a `ds5profile` CLI. No
  such tool was ever shipped, so every exported profile carried a note pointing at
  something that doesn't exist.

### Documentation
- The README now explains **how far each stage of a multi-stage effect runs**. A
  stage hands over shortly before the next stage's region begins, so the next
  effect is armed ahead of your finger. Stages that don't overlap each play their
  whole span; stages that overlap are cut short — which softens a bow's ending by
  cutting it before its snap, useful deliberately but easy to hit by accident.

## [1.17.1] — 2026-07-25

### Fixed
- **The automation could not activate slots above 16.** Raising the slot count to
  24 left two limits behind: the profile-override parser only recognised
  `slot 1`-`slot 16`, and the slot-activate page rejected anything higher. An
  override like `slot17` therefore failed to parse, was treated as a profile
  filename, and — finding no such file — silently fell back to the default
  profile, activating the wrong slot. Both now accept 1-24, and a slot number
  outside that range is reported as an out-of-range slot instead of a missing
  profile file, so the failure names its real cause.

## [1.17.0] — 2026-07-25

### Changed
- **24 profile slots**, up from 16. Slots occupy three flash sectors instead of
  two, still well clear of the configuration page, with megabytes free beneath.
  Activating a slot is unaffected — it is an index lookup, not a scan, so it costs
  the same regardless of how many slots exist.
- **The slot list is only re-read when it is on screen.** The portal re-renders on
  every tab switch, and the sweep costs one round-trip per slot, so it was
  spending over a second on HID traffic even when the Slots tab wasn't showing.
  Saving, activating and deleting a slot still refresh the list immediately.

### Upgrade note
- Existing slots are untouched: slot storage grows downward into previously unused
  flash, so slots 0-15 keep their addresses and contents. An older portal against
  this firmware simply shows the first 16; a newer portal against older firmware
  shows the extra slots as empty.

## [1.16.4] — 2026-07-25

### Changed
- Portal section names are clearer about what they cover: **Auto-Haptics** is now
  *Auto-Haptics & Speaker Effect Leak* (the leak settings live there because the
  mode setting governs both, so they can't sensibly be split out), **Haptics &
  Audio** is *General Haptics & Audio*, and **Native Haptics** is *Native Haptics
  Filter*.
- A bold labelled **Speaker Effect Leak** divider now separates the leak settings
  from the auto-haptics ones inside that card, instead of the two halves running
  together.

## [1.16.3] — 2026-07-25

### Fixed
- **Profiles and auto-apply HTML files now carry custom effects.** Both exported
  only the numbered settings, so a profile could record that a trigger *has* a
  custom effect — its enable flag, condition, zone and state count — while
  carrying nothing to recreate it, leaving the effect existing only on the device.
  Export now reads the effect states off the device and includes them; Import and
  auto-apply write them back. Profile files are format version 2; version 1 files
  still import, just without effects, since they never contained any.

## [1.16.2] — 2026-07-25

### Added
- **Export effect from L2 / R2** in the Build a Custom Effect panel: saves the
  effect currently assigned to a trigger to a JSON file, read back from the
  device. Profile exports carry only the numbered settings, so an effect could be
  enabled in a saved profile yet impossible to get back out as a file — this
  recovers one after the fact.

### Changed
- **L2 is now on the left and R2 on the right**, matching their positions on the
  controller — in the side-by-side trigger columns and in the Load custom effect
  file, Assign, Remove and Export buttons.

## [1.16.1] — 2026-07-25

### Changed
- **R2 and L2 settings are now side by side.** The two triggers hold the same
  settings in the same order, so they are drawn as two aligned columns in a single
  Adaptive Triggers card — roughly halving the scroll and making the two triggers
  directly comparable. Rows are paired by setting rather than by position and laid
  out on a grid, so a label that wraps on one side cannot make the columns drift.
  "Kick follows", which is shared by both triggers rather than belonging to R2, is
  lifted out to a full-width row beneath the columns instead of leaving one column
  a row longer than the other.

### Added
- A fourth portal regression test checks the two trigger columns stay aligned
  row-for-row and that no setting is dropped from the paired view.

## [1.16.0] — 2026-07-25

### Fixed
- **Slot backups now include custom effects.** "Back up all slots" exports each
  slot field by field, but a custom effect's raw states are arrays inside the slot
  body rather than settings, so every backup silently lost them: restoring a slot
  brought back its settings with the effect itself missing. A new firmware command
  reads a slot's stored effect states directly (without activating the slot), the
  backup file now carries them per slot, and restore writes them back before
  saving each slot. Backup files are now format version 2; version 1 files still
  restore, just without effects, since they never contained any.

## [1.15.3] — 2026-07-25

### Changed
- **Trigger synthesis now backs off when the trigger is idle.** The fast 8 ms
  cadence exists so a stage sequence can see the trigger cross a boundary
  mid-pull, but it was running whenever a custom effect was merely *enabled* —
  composing 125 times a second even with both triggers resting, and re-sending a
  sustained vibration every 25 ms indefinitely. The cadence is now 8 ms while a
  trigger is touched or has moved in the last 300 ms, and 50 ms otherwise, which
  cuts idle work by about 84% with no change during a pull. The interval is
  re-evaluated every main-loop pass and trigger position comes from the input
  report path, so movement restores the fast cadence within microseconds — stage
  arming keeps the reliability it gained in 1.14.x. Profiles without a custom
  effect are unaffected.

## [1.15.2] — 2026-07-25

### Changed
- **Custom effects are no longer a separate tab.** Every custom effect is a
  trigger effect, and its settings cross-reference the resistance mode on the
  same trigger — the gate hand-off warning names that setting by name — so
  splitting them put a warning on one tab and its fix on another. Custom-effect
  settings now sit in their trigger's own card, directly below the resistance
  settings they interact with, and the effect monitor and builder are on the
  Triggers tab. The Triggers tab is longer as a result, but everything about a
  trigger is in one place.
- Tab order is now Device · Haptics · Triggers · Gyro · Slots — connection first,
  then the feature areas, with Slots (where you save what you just set up) last.
  The portal still opens on Triggers.

## [1.15.1] — 2026-07-24

### Fixed
- **Wake-on-PS still unreliable with a custom effect engaged.** The 1.14.3 fix
  stopped trigger synthesis while the host was suspended, but stopping only
  halted *updates* — whatever effect was last written stayed **latched on the
  controller** for the entire sleep. That is specific to custom effects: a
  while-held effect is asserted continuously and never releases on its own, so
  the trigger actuator stayed energized (and a captured vibration, which only
  ends on physical release, kept buzzing) right across the deferred controller
  power-off that the wake path depends on. Slider effects release by themselves
  when the trigger is let go, which is why this only showed with a custom effect.
  The bridge now sends one explicit trigger Off the moment the host suspends,
  before standing down.

## [1.15.0] — 2026-07-24

### Changed
- **The configuration portal is now organised into tabs** — Triggers, Custom
  Effects, Haptics, Gyro, Slots and Device — instead of one long column of
  eighty-five settings and four panels. Custom-effect settings for both triggers
  now sit on the Custom Effects tab alongside the monitor and the builder, so
  everything about an effect is in one place. No setting was renamed, removed or
  re-bound: tabs are a render-time grouping of the existing definitions, so saved
  profiles, slots and the automation entry point are unaffected.

### Added
- **Portal regression tests** (`tools/run-portal-tests.sh`). The portal is a
  single large hand-edited file and several regressions this cycle were invisible
  — a field silently unreachable, an event handler truncated by an embedded quote,
  a panel showing a placeholder while its data still existed. Three harnesses now
  check the script parses, that every one of the 85 settings renders on exactly
  one tab, and that no generated event handler contains a stray quote or
  unbalanced parenthesis. A self-check also runs when the portal loads and shows a
  banner if any setting would be unreachable.

## [1.14.8] — 2026-07-24

### Fixed
- **Removing a custom effect reset the custom-effect-vs-sliders hand-off.** That
  setting describes how the trigger is shared between the two systems and belongs
  to the profile, not to the effect, so wiping it meant re-picking it every time an
  effect was swapped. Remove now clears only the effect itself — the stored states,
  the enable flag and the state count — and leaves every other setting for that
  trigger untouched.

## [1.14.7] — 2026-07-24

### Fixed
- **Every dropdown in the portal stopped working in 1.14.6.** The warning added in
  that release embedded a JSON array inside the select's onchange attribute; its
  double quotes closed the attribute, truncating the handler for *all* selects. No
  dropdown change took effect — which made the custom effect appear to own its
  trigger permanently (the hand-off setting could not be changed off its default)
  and stopped the new warning from ever appearing. The handler no longer embeds
  quoted data.
- **Builder list could reject a stage the list did not show.** Any full re-render
  rebuilt the builder card back to its "No stages yet" placeholder while the
  staged effects still existed in memory, so adding a stage was refused as a
  duplicate zone against invisible entries. The list is now repopulated after
  every render, and *Remove effect* also empties the staging list. The duplicate
  message points at the list and at Clear list.

## [1.14.6] — 2026-07-24

### Added
- **Bow / snap in the custom effect builder.** A bow builds resistance through the
  draw and then pushes the trigger back at the end — the one thing resistance and
  weapon break cannot do. Set a draw start, draw end, draw strength and snap
  force; the encoding mirrors the firmware's own bow writer, and reproduces real
  captured bows byte-for-byte. The builder's bow is also freer than the slider
  bow, which is fixed to a four-zone span and only fires as a kick: here it can
  take any span and be staged alongside walls, resistances and vibrations.
- **Warning for gate hand-off combinations that leave one half silent.** Choosing
  "custom effect while gated" while that trigger's resistance mode is not set to
  *always on* now shows an inline warning naming the setting to change, since the
  ungated half would have nothing armed. The same applies to "sliders while gated"
  with the resistance switched off. Nothing is changed automatically — the
  resistance mode stays yours to set.

## [1.14.5] — 2026-07-24

### Fixed
- **Bow / snap effects buzzed continuously when assigned as a set.** Bow (0x22)
  was classified as neither mechanical nor vibration, so a bow set fell through to
  the time-based path; with the short durations a capture typically carries, the
  states were retriggered around twenty times a second, which feels like a
  constant buzz rather than a bow. Bow is a force effect and now sits with
  resistance and weapon-break: placed by trigger position, recorded durations
  ignored. (This only became visible in 1.14.4 — before the assignment fix, state
  count was never written to the device, so only the first state ever played.)

## [1.14.4] — 2026-07-24

### Fixed
- **"Custom effect while gated, sliders otherwise" produced silence on the slider
  half.** The hand-off used the slider path's *engagement* as its gate, but with a
  gated resistance mode the gate being open also means the sliders are disengaged
  — so yielding to them gave nothing. The gate now reads the physical gate input
  (the opposite trigger past its threshold, or the shoulder button), with its own
  hysteresis, so both directions work. Pair *custom while gated* with an
  always-on resistance mode to cover the ungated half.

### Added
- **Remove effect from R2 / L2** in the Build a Custom Effect panel: wipes that
  trigger's stored states, turns the custom effect off and hands the trigger back
  to the sliders, with a device read-back to confirm. Previously an assigned
  effect could only be overwritten, never cleared.

## [1.14.3] — 2026-07-24

### Fixed
- **Wake-on-PS reliability regression introduced in 1.14.x.** The trigger
  synthesis tick was raised from 50 ms to 8 ms so custom-effect stages could
  track trigger position, and a custom vibration re-sends itself every 25 ms to
  stay alive. Neither was gated on host state, so with a custom effect enabled
  the bridge kept composing and pushing reports to the controller while the PC
  was asleep — competing with the input reports wake-on-PS has to observe.
  Trigger synthesis now stands down completely while the host is suspended and
  resumes on wake. Wake's own logic is unchanged from 1.13.3.

## [1.14.2] — 2026-07-24

### Added
- **Gate hand-off between a custom effect and the trigger sliders**, per trigger.
  A custom effect no longer has to own its trigger outright: it can share with the
  synthesized slider effect, using that trigger's existing resistance mode as the
  gate (L2-gated, L1-gated), so the threshold and hysteresis already configured
  apply unchanged. Three settings per trigger: custom effect always owns it
  (default, unchanged behaviour); sliders while gated and custom effect otherwise;
  or the inverse. With R2 resistance set to L2-gated this gives hip-fire on the
  captured effect and aim-down-sights on the synthesized resistance, or the
  reverse. Only one effect plays at a time — this is a switch, not a layer.
  Existing profiles are unaffected: the setting defaults to the previous
  behaviour, and the whole path is skipped when no custom effect is enabled.

## [1.14.1] — 2026-07-24

### Fixed
- **Custom-effect assignment never wrote its settings to the device.** The portal
  sent enable / condition / zone / state count as bare command IDs instead of
  wrapping them as config-field writes, so the firmware silently ignored them.
  Only the raw effect states were stored, leaving state count at its previous
  value — so a multi-stage effect played only its first state, and behaviour
  appeared to depend on unrelated manual settings changes. All assignment paths
  (monitor, timeline, file load, builder) now write fields correctly, and the
  builder reads the values back from the device and reports a mismatch instead of
  failing silently.
- Sequencer stage reset ran after the stage advance and used the raw re-arm zone,
  which for any zone above 0 sits inside the sequence — undoing each advance in
  the same evaluation. The reset now runs first and is clamped to below the whole
  sequence, so later stages are reachable whatever the zone is set to.

### Documentation
- README explains how the trigger sliders and custom captured effects differ in
  principle (a live synthesiser driven by rumble and audio, versus a player of
  fixed states driven by trigger position), and documents the three replay
  behaviours: single state, mechanical position sequence, and time-based
  vibration.

## [1.14.1] — 2026-07-23 (update 5)

### Added
- **Build a Custom Effect** — author effect stages by hand in the portal, no game
  capture required. Pick a type (weapon break / resistance / vibration), a start
  zone, a break-or-end zone and a strength, add up to 5 stages, and assign them to
  a trigger. The encoding mirrors the firmware's own effect writers, so a built
  stage is byte-identical to what the sliders or a game would emit for the same
  values — e.g. "weapon break, start zone 2, break zone 5, 100%" reproduces
  *Ratchet & Clank*'s first wall exactly. Stages can be saved to the same JSON
  files as captured effects, so built and captured effects interoperate.
  This gives multi-stage pulls (two walls, wall + end-resistance) that the single
  trigger-effect sliders cannot express.
- **Vibration can now be a stage in a positional sequence.** A set that mixes at
  least one mechanical stage with a vibration is sequenced by position — e.g. wall
  → wall → buzz while held deep. The vibration takes over at its own depth rather
  than layering (the controller plays one trigger effect at a time). Sets that are
  *entirely* vibration keep the time-based behaviour (A<->B rate blend, or a
  recorded timeline).

## [1.14.1] — 2026-07-23 (update 4)

### Fixed
- **Mechanical states no longer fall into timeline replay.** Loading a saved file
  (or assigning timeline entries) whose states carried recorded durations put
  wall/resistance sets into the timeline stepper - which swapped states on
  multi-second timers regardless of trigger position: the effect "sometimes
  worked", and the wall armed/disarmed under a resting finger (phantom clicks).
  Mechanical states (walls, resistances) now ALWAYS replay positionally - the
  firmware ignores durations for them, and the portal strips durations on load
  and timeline-assign. Recorded durations still drive replay for vibration
  states, where the rhythm is the effect. Existing loaded profiles are fixed by
  the firmware change alone - no re-loading needed.

## [1.14.1] — 2026-07-23 (update 3)

### Changed
- **The positional sequencer now accepts resistance stages** (up to 5 mechanical
  states per action, walls AND resistances mixed). States are ordered by their
  captured trigger positions and played in sequence along the pull — each next
  stage armed just before the finger reaches its region. This reproduces full
  actions like Ratchet & Clank's wall -> wall -> end-resistance, or a resistance
  BEFORE a wall. Tick order doesn't matter; positions come from the bytes.
- Behavior change: a mechanical pair (e.g. wall + resistance) now replays
  POSITIONALLY instead of the rate-blend. The rate-based A<->B blend remains for
  vibration pairs only. Monitor multi-select cap raised from 2 to 5.

## [1.14.1] — 2026-07-23 (update 2)

### Added
- **Two-wall sequencer** for custom effects (Ratchet & Clank-style hold model):
  assign TWO weapon-break states with different wall positions and, while-held,
  the firmware plays the lower wall first, then — the moment your pull passes it —
  force-sends the upper wall (which sits ahead of the finger at that moment, so
  arming it should not be felt). Push through both in one pull; the sequence
  resets to the lower wall on release. Auto-detected: 2 states, both weapon-break
  type, different wall zones, no timeline durations.

### Changed
- **Re-arm zone is now configurable** (the threshold field, in while-held mode):
  walls re-arm/reset when the trigger returns to or below this zone. Default 1;
  set 0 to re-arm ONLY at full release (finger off the trigger — nothing to push
  against, click-free, matching how the game re-arms on release). Higher values
  re-arm earlier at the cost of possible arming feel under a hovering finger.

## [1.14.1] — 2026-07-23 (update)

### Added
- **Timeline capture** for custom effects: Record captures the effect's real
  RHYTHM — every state change with its held duration (the plain history loses
  all timing). Replay steps the timeline verbatim (each state held for its
  recorded ms, looping), reproducing asymmetric patterns like a switch-then-HOLD
  exactly, with no rate dial-in. Up to 5 timestamped states per trigger; files
  gain per-state duration_ms (format v2). Assignments from the plain history
  (no durations) keep the rate-based A<->B cycling, whose blend stacks
  mechanical pairs nicely.

### Changed
- **Weapon-break re-arm is now a forced fresh re-send** of the same effect
  (no Off pulse in between): after breaking through and returning below the
  wall's own start zone, the break re-arms without the wall-drop-then-restore
  that caused an audible/palpable click on the next press.

## [1.14.1] — 2026-07-22

### Added
- **Custom Captured Effects** — capture a real adaptive-trigger effect from a game
  and replay it on any trigger, with no fidelity loss. A new Trigger Effect Monitor
  panel shows the genuine trigger effects a game sends (resistance, weapon-break,
  vibration). A game action is usually TWO states that alternate rapidly (e.g. a
  weapon's resistance and its break point, or a vibration's A/B pump) — tick both
  in the monitor and Assign them as a custom effect on that trigger. The firmware
  stores the raw 11-byte states VERBATIM (the game's force curves don't round-trip
  through slider values, so raw storage preserves the exact feel) and, while the
  trigger is engaged, cycles A<->B like the game does. Per trigger, independent:
  enable, trigger condition (while-held / on-press / on-release), threshold zone
  (dead-zone compatible), and A<->B toggle rate (for vibration actions). Effects
  can be saved to and loaded from JSON files for sharing. Custom effects save to
  on-device profile slots with the rest of the config.
  - Only GENUINE game-sent trigger effects can be captured — firmware-converted
    rumble (e.g. Control's rumble->trigger) and DS4Windows/Xbox rumble are NOT
    capturable, as those aren't trigger effects the game sent.

## [1.13.3] — 2026-07-19

### Fixed
- **Portal errors after the 0x82 reply-channel move.** Report 0x82 is declared
  in the HID descriptor as a **9-byte** feature report (DualSense-authentic);
  63-byte slot replies routed through it overflowed the USB transfer buffer
  and made portal slot reads throw. Slot-family replies (save 0x08, activate
  0x09, info 0x0a, slot-field-read 0x0d) now live on **0x84**, which the
  descriptor declares at the full 63 bytes — keeping the collision fix (the
  portal's 1-second 0x81 diagnostic poll can never consume a slot reply) on a
  correctly sized channel. The descriptor itself is untouched.
- **Channel consistency.** 1.13.2 had migrated only activate and slot-field
  reads; save and slot-info still replied on 0x81, and the bulk config get/set
  commands (0x0b/0x0c — portal commands, not slot commands) had been dragged
  along. All slot commands now reply on 0x84; everything else stays on 0x81.
  The portal routes readers by command id accordingly.
- **USB get-report hardening.** The report callback now clamps every copy to
  the host-requested length, so a mis-sized report can never overflow the USB
  stack again regardless of future channel choices.

## [1.13.2] — 2026-07-19

### Fixed
- **Slot activation "no reply (timeout)" while the config portal is open — the
  real root cause.** Every firmware command reply was posted to a single shared
  feature-report buffer (report 0x81). The config portal polls 0x81 once a second
  for diagnostics, and because the portal tab and the slot-activate page are
  SEPARATE browser processes, nothing serialized them: the portal's poll would
  consume a slot-activate reply before the slot page read it, so the page saw a
  timeout even though the activation had applied. Slot-command replies (activate
  0x09, slot field-read 0x0d) now use report 0x82 instead - descriptor-declared
  in both DS and DSE identities, and untouched by DualSense-native and PS-app
  profile passthrough - which the portal's 0x81 poll can never collide with. The
  slot page and the portal's backup reader read 0x82 for these; all other
  commands stay on 0x81. Builds on the prior background-tab timer fix (Web Worker
  timing) and the persist-deferred status fix. Needs the new firmware AND the
  regenerated slot-activate.html (re-run ds5-setup.bat).

## [1.13.0] — 2026-07-17

### Fixed
- **Slot activation across a re-enumeration no longer blocks the next slot load.**
  When an activated slot changes the USB descriptor (wake on/off, or controller
  type), the firmware re-enumerates the device. The slot page was holding its
  WebHID handle open ACROSS that USB drop - so the OS kept the granted permission
  bound to the vanishing device instance, and the next page (e.g. the native
  profile at game start) could not claim the re-enumerated controller. The page
  now closes its handle BEFORE triggering the reconnect (then briefly reopens
  only to deliver the reconnect command and closes again), so no handle spans the
  re-enumeration. Also releases the device on focus loss as a backstop. This is
  the real fix for "slot page stays open, native game controller detection
  breaks". Re-run ds5-setup.bat to regenerate slot-activate.html. Generated-page
  fix, no reflash.

### Added
- **Back up / restore all profile slots to a file.** The slots panel gains "Back
  up all slots" (reads every field of all 16 slots and downloads one JSON file,
  names included) and "Restore from file" (writes every saved slot in the file
  back). Protects hard-won slot tuning against a flash wipe, a bad flash, or a
  dead board, and moves profiles between dongles. Restore is non-destructive to
  slots the backup did not cover, and unknown fields (from a newer/older backup)
  are skipped rather than rejected. Needs new firmware: a read-slot-field command
  (0x0d) that reads a slot's stored config without disturbing the active profile.

## [1.12.0] — 2026-07-17

### Added
- **Automation: default profiles can be on-dongle slots.** `$NativeProfile`,
  `$AudioProfile` (start script) and the exit-restore `$AudioProfile` (stop
  script) now accept `"slot N"` (N = 1-16) as well as an .html filename - the
  same syntax profile-overrides always used. A slot activation is one atomic
  firmware command via the self-closing page: faster than a field-by-field html
  apply, and no browser window stays open (kinder to fullscreen-fragile games).
  Example: `$AudioProfile = "slot 1"`. Re-run ds5-setup.bat to regenerate.
- **Weapon break trigger shape** (per trigger, 4th shape): the hardware Weapon
  effect (0x25) - a rigid wall from the start position to the break point, then a
  hardware-sharp SNAP-THROUGH release. The classic semi-auto shot break: tension,
  clean give, free travel. Distinct from the two-stage detent (a bump with force
  on both sides); here resistance ENDS at the break. Field reuse: start position
  = wall start (hw 2-7), Detent zone = break point (hw 3-8, forced above start),
  Strength A = wall force (Strength B unused). Pair with the Activation dead zone
  at the break zone so the shot registers exactly at the snap.
- **Effect leak Max Burst** (x5 ms, 0 = unlimited/off): caps how long one gate
  opening may last. Transients (shots, impacts) end within the cap naturally;
  SUSTAINED content (dialogue, music) used to hold the gate open and duplicate
  the room audio - now it is cut at the cap, and a refractory keeps the gate shut
  until the signal genuinely falls: one sustained sound = one short accent.
  Reframes the leak as a PERCUSSION layer - run it louder, because bursts
  punctuate instead of duplicating. Start at 30 (150 ms); Attack shapes the
  onset, Decay shapes how the forced close fades out.

## [1.11.0] — 2026-07-17

### Added
- **Three more gyro aiming gates**: "Only while R2 held" (analog, same threshold
  as the L2 gate), "Only while L1 held" and "Only while R1 held" (digital), for
  games that don't put aiming on L2. Sit alongside the existing L2/touchpad/
  ratchet modes - nothing removed, values unchanged, existing profiles
  unaffected.

## [1.10.0] — 2026-07-16

### Added
- **Native Passthrough in Mix (%)** (0-100, default 100): per-profile fader on
  the ch3/4 native-haptics contribution in MIX mode. Only Mix consumes it: Off
  always passes ch3/4 at full (that is how native passthrough profiles work) and
  Replace always discards them, so the fader is ignored in both. Use cases:
  - **Mix + native game -> 100**: classic Mix, game haptics + derived on top.
  - **Mix + non-native game (DS4Windows/XB360) -> 0 (or taste)**: removes the
    ds5audio `duplicate` copy of the game audio from ch3/4, so Intensity, the
    frequency split and the gate control the WHOLE haptic output (converted
    rumble keeps its own strength knob). This is the per-profile equivalent of
    running ds5audio with `--map front` - with the fader at 0 the --map choice no
    longer matters in Mix.
  Default 100 = pre-1.10.0 behavior; existing profiles unchanged.

## [1.9.1] — 2026-07-16

### Fixed
- **Auto-haptics Intensity 0 was silently reset to 100** by validation (fresh-
  flash protection conflating a legitimate 0 with garbage). 0 now means what it
  says: the derived auto-haptics are silenced.

### Clarified (documented, not a code change)
- **In Mix mode the native haptic channels (ch 2/3) pass through UNSCALED** by
  Intensity, the frequency split, or the gate - those controls shape only the
  DERIVED (audio-envelope) component. With ds5audio's default `--map duplicate`,
  ch 2/3 carry a copy of the game audio, so Mix feels like "derived + a full-
  strength shadow of it that no knob touches". If auto-haptics should own the
  actuators (typical for DS4Windows/Xbox360 games), run ds5audio with
  `--map front` (audio to ch 0/1 only, ch 2/3 silent): Intensity, the split and
  the gate then control everything, with converted rumble on its own strength
  knob. Keep `duplicate` only when you WANT raw bass passthrough as part of the
  feel. Also note: Intensity values beyond ~120 compress toward the same felt
  strength (soft-limit saturation) - differences are clearest in the 0-120 range.

## [1.9.0] — 2026-07-16

### Added
- **16 profile slots** (was 8). Slots occupy 512 bytes each, 8 per 4 KB flash
  sector; the store now spans two sectors. Slots 1-8 remain at their exact
  pre-1.9.0 flash location - existing saved profiles carry over IN PLACE with no
  migration - and slots 9-16 live in a new sector growing downward, away from the
  config sector and the Bluetooth link-key bank. Sectors are erased and rewritten
  independently, so saving a slot never touches the other sector. Automation
  profile-overrides accept "slot 1".."slot 16"; re-run ds5-setup.bat (or update
  slot-activate.html) for the extended range. Adding further sectors later is a
  one-constant change (~zero firmware weight: slots cost flash sectors, not
  code or RAM).

## [1.8.0] — 2026-07-16

### Added
- **Trigger activation dead zone** (per trigger, 0=off, 1-9): below the chosen
  zone the GAME sees the trigger as untouched (analog forced to 0, digital press
  bit cleared) - the action registers only once the pull reaches the zone. Fixes
  hair-trigger games where the shot fires before the resistance/detent/bow zone
  is reached, breaking the feel: set the dead zone to match your effect's zone
  and the shot lands exactly where the squeeze says it should. All internal
  effects (gating, kick, shapes) always read the RAW trigger, so the feel
  machinery is unaffected; the mask applies to everything downstream identically
  (games, DS4Windows, Steam Input).

### Notes
- The analog value the game sees jumps from 0 to the threshold value on crossing
  (by design - this targets button-like trigger actions). Not intended for analog
  driving inputs: a gas pedal with a dead zone would lose its lower range.

## [1.7.1] — 2026-07-16

### Fixed
- **Strength A = 0 disabled shaped triggers entirely.** The engagement guard
  (predating shapes) treated Strength A == 0 as "feature off", so Ramp 0->B and
  detents with a free base never engaged. Shaped triggers now count as ON when
  EITHER strength is nonzero; only Constant keeps the A=0 = off convention.
- **Strength 0 zones now mean genuinely free travel.** The 0x21 effect's 3-bit
  zone value is force level 1..8 - the only true zero is excluding the zone from
  the bitmap, which shaped triggers now do. Ramp A=0 starts truly free instead of
  faintly dragging, and Detent A=0 becomes a new capability: a pure bump at the
  detent zone with free travel everywhere else.

## [1.7.0] — 2026-07-16

### Added
- **Trigger resistance shapes** (per trigger, composes with all gating modes and
  kick): the 0x21 feedback effect's 10 hardware travel zones now programmable as:
  - **Constant** — start position..full at Strength A (pre-1.7.0 behavior, default)
  - **Ramp (A → B)** — resistance changes linearly across the pull. Racing: light
    ->heavy for a loading gas pedal (A=15, B=95), heavy->light for brake bite
    (A=90, B=30). Direction is just which of A/B is larger.
  - **Two-stage detent** — base Strength A with a wall of Strength B at a chosen
    zone: a tactile bump marking half-press from full-press, for games with
    fire/alt-fire on trigger depth (e.g. Ratchet & Clank). The game reads the
    analog axis as always - the detent gives your finger the reference point.
  Zone strengths are evaluated by the controller hardware against trigger
  position - zero runtime cost, perfectly smooth response. Existing profiles are
  unaffected (shape defaults to Constant).

### Notes
- Hardware strength resolution is 8 levels per zone; ramps quantize to that but
  feel smooth in practice (native games use the same mechanism).
- Resistance responds to trigger POSITION only. Game-state-driven effects (e.g.
  resistance varying with speed) are only possible in native DualSense games.

## [1.6.4] — 2026-07-14

Code-audit release: two latent bugs in legacy profile-slot recovery, found by
review (no user-visible symptoms reported).

### Fixed
- **Legacy (pre-1.4.0) slot recovery had gone stale.** The recovery candidates
  were expressed relative to the CURRENT config size, so when the config grew in
  1.5.0 they silently shifted - slots written by config v8, v10 and v11 firmware
  would no longer be recognized during migration (v9 still matched by
  coincidence). Recovery now brute-force scans every plausible record length and
  validates by CRC, which recovers records from ANY historical layout and can
  never go stale again. Affects only users upgrading directly from <=1.3.1 with
  surviving legacy slots; already-migrated v2 slots were never at risk.
- **v2 slot records with a zero body length could false-validate** as a
  degenerate record; now rejected outright.

## [1.6.3] — 2026-07-13

### Changed
- **Faster Bluetooth reconnect.** Adopted an aggressive interlaced page-scan
  setting (11.25 ms interval) so the dongle re-listens for the host more quickly
  after a disconnect, set once the BT stack reaches its working state. Adopted
  from awalol upstream ("fix: reconnect speed"); this fork did not previously set
  page-scan parameters at all. Low-risk - affects only reconnect/discoverability
  timing, not the active connection.

## [1.6.2] — 2026-07-10

### Fixed
- **Shoulder-gated trigger modes (added in 1.6.0) did nothing.** The config
  validator still clamped the trigger mode to a maximum of 2, so selecting
  "Gated by shoulder" (value 3) was silently reset to Off on every save/load/apply
  - the mode never survived to the engagement logic. Raised the bound to 3 for
  both R2 (at_mode) and L2 (at_l2_mode). L1->R2 and R1->L2 gating now work.

## [1.6.1] — 2026-07-10

### Changed
- **Adaptive Triggers (R2, L2) and Gyro Aiming mode menus reordered** so "Off" and
  "Always on" lead, followed by the conditional/gated modes. Display order only -
  stored values are unchanged, so existing profiles and slots are unaffected.

### Fixed
- Portal select menus now honor an explicit option order. (JavaScript forces
  integer object keys into ascending order, which had silently re-sorted menus
  regardless of how the options were written; selects now use an ordered-array
  form.)

## [1.6.0] — 2026-07-10

### Added
- **Shoulder-button gating for adaptive triggers.** Each trigger's Mode dropdown
  gains a "Gated by shoulder" option: R2 can now be armed by **L1**, and L2 by
  **R1** (opposite-side, digital). This sits alongside the existing opposite-
  trigger gating (L2 arms R2 / R2 arms L2) - nothing is removed, it's an extra
  mode. Because shoulder buttons are digital, arming is simple on/off with no
  threshold (the Arming threshold field is ignored in this mode). Useful when the
  aim/ready action in a game is bound to a bumper rather than a trigger. Composes
  with per-trigger strength, kick, and bow settings exactly like the other modes.

### Notes
- No config-layout change (the new mode is just an added enum value), so existing
  profiles and slots are unaffected; config version stays 12.

## [1.5.2] — 2026-07-10

### Fixed
- **ds5audio: surround (5.1/7.1) capture starved the haptics.** Non-stereo
  captures previously kept only the front L/R channels - on AVR endpoints that
  silently discarded the LFE channel, where games route most impact bass, making
  haptics and effect leak much weaker than on a 2.0 endpoint. ds5audio now
  downmixes: LFE at full weight, side/rear at half, center (dialog) excluded by
  default (tunable via --lfe-gain / --surround-gain / --center-gain). Requires
  numpy (`pip install numpy`).
- **Effect leak much quieter since the 1.2.0 band-pass window.** Two causes: the
  window walls overlap (narrow windows lost several dB of passband level), and
  the output low-pass default of 3500 Hz removed the 3-8 kHz range where the
  controller's small speaker is most efficient - the leak could need volume ~100
  to match the old ~15. Fixes: (1) automatic make-up gain normalizes the window's
  center to unity (clamped +12 dB), so moving the walls changes character, not
  loudness - the volume slider owns loudness again; (2) the low-pass DEFAULT is
  now 8000 Hz (existing saved values are untouched - raise yours toward 8000 to
  restore loudness, or keep it low if you prefer the tamed sizzle at a higher
  volume setting).

## [1.5.1] — 2026-07-10

### Fixed
- **Slot-activation page: false failure banners eliminated.** A missing
  confirmation reply is no longer treated as failure (the activation itself
  virtually always succeeds; only the reply races - portal tab open, flash-write
  stall, mid-apply reconnect). The page now shows a blocking banner only when the
  command could not be SENT at all or the firmware explicitly replies "failed"
  (genuinely empty slot). An unconfirmed-but-sent activation shows a brief
  self-closing note instead, and the page refreshes its device handle mid-retries.
  Re-run ds5-setup.bat (or drop in the updated slot-activate.html) - the fix
  lives in the generated page.
- **Out-of-range crossover silently disabled the frequency split.** Entering e.g.
  500 Hz validated to 0 (= off) with no feedback, making the band gains appear to
  do nothing. Out-of-range values now clamp to the nearest bound (30/200 Hz)
  instead. Portal label now also states the valid range and that the crossover
  must sit below the LP Cutoff (the high band is crossover..cutoff; content above
  the cutoff never reaches the haptics at all).

## [1.5.0] — 2026-07-10

Auto-haptics frequency split: independent control of what the low and high parts
of the haptics band contribute.

### Added
- **Frequency split** (`Crossover Hz`, 0 = off): divides the haptics band at a
  tunable crossover (30-200 Hz) into a LOW band (impacts, explosions, engine
  weight) and a HIGH band (crossover..LP cutoff - where music bass lines and
  voice fundamentals sit), each with its own gain (0-100). Both envelopes feed
  the SAME gate and 90 Hz carrier, so the felt character is preserved - only the
  per-band contribution changes. Typical use: crossover ~80 Hz, low 100, high
  30-50 to keep full impact weight while taming music/dialog-driven buzz.
- **Off by default and byte-identical when off**: crossover 0 bypasses the split
  entirely; existing profiles behave exactly as before.

### Notes
- Band edges are gentle (12 dB/oct), so the two bands overlap softly: gain 0 on a
  band reduces its content roughly 3-4x rather than to absolute zero - a musical
  transition rather than a surgical cut.

## [1.4.0] — 2026-07-09

Storage moved out of Bluetooth's flash territory (fixes slots/config corruption on
controller sleep/wake), plus bulk config transfer for near-instant profile applies.

### Fixed
- **Profile slot 0 (and potentially more, eventually the config) corrupted by
  Bluetooth link-key storage.** btstack's TLV flash bank occupies the LAST TWO
  flash sectors by pico-sdk default — the exact sectors config and slots lived in.
  Any TLV write (link-key churn on controller sleep/wake or re-pair, the pairing
  blacklist) could clobber them; the bank header lands at the start of its sector,
  which was profile slot 0 — hence "first profile shows empty after sleep/wake".
  Config and slots now live two sectors lower, fully out of the bank's range, and
  a one-shot boot migration rescues everything still CRC-valid from the legacy
  locations. Anything the bank already overwrote (typically slot 0) is
  unrecoverable — re-save that profile once.

### Added
- **Bulk config transfer** (cmds 0x0b write / 0x0c read): the portal and all
  exported auto-apply pages now move the whole config in ~5 packets per direction
  instead of ~60 individual field round-trips each way. A profile apply at game
  launch completes in a fraction of the previous time — launch-delay workarounds
  for native games should no longer be needed. Older firmware is auto-detected
  and falls back to per-field transfer.

## [1.3.3] — 2026-07-09

### Fixed
- **Triggers stuck in resistance after rapid R2/L2 play (both-gated setups).**
  The controller only ever received a state report when the host sent one; games
  that send output reports only when rumble changes go silent between actions, so
  whichever trigger was engaged at the last report stayed engaged on the
  controller indefinitely — usually L2 (its gate, R2, is pressed most). A 50 ms
  synth tick now re-evaluates gating from LIVE trigger positions using the cached
  host intent and pushes the state whenever it changes, host traffic or not.
  Bonus: gated resistance now also engages/releases correctly in games that send
  no rumble at all (previously gating needed host traffic to be felt). Stale
  cached rumble is zeroed after 300 ms so nothing synthesizes from old data
  (replaces the old release-only watchdog, which cleared local state but could
  never transmit the clear).

## [1.3.2] — 2026-07-09

Profile slots now survive firmware upgrades — and this release recovers slots
that "disappeared" after flashing from 1.1.x.

### Fixed
- **Profile slots lost on firmware upgrade.** Slot records embedded the raw config
  body with a CRC spanning its compile-time size — so whenever a firmware upgrade
  grew the config (new features), older slot records failed validation and read as
  empty. They were never erased, just unreadable. Slots are now written in a v2
  format that stores its own body length (future firmware can always validate and
  read them, missing new fields default sanely), and the loader also recognizes
  legacy v1 records from config v8/v9/v10 — **slots saved under 1.1.x that were not
  overwritten since are recovered automatically on first boot of this firmware.**
- **False "activation failed" banner on first game launch after saving a profile.**
  The slot-activation page's reply can be lost when another page holds the same
  device — typically the config portal tab left open right after saving the
  profile — or during the flash-write USB stall. Activation itself succeeded; only
  the confirmation raced. The page now retries patiently (4 attempts, growing
  delays) and, if it still can't confirm, says exactly what to do (close the
  portal tab and retry) instead of claiming the slot is empty.

## [1.3.1] — 2026-07-09

Fully independent per-trigger adaptive triggers, and a mechanical bow-snap kick.

### Added
- **Two independent adaptive-trigger sections — R2 and L2.** Each trigger now has
  its own complete set: mode (Off / Gated / Always-on), resistance strength,
  arming threshold, start position, kick strength, kick style and kick frequency.
  "Gated" arms when the OPPOSITE trigger passes that trigger's own threshold
  (R2 gated = L2 arms it; L2 gated = R2 arms it), with release hysteresis. Any
  combination works: L2 always + R2 off, L2 always + R2 gated, R2 kicks while L2
  only resists, different kick styles per trigger, etc. Only "Kick follows" (the
  envelope source) is shared — it is one signal; per-trigger kick strength 0
  disables the kick on that trigger.
- **Bow-snap kick style** (per trigger): the kick can be delivered as the DualSense
  Bow effect (0x22) instead of the vibration thump — the burst momentarily switches
  the trigger to Bow, whose snap force physically presses the trigger back against
  the finger. Sharper, more "recoil" than buzz; the envelope drives the snap force.
  Experimental: the feel varies with how deep the trigger is held (the snap needs
  the finger past the bow's end zone — start position + 4).

### Changed
- `at_target` (1.2.0) and the interim kick mask are superseded by the per-trigger
  sections and removed.
- Config version 9 -> 11. Re-check both Adaptive Triggers sections after flashing
  and re-save any slots that use them.

## [1.2.0] — 2026-07-09

Selective effect leak (band-pass window + anti-flutter gate) and left-trigger
support for resistance/kick.

### Added
- **Effect-leak output low-pass** (`effect_leak_lp_hz`, default 3500 Hz). Together
  with the existing output high-pass this forms a band-pass window — only sound
  INSIDE the window ever leaks. Both walls are now 12 dB/oct (was a single 6 dB/oct
  high-pass), so window placement is real selectivity: 400–3500 Hz passes impact
  bodies while rejecting voice fundamentals below AND the treble sizzle above that
  previously read as crackle.
- **Effect-leak gate hold + hysteresis** (`effect_leak_hold`, x5 ms, default
  100 ms). The transient test used to flicker when the envelope hovered at the
  threshold — one hit could chatter the gate open/closed ~30 times (each re-open a
  pop, the hit chopped short). The gate is now a state machine: opens on a clear
  transient, stays open a minimum hold, closes only when the level falls well below
  the open threshold. One clean open/close per hit.
- **Adaptive-trigger target selector** (`at_target`): resistance and kick can now
  apply to **R2 (L2 gates — default, unchanged)**, **L2 (R2 gates — southpaw /
  L2-fire layouts)**, or **both (either trigger arms)**. The kick envelope and
  burst state are computed once per cycle and shared, so both triggers thump in
  sync. Per-trigger game-ownership yielding is preserved: a game driving one
  trigger only suppresses synthesis on that trigger.

### Changed
- Effect-leak output high-pass steepened from 6 to 12 dB/oct (sharper dialog
  rejection at the same cutoff).
- Config version 8 -> 9; new fields default sanely on first boot after flashing
  (existing settings preserved).

## [1.1.2] — 2026-07-08

Profile slots: complete configurations stored on the dongle, switched with one
atomic command. Firmware reports 1.1.2.

### Added
- **Profile slots (firmware + portal + automation).** Eight 512-byte slots in a
  dedicated flash sector, each holding a named full configuration. New HID
  commands: 0x08 save-current-to-slot, 0x09 activate-slot (reports whether a
  USB re-enumeration is needed and only then triggers one), 0x0a slot-info.
  Portal gains a **Profile Slots** panel (save/activate with names); the
  automation gains `Game = slot N` syntax in `profile-overrides.txt`, served by
  a generated one-command activator page (`profiles\slot-activate.html`) that
  self-closes in under a second — game-launch profile switching is now atomic
  and near-instant instead of a multi-second field-by-field write. Slots are
  validated on activation, so slots saved by older firmware stay safe.
- **Portal HID transaction lock.** All command/reply exchanges (slot queries,
  diagnostics polling, saves) are serialized over the shared reply buffer,
  with slot replies additionally carrying a pending marker and slot-index echo
  — concurrent reads can no longer swallow each other's replies (which showed
  up as slots randomly listed as empty or shuffled).

### Fixed
- **False "Save failed" reports.** Field writes retry transient errors and
  report per-field instead of aborting; the flash-save step tolerates the USB
  stall its own write causes; the final error message now says the truth
  (settings are usually saved by the time late stages can throw) and points at
  Re-read for verification.
- **Slots panel recovers after reconnects.** After a save/activate that
  re-enumerates the device, the panel retries its probes and self-heals
  instead of sticking on "Connect to manage slots" until a manual page reload.

### Changed
- Portal sections **Rumble → Trigger** and **Gyro Aiming** drop their
  "(experimental)" tag; the adaptive-triggers section is now titled **Stage 2**
  (resistance + push-back kick). **Advanced — BT Latency** keeps its
  experimental label.

## [1.1.1] — 2026-07-08

Automation feature + documentation release — firmware and portal unchanged
(still 1.1.0).

### Added
- **Per-game profile overrides.** `profile-overrides.txt` (generated by setup)
  maps game names to custom exported profiles
  (`game = file.html [, audio|noaudio]`): export a profile, drop it into
  `profiles\`, add one rule, and that game gets its own settings — applied in
  the normal pre-launch flow (one window, no focus loss), with the mix profile
  restored automatically on exit. Matching follows the same partial,
  encoding-tolerant rules as `native-games.txt`.
- **Start/stop state hand-off.** The start script records its decision in
  `ds5-last-start.txt`; the stop script reads it to decide whether to restore
  the mix profile. This works across separate PowerShell invocations (the
  `.bat` route) and is override-aware, replacing the native-list recompute as
  the primary exit decision.

### Changed
- **Playnite wiring docs corrected.** The recommended commands are the `.bat`
  launchers (`& "<your-folder>\automation\ds5-start.bat" "{Name}"` and the
  matching `ds5-stop.bat` line) — pasting `.ps1` paths gets them opened in
  Notepad instead of executed. Setup already printed the `.bat` lines; the
  automation README now matches.
- **native-games.txt ships as a curated default list** of PC games with native
  DualSense haptics (Returnal, God of War Ragnarök, PRAGMATA, Indiana Jones and
  the Great Circle, Ratchet & Clank: Rift Apart, Until Dawn, Alan Wake II,
  Days Gone, DOOM: The Dark Ages, The Last of Us Part I, Marvel's Spider-Man 2,
  Prince of Persia: The Lost Crown, SILENT HILL 2) instead of placeholder
  examples. Written as UTF-8; existing lists are never overwritten.
- Automation README setup steps updated to the policy-based grant flow and the
  full generated-file list.

## [1.1.0] — 2026-07-07

Adaptive triggers gain a push-back recoil kick (firmware), and profile
auto-apply is now robust with the wake feature enabled and fully hands-off
(portal + automation).

### Fixed
- **Wake broke profile auto-apply.** Enabling *Wake PC on PS Button* adds a
  boot-keyboard HID interface with the same VID/PID as the gamepad interface;
  the portal and profile pages could select it and feature reports failed. Device
  selection now skips an interface only when it is unambiguously that keyboard
  (every top-level collection is Generic Desktop/Keyboard) and otherwise behaves
  exactly as before. Applies to the portal and both auto-apply profiles; future
  exports inherit the fix.
- **Native game matching survives encoding damage.** Game names arriving with
  mangled non-ASCII characters (e.g. `Ragnarök` for `Ragnarök`) no longer fall
  through to the non-native branch: matching folds both sides to lowercase ASCII,
  and `native-games.txt` is read as UTF-8 explicitly.
- **Process cleanup works under both Windows PowerShell 5.1 and PowerShell 7.**
  Newer Playnite hosts PowerShell 7, where `Get-WmiObject` does not exist and
  the audio-capture kill silently failed; the scripts now use `Get-CimInstance`
  with a 5.1 fallback and `Stop-Process`, which work in both runtimes.
- **ds5audio survives silent launch and USB hiccups.** Under `pythonw` (silent
  automation launch) output is redirected to `ds5audio.log` instead of crashing on
  the first print; mid-stream device errors (e.g. `-9999` after a re-enumeration)
  now reconnect automatically instead of killing haptics until the next launch.

### Added
- **Adaptive triggers Stage 2 — push-back kick (recoil).** While Stage 1
  resistance is engaged, the vibration envelope fires a low-frequency vibration
  burst on R2: each rumble/haptics burst knocks the trigger back against the
  finger, then resistance resumes as it fades (hysteresis at envelope 32/16 plus
  a 45 ms minimum burst prevents mode chatter). New settings: kick strength
  (0–100, 0 = off and byte-identical Stage 1 behavior), envelope source (rumble /
  audio haptics / both — "both" means even rumble-less games kick on gunfire),
  and thump frequency (default 35; lower = heavier). New config fields 0x39–0x3b;
  live envelope + KICK flag added to the portal diagnostics (diag 0x3c). FW
  version reads 1.1.0.
- **Policy-based WebHID grant (`ds5-policy.bat` / `ds5-policy-remove.bat`).**
  Pre-grants the dongle to the profile pages via the Chromium
  `WebHidAllowDevicesForUrls` policy: no Connect click, immune to browser
  restarts and clear-site-data-on-close, and works with the DualSense-authentic
  (serial-less) USB identity. Generated by `ds5-setup`; requires admin once;
  fully reversible.
- **Profile pages auto-close after applying** (embedded profiles only), so tabs
  no longer pile up; failure states stay open and flag themselves in the taskbar
  title ("CONNECT NEEDED").
- **Profile windows open minimized** in their own browser window; HID waits run
  in a Web Worker so applying keeps full speed while hidden.
- **Debug and setup switches on `ds5-global-start.ps1`:** `-ShowWindow` runs the
  full pipeline visibly (log lines on screen, ds5audio in a console that stays
  open even if Python crashes) and `-GrantSetup` opens a profile page
  unminimized for a manual per-session grant.
- **Local profile server (opt-in).** `ds5-profile-server.py` (generated by
  setup) can serve the profile pages from `http://127.0.0.1:8377` instead of
  `file://` — enable via `$UseLocalServer` in the start/stop scripts. Off by
  default; useful only if a browser refuses `file://` grants, and covered by
  the policy grant either way.
- **Interpreter auto-detection** for the audio capture: tries
  `pythonw`/`python`/`py` in preference order, verifies the process survives
  startup, and supports pinning via `$PythonExe`.

### Removed
- Debug launchers `ds5-start-visible.bat` and `ds5-test-audio.bat` are no longer
  generated (stable now; run `ds5-global-start.ps1 -ShowWindow` or
  `python ds5audio.py --verbose` manually when debugging).
- `ds5-grant.bat` — superseded by the policy bats (the `-GrantSetup` switch on
  `ds5-global-start.ps1` remains as a manual fallback).

## [1.0.9] — 2026-07-05

Trigger-to-rumble "on press" now genuinely gates on trigger position, and native
haptics no longer break when trigger/rumble features are enabled.

### Fixed
- **`r2t` "on press" buzzed continuously.** The Vibration effect (0x26) buzzes
  whenever amplitude > 0 regardless of trigger position — restricting the zone
  bitmap did not gate it. "On press" now reads the actual analog trigger position
  and only emits vibration once the trigger is pulled past ~25%, so the triggers
  stay quiet at rest. Right-trigger position (`g_r2_pos`) is now captured alongside
  the existing left (`g_l2_pos`) in both report paths.

### Notes
- **Documented the wake / native-haptics conflict.** Enabling *Wake PC on PS Button*
  changes the USB descriptor (USB 2.1 + BOS + keyboard interface), which can make
  Steam Input stop recognizing the pad as a native DualSense — reverting some games
  (e.g. *Ratchet & Clank*) to Xbox-style rumble and disabling speaker audio. Keep
  wake off for native-haptics games, or switch it per-game. See the README.
- **Native haptics broke when trigger/rumble features were enabled (needed a
  reflash to recover).** The trigger-FFB "Allow" bits share output report byte 0
  with the rumble/haptic-mode flags. Synthesis left those Allow bits asserted in
  the persistent state, corrupting the haptic-control byte every cycle. The release
  path now clears the Allow bits (only when the game itself isn't driving the
  trigger), the game's Allow bits are synced from the host each cycle, and the
  staleness watchdog clears rather than sets them. Disabling a feature now restores
  native haptics live, without a reflash.

## [1.0.8] — 2026-07-04

### Fixed
- **Byte-0 haptics corruption** (see 1.0.9 notes — the persistent-Allow-bit fix
  landed here and was refined in 1.0.9).
- **Audio-transport fields could starve native haptics.** Documented that
  `audio_buffer_length` below the default and `polling_rate_mode = 2` (real-time)
  can interfere with the native haptic actuator stream, which shares the audio USB
  pipe. Defaults are safe; these are opt-in.

### Added
- **Channel-level audio diagnostics.** Portal now shows peak signal on ch0-1
  (speaker / DSP source) and ch2-3 (native actuators), so you can see whether real
  audio is reaching the DSP input (fields 0x37 / 0x38).

## [1.0.7] — 2026-07-04

### Fixed
- **Trigger feature priority and phantom L2.** Resistance now wins over vibration
  while a trigger is engaged (vibration resumes on release). Rumble bytes are only
  trusted when the host marks them valid, preventing phantom trigger buzz from
  apps that reuse those report offsets. Added an always-on resistance mode and a
  staleness watchdog that releases synthesis after 300 ms of no updates.

## [1.0.6] — 2026-07-03

### Added
- **Adaptive-trigger Stage 1 (`at`)** — L2-gated constant resistance on R2.
- **Trigger-to-rumble (`r2t`)** — convert rumble into trigger vibration, per-trigger.
- **Gyro-to-stick (`gyro`)** — motion aiming with configurable axis, sensitivity,
  invert, and touch modes; corrected yaw axis (byte 17) and 10x sensitivity range.
- **Haptics anti-alias** (`haptics_aa`) — 3-way filter on the native haptic stream.
- **Synthesis force override** and per-feature diagnostics fields.
- **Firmware version reported** to the portal (fields 0x7D/0x7E/0x7F).

### Fixed
- Empty-payload ownership bug where Steam Input / DS4Windows Allow-bit spam was
  mistaken for the game owning a trigger, disabling the synthesized effects.

## [1.0.5] — 2026-07-02

### Added
- Initial trigger-synthesis groundwork (rumble-to-trigger prototype) and expanded
  configuration surface, with RAM/CELT optimizations to fit the feature data.

## [1.0.4] — 2026-07-01

### Changed
- RAM optimization pass (CELT working memory) to make room for the auto-haptics
  feature data without exhausting the Pico 2W's memory.

## [1.0.3] — 2026-06-30

### Added
- Auto-haptics feature-data plumbing and configuration fields beyond the 1.0.2
  base, ahead of the trigger/gyro features in 1.0.6.

## [1.0.2] — 2026-06-20

Makes the **Wake PC on PS Button** feature genuinely usable alongside DS4Windows, and
hardens USB suspend handling. Wake remains **off by default**.

### Fixed
- **Clean controller disconnect with wake enabled.** Previously, enabling wake kept the
  USB device on the bus after the controller powered off, so the controller lingered in
  DS4Windows and the configuration portal could read stale/zero values. The controller
  now disconnects cleanly from the host whenever the PC is awake — even with wake on —
  and the device is kept on the bus only while the PC is actually suspended (where wake
  needs it to signal a wake-up). Turning the controller off no longer leaves a phantom
  USB device behind.
- **Ride out hub-induced USB suspends.** Some USB hubs briefly suspend a live bus while
  the host is awake; since the base firmware a suspend immediately powered off the
  controller's Bluetooth, dropping the controller behind such hubs. The power-off is now
  debounced (committed only after a sustained suspend — a real sleep or shutdown), so
  transient hub blips are ridden through. (Ported from upstream PR #186 by
  up2urheadlights.)
- **Deliberate USB reconnect no longer drops the controller.** A portal "Reconnect USB"
  (used when saving settings that require re-enumeration) briefly looks like a suspend.
  A grace window now exempts it, so saving such settings no longer powers off the
  controller. This also resolves the save instability that could occur with wake enabled.
- **Portal: stale handle after controller reconnect.** The configuration portal now
  listens for the WebHID disconnect event and releases its device handle, so saving
  immediately after disconnecting and reconnecting the controller works without manually
  clicking Connect again.

### Added
- **Wake PC on PS Button** is exposed as a portal toggle (in *Device & Connection*),
  off by default. Enable it only if you want the controller's PS button to wake the PC
  from sleep.

### Notes
- The stuck-rumble fix from 1.0.1-hotfix2 is included and confirmed compatible with the
  clean-disconnect behavior (the earlier disconnect regression was the wake feature, not
  the rumble fix).
- Connecting from sleep can take a few extra seconds; some variability is inherent to the
  Bluetooth reconnect path and is not specific to this build.
- After flashing, run `flash_nuke.uf2` first if you are coming from a different config
  layout.

## [1.0.1-hotfix2] — 2026-06-19

Adds an upstream stuck-rumble fix, ported to this build.

### Fixed
- **Stuck rumble while audio is active.** When the controller speaker was active
  (which is the case whenever audio passthrough or auto-haptics is in use), the
  firmware skipped re-sending state to the controller for efficiency — which
  swallowed rumble start/stop commands and could leave the motors stuck on. The
  state update now reports whether the controller-facing output actually changed,
  and the change is sent even while the speaker is active, so rumble starts and
  stops are no longer dropped. (Ports mik9's upstream "Fix stuck rumble" to the
  v0.7.0 base used here.)

### Note
- A side effect of the rumble path now being complete is that converted rumble in
  Mix mode may feel slightly stronger than before (rumble commands that were
  previously dropped now apply). Rebalance with **Converted Rumble Strength** if
  needed. This does not affect audio-derived auto-haptics, which run through a
  separate path.

## [1.0.1-hotfix] — 2026-06-19

Hotfix over 1.0.1 addressing a wake-related connection bug.

### Fixed
- **Stuck connection with wake enabled.** With the wake feature on, the USB device
  stays on the bus after the controller powers off (so a later button press can
  wake the host). On reconnection this left the controller stuck — connected but
  non-functional, with a steady (yellow) LED — until the dongle was physically
  replugged, because the bare `tud_connect()` was a no-op while the device was
  still enumerated. The firmware now forces a clean USB re-enumeration
  (`tud_disconnect()` → settle → `tud_connect()`) on reconnect when wake is
  enabled, so the connection completes normally.

### Known limitations
- With wake enabled, the device may remain listed in DS4Windows after the
  controller disconnects (the persistent USB presence wake requires). Turn wake off
  for clean disconnect behavior in DS4Windows.
- Reconnection may occasionally take slightly longer with wake enabled, as a result
  of the clean re-enumeration.

### Note
- The "Wake PC on PS Button" toggle is labeled without a device-type qualifier; the
  feature asserts USB remote wakeup on a button press to wake the host.

## [1.0.1] — 2026-06-19

First public release. Built on awalol/DS5Dongle v0.7.0. Refines the initial 1.0.0
cut with the effect-leak rework, the wake toggle, and a batch of portal reliability
fixes.

### Added
- **Effect leak output high-pass** — removes deep bass from the speaker output to
  stop the small controller speaker from popping on low-frequency content (separate
  from the transient detection band).
- **Effect leak attack control** — configurable gate-open speed to trade immediacy
  against onset smoothness.
- **Effect leak decay control** — configurable fade-out length so effects ring out
  gradually instead of cutting off abruptly.
- Exposed the base firmware's **Wake PC on PS Button** (USB remote wakeup) as a
  portal toggle.

### Changed
- **Effect leak reworked to transient detection** — opens the speaker only on sharp
  onsets rather than passing all high-frequency content, so sustained dialog/music
  stays muted while discrete effects pass. Output is full-band (gated) to avoid the
  thin/crackly sound of the earlier high-pass-only approach.
- **Smart save** — the portal now only triggers a reconnect when a setting that
  requires USB re-enumeration changed (polling rate, audio buffer, mic/speaker
  enable, wake); all other settings apply live with no reconnect.

### Fixed
- **Portal save reliability** — background diagnostic/RSSI polling no longer
  collides with saves (which previously caused saves to fail after a few cycles).
- **Stale device-handle recovery** — saves re-acquire a fresh handle if the cached
  one went stale after a reconnect, instead of silently failing.
- **Refresh no longer blanks values to 0** — failed reads preserve the previous
  value and retry, with a settle delay after reconnect before reading.

## [1.0.0] — 2026-06-18

Initial internal build. Built on awalol/DS5Dongle v0.7.0.

### Added
- **Audio-derived auto-haptics** for games without native DualSense haptics, using
  bass-envelope amplitude modulation of a 90 Hz carrier so the voice-coil actuator
  physically actuates (a plain low-pass produces a near-DC signal the coil cannot
  render).
- **Three modes:** Off (native/rumble passthrough), Mix (native + derived),
  Replace (derived only).
- **DS4Windows compatibility:** auto-haptics now work under DS4Windows in
  passthrough, DualShock 4, and Xbox 360 modes. Fixed the base firmware forcing
  `UseRumbleNotHaptics`, which silenced the actuator path that auto-haptics needs.
- **Converted-rumble blending (Mix mode):** DS4Windows rumble from emulated
  controllers is converted to actuator vibration and blended with the audio
  haptics, with an independent strength control.
- **Effect leak (transient detection):** optional speaker passthrough that opens
  only on sharp onsets (shots, clinks, impacts) and stays muted for sustained
  dialog/music. Configurable volume, sensitivity, attack, decay, detection band,
  and an output high-pass that protects the speaker from low-frequency popping.
- **DSP controls:** intensity (curved), smoothness, noise gate, low-pass crossover,
  and selectable filter slope (6/12/24 dB/oct).
- **Lightbar Off in Replace mode** to suppress the default glow (e.g. blue in
  Xbox 360 emulation).
- **Live RSSI / Bluetooth signal display** in the portal.
- **Reboot-to-bootloader** command from the portal.
- **Experimental BT latency controls:** flush timeout and QoS setup (both default
  off; inconclusive in testing, retained for tinkering).
- **Web configuration portal:** sectioned UI, full field set, robust device-handle
  and read handling, smart save (only reconnects when a setting that requires USB
  re-enumeration actually changed).
- Exposed the base firmware's **Wake PC on PS Button** (USB remote wakeup) as a
  portal toggle.

### Changed
- Channel detection (2ch vs 4ch) so DS4Windows/Windows stereo streams are handled
  correctly; mode remains authoritative (2ch input does not force auto-haptics on).
- Mix mode low-passes the native haptic channels before mixing to prevent full-band
  audio (e.g. duplicated stereo in VoiceMeeter setups) leaking to the actuators.

### Notes
- Native haptics over Bluetooth remain slightly less tight than over USB; this is
  inherent to the BT transport and not tunable in firmware.
- The effect leak inherits the controller speaker's Opus-over-Bluetooth pipeline
  latency; it is minimized but not zero, and transient effects expose it more than
  continuous audio.
- After saving, the PlayStation accessory app may take 2–3 seconds (or a second
  reopen) to display an updated value, though the setting is saved immediately.
- The firmware ships with stock defaults; see the README "Suggested setup" for a
  tuned auto-haptics + effect-leak configuration to apply in the portal.

[1.0.2]: https://github.com/awalol/DS5Dongle
[1.0.1-hotfix2]: https://github.com/awalol/DS5Dongle
[1.0.1-hotfix]: https://github.com/awalol/DS5Dongle
[1.0.1]: https://github.com/awalol/DS5Dongle
[1.0.0]: https://github.com/awalol/DS5Dongle
