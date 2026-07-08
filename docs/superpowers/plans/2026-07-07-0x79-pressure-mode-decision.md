# Decision: the `0x79` (DS2 pressure) mode gap in `ds2_protocol.c`

Status: proposed | Author: planning subagent | Date: 2026-07-07

This is a decision-support document. It does not change any source code.

## 1. Problem statement

`src/ps2_device/ds2_protocol.c` has two independent paths that can put the
protocol state machine into `MODE_ANALOG_PRESSURE` (`0x79`), but the response
builder that actually serializes a poll frame has no branch for that mode. The
result is a malformed frame that, on real DS2-aware hardware, reads back as
"every pressure button is fully pressed."

**Path 1 — `CMD_POLL_CONFIG` (`0x4F`) directly flips the mode**
(`src/ps2_device/ds2_protocol.c:133-140`, in `ds2_apply_request`):

```c
case CMD_POLL_CONFIG:
    if (st->config && req_len > 4) {
        for (int i = 0; i < 4; i++) st->poll_config[i] = req[1 + i];
        int sum = st->poll_config[0] + st->poll_config[1]
                + st->poll_config[2] + st->poll_config[3];
        st->mode = (sum != 0) ? MODE_ANALOG_PRESSURE : MODE_ANALOG;
    }
    break;
```

**Path 2 — `CMD_ANALOG_SWITCH` (`0x44`) routes through `detect_analog()`**
(`ds2_protocol.c:127-131` calling `detect_analog()` at `ds2_protocol.c:17-21`):

```c
static uint8_t detect_analog(const ds2_state_t *st) {
    int sum = st->poll_config[0] + st->poll_config[1]
            + st->poll_config[2] + st->poll_config[3];
    return (sum > 0) ? MODE_ANALOG_PRESSURE : MODE_ANALOG;
}
...
case CMD_ANALOG_SWITCH:
    if (st->config && req_len > 2) {
        st->mode = (req[1] == 0x01) ? detect_analog(st) : MODE_DIGITAL;
        st->analog_lock = (req[2] == 0x03);
    }
    break;
```

So a console can reach `MODE_ANALOG_PRESSURE` either by sending a nonzero
`0x4F` directly, or by sending a nonzero `0x4F` followed later by `0x44` — the
real-hardware handshake order (see §"Real-game behavior" below) is actually
`0x44` *then* `0x4F`, but both orders reach the same state because
`poll_config` is sticky across calls.

**The missing branch — `ds2_response`'s `CMD_POLL` case**
(`ds2_protocol.c:33-46`):

```c
case CMD_POLL: {
    if (st->mode == MODE_DIGITAL) {
        if (n < cap) out[n++] = in->buttons1;
        if (n < cap) out[n++] = in->buttons2;
    } else { // ANALOG (pressure handled in a later task)
        if (n < cap) out[n++] = in->buttons1;
        if (n < cap) out[n++] = in->buttons2;
        if (n < cap) out[n++] = in->rx;
        if (n < cap) out[n++] = in->ry;
        if (n < cap) out[n++] = in->lx;
        if (n < cap) out[n++] = in->ly;
    }
    break;
}
```

There are only two arms: `MODE_DIGITAL` and "everything else." When
`st->mode == MODE_ANALOG_PRESSURE`, control falls into the `else` arm and
`ds2_response` emits exactly 6 payload bytes — correct for `MODE_ANALOG`
(`0x73`, whose ID low nibble `3` means 3 words = 6 bytes), **wrong** for
`MODE_ANALOG_PRESSURE` (`0x79`, whose ID low nibble `9` means 9 words = 18
bytes).

**Failure mechanism.** `id_byte()` (`ds2_protocol.c:13-15`) returns
`st->mode` verbatim when not in config mode, so `out[0]` becomes `0x79`. The
console reads that ID byte, computes "18 payload bytes follow," and clocks 18
more bytes on SPI — but this firmware only drives 6 of them (buttons1,
buttons2, rx, ry, lx, ly); the remaining 12 clock cycles happen after this
device's PIO/transport layer has nothing left to shift out, so the bus reads
back the idle/undriven line value. Per confirmed real-hardware behavior for
DS2 pressure bytes, `0x00` = unpressed and **`0xFF` = fully pressed** — so the
12 "missing" bytes are read by the console as full pressure on every one of
the 12 pressure-sensitive inputs (D-pad × 4, face buttons × 4, L1/R1, L2/R2)
simultaneously, for every poll, for as long as the pad stays in this mode.
This is not a benign zero-fill; it is the worst-case value for this
particular field. On real hardware this presents as a broken pad (e.g. a
racing title reading max throttle and max brake at once, or a stealth title
reading a permanently-held attack button).

