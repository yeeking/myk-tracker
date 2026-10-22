# AGENTS.md

## Purpose

MYK Tracker GL is a keyboard-driven sequencer, sampler, synthesizer, effects
system, and live-performance application built with JUCE and C++17. The main
target is an audio plug-in with a custom standalone host. The repository also
builds a local OSC hardware emulator and optional performance tooling.

Read these before changing behaviour:

- `README.md` for build, VS Code, and local MCP instructions.
- `doc/MYK_Tracker_cheat_sheet.svg` for the user-facing key map.
- `MAKING_MACHINES_TUTORIAL.md` before adding or substantially changing a
  machine.

Update the relevant documentation when controls, pages, state, build targets,
or external interfaces change.

## Repository map

- `CMakeLists.txt`: all production, test, emulator, and optional benchmark
  targets. A new production `.cpp` is not compiled until it appears here.
- `src/TrackerMainProcessor.*`: top-level processor, transport and clock,
  eight machine stacks, sequence sets and Song state, mixer/aux/master path,
  recording, OSC, scopes/meters, and tracker-state serialization.
- `src/TrackerMainUI.*`: JUCE editor, keyboard handling, page preparation,
  transient save/load overlay, cached traces, and connection to the OpenGL
  renderer.
- `src/TrackerUIComponent.*`, `src/Segment14Geometry.h`, `src/Palette.h`:
  OpenGL grid, text, trace, meter, and highlight rendering.
- `src/Sequencer.*`, `src/SequencerCommands.*`: sequence data, read heads,
  command registry, timing, and command execution.
- `src/SequencerEditor.*`, `src/TrackerController.*`: page/cursor state,
  keyboard-oriented editing, machine-grid assembly, and higher-level actions.
- `src/MachineInterface.h`, `src/machines/`: machine contract, instruments,
  effects, fixed modulation matrix, DSP helpers, and machine-state helpers.
- `src/SuperSamplerProcessor.*`, `src/SuperSamplePlayer.*`: sampler UI,
  loading, portable-asset export/restore, tuning, and playback.
- `src/PortableTrackerState.*`: explicit portable `.myktracker` ZIP files with
  `state.xml`, `manifest.json`, and embedded 24-bit FLAC sampler assets.
- `src/TrackerFileBrowser.*`: reusable message-thread directory model shared by
  sampler and tracker save/load UI.
- `src/AudioConfiguration.h`, `src/standalone/`: standalone device setup,
  saved audio preferences, performance statistics, audio/MIDI host, and WAV
  recording. Plug-in builds receive device settings from the host.
- `src/TrackerControlService.*`, `src/McpRouter.*`, `src/HTTPServer.*`:
  revisioned control boundary and standalone-only loopback MCP server.
- `src/hardware_emulator/`: separately built JUCE OSC emulator application.
- `tests/`: JUCE console regression programs registered with CTest.
- `tools/PerformanceBenchmark.cpp`, `src/PerformanceMetrics.h`: optional
  dynamic-performance harness and compile-time instrumentation.
- `devices/`: external hardware helpers. They are not part of the tracker
  CMake targets.
- `doc/`: source artwork and the keyboard cheat sheet. Do not overwrite source
  SVGs with generated bitmaps.
- `libs/JUCE/`: local JUCE checkout. Do not modify or commit it during normal
  tracker work.
- `libs/httplib.h`: vendored HTTP server header. Do not replace or reformat it.
- `build/`, `build_works/`, `performance-results/`, `releases/`: generated or
  local output. Never hand-edit or stage these unintentionally.

The old arpeggiator machine implementations and command IDs have been removed.
Arpeggiation belongs to sequence read heads; do not reintroduce stack machines
for it.

## Bootstrap and build

The required vendored dependency is JUCE at `libs/JUCE`. If it is absent:

```sh
git clone https://github.com/juce-framework/JUCE.git libs/JUCE
```

