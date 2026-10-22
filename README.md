# MYK Tracker GL

MYK TRACKER is an integrated live performance and composition sequencer, sampler, synthesizer and effects system. 

It aims to capture the joy of rapidly editing and performing with sequences, with all sounds built-in. You can even save your songs in a MOD-like format and share them with other people. 

## Instructions

You can find most of the commands in the cheat sheet poster in the docs folder: 

https://github.com/yeeking/myk-tracker/blob/main/doc/MYK_Tracker_cheat_sheet.svg

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

### VS Code debugging

Open the repository folder in VS Code, select `MYK Tracker GL (Debug
Standalone)` in the Run and Debug view, and press the green debug button (or
`F5`). The repository launch configuration configures the `build` directory as
a Debug build, builds the standalone target with two jobs, and launches it
under GDB. The Microsoft C/C++ extension is required for the `cppdbg` debugger.

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
advance the former; navigation advances the latter. The endpoint is bound to
IPv4 loopback and rejects non-loopback `Host` and `Origin` values.

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
note and control entry.

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