The project's own design spec explicitly defers this: `docs/superpowers/specs/2026-07-07-ps2-dualshock-bluetooth-adapter-design.md`
lists "Pressure-sensitive buttons (DS2 mode `0x79`)" under Non-goals, and
states (§5 item 2) "In MVP analog mode (`0x73`) there are no pressure bytes,
so the analog triggers are thresholded to the digital L2/R2 button bits...
full analog pressure arrives with the deferred `0x79` mode." The
implementation plan's own Self-Review section
(`docs/superpowers/plans/2026-07-07-ps2-dualshock-bluetooth-adapter.md`,
end of file) asserts: "Deferred (pressure/rumble/multitap) correctly
excluded; `ds2_init` biases to analog (Task 4). ✓" — that claim is currently
**false**: `ds2_init` biases to analog only at *init* time
(`ds2_protocol.c:5-11`, `poll_config` zeroed), but nothing stops a later
`0x4F`/`0x44` sequence from promoting the pad into the very mode the plan
says is excluded. There is also no task in the 15-task implementation plan
for pressure/`0x79` work at all — `M5` (pressure), `M6` (rumble), `M7`
(multitap) exist only as forward-looking labels in the design spec's
milestone list, never as concrete plan tasks.

**Confirmed via web research** (protocol-notes / DualShock 2 documentation
consulted 2026-07-07):
- The 12 pressure bytes, in wire order following the 6 digital/analog bytes,
  are: **Right, Left, Up, Down, Triangle, Circle, Cross, Square, L1, R1, L2,
  R2** — each one byte, `0x00` = not pressed, `0xFF` = fully pressed. This
  matches, byte-for-byte, the reference fork's authoritative implementation
  (`controller_simulator.cpp:151-160`, `processPoll()`'s `MODE_ANALOG_PRESSURE`
  case), which was already read in full as part of this task's required
  reading.
- A real console's standard sequence to *opt into* pressure is:
  `0x43`(enter config) → `0x44`(select analog, req[1]=0x01) →
  `0x4F`(enable pressure via a nonzero bitfield) → `0x43`(exit config). This
  is a deliberate, game-initiated action, not an incidental part of a
  generic controller handshake — games that don't care about pressure simply
  never send `0x4F`.
- Games confirmed (via general DS2-protocol documentation, not
  exhaustively verified against ROMs) to use pressure-sensitive input
  include *Metal Gear Solid 2* and *3*, *Gran Turismo 4*, *Grand Theft Auto:
  San Andreas* (its throttle-by-X-pressure driving mechanic is a commonly
  cited example), *Tekken* titles, and *Okami*. This means the bug is not a
  hypothetical edge case: it is a predictable, reproducible failure for a
  recognizable, non-trivial slice of the PS2 library, triggered exactly when
  those titles perform their own normal pressure-enable handshake.

## 2. Options

### A. Clamp to analog (MVP-safe)

Make it structurally impossible for `st->mode` to become
`MODE_ANALOG_PRESSURE` anywhere in the state machine, while still recording
`poll_config` (harmless, and preserves state for a future, properly-scoped
pressure implementation).

**Exact code changes**, both in `src/ps2_device/ds2_protocol.c`:

```c
// ds2_protocol.c:17-21 (detect_analog)
static uint8_t detect_analog(const ds2_state_t *st) {
    (void)st;  // MVP: pressure mode (0x79) is out of scope — ds2_response has
               // no 18-byte branch for it, so poll_config must never be able
               // to promote us into MODE_ANALOG_PRESSURE. See
               // docs/superpowers/plans/2026-07-07-0x79-pressure-mode-decision.md.
    return MODE_ANALOG;
}
```

```c
// ds2_protocol.c:133-140 (CMD_POLL_CONFIG case in ds2_apply_request)
case CMD_POLL_CONFIG:
    if (st->config && req_len > 4) {
        for (int i = 0; i < 4; i++) st->poll_config[i] = req[1 + i];
        // MVP: pressure mode (0x79) unsupported by ds2_response; keep the pad
        // in MODE_ANALOG regardless of what the console asked to enable.
        st->mode = MODE_ANALOG;
    }
    break;
```

