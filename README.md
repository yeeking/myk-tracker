# MYK Tracker GL

MYK TRACKER is an integrated live performance and composition sequencer, sampler, synthesizer and effects system. 

It aims to capture the joy of rapidly editing and performing with sequences, with all sounds built-in. You can even save your songs in a MOD-like format and share them with other people. 

## Instructions

You can find most of the commands in the cheat sheet poster in the docs folder: 

https://github.com/yeeking/myk-tracker/blob/main/doc/MYK_Tracker_cheat_sheet.svg

### Chord keys

Play a computer-piano key first to select a chord root. Plain `Q–P` retain the
original chord-family map: `MAJ`, `MIN`, `DOM`, `DIM`, `AUG`, `SUS`, `MAJX`,
`MINX`, `ALT`, and `QRTL`.

`Alt+Q–P` enter voice-led tone-network cycles. Release and press the same
shortcut again to advance that cycle. In an armed Sequence page or the Step
page, each entered Tonnetz chord writes to the current step like regular
chord entry and stays there. They stay audition-only on a disarmed Sequence
page and other performance pages.

| Key | Cycle |
| --- | --- |
| `Alt+Q` | `3PHX` Eulerian 3p hexacycle |
| `Alt+W` | `2PHX` Eulerian 2p hexacycle |
| `Alt+E` | `4POC` Eulerian octacycle |
| `Alt+R` | `2PDC` Eulerian decacycle |
| `Alt+T` | `ARCH` Archimedean hexacycle |
| `Alt+Y` | `TRST` Tristan hexacycle |
| `Alt+U` | `3PRV` reverse Eulerian 3p hexacycle |
| `Alt+I` | `2PRV` reverse Eulerian 2p hexacycle |
| `Alt+O` | `TR2P` alternate Tristan 2p hexacycle |
| `Alt+P` | `ARCR` reverse Archimedean hexacycle |

The tracker chooses compact, ascending inversions for the complete cycle,
minimizing voice movement and returning to the original voicing at the wrap.

## Developer Build

Clone the tracker repo, clone JUCE into `libs/JUCE`, then configure and build with CMake.

```bash
git clone <myk-tracker-repo-url>
cd myk-tracker
git clone https://github.com/juce-framework/JUCE.git libs/JUCE
cmake -B build .
cmake --build build --target myk-tracker-plug_Standalone
```

### Performance benchmark

The opt-in offline benchmark measures callback and per-stage CPU time at 44.1
and 48 kHz with a 256-sample block. It includes isolated WAVE and DRUM scenes,
their effect paths, and dense eight-stack stress scenes. It does not affect a
normal build:

```bash
tools/run-performance-benchmark.sh performance-results/my-run
```

The standalone app is produced under:

```bash
build/myk-tracker-plug_artefacts/Debug/Standalone/
```

### VS Code debugging

Open the repository folder in VS Code, select `MYK Tracker GL (Debug
Standalone)` in the Run and Debug view, and press the green debug button (or
`F5`). The repository launch configuration configures the `build` directory as
a Debug build, builds the standalone target with two jobs, and launches it
under GDB. The Microsoft C/C++ extension is required for the `cppdbg` debugger.

## Project state compatibility

Tracker projects write the current schema version. Both lightweight
`STATE ONLY` XML files and portable `WITH AUDIO` ZIP/FLAC files contain the
same tracker document. Loading is deliberately permissive: a missing or
different version is treated as the current version-1 layout and MYK Tracker
attempts to restore the available fields. Malformed XML or JSON is still
rejected safely. There are no older-layout migration branches.

This permissive current-layout policy applies to project and plug-in state. It
is independent of the optional legacy MCP client handshake described below.

## Drum synth 808/909 presets

The DRUM machine has a `PSET` cell in the snare strip. `808` and `909` load
matched synthesized snare, hat, crash, and ride settings; `CUST` means one of
those timbral controls was edited afterwards. Presets leave kick/tom settings,
note responders, enable toggles, and gate mode untouched. The development-only
`drum-synth-render` target plus `tools/analyze_drum_renders.py` render and
compare the voices with local reference WAVs without making recordings part of
the tracker project or distribution.