Configure from the repository root and build the narrowest affected target:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --target myk-tracker-plug_Standalone -j2
cmake --build build --target myk-tracker-plug_VST3 -j2
cmake --build build --target tracker-hardware-emulator -j2
```

The normal Debug standalone artifacts are:

- `build/myk-tracker-plug_artefacts/Debug/Standalone/MYK Tracker GL`
- `build/tracker-hardware-emulator_artefacts/Debug/MYK Tracker Hardware Emulator`

The project declares AU, VST3, and Standalone formats. AU is platform-specific;
do not assume it is available on Linux. When present, the repository-local
`.vscode` launch/task configuration can configure, build, and run the Debug
standalone under GDB.

CTest currently registers six targets:

```sh
cmake --build build --target \
  sequence-read-head-tests sampler-tuning-tests modulation-matrix-tests \
  wavetable-synth-tests drum-synth-tests drum-processor-tests -j2
ctest --test-dir build --output-on-failure
```

Run the focused target while iterating, then the full suite for a completed
change. `devices/**/test_*.py` files are hardware utilities, not the tracker
regression suite.

For an opt-in profiled build:

```sh
cmake -S . -B build-perf -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DMYK_TRACKER_BUILD_PERFORMANCE_BENCHMARK=ON
cmake --build build-perf --target myk-tracker-performance-benchmark -j2
```

Normal builds leave performance instrumentation disabled.

## Threading and real-time invariants

- Audio enters through `TrackerMainProcessor::processBlock`. The callback must
  not allocate, resize containers, discover routes, perform file/network I/O,
  log, sleep, or wait on message-thread work. Prepare buffers, lookup tables,
  event capacity, and DSP state in `prepareToPlay` or a safe control path.
- `processBlock` uses `processing` plus `audioMutex`. Message-thread code that
  reads or mutates processor/sequencer/machine data shared with audio must use
  `withAudioThreadExclusive`. The helper is re-entrant on its current owner;
  do not manually take `audioMutex`, call it while holding another lock the
  callback needs, or retain pointers/references after the exclusive section.
- Structural sequence edits are especially sensitive. Clamp every sequence,
  step, read-head, stack, and slot index before dereferencing, and ensure the
  audio thread cannot observe a vector while the UI resizes it.
- UI work belongs on the JUCE message thread. OpenGL resource creation and
  destruction belong to renderer lifecycle callbacks. Portable audio encoding,
  file browsing, and device changes never belong in the callback.
- Parameter values shared directly between UI and DSP should follow the
  existing atomic/cached patterns. Snapshot atomics once per block where
  possible; do not recompute expensive coefficients per sample when a value is
  unchanged.
- `allNotesOff`, held note-off, route changes, transport reset, and machine
  bypass must leave no stuck MIDI, sampler, WAVE, DRUM, filter, or modulation
  events. Preserve deliberate delay/reverb tails unless the operation is an
  explicit hard reset.

## Sequencer and Song model

- A sequence event row is exactly `[command, note, velocity, duration]`.
  Probability, rhythm, traversal, polyphony, octave variation, and ticks per
  step are read-head configuration. State loading accepts only this row shape.
- A sequence set is one pattern containing the sequence columns. Song rows
  reference sequence sets and a beat count. BPM is stored per sequence set.
- Machine parameters and stack order are global per stack. Each sequence set
  stores its own slot enabled/bypassed snapshot and fixed MOD enabled state;
  Song changes recall those states at the existing boundary.
- Sequence mute/solo and stack mute/solo are separate layers. Playback and
  presentation intentionally combine them; do not collapse them into one flag.
- Sequence note-entry ARM is transient editor state. Performance note-length
  mode (`off`, `half`, `quarter`) is persisted per sequence.
- Changes to read-head count/configuration must preserve valid live state and
  pending note-offs. Never index cached per-head storage using a newly resized
  count without rebuilding or bounds-checking the corresponding storage.

## Machine-stack model

There are eight fixed stacks (`TrackerMainProcessor::kMachineStackCount`). Each
stack owns one persistent instance of:

- `SuperSamplerProcessor`
- `WavetableSynthMachine`
- `DrumSynthMachine`
- `WaveshaperDistortionMachine`
- `DelayFxMachine`
- `ChannelStripMachine`
- `FilterFxMachine`
- `ModulationMatrixMachine`

The ordered `slots` vector contains `SlotState { id, type, enabled,
sendLevelDb, returnLevelDb }`. A slot references the persistent instance; it
does not own a machine. Types are unique within a stack. The cycle is:

`MIDI → WAVE → SAMPLER → DRUM → DIST → DELAY → CHSTR → AUX1 → AUX2 → FILTER`

Fresh/reset stacks contain one enabled WAVE slot. A slot ID such as `slot-7`
is the stable MCP identity; grid coordinates are not API identities.

Important distinctions:

- MIDI, WAVE, SAMPLER, and DRUM are terminal routes. Notes walk enabled slots;
  terminal slots consume/emit them. With no terminal slot at all, the stack
  falls back to external MIDI.
- DIST, DELAY, CHSTR, and FILTER are serial, in-place audio effects. SEND gain
  is applied before and RETURN gain after the effect.
- AUX1/AUX2 slots are taps into two shared stereo `AuxReverbMachine` returns.
  Each tap sees the mono stack signal at that slot's position in the ordered
  effect walk; they are not stack-owned reverb instances.
- MOD is a fixed pseudo-slot at the top of every stack. It cannot be moved or
  removed and is processed before sources. It has the stable control slot ID
  `mod-matrix`; its enabled state is recalled per sequence set.
- Stack audio is deliberately mono through sources and serial effects. After
  stack gain, scope, and meter capture, the mixer applies equal-power stereo
  pan. Aux sends copy the mono signal to both reverb inputs; aux returns remain
  stereo and use balance panning. Master gain/mute and recording are last.
- Only active source/effect paths should do substantial DSP. Slot structural or
  enable changes must call `refreshStackProcessingState`. Disabled delay still
  drains its tail through a separate mono buffer so the tail decays naturally.
- `AudioEffectMachine` finalizes the generic prepare/release/process contract;
  subclasses implement `prepareDsp` and `processAudioBuffer` and optionally
  `clearTransientState`. See `MAKING_MACHINES_TUTORIAL.md`.

## Machine registration and state

- `CommandType` numeric values are persisted inside the current tracker schema.
  Update the canonical `kLastCommandType` endpoint when adding a type.
  `machineTraits()` is the shared classification table used by processor,
  editor, UI, and control service.
- A new stack-owned machine normally needs ownership/construction, inclusion in
  `MachineStack::forEachMachine`, mapping in `getMachineForStackType`, a key in
  `stackMachineStateEntries`, processing-state accounting, CMake sources, and
  tests. Terminal instruments additionally need event queues/dispatch/render
  wiring. Read the tutorial checklist instead of copying only one switch.
- Machine UI grids are column-major: `cells[column][row]`. Use `UIBox`
  callbacks for activate/adjust/insert/reset/preview and mark disabled cells.
  `getUIBoxes()` is also the generic MCP control surface, so labels and layout
  affect generated control IDs. Preserve stable labels/coordinates when
  possible.
- Machine blobs use exact-version JSON and `MachineStateCodec.h` helpers.
  Malformed, non-finite, and out-of-range values must be rejected or clamped
  safely in Release builds; assertions are not validation.
- Top-level plug-in state is JSON stored in the APVTS `ValueTree`, then written
  as XML by `getStateInformation`. The JSON carries an exact `stateVersion`;
  unversioned or mismatched projects are rejected without mutating live state.
  Machine blobs are Base64 properties inside that document. Host state and
  automatic preferences remain path-based.
- Explicit `WITH AUDIO` saves use `PortableTrackerState`: a ZIP container with
  the same `.myktracker` extension, `state.xml`, `manifest.json`, and per-player
  24-bit FLAC. `STATE ONLY` files use the current lightweight XML container.
  Loading detects both containers, then requires the current tracker schema.
  Keep portable save/load off the audio thread and preserve atomic replacement.

## UI, renderer, and control boundary

- `SequencerEditorMode` includes Song, Sequence, Step, Sequence Config, Machine,
  Mixer, Audio Config, Help, and reset confirmation. Transient machine/sample
  browsers and tracker save/load overlays can take exclusive keyboard input.
- The renderer consumes column-major `UIBox` grids plus cached traces. Reuse
  static waveform/envelope glyphs and build only the visible page/window where
  possible. Do not rebuild hidden pages or hold audio exclusivity while doing
  OpenGL/text work.
- The Audio Config page delegates to `AudioConfigurationHost`. Standalone can
  stage/apply device, rate, and buffer changes asynchronously; VST3/AU are
  host-controlled. Settings are application preferences, not song state.
- OSC control is optional. Bind/connect failure must never stop the tracker.
- The standalone MCP endpoint is IPv4 loopback-only at
  `http://127.0.0.1:8080/mcp` by default. `TrackerControlService` is shared by
  MCP and UI-style actions and tracks content/view revisions.
