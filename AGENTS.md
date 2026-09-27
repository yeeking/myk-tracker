# AGENTS.md

## Purpose

This repository contains MYK Tracker GL, a keyboard-driven music tracker built
with JUCE and C++17. The main deliverable is an audio plug-in with a custom
standalone host; the repository also builds a small OSC hardware emulator.

Read `README.md` before changing behaviour. It is the user-facing description
of the tracker pages and keyboard controls, and changes to those controls should
be reflected there.

## Repository map

- `CMakeLists.txt`: the complete build definition. Add new production `.cpp`
  files to the appropriate `target_sources` list.
- `src/TrackerMainProcessor.*`: top-level audio processor, transport/clock,
  machine stacks, song state, OSC, and plug-in state serialization.
- `src/TrackerMainUI.*`: JUCE editor, keyboard/mouse handling, page assembly,
  and connection between tracker state and the renderer.
- `src/TrackerUIComponent.*`, `src/Segment14Geometry.h`, `src/Palette.h`:
  OpenGL grid/text rendering and visual styling.
- `src/Sequencer.*`, `src/SequencerCommands.*`: sequence/step data and command
  execution.
- `src/SequencerEditor.*`, `src/TrackerController.*`: editor navigation and
  high-level tracker actions.
- `src/MachineInterface.h`, `src/machines/`: machine contract and the internal
  instruments, arpeggiators, and effects.
- `src/SuperSamplerProcessor.*`, `src/SuperSamplePlayer.*`: sample loading and
  playback.
- `src/standalone/`: the custom standalone audio/MIDI host.
- `src/TrackerControlService.*`, `src/HTTPServer.*`: the shared UI/MCP control
  boundary and standalone-only local MCP server.
- `src/hardware_emulator/`: separately built JUCE OSC emulator application.
- `devices/`: external hardware helper programs and Python utilities. These are
  not part of the main CMake targets.
- `doc/`: design/source artwork. Treat generated bitmap assets separately from
  their SVG sources.
- `libs/JUCE/`: locally cloned third-party dependency. Do not modify or commit
  it as part of normal tracker work.
- `build/`: local generated output. Never hand-edit or commit it.

## Bootstrap and build

The only required vendored dependency is JUCE at `libs/JUCE`. If it is absent:

```sh
git clone https://github.com/juce-framework/JUCE.git libs/JUCE
```

Configure from the repository root and build only the target relevant to the
change:

```sh
cmake -S . -B build
cmake --build build --target myk-tracker-plug_Standalone -j2
cmake --build build --target myk-tracker-plug_VST3 -j2
cmake --build build --target tracker-hardware-emulator -j2
```

The standalone binaries are normally written to:

- `build/myk-tracker-plug_artefacts/Debug/Standalone/MYK Tracker GL`
- `build/tracker-hardware-emulator_artefacts/Debug/MYK Tracker Hardware Emulator`

The project also declares AU and VST3 formats. Format availability and artifact
layout vary by platform and generator. Do not assume AU can be built on a
non-Apple host.

There is currently no automated C++ test target, lint target, or repository
formatter configuration. The Python files named `test_*.py` under `devices/`
are hardware-oriented utility scripts, not the main application's test suite.

## Architecture and invariants

- `TrackerMainProcessor` owns long-lived sequencing and editor state because a
  JUCE plug-in editor can be destroyed while its processor remains alive.
- Audio rendering enters through `TrackerMainProcessor::processBlock` and then
  dispatches through machine stacks. Treat this as real-time-sensitive code:
  avoid adding allocation, file/network access, logging, sleeps, or contended
  synchronization in the audio path. Allocate buffers and initialize DSP in
  `prepareToPlay` where possible.
- UI work belongs on the JUCE message thread; OpenGL resource creation and
  destruction belong in the renderer lifecycle callbacks.
- Cross-thread processor mutations must respect the existing `processing`
  guard, `audioMutex`, and `withAudioThreadExclusive` pattern. Inspect both the
  message-thread caller and audio-thread reader before changing shared state.
- Tracker grids use a column-major convention: the outer vector is columns and
  each inner vector contains rows. Preserve this convention in model, editor,
  and `UIBox` code.