The DRUM page is intentionally compact: every voice has `LEV`, Roland-style
controls, and one adventurous trigger-time control. Kick uses `TUNE`, `TONE`,
`DEC`, `PUNCH`, and the restored nine-shape `WAVE` selector; snare uses `TUNE`, `TONE`, `SNAP`, and `RING`; hats use
`TUNE`, `CDEC`, `ODEC`, and `METAL`; crash uses `TUNE`, `TONE`, `DEC`, and
`EDGE`; ride uses `TUNE`, `TONE`, `DEC`, and `BELL`; tom uses `TUNE`, `DEC`,
and `BEND`; clap uses `TONE`, `DEC`, `DENS`, and `ROOM`. Clap is a procedural
four-burst filtered-noise voice with a separate tail, using note 78 on fresh
machines (older projects leave it disabled to preserve silent formerly-unmapped
notes). `RING` now changes snare noise-tail duration only, leaving its tonal
shell unchanged. Adventure controls span `-10` to `+10`, preserve the selected
patch at zero, and alter several existing synthesis dimensions only when a
voice is triggered. Decay adjustments use geometric steps for fast traversal;
direct number entry remains exact. The compact grid regenerates DRUM MCP
control IDs, so direct-control clients must rediscover them. Active drum voices
briefly flash their name/enable header pair in the editor.

`drum-synth-render` accepts repeatable overrides such as
`--set hat.METAL=10`; `tools/analyze_drum_adventure.py` reports normalised
spectral, envelope, and onset changes for rendered adventure-control sweeps.

## Sampler samplesets

The sampler machine's `LOAD SET` control imports a version-1 `DrumSampleSet`
XML document. Each `Sound` contributes its `midiNote`, and each child `Layer`
becomes one sampler row using its inclusive `velocityLow` and `velocityHigh`
range. Relative layer paths are resolved beside the XML first, then against
its optional `rootFolder`. Import decodes and validates the complete set in
the background and replaces the sampler only when every layer succeeds.

Sampleset layers use velocity to select the appropriate recording without a
second velocity-gain multiplier. Manually loaded samples keep the existing
velocity-gain behavior. `STATE ONLY` saves preserve layer paths and ranges;
`WITH AUDIO` additionally embeds every layer as portable FLAC audio.

Computer-key audition automatically changes with the current sequence's stack.
Without an enabled sampler it uses the normal lower-row chromatic piano map.
With an enabled sampler it uses the 22-key GM-drum map in the same order as
the sampleset XML `Sound` entries: `Z S X D C V G B H N J M Q W E R T Y U I O P`.
Each drum key emits its fixed GM note; `.` and `,` shift the entire GM row up
or down one octave so a pattern can be re-keyed into another octave. The shift
is clamped so every mapped key stays inside MIDI 1-127, and it never rewrites
the note under the cursor. A right-HUD badge shows the active entry octave for
both maps (e.g. `GM OCT +1` or `OCT 7`) while a key is being entered or the
octave is adjusted. On the Sequence page, piano-mode octave keys rewrite the
note under the cursor only while note entry is ARMed; the Step page keeps its
always-editable behaviour.

## Network MCP and OSC control

The standalone application starts an MCP Streamable HTTP server on all IPv4
interfaces at `http://<tracker-host>:8080/mcp`. Set `MYK_TRACKER_MCP_PORT`
before launching the app to use another port, or set
`MYK_TRACKER_MCP_BIND_ADDRESS=127.0.0.1` to restore loopback-only behaviour.
The server implements MCP `2026-07-28` and provides neither TLS nor
authentication: use it only on a trusted LAN, or firewall the selected port.

Tracker receives OSC control on UDP port `9001` from local or remote hosts.
It sends hardware/LCD cell updates to `127.0.0.1:9000` by default; set
`MYK_TRACKER_OSC_DISPLAY_HOST` and optionally `MYK_TRACKER_OSC_DISPLAY_PORT`
to direct that output to another machine.

The same `/mcp` endpoint also accepts the tool-focused legacy handshake used
by LM Studio and similar clients: `initialize`, `notifications/initialized`,
`tools/list`, and `tools/call`. It negotiates `2024-11-05`, `2025-03-26`,
`2025-06-18`, or `2025-11-25` and routes every tool through the same tracker
control service. No token or OAuth configuration is needed; resources remain
a modern-MCP feature.