- Prefer extending existing generic resources/actions over adding a bespoke MCP
  operation. Direct machine controls address `stackId`, persisted `slotId`, and
  generated control ID; they must not move the visible cursor. The MOD block
  uses pseudo-slot `mod-matrix`.

## Coding conventions

- Use C++17 and match the local file style. Prefer JUCE types at JUCE boundaries
  and `std::size_t` for container indices, with explicit conversions.
- Search with `rg`/`rg --files`. Do not broadly format unrelated code.
- Prefer RAII and existing ownership patterns. Make thread affinity and
  lifetime explicit.
- Use anonymous namespaces for file-local constants/helpers.
- Keep machine cell labels compact and uppercase. Populate `UIBox::kind`,
  callbacks, disabled/active state, and value scaling consistently.
- Add concise comments for real-time, timing, migration, ownership, or thread
  decisions—not comments that merely restate code.
- Do not add approximate or block-rate DSP merely for speed without discussing
  the audible/timing tradeoff. The project prioritizes predictable live CPU and
  avoids surprise initialization when a page is opened.

## Verification

Always report exact commands and manual checks; do not say only “tests passed.”

- Sequencer/read-head work: build `sequence-read-head-tests`; exercise boundary
  rows/columns, live config edits, pending note-offs, transport, Song switches,
  ARM, mute/solo, and save/load.
