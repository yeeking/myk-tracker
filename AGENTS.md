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
  instruments and effects. Arpeggiation is implemented by sequence read heads,
  not stack machines. `ArpeggiatorMachine.*` and `PolyArpeggiatorMachine.*`
  are legacy leftovers absent from `target_sources` and never compiled into
  the plugin; the `LegacyArpeggiator`/`LegacyPolyArpeggiator` command types
  exist only to swallow and migrate old state. Do not resurrect or extend
  them.
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
- `libs/httplib.h`: vendored single-header HTTP server used by the local MCP
  server; `src/HTTPServer.h` includes it as `../libs/httplib.h`. Do not replace
  or reformat it.
- `tests/`: standalone console regression tests for sequence read heads. See
  Bootstrap and build for how to run them.
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

There is no lint target or repository formatter configuration. The only
automated tests are the CTest target `sequence-read-head-tests`, a JUCE
console app over `tests/SequenceReadHeadTests.cpp` (built when
`BUILD_TESTING` is on, which is the default):

```sh
cmake --build build --target sequence-read-head-tests -j2
ctest --test-dir build
```

Run it whenever you touch `Sequencer.cpp`, `SequencerCommands.cpp`, or
read-head behaviour. The Python files named `test_*.py` under `devices/` are
hardware-oriented utility scripts, not the main application's test suite.

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
- Sequence events contain exactly `[command, note, velocity, duration]`.
  Probability, traversal, rhythm, and timing belong to the sequence's one to
  three read heads. Legacy five-/six-field rows are migrated on restore.
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

## Machine stacks

- There are 16 fixed stacks (`kMachineStackCount`), each a `MachineStack` in
  `TrackerMainProcessor`. A stack owns one persistent instance per machine
  type — `SuperSamplerProcessor`, `WavetableSynthMachine`,
  `WaveshaperDistortionMachine`, `DelayFxMachine`, `ChannelStripMachine` —
  plus an ordered `slots` list.
- `slots` are `SlotState {id, type, enabled, sendLevelDb, returnLevelDb}`
  routing/chain entries that reference those fixed instances; a slot never
  owns its machine. Slot types are unique per stack and cycle in the order
  MidiNote, WavetableSynth, Sampler, DistortionFx, DelayFx, ChannelStripFx,
  AuxSend1Fx, AuxSend2Fx. Fresh and reset stacks default to a single enabled
  `WavetableSynth` slot so new sessions are audible without MIDI routing.
  Slot `id` (e.g. `slot-7`) is the stable identity used by MCP; grid
  coordinates are deliberately not an API.
- Aux sends are parallel: two global `AuxReverbMachine` buses are shared by
  all stacks; an enabled aux slot mixes a send-gain-scaled copy of the stack
  output into its bus, and the buses are summed into the plugin output after
  all stacks.
- Notes walk the slots in order (`dispatchNoteThroughStack`). Effect slots
  pass notes through unchanged; terminal slots consume them: `MidiNote`
  queues external MIDI on the stack's MIDI channel, `Sampler` queues the
  stack's internal sampler, `WavetableSynth` queues the stack's wavetable
  synth (notes are applied sample-accurately in `processBlock`). When a stack
  has no terminal slot at all, notes default to external MIDI. A sequence
  routes to a stack via its `machineId` (the stack index) through
  `sendMessageToMachine`; clock-driven machine events re-enter the chain at
  their own `slotIndex + 1`.
- Audio renders per stack into `renderBuffer`: sampler and wavetable sources
  mix first, then enabled non-aux effect slots process the buffer in place in
  slot order with SEND gain applied before and RETURN gain after, then the
  stack gain applies. Only stacks with at least one enabled audio path are
  processed: slot changes (add/remove/cycle/move/enable) must call
  `refreshStackProcessingState` so the `*ProcessingActive` flags stay
  correct. A *disabled* DelayFx slot still drains its tail buffer so delay
  tails decay out instead of freezing.
- `AudioEffectMachine` is the base for audio-only effects: it finalizes
  `processBlock` into an in-place `processAudioBuffer(buffer)` and never
  consumes note events. Delay machines are the only `ClockAbs` listeners;
  sequence read heads require no machine clock.
- `allNotesOffForStack` must silence every voice source in a stack:
  wavetable synth, delay tail, sampler (queued `allNotesOff`), and the
  stack's external MIDI channel.

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

For every change, at minimum build the narrowest affected target. The only
automated coverage is `sequence-read-head-tests`; the audio, UI, and
transport paths are untested, so report the exact build, test runs, and
manual checks you performed rather than saying simply that tests passed.

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
- There is no `.gitignore`: `build/` and `libs/JUCE/` are untracked but *not*
  ignored. Never run `git add -A` or commit with `-a`; stage only the specific
  files you changed.
- Keep changes scoped. Do not regenerate assets, update JUCE, or perform broad
  formatting unless the task explicitly requires it.
- Update `README.md` when changing user-visible pages, keyboard shortcuts,
  setup, build commands, or artifact locations.
- In the handoff, call out real-time, state-compatibility, MIDI, OSC, and manual
  testing implications when relevant.