- Machines implement `MachineInterface`; audio-only effects additionally use
  the conventions in `AudioEffectMachine.h`. Keep preparation, note/clock
  handling, UI cells, state persistence, and `allNotesOff` behaviour coherent.
- Plug-in state is serialized by `TrackerMainProcessor` as JSON stored in the
  APVTS `ValueTree`. Individual machines serialize their own versioned JSON.
  Keep old sessions loadable: use defaults for missing properties, validate and
  clamp restored values, and increment a state version only with a migration
  plan.
- OSC is optional external I/O. Failure to bind or connect must not prevent the
  tracker or plug-in from running.
- Any collection indexed by sequence, step, song row, machine stack, or slot
  needs explicit bounds handling. Preserve note-off and `allNotesOff` cleanup
  when changing transport or routing.

## Coding conventions

- Use C++17 and JUCE types/utilities where they already form the surrounding
  API (`juce::String`, `juce::jlimit`, `juce::AudioBuffer`, `juce::MidiBuffer`).
- Match the style of the file being edited. The older sequencer code and newer
  machine code differ in indentation; do not reformat unrelated code.
- Prefer RAII and existing ownership patterns (`std::unique_ptr`, stack values,
  scoped locks). Make ownership and thread affinity explicit.
- Keep declarations in the matching header and implementation in the `.cpp`.
  Use anonymous namespaces for file-local helpers/constants.
- Use `std::size_t` for container indices unless a JUCE API requires `int`, and
  make conversions explicit at API boundaries.
- Follow existing JUCE debug checks (`jassert`) or standard `assert` in the
  subsystem being touched. Do not rely on assertions for malformed persisted or
  external input; validate such input in release builds.
- Keep tracker cell labels compact and uppercase where the existing machine UI
  does so. Populate `UIBox` callbacks and disabled state consistently.
- Add concise comments for thread, timing, ownership, or persistence decisions;
  avoid comments that merely restate the code.

## Verification

For every change, at minimum build the narrowest affected target. Because the
main app has no automated tests, report the exact build and manual checks you
performed rather than saying simply that tests passed.

Use focused manual checks as applicable:

- Sequencer/editor changes: navigate all affected pages, edit boundary rows and
  columns, start/stop/rewind, and confirm cursor and armed/muted state.
- Audio or machine changes: test at more than one sample rate and block size;
  exercise note-on/note-off, transport stop/reset, bypass/enable, tails, and
  rapid parameter edits while playing. Listen for clicks and stuck notes.
- State changes: save and reload the new state, then load a state lacking the
  new fields to verify defaults and compatibility.
- UI/OpenGL changes: resize, zoom, pan, switch pages repeatedly, and recreate
  the editor/window to catch stale OpenGL resources or processor/editor lifetime
  assumptions.
- MIDI/host changes: verify both the standalone target and, when the platform
  supports it, the VST3 target in a host.
- OSC/emulator changes: build both `myk-tracker-plug_Standalone` and
  `tracker-hardware-emulator`, and confirm the tracker remains usable when the
  peer is unavailable.
- MCP changes: build the standalone target, verify `/health`, then use an MCP
  client to call `server/discover`, `tools/list`, a read-only resource, and one
  harmless transport/UI tool. Modern `2026-07-28` calls place the protocol
  envelope in `params._meta` and mirror method/name in the MCP headers. The
  endpoint is IPv4-loopback-only by design.
  Legacy client coverage should additionally exercise `initialize`,
  `notifications/initialized`, `tools/list`, and `tools/call` without modern
  MCP headers or metadata; these calls use the tools-only compatibility mode.

## Change discipline

- Inspect `git status` before and after work. The working tree may contain local
  JUCE/build directories or unrelated user changes; preserve them.
- Keep changes scoped. Do not regenerate assets, update JUCE, or perform broad
  formatting unless the task explicitly requires it.
- Update `README.md` when changing user-visible pages, keyboard shortcuts,
  setup, build commands, or artifact locations.
- In the handoff, call out real-time, state-compatibility, MIDI, OSC, and manual
  testing implications when relevant.