- Sampler/portable files: build `sampler-tuning-tests`; test missing/original
  paths, embedded audio, multiple sample rates/channels, tuning, and retrigger.
- MOD/delay routing: build `modulation-matrix-tests`; test bypass, empty/dense
  routes, clock/note triggers, destination bounds, and state migration.
- WAVE: build `wavetable-synth-tests`; test mono/polyphony limits, modulation,
  zero cycler rate, sample rates, and live parameter changes.
- DRUM: build both drum test targets; test note mapping/learn, per-engine enable,
  one-shot/gated behavior, velocity, choke/retrigger, idle cost, and state.
- Processor/audio/mixer work: build Standalone and normally VST3; audition more
  than one rate/block size, effect order, aux tails, pan, mute/solo, master
  meter/recording, rapid toggles, and all-notes-off.
- UI/OpenGL work: resize, zoom/pan, switch pages repeatedly, scroll wide pages,
  hide/show and recreate the editor, and inspect low-resolution rendering.
- MCP work: build/run Standalone, check `/health`, modern `server/discover`,
  `tools/list`, a read resource, a harmless UI/transport action, a direct
  machine control, and `tracker_screenshot`. Also cover the legacy
  `initialize` → `notifications/initialized` → `tools/list` → `tools/call`
  flow when MCP compatibility code changes. MCP protocol compatibility is
  independent from tracker-project compatibility.

## Change discipline

- Inspect `git status --short` before and after work. The worktree commonly has
  local JUCE/build/result directories and user files. Preserve all unrelated
  changes.
- There is no broad `.gitignore`; never use `git add -A`, `git commit -a`,
  destructive reset/checkout, or cleanup commands over the workspace. Stage
  only files intentionally changed.
- Do not edit generated build artifacts or vendored JUCE.
- Keep changes scoped. Avoid state migrations, new external APIs, or renderer
  rewrites unless the task requires them.
- Do not commit unless the user asks. In the handoff, call out real-time,
  compatibility, MIDI/OSC/MCP, and manual-testing implications.