Both mode-entry paths (`0x4F` direct, and `0x44` via `detect_analog()`) now
converge on `MODE_ANALOG`. `poll_config` is still recorded (useful diagnostic
state, and reusable if/when pressure is implemented later), it just no
longer drives a mode transition. `MODE_ANALOG_PRESSURE` stays defined in
`ds2_ids.h` (harmless — it becomes unreachable dead code by design, which is
fine and should be noted with a comment there for a future reader).

**Test changes**, both in `test/test_ds2_protocol.c`:

1. Repurpose `test_pollconfig_4F_enables_pressure_mode`
   (`test_ds2_protocol.c:135-141`) to assert the opposite — the pad stays
   `0x73`:

   ```c
   static void test_pollconfig_4F_does_not_enable_pressure_mode(void) {
       ds2_state_t st; ds2_init(&st); st.config = true; st.mode = MODE_ANALOG;
       // req[1..4] = the 4 config bytes; nonzero sum previously flipped the
       // pad to MODE_ANALOG_PRESSURE. ds2_response has no 0x79 branch (MVP
       // scope), so mode must now stay MODE_ANALOG regardless of what the
       // console requests via 0x4F.
       const uint8_t req[] = {0x00, 0xFF, 0xFF, 0x03, 0x00, 0x00};
       ds2_apply_request(&st, CMD_POLL_CONFIG, req, sizeof req);
       TEST_ASSERT_EQUAL_HEX8(MODE_ANALOG, st.mode);
   }
   ```

   Rename the `RUN_TEST(...)` entry to match.

2. Add a new end-to-end handshake test covering the *other* path into
   pressure mode (`0x4F` then `0x44`, the real-console order confirmed by
   research above) and verifying the resulting poll frame is well-formed:

   ```c
   static void test_handshake_4F_then_44_stays_analog(void) {
       ds2_state_t st; ds2_init(&st); st.config = true;
       // Console requests pressure via 0x4F (nonzero config)...
       const uint8_t poll_cfg_req[] = {0x00, 0xFF, 0xFF, 0x03, 0x00, 0x00};
       ds2_apply_request(&st, CMD_POLL_CONFIG, poll_cfg_req, sizeof poll_cfg_req);
       // ...then selects analog + lock via 0x44, as a real init sequence does.
       const uint8_t analog_req[] = {0x00, 0x01, 0x03, 0x00, 0x00, 0x00};
       ds2_apply_request(&st, CMD_ANALOG_SWITCH, analog_req, sizeof analog_req);

       TEST_ASSERT_EQUAL_HEX8(MODE_ANALOG, st.mode);
       TEST_ASSERT_TRUE(st.analog_lock);

       PSXInputState in = ds2_neutral_state();
       uint8_t out[32];
       size_t n = ds2_response(&st, CMD_POLL, &in, NULL, 0, out, sizeof out);
       const uint8_t expect[] = {0x73, 0x5A, 0xFF, 0xFF, 0x80, 0x80, 0x80, 0x80};
       TEST_ASSERT_EQUAL_UINT(sizeof expect, n);
       TEST_ASSERT_EQUAL_HEX8_ARRAY(expect, out, n);
   }
   ```

**Effort:** trivial. Two function bodies changed (~6 lines total), one test
renamed/flipped, one new ~15-line test. No header, struct, or build changes.
Estimated 15-30 minutes including verification.

**Risk:** very low. All 12 pre-existing tests other than
`test_pollconfig_4F_enables_pressure_mode` are untouched and their assertions
are unaffected — `detect_analog()`'s new unconditional `MODE_ANALOG` return
and the `CMD_POLL_CONFIG` clamp only change behavior on the *nonzero*
`poll_config` path, which no other existing test exercises.

**Game compatibility lost:** titles that explicitly negotiate pressure mode
(MGS2/3, GT4, GTA: San Andreas, Tekken, Okami, per the confirmed list above)
will play in analog mode with L2/R2 thresholded to their digital bits (per
design spec §5 item 2) instead of true 12-point pressure — this is a fidelity
downgrade for those specific titles (e.g. GTA:SA's pressure-based throttle
becomes binary), not a crash or hang; the console still receives a
well-formed, correctly-sized `0x73` frame it can parse normally. The
overwhelming majority of the PS2 library, which never sends `0x4F`, is
entirely unaffected.

### B. Implement `0x79` fully now

Add a real `MODE_ANALOG_PRESSURE` branch to `ds2_response`'s `CMD_POLL` case,
keep both existing mode-transition paths as-is.