Useful resources include `myktracker://state`, `myktracker://view`,
`myktracker://song`, and `myktracker://capabilities`. The server exposes tools
for transport, steps, sequences, song rows, machine stacks, machine cells,
local sample loading, GUI-equivalent actions, UI screenshots, and confirmed
application actions. `tracker_application` requires `confirm: true` for reset
and quit. Sample loads return a `loadId`; call `tracker_load_sample` with
`action: "status"` to poll.

`tracker_screenshot` captures the active tracker editor as a PNG MCP image
content block for vision models. It requires a visible standalone editor and
returns `ui_unavailable` when no editor is active. The base64 PNG is in
`content[0].data`; `structuredContent` reports `width`, `height`, and `bytes`
without duplicating the image payload.

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
advance the former; navigation advances the latter. The endpoint accepts
requests on the configured IPv4 bind address.

`tracker_machine_control` supports direct, GUI-independent addressing. Query
`tracker_get_state` with `{"scope":"machine_controls"}` to obtain every
stack/slot control and its stable ID, then call the tool with `stackId`, the
persisted `slotId`, and either the control suffix or full address. For example,
an envelope attack adjustment can be made without moving the visible cursor:

```json
{"stackId":0,"slotId":"slot-1","controlId":"a-5-1","action":"adjust","direction":20}
```

The full equivalent control ID is
`stack/0/slot/slot-1/control/a-5-1`. Direct machine calls do not change the
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

`tracker_edit_sequence` accepts `controlMode` plus `headCount`, `headIndex`,
`ticksPerStep`, `mode` (`linear`, `random`, or `rand_chord`), `polyphony`, `octaves` (1–4),
`rhythm`, and `headProbability`. Rhythm is any nonzero binary pattern of one
to four bits. Head configuration and live positions are included in state and
sequence-set resources. Probability gates a whole step/chord; events no longer
carry their own probability field.

`tracker_set_step` accepts note-mode and control-mode commands, including
`toggle_aux_1`, `toggle_aux_2`, and `filter_cutoff` (aliases `taux1`, `taux2`,
and `coff`). A `filter_cutoff` event stores its cutoff in the note field;
supply `note` or `cutoffHz` in the range `20`–`20000` Hz. `tracker_ui_action`
also supports `action: "control_mode"` to toggle the current sequence between
note and control entry. Use `action: "focus_sequence"` with `setId`,
`sequenceId`, and optional `stepId` to return to a sequence cursor without
changing its data.

`tracker_notes_set` queues an atomic, offscreen track replacement for the next
audio block. It does not change the visible page or cursor. Its compact `notes` array
is step-ordered: a number is one note, an inner array is a chord, and `0` or
`null` is a rest. Its optional matching `velocities` array preserves per-note
MIDI velocities: use a number for a single note and an equally sized array for
a chord (rest entries are ignored). Without it, `velocity` and `durationTicks`
apply to every supplied note.
If the list is longer than the current track, the track grows to fit it, up to
128 steps; longer lists are rejected without changing the pattern. Supply an
optional `length` (at least `notes.length`, 1–128) to set the final track
length, including shrinking an existing track.
`tracker_notes_set`, `tracker_lengths_set`, `tracker_set_step`, and
`tracker_edit_sequence` address their `setId`/`sequenceId` data directly and
never move the visible cursor or change the editor page. Use the explicit
`tracker_ui_action` `focus_sequence` action only when an MCP client wants to
navigate the tracker UI.
For exact event replacement, supply `steps` instead of `notes`; it must contain
exactly `length` objects in the form
`{"active":true,"rows":[[command,note,velocity,durationTicks],...]}`. The two
forms are mutually exclusive and replacements remain limited to 128 steps.
The response includes `status:"queued"`, `updateId`, and whether it superseded
an unapplied replacement. Call the same tool with `action:"status"` to retrieve
the latest accepted and applied IDs for a sequence.

Validation, normalization, allocation, and display preparation run on the
message thread. The callback performs one preallocated step-bank swap at its
next block boundary and returns old storage for message-thread destruction;
there is no per-step callback locking, allocation, or GUI rebuild. This makes
the tool suitable for background variation polling without moving the cursor
or stalling audio.
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
