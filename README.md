# MYK Tracker GL

MYK Tracker GL is a keyboard-driven music tracker built with JUCE. It combines classic step sequencing with sequence-owned read heads, machine stacks, internal instruments, and audio effects, so you can build patterns, arrange them into songs, and perform most editing directly from the keyboard.

Fresh or reset tracker sessions place an enabled wavetable synth in every
machine stack, so newly entered notes are audible without first configuring
external MIDI routing.

## Main Pages

- `Song` page: arrange sequence sets into a song and choose how many beats each row runs before switching.
- `Sequence` page: browse sequences and steps, mute/arm tracks, and move around the current pattern.
- `Step` page: edit the command rows inside a single step: command, note, velocity, and duration.
- `Machine` page: inspect and configure the machine stack for the current track, including instruments and effects.
- `Machine Detail` page: open the focused machine's compact tracker UI for detailed parameter editing.
- `Sequence Config` page: edit `SEND`, read-head count/selection, TPS, traversal mode, chord polyphony, rhythm, and head probability. Up to three independently timed heads may read the same sequence.
- `Mixer` page: edit the 16 machine stacks' mute, solo, gain, and post-mute meters. Multiple soloed stacks remain audible together.
- `Reset / Quit` confirmation page: confirm tracker reset and, in standalone builds, quit.

## Keyboard Shortcuts

- `Space`: start/stop playback.
- `1`: go to Song page.
- `2`: go to Sequence page.
- `3`: go to Step page.
- `4`: go to Machine page.
- `5`: open Machine Detail from anywhere, or cycle to the next machine detail while already in Machine page detail view.
- `6`: go to Sequence Config page.
- `7`: go to Mixer page.
- `Enter`: activate/click the current item.
- `Esc`: dismiss the current transient UI if a machine owns one.
- `Arrow keys`: move the editor cursor.
- `Page Up` / `Page Down`: jump by larger row amounts on the Machine page.
- `Tab`: next step (wrapping to the first step on the Step page), or next
  machine detail when already editing a machine detail view.
- `Backspace`: reset or clear the current item. On machine pages, this first tries the machine-specific clear action.
- `q`: mute/unmute the current sequence.
- `e`: arm the current sequence for note entry.
- `r`: rewind transport.
- `-`: remove a row or entry where supported.
- `=`: add a row or entry where supported.
- `[` / `]`: decrement/increment the current value.
- `,` / `.`: octave down / octave up for note entry.
- `_` / `+`: decrease / increase BPM by 1.
- Piano note keys: enter notes into steps and machine note cells using the current octave.
- Chord shortcut keys on the Step page:
  - `q`: major triad
  - `w`: minor triad
  - `e`: dominant 7
  - `r`: major 7
  - `t`: minor 7
  - `y`: diminished 7
  - `u`: half-diminished
  - `i`: sus4
  - `o`: major 9
  - `p`: minor 9
- `Shift+C`: toggle the internal clock on/off.
- `Ctrl+C` / `Ctrl+V`: copy and replace a complete sequence's event data and
  playback length while retaining the destination machine stack and timing.
- `Shift+Up` / `Shift+Down` on the Sequence page: select a highlighted range
  of steps. `Ctrl+C` copies that range; `Ctrl+V` pastes it beginning at the
  target sequence's current step cursor. Range pastes retain routing and timing.
- `Ctrl+=` / `Ctrl+-`: rotate the current sequence down / up by one step, with
  the displaced end step wrapping around. This rotates all events and active
  state, while retaining routing, timing, and cursor position.
- `Ctrl+R`: open tracker reset confirmation.
- Standalone only:
  - `Ctrl+Q`: open quit confirmation.
  - `Ctrl+P`: open audio/MIDI device settings.

## Developer Build

Clone the tracker repo, clone JUCE into `libs/JUCE`, then configure and build with CMake.

```bash
git clone <myk-tracker-repo-url>
cd myk-tracker
git clone https://github.com/juce-framework/JUCE.git libs/JUCE
cmake -B build .
cmake --build build --target myk-tracker-plug_Standalone
```

The standalone app is produced under:

```bash
build/myk-tracker-plug_artefacts/Debug/Standalone/
```

## Local MCP control

The standalone application starts a local-only MCP Streamable HTTP server at
`http://127.0.0.1:8080/mcp`. Set `MYK_TRACKER_MCP_PORT` before launching the
app to use another port. The server implements MCP `2026-07-28`; it does not
provide remote access, TLS, or authentication.