**Exact response-builder addition** (`ds2_protocol.c:33-46`), based
byte-for-byte on the reference fork's `processPoll()`
(`controller_simulator.cpp:151-160`), translated to this project's
active-low `PS_*` masks from `ds2_ids.h`:

```c
case CMD_POLL: {
    if (st->mode == MODE_DIGITAL) {
        if (n < cap) out[n++] = in->buttons1;
        if (n < cap) out[n++] = in->buttons2;
    } else if (st->mode == MODE_ANALOG_PRESSURE) {
        if (n < cap) out[n++] = in->buttons1;
        if (n < cap) out[n++] = in->buttons2;
        if (n < cap) out[n++] = in->rx;
        if (n < cap) out[n++] = in->ry;
        if (n < cap) out[n++] = in->lx;
        if (n < cap) out[n++] = in->ly;
        // 12 pressure bytes, wire order: Right, Left, Up, Down, Triangle,
        // Circle, Cross, Square, L1, R1, L2, R2. PS_* bits are active-low
        // (1=released, 0=pressed), so "bit set" -> 0x00 (no pressure) and
        // "bit clear" -> 0xFF (full pressure). Digital-derived: no per-button
        // force sensor exists upstream (BluePad32 pads don't provide one).
        if (n < cap) out[n++] = (in->buttons1 & PS_RIGHT) ? 0x00 : 0xFF;
        if (n < cap) out[n++] = (in->buttons1 & PS_LEFT)  ? 0x00 : 0xFF;
        if (n < cap) out[n++] = (in->buttons1 & PS_UP)    ? 0x00 : 0xFF;
        if (n < cap) out[n++] = (in->buttons1 & PS_DOWN)  ? 0x00 : 0xFF;
        if (n < cap) out[n++] = (in->buttons2 & PS_TRI)   ? 0x00 : 0xFF;
        if (n < cap) out[n++] = (in->buttons2 & PS_CIR)   ? 0x00 : 0xFF;
        if (n < cap) out[n++] = (in->buttons2 & PS_X)     ? 0x00 : 0xFF;
        if (n < cap) out[n++] = (in->buttons2 & PS_SQU)   ? 0x00 : 0xFF;
        if (n < cap) out[n++] = (in->buttons2 & PS_L1)    ? 0x00 : 0xFF;
        if (n < cap) out[n++] = (in->buttons2 & PS_R1)    ? 0x00 : 0xFF;
        if (n < cap) out[n++] = (in->buttons2 & PS_L2)    ? 0x00 : 0xFF;
        if (n < cap) out[n++] = (in->buttons2 & PS_R2)    ? 0x00 : 0xFF;
    } else { // MODE_ANALOG
        if (n < cap) out[n++] = in->buttons1;
        if (n < cap) out[n++] = in->buttons2;
        if (n < cap) out[n++] = in->rx;
        if (n < cap) out[n++] = in->ry;
        if (n < cap) out[n++] = in->lx;
        if (n < cap) out[n++] = in->ly;
    }
    break;
}
```

**Important divergence from the fork:** the fork passes real analog trigger
values through for the last two pressure bytes
(`inputState.l2, inputState.r2`, from a DS4's genuine analog triggers). This
project's `PSXInputState` (per `input_state.h` / `ds2_neutral_state()` as
used throughout `test_ds2_protocol.c`) currently has only `buttons1,
buttons2, rx, ry, lx, ly` — no raw L2/R2 trigger-depth field. Design spec §5
item 2 already documents this limitation for MVP analog mode ("thresholded to
the digital L2/R2 button bits"). So a faithful-to-the-fork L2/R2 passthrough
is **not available today** without first extending `PSXInputState` and the
BluePad32 input-mapping layer (outside `ds2_protocol.c`, not part of this
task's required reading) to carry real trigger magnitudes — that is
additional, uncosted scope. The code above therefore digital-derives *all 12*
pressure bytes, including L2/R2, for consistency and to avoid touching
unrelated files.

**Bounds-guard requirement:** identical `n < cap` pattern already used
throughout `ds2_response` (this exact discipline was the subject of a prior
critical bounds-safety fix in this file — commit `4fbc790`, Task 4). A full
`MODE_ANALOG_PRESSURE` frame is 20 bytes (2-byte header + 6 + 12); every
existing test buffer (`uint8_t out[32]`) already exceeds this.

**New tests needed** in `test/test_ds2_protocol.c`:

1. `test_pressure_poll_neutral` — neutral input, `st.mode = MODE_ANALOG_PRESSURE`,
   expect the full 20-byte neutral frame:

   ```c
   static void test_pressure_poll_neutral(void) {
       ds2_state_t st; ds2_init(&st); st.mode = MODE_ANALOG_PRESSURE;
       PSXInputState in = ds2_neutral_state();
       uint8_t out[32];
       size_t n = ds2_response(&st, CMD_POLL, &in, NULL, 0, out, sizeof out);
       const uint8_t expect[] = {
           0x79, 0x5A, 0xFF, 0xFF, 0x80, 0x80, 0x80, 0x80,
           0x00, 0x00, 0x00, 0x00,   // Right, Left, Up, Down
           0x00, 0x00, 0x00, 0x00,   // Triangle, Circle, Cross, Square
           0x00, 0x00, 0x00, 0x00    // L1, R1, L2, R2
       };
       TEST_ASSERT_EQUAL_UINT(sizeof expect, n);
       TEST_ASSERT_EQUAL_HEX8_ARRAY(expect, out, n);
   }
   ```

2. `test_pressure_poll_reflects_pressed_buttons` — press Cross and Right,
   assert only their pressure bytes read `0xFF`:

   ```c
   static void test_pressure_poll_reflects_pressed_buttons(void) {
       ds2_state_t st; ds2_init(&st); st.mode = MODE_ANALOG_PRESSURE;
       PSXInputState in = ds2_neutral_state();
       in.buttons1 &= (uint8_t)~PS_RIGHT;  // press Right
       in.buttons2 &= (uint8_t)~PS_X;      // press Cross
       uint8_t out[32];
       size_t n = ds2_response(&st, CMD_POLL, &in, NULL, 0, out, sizeof out);
       TEST_ASSERT_EQUAL_UINT(20, n);
       TEST_ASSERT_EQUAL_HEX8(0xFF, out[8]);   // Right pressure
       TEST_ASSERT_EQUAL_HEX8(0x00, out[9]);   // Left pressure (untouched)
       TEST_ASSERT_EQUAL_HEX8(0xFF, out[14]);  // Cross pressure
       TEST_ASSERT_EQUAL_HEX8(0x00, out[11]);  // Triangle pressure (untouched)
   }
   ```

3. `test_pressure_poll_respects_cap` — mirror the existing
   `test_response_respects_cap` (`test_ds2_protocol.c:45-55`) at a cap below
   20 bytes, verifying `n <= cap`, canary intact, and a correct truncated
   prefix.

4. Keep `test_pollconfig_4F_enables_pressure_mode`
   (`test_ds2_protocol.c:135-141`) unchanged — it already asserts the state
   *transition*; Option B's change is entirely in the response builder.

**Effort:** medium. One new response-builder arm (~16 lines), 3 new tests
(~50-60 lines). No struct/header changes for the digital-derived-only
version above. Estimated 45-60 minutes including verification. A
fully-faithful version with real L2/R2 analog passthrough would add
unscoped work in `input_state.h` and the BluePad32 mapping layer.

**Risk:** medium-high, for reasons independent of code correctness:
- This is genuinely new, previously-unexercised protocol surface — `0x79` is
  an explicit design-spec Non-goal — being added without real-hardware
  validation. Task 14 ("Full lifecycle on a real PS2," the plan's first
  real-console milestone) has not happened yet even for the simpler analog
  path; adding a third mode multiplies the on-hardware unknowns before that
  gate is cleared.
- There is no task for this in the actual 15-task plan (confirmed: `M5` is
  a design-spec milestone label only). Doing it now is scope creep relative
  to the plan the rest of the project is following, and works against the
  plan's own Self-Review claim that pressure is "correctly excluded."
- Digital-deriving L2/R2 (rather than real analog passthrough) is arguably a
  *downgrade* from what `MODE_ANALOG` already does for L2/R2 today (a
  continuous-feeling stick-based threshold per spec §5 item 2) — games that
  specifically rely on analog trigger depth (many racing titles) would get a
  binary on/off L2/R2 in pressure mode, which is no better, and arguably
  worse to reason about, than what MVP analog mode already provides.

### C. Defer as tracked issue

Leave `ds2_protocol.c` completely unmodified. Record the hazard as an
explicit, hard precondition on the plan's own real-hardware milestone.

**Where to record it:**
- Add a blocking-precondition note directly above **Task 14 ("Full lifecycle
  on a real PS2")** in
  `docs/superpowers/plans/2026-07-07-ps2-dualshock-bluetooth-adapter.md`,
  stating that Task 14 must not be attempted with any title that negotiates
  pressure mode (`0x4F` with a nonzero payload) until this decision doc's
  chosen option has landed, and linking to this file
  (`docs/superpowers/plans/2026-07-07-0x79-pressure-mode-decision.md`).
- Task 15 already plans to write `README.md`; add a "Known Issues" bullet
  there once it exists, naming the confirmed at-risk titles (MGS2/3, GT4,
  GTA: San Andreas, Tekken, Okami) so anyone doing hands-on hardware testing
  knows to avoid them, or knows what symptom to expect ("every pressure
  button reads as fully pressed") if they hit it.
- This repo has no existing "Known Issues" doc structure outside `specs/`
  and `plans/`, so the plan-file precondition is the most durable immediate
  option; a dedicated issue tracker entry would be preferable if/when one
  exists.

**Effort:** ~0 for code. ~5-10 minutes to add the documentation notes
described above (not performed in this pass, since this task's mandate is
limited to writing this single decision document and must not edit other
files or source code — but it is a concrete, cheap follow-up any maintainer
can do immediately after reading this doc).

**Risk:** high, and not merely theoretical. Task 14's own acceptance
criterion is informal — "the console shows a controller connected; in a
game, digital buttons work and the analog sticks move" — it does not name a
specific title, and does not warn against pressure-negotiating titles. Per
the confirmed real-game list above, a plausible, easy-to-reach-for test
title (a Gran Turismo game, for its analog-stick feel; or Metal Gear Solid,
a very commonly-owned PS2 title) is *exactly* the kind of title that would
trip this bug during that very first hardware bring-up session. The observed
symptom — every pressure input reading fully pressed — would look like an
unrelated, confusing hardware/firmware bug (stuck buttons, runaway
acceleration, permanently-held attack) rather than a known, already-diagnosed
gap, unless this document is in hand at debug time. Option C fixes the
documentation gap but not the runtime hazard; the landmine stays live and
sits directly on the path of the plan's very next milestone.

## 3. Recommendation

**Recommend Option A: clamp to analog.**

Reasoning, tied to the four factors requested:

- **MVP scope.** The design spec lists `0x79` as an explicit Non-goal and
  the plan's own Self-Review already *claims* pressure is "correctly
  excluded." Option A is the only option that makes that claim true; today
  it is false. Option B directly contradicts the spec's stated MVP boundary
  by implementing the deferred feature now, with no corresponding task in
  the actual plan to justify doing so ahead of schedule.
- **The M3 (Task 14) ordering hazard.** Task 14 — the plan's very next
  milestone — is real-console verification with an unrestricted "in a game"
  acceptance test, and the confirmed list of pressure-using titles (GT4, MGS
  series, GTA: San Andreas, Tekken, Okami) includes exactly the kind of
  well-known titles someone would reach for during hardware bring-up.
  Clamping to analog removes the entire hazard class *before* Task 14 runs,
  without touching any of Task 14's own scope (clock speed, GPIO drive
  strength, connect/disconnect lifecycle). Option C leaves the hazard live
  precisely where Task 14 would trip it; Option B adds a third,
  hardware-unvalidated mode into the very milestone meant to validate the
  first two.
- **Effort.** Option A is a ~6-line, two-function change plus one repurposed
  and one new test — 15-30 minutes, fully verifiable on the host test suite
  with no hardware. Option B is several times the effort and, per the
  analysis above, produces pressure data of questionable value (all-digital,
  including L2/R2) without the upstream input-capture work needed to make it
  genuinely useful. Option C's code effort is zero but leaves work (and
  risk) for later, on hardware, under worse conditions to debug.
- **Real-game behavior.** Research confirms games only exercise `0x4F` when
  they specifically want pressure — so Option A costs nothing for the
  overwhelming majority of the PS2 library, and for the pressure-using
  minority it produces a graceful degradation (correctly-sized `0x73` analog
  frame, thresholded L2/R2, exactly matching what non-pressure titles
  already get) rather than a malformed `0x79` frame that reads as a stuck
  pad. That is a strictly better failure mode for every title in the
  compatibility list identified in §1.

Option B should be revisited post-MVP, once BluePad32's real per-trigger
analog capability (where available) can be threaded through
`PSXInputState`, and once Task 14/15 have validated the simpler analog path
on real hardware — i.e., roughly where the design spec's own `M5` milestone
already sits.

## 4. Task list for the recommended option (Option A)

Follows the existing plan's TDD step style (failing test → implement →
verify → commit). All file paths are relative to the repo root.

**Task 16: Clamp pressure mode to analog (close the `0x79` gap)**

1. **Step 1 — write the failing/changed test.** In
   `test/test_ds2_protocol.c`, replace `test_pollconfig_4F_enables_pressure_mode`
   (lines 135-141) with `test_pollconfig_4F_does_not_enable_pressure_mode`
   (body given in §2, Option A above), asserting
   `TEST_ASSERT_EQUAL_HEX8(MODE_ANALOG, st.mode)`. Update the corresponding
   `RUN_TEST(...)` line in `main()`. Also add
   `test_handshake_4F_then_44_stays_analog` (body given above) and its
   `RUN_TEST(...)` entry. Confirm these two tests currently **fail** (or, for
   the renamed test, that the *old* assertion — `MODE_ANALOG_PRESSURE` — is
   what today's code still produces) by building and running:
   ```
   ctest --test-dir build-test
   ```
   Expect the renamed test to fail against the pre-fix `detect_analog`/`CMD_POLL_CONFIG`
   logic (it should currently report `st.mode == 0x79`, not `0x73`).

2. **Step 2 — implement the clamp.** In `src/ps2_device/ds2_protocol.c`:
   - Change `detect_analog()` (lines 17-21) to unconditionally
     `return MODE_ANALOG;` (keep the `(void)st;` cast to avoid an unused-parameter
     warning), with a comment citing this decision doc.
   - Change the `CMD_POLL_CONFIG` case in `ds2_apply_request`
     (lines 133-140) to keep recording `st->poll_config[i]` but set
     `st->mode = MODE_ANALOG;` unconditionally instead of branching on the
     config-byte sum.

3. **Step 3 — verify green.** Rebuild and rerun the full host suite:
   ```
   ctest --test-dir build-test --output-on-failure
   ```
   Expected: all tests pass, including:
   - `test_pollconfig_4F_does_not_enable_pressure_mode` →
     `st.mode == MODE_ANALOG` (`0x73`) after a nonzero `0x4F`.
   - `test_handshake_4F_then_44_stays_analog` → `st.mode == MODE_ANALOG`,
     `st.analog_lock == true`, and the poll frame equals exactly
     `73 5A FF FF 80 80 80 80` (8 bytes).
   - All 11 previously-existing, unmodified tests (`test_neutral_state_defaults`,
     `test_analog_poll_neutral`, `test_digital_poll_neutral`,
     `test_response_respects_cap`, `test_digital_poll_respects_cap`,
     `test_config_mode_id_is_F3`, `test_analog_poll_reflects_input`,
     `test_enter_config_sets_flag`, `test_exit_config_clears_flag`,
     `test_analog_switch_sets_analog_and_lock`,
     `test_analog_switch_ignored_outside_config`,
     `test_status_45_in_analog_config`) continue to pass unchanged.

4. **Step 4 — add a code comment marking `MODE_ANALOG_PRESSURE` as
   currently unreachable.** In `src/ps2_device/ds2_ids.h`, add a one-line
   comment next to `#define MODE_ANALOG_PRESSURE 0x79` noting it is defined
   but currently unreachable by design (MVP clamp), with a pointer to this
   decision doc, so a future implementer of real pressure support
   (Option B, post-MVP) knows where the guard rails are.

5. **Step 5 — (optional, recommended) record the Task 14 precondition.**
   As described in Option C's "where to record it," add a short blocking
   note above Task 14 in
   `docs/superpowers/plans/2026-07-07-ps2-dualshock-bluetooth-adapter.md`
   confirming the hazard is now closed by this task (rather than leaving it
   as an open precondition), so the plan's own history reflects that the
   Self-Review's "correctly excluded" claim is now actually true.

6. **Step 6 — commit.**
   ```
   git add src/ps2_device/ds2_protocol.c src/ps2_device/ds2_ids.h test/test_ds2_protocol.c
   git commit -m "Clamp 0x4F/0x44 pressure-mode transition to analog (close 0x79 response gap)"
   ```
   (Include the plan-file edit from Step 5 in the same or a follow-up commit
   per the project's usual convention.)

**Expected test vectors, restated for quick reference:**
- `test_pollconfig_4F_does_not_enable_pressure_mode`: input
  `req = {0x00, 0xFF, 0xFF, 0x03, 0x00, 0x00}` via `CMD_POLL_CONFIG` while
  `st.config == true` → `st.mode == MODE_ANALOG` (`0x73`), not
  `MODE_ANALOG_PRESSURE` (`0x79`).
- `test_handshake_4F_then_44_stays_analog`: `CMD_POLL_CONFIG` with the same
  nonzero `req` above, then `CMD_ANALOG_SWITCH` with
  `req = {0x00, 0x01, 0x03, 0x00, 0x00, 0x00}` → `st.mode == MODE_ANALOG`,
  `st.analog_lock == true`, and a subsequent neutral `CMD_POLL` response of
  exactly `73 5A FF FF 80 80 80 80` (8 bytes, matching
  `test_analog_poll_neutral`'s existing expected frame).

## Summary for caller

**Doc path:** `/Users/ramseymcgrath/code/ps2-controller/docs/superpowers/plans/2026-07-07-0x79-pressure-mode-decision.md`

**One-line recommendation:** Clamp `detect_analog()` and the `0x4F` handler
in `ds2_protocol.c` to always resolve to `MODE_ANALOG` (Option A) — cheap,
low-risk, and it makes the plan's own "pressure correctly excluded" claim
true before Task 14's real-console milestone.

**5-line tradeoff summary:**
1. Option A (clamp): ~6-line fix, trivial risk, removes the hazard entirely;
   pressure-using titles (GT4, MGS2/3, GTA:SA, Tekken, Okami) degrade
   gracefully to analog-only instead of getting a malformed frame.
2. Option B (implement `0x79` now): correct in principle (byte layout
   confirmed against the reference fork and independent web research), but
   pulls unplanned M5 scope forward, is unvalidated on real hardware, and
   can only digital-derive all 12 pressure bytes today (no analog L2/R2
   source in `PSXInputState` yet) — arguably worse than current analog mode.
3. Option C (defer, document only): zero code effort but leaves a live,
   confirmed-reachable hazard sitting directly on Task 14's untested,
   unrestricted "in a game" acceptance path.
4. The bug is real and reachable today via two independent paths (`0x4F`
   direct, and `0x4F` then `0x44`), not merely theoretical — and idle bus
   value `0xFF` reads as "fully pressed" for DS2 pressure bytes, so the
   failure mode is worst-case, not benign.
5. Recommend doing Option A now (Task 16 above), revisiting proper pressure
   support (Option B, with real analog trigger passthrough) post-MVP once
   Task 14/15 have validated analog mode on real hardware.

---

## Primary-source grounding (added 2026-07-07)

There is **no official Sony public specification** for the PS2 controller protocol. The
authoritative references are community reverse-engineering, and they cross-confirm the
decision:

- **psx-spx / nocash "PlayStation Specifications"** — https://psx-spx.consoledev.net/controllersandmemorycards/
  (the de-facto canonical spec; "Controllers - Analog Buttons (Dualshock2)" + "Configuration Commands").
- **curiousinventor, "Interfacing a PS2 Controller"** — https://store.curiousinventor.com/guides/PS2
- **"Micah's notes" DualShock 2 protocol gist** — https://gist.github.com/RebelliousX/fe68cca65f1e9de5a8a8
- **BlueRetro PS1/PS2 SPI notes (darthcloud)** — https://hackaday.io/project/170365-blueretro/log/186471-playstation-playstation-2-spi-interface

**Decisive confirmed facts:**
1. **Pressure bytes are NOT sent by default.** curiousinventor: *"By default, the pressure
   values are not sent back, so [0x4F] is the command that is necessary to enable them."*
   psx-spx: the poll response length "depends on the controller mode… controlled by Command
   0x4F"; 0x4F "only works when the controller is already in configuration mode (0xF3)."
2. **The ID byte's low nibble is the packet length:** ID `0x41`→5 bytes total, `0x73`→9,
   `0x79`→21 (18 data bytes). Advertising 0x79 obligates the device to supply all 18.
3. **Pressure byte order:** `R L U D Tri O X Sqr L1 R1 L2 R2`, `0x00`=released `0xFF`=pressed
   — matches this project's brief and the DS4toPS2 fork.
4. curiousinventor caveat: *"the PlayStation doesn't always wait for all these bytes"* — do
   NOT rely on this; the safe contract is to serve exactly what the ID advertises.

**Implication for the decision (reinforces Option A):** A controller that remains in `0x73`
analog and never enables pressure is a fully in-spec DualShock; every PS2 game already
supports analog-only pads. Option A (never advertise/enter `0x79`) therefore presents a
legitimate controller and eliminates the one illegal state — ID says 0x79 but payload is
0x73-length — that the current code can reach. Primary sources support Option A as
spec-correct, not a workaround.