The same `/mcp` endpoint also accepts the tool-focused legacy handshake used
by LM Studio and similar clients: `initialize`, `notifications/initialized`,
`tools/list`, and `tools/call`. It negotiates `2024-11-05`, `2025-03-26`,
`2025-06-18`, or `2025-11-25` and routes every tool through the same tracker
control service. No token or OAuth configuration is needed for this loopback
server; resources remain a modern-MCP feature.

Useful resources include `myktracker://state`, `myktracker://view`,
`myktracker://song`, and `myktracker://capabilities`. The server exposes tools
for transport, steps, sequences, song rows, machine stacks, machine cells,
local sample loading, GUI-equivalent actions, and confirmed application actions.
`tracker_application` requires `confirm: true` for reset and quit. Sample loads
return a `loadId`; call `tracker_load_sample` with `action: "status"` to poll.

Every RPC request needs the modern MCP headers and a per-request protocol
envelope in `params._meta`. For example:

```bash
curl -X POST http://127.0.0.1:8080/mcp \
  -H 'Content-Type: application/json' \
  -H 'Mcp-Protocol-Version: 2026-07-28' \
  -H 'Mcp-Method: tools/call' \
  -H 'Mcp-Name: tracker_get_state' \
  --data '{"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"tracker_get_state","arguments":{"scope":"view"},"_meta":{"io.modelcontextprotocol/protocolVersion":"2026-07-28"}}}'
```

Mutating tools can include `expectedContentRevision` and
`expectedViewRevision` from a prior response. State-changing musical edits
advance the former; navigation advances the latter. The endpoint is bound to
IPv4 loopback and rejects non-loopback `Host` and `Origin` values.

`tracker_machine_control` supports direct, GUI-independent addressing. Query
`tracker_get_state` with `{"scope":"machine_controls"}` to obtain every
stack/slot control and its stable ID, then call the tool with `stackId`, the
persisted `slotId`, and either the control suffix or full address. For example,
an envelope attack adjustment can be made without moving the visible cursor:

```json
{"stackId":0,"slotId":"slot-1","controlId":"a-4-1","action":"adjust","direction":20}
```

The full equivalent control ID is
`stack/0/slot/slot-1/control/a-4-1`. Direct machine calls do not change the
current GUI page or cursor, so they are safe while a person is navigating the
tracker. The older `row`/`column` form remains only for compatibility and is
cursor-driven.

`tracker_edit_machine_stack` supports idempotent `set_muted` and `set_solo`
actions as well as `action: "set_send"`; provide the
numeric stack and slot indexes plus `gainDb`. Set `gainDb` to `-60` to mute an
aux send without altering the selected UI cell.

For agent-safe machine bypass, use `action: "set_enabled"` with `stackId`,
`slotId`, and `enabled: true` or `enabled: false`. This is idempotent; unlike
the older `toggle` action, retrying it cannot accidentally reverse the state.

### Compact sequence tools

For LLM-friendly pattern editing, use the compact tools rather than reading
the complete state document. `tracker_notes` returns sparse note rows as
`n: [[stepId, note, ...], ...]`; `tracker_step` returns raw event rows as
`v: [[command, note, velocity, durationTicks], ...]`.

`tracker_edit_sequence` accepts `headCount`, `headIndex`, `ticksPerStep`,
`mode` (`linear`, `random`, or `rand_chord`), `polyphony`, `rhythm`, and
`headProbability`. Rhythm is any nonzero binary pattern of one to four bits.
Head configuration and live positions are included in state and sequence-set
resources. Probability gates a whole step/chord; events no longer carry their
own probability field.

`tracker_notes_set` replaces all note events in one track. Its `notes` array
is step-ordered: a number is one note, an inner array is a chord, and `0` or
`null` is a rest. `velocity` and `durationTicks` apply to every supplied note.
If the list is longer than the current track, the track grows to fit it, up to
128 steps; longer lists are rejected without changing the pattern.
`tracker_lengths_set` takes one duration per track step and updates every note
in that step (the example below assumes a four-step track). For example:

```json
{"setId":0,"sequenceId":0,"notes":[60,[64,67],0,72],"velocity":96,"durationTicks":2}
```

```json
{"setId":0,"sequenceId":0,"lengths":[1,2,1,4]}
```

The `/health` endpoint is available for local diagnostics. If port binding
fails, the tracker continues running without MCP control.
