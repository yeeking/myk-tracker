# Making Machines Tutorial

This guide explains how to add a machine to the current MYK Tracker GL codebase.
It is written for both human contributors and coding agents. It describes the
architecture that exists now, the integration points that are easy to miss, and
the checks needed before a new instrument or effect is safe to use live.

The worked example is a conventional serial audio effect. Terminal instruments,
shared buses, the fixed modulation matrix, and modulation destinations need
additional work described later.

## Before you start

Read:

1. `AGENTS.md` for repository, thread, build, and verification rules.
2. `README.md` for current build and MCP instructions.
3. `src/MachineInterface.h` and `src/machines/AudioEffectMachine.h`.
4. One existing machine of the same category as the proposed machine.

Good references are:

- Simple serial effect: `WaveshaperDistortionMachine.*`
- Clock-aware effect with a tail: `DelayFxMachine.*`
- Note-aware/enveloped effect: `FilterFxMachine.*`
- Polyphonic terminal: `WavetableSynthMachine.*`
- Multi-engine terminal: `DrumSynthMachine.*`
- Sampler/file UI: `SuperSamplerProcessor.*`
- Shared stereo return: `AuxReverbMachine.*`
- Fixed pre-source control generator: `ModulationMatrixMachine.*`

The old arpeggiator machine implementations and command IDs have been removed.
Arpeggiation is implemented by sequence read heads; do not reintroduce stack
machines for it.

## Agent fast path

For an ordinary stack effect, the usual path is:

1. Add a class derived from `AudioEffectMachine`.
2. Make parameters thread-safe, state versioned, and the callback allocation-free.
3. Return a column-major `UIBox` grid.
4. Add the `.cpp` to `CMakeLists.txt`.
5. Append a stable `CommandType`; update `machineTraits` and `kSlotCycleTypes`.
6. Add one persistent instance to each `MachineStack`.
7. Add construction, `forEachMachine`, type lookup, state entry, count, restore,
   and exhaustive-switch handling.
8. Update the canonical command endpoint and inspect type allowlists; do not
   assume the compiler finds every semantic routing case.
9. Add focused tests, build the Debug Standalone, and run CTest.
10. Manually test order, bypass, state, sample-rate changes, and live edits.

Terminal instruments also need event queues, sample-offset distribution, note
dispatch/release, source rendering, activity flags, and note descriptions.

## The current machine model

MYK Tracker has eight fixed machine stacks. A stack owns one persistent object
for every supported stack-owned machine type. Its editable `slots` vector stores
only routing/order state:

```text
SlotState
  id               stable identity, e.g. slot-7
  type             CommandType
  enabled          bypass state
  sendLevelDb      gain before a serial effect or into an aux send
  returnLevelDb    gain after a serial effect
```

Moving serial effects or aux taps changes their order in the DSP walk but never
moves or reconstructs a machine object. Terminal source audio is always mixed
before that walk, regardless of where its terminal slot is displayed. Machine
parameters therefore remain attached to the stack and type. Slot enabled
states, including the fixed MOD enabled state, are recalled per sequence set;
machine parameters are shared across sequence sets.

### Current types

| UI | Category | Ownership | Audio behavior |
|---|---|---|---|
| `MIDI` | terminal | no DSP object | emits external MIDI |
| `WAVE` | terminal instrument | one per stack | mixes mono source audio |
| `SAMPLER` | terminal instrument | one per stack | mixes mono sample audio |
| `DRUM` | terminal instrument | one per stack | mixes six-engine mono drums |
| `DIST` | serial effect | one per stack | transforms mono stack buffer |
| `DELAY` | serial effect | one per stack | transforms mono buffer; drains tails when bypassed |
| `CHSTR` | serial effect | one per stack | transforms mono stack buffer |
| `FILTER` | serial/note-aware effect | one per stack | transforms mono audio and receives note timing |
| `AUX1`, `AUX2` | parallel send slots | two shared global returns | copy mono send to stereo reverb buses |
| `MOD` | fixed control block | one per stack | runs before sources; not a serial slot |

The user-facing slot cycle is:

```text
MIDI → WAVE → SAMPLER → DRUM → DIST → DELAY → CHSTR → AUX1 → AUX2 → FILTER
```

`MOD` is permanently displayed above the slots and uses the pseudo-slot ID
`mod-matrix`. It is not in the cycle and cannot be moved or removed.

### Current audio path

```text
sequencer/audition note
        |
        +--> fixed MOD note/PITCH observation
        |
        +--> enabled terminal slot(s): MIDI / WAVE / SAMPLER / DRUM

WAVE + SAMPLER + DRUM                    (mono stack buffer)
        |
        v
ordered non-source slot walk             (mono)
  serial effect: SEND -> effect -> RETURN
  AUX slot: copy the signal at this point ----> shared stereo reverb
        |
        v
stack gain -> stack scope/meter -> equal-power stack pan
        |                                  (stereo from here)
        +--------------------------+
                                   v
                         AUX return gain/balance/mute
                                   |
                                   v
                         master gain/mute/meter
                                   |
                                   v
                         host output and WAV recorder
```

A new serial effect normally receives a one-channel buffer. It should still be
correct for every channel JUCE supplies so the class remains reusable and tests
can exercise stereo buffers. Do not make the entire stack stereo to implement a
local effect.

## Choose the right extension type

### Serial audio effect

Use `AudioEffectMachine` when the machine changes existing audio in place and
does not generate notes. It supplies final implementations of the generic
prepare/release/process/note methods. Implement:

```cpp
void prepareDsp(double sampleRate, int samplesPerBlock) override;
void processAudioBuffer(juce::AudioBuffer<float>& buffer) override;
```

Optionally override `clearTransientState()` for filters, delay lines, phase, or
envelopes. Do not override the base class's final `prepareToPlay`,
`releaseResources`, or `processBlock` methods.

### Terminal instrument

Derive directly from `MachineInterface` when the machine generates audio from
notes. Use the existing sample-offset pipeline:

- UI/control code queues a note for its stack.
- `processBlock` distributes events into the current block as
  `MachineScheduledNote` values.
- The processor calls `scheduleBlockNotes()` before the instrument renders.
- Rendering applies events at `sampleOffset`, not merely at the block edge.
- Held keyboard audition arrives as explicit note-on/note-off events.

Preallocate voices and event storage. A disabled or idle terminal should do
negligible work, while enabling it must not allocate or cold-initialize DSP in
the callback.

### Note-aware effect

FILTER is both a serial effect and a consumer of scheduled note timing for its
envelope. Treat this as a special case: the effect still renders in slot order,
but processor dispatch must also deliver note events to it.

### Shared bus or fixed block

Do not force these through the ordinary slot recipe:

- AUX return machines are shared stereo processors with per-stack send slots.
- MOD is a fixed pre-source generator with a compiled route matrix and
  sequence-set-specific bypass.

Adding another global bus or fixed block is a processor architecture change,
not simply “one more effect.” Plan its ownership, state, UI identity, and signal
position explicitly.

## The `MachineInterface` contract

Every machine provides:

- preparation, release, transient reset, and audio/MIDI processing;
- a column-major editor grid from `getUIBoxes()`;
- note handling, even if the implementation returns `false`;
- state serialization and restoration.

Optional hooks support:

- sample-offset block notes;
- tracker tick duration and clock ticks;
- `allNotesOff`;
- learned note assignment;
- add/remove rows;
- transient file/popup UI;
- wide-grid left/right navigation;
- modulation block input;
- text entry, backspace, cell clearing, and note shifting.

Implement only hooks your machine owns. Keep generic behavior generic.

## Worked example: an efficient tremolo effect

This example emphasizes the repository conventions rather than a particular
tremolo sound. It uses a triangle LFO so there is no trigonometry in the sample
loop.

### 1. Create the files

`src/machines/TremoloFxMachine.h`:

```cpp
#pragma once

#include <atomic>
#include <JuceHeader.h>

#include "AudioEffectMachine.h"

class TremoloFxMachine final : public AudioEffectMachine
{
public:
    std::vector<std::vector<UIBox>> getUIBoxes(const MachineUiContext&) override;
    void processAudioBuffer(juce::AudioBuffer<float>&) override;
    void clearTransientState() override;
    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void*, int) override;

protected:
    void prepareDsp(double sampleRate, int samplesPerBlock) override;

private:
    static constexpr float kDefaultRate = 4.0f;
    static constexpr float kDefaultDepth = 0.5f;
    std::atomic<double> sampleRate { 44100.0 };
    std::atomic<float> rateHz { kDefaultRate };
    std::atomic<float> depth { kDefaultDepth };
    float phase = 0.0f;
};
```

`src/machines/TremoloFxMachine.cpp`:

```cpp
#include "TremoloFxMachine.h"
#include "MachineStateCodec.h"
#include "MachineUi.h"

#include <cmath>

namespace
{
constexpr int kStateVersion = 1;
constexpr float kMinRate = 0.01f;
constexpr float kMaxRate = 40.0f;
}

void TremoloFxMachine::prepareDsp(double newRate, int samplesPerBlock)
{
    juce::ignoreUnused(samplesPerBlock);
    sampleRate.store(sanitizedSampleRate(newRate), std::memory_order_relaxed);
}

void TremoloFxMachine::clearTransientState()
{
    phase = 0.0f;
}

void TremoloFxMachine::processAudioBuffer(juce::AudioBuffer<float>& buffer)
{
    const float rate = rateHz.load(std::memory_order_relaxed);
    const float amount = depth.load(std::memory_order_relaxed);
    const float increment = rate / static_cast<float>(sampleRate.load(std::memory_order_relaxed));

    float currentPhase = phase;
    for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
    {
        const float triangle = 1.0f - 4.0f * std::abs(currentPhase - 0.5f);
        const float unipolar = 0.5f * (triangle + 1.0f);
        buffer.applyGain(sample, 1, (1.0f - amount) + amount * unipolar);
        currentPhase += increment;
        if (currentPhase >= 1.0f)
            currentPhase -= 1.0f;
    }
    phase = currentPhase;
}
```

Load atomics once per block. If rate/depth changes need smoothing, add prepared
fixed-state smoothers; do not allocate during the edit or callback.

### 2. Build a column-major machine grid

The outer vector is columns and the inner vector is rows:

```cpp
std::vector<std::vector<UIBox>> TremoloFxMachine::getUIBoxes(const MachineUiContext& context)
{
    juce::ignoreUnused(context);
    std::vector<std::vector<UIBox>> cells(2, std::vector<UIBox>(2));

    cells[0][0] = makeLabelCell("RATE");
    cells[1][0] = makeFloatCell(rateHz.load(std::memory_order_relaxed),
                                kMinRate, kMaxRate, 2,
        [this](int direction)
        {
            const float old = rateHz.load(std::memory_order_relaxed);
            rateHz.store(juce::jlimit(kMinRate, kMaxRate,
                         old + 0.1f * static_cast<float>(direction)),
                         std::memory_order_relaxed);
        });
    cells[1][0].onInsert = [this](double value)
    {
        rateHz.store(juce::jlimit(kMinRate, kMaxRate, static_cast<float>(value)),
                     std::memory_order_relaxed);
    };

    cells[0][1] = makeLabelCell("DEP");
    cells[1][1] = makeFloatCell(depth.load(std::memory_order_relaxed),
                                0.0f, 1.0f, 2,
        [this](int direction)
        {
            const float old = depth.load(std::memory_order_relaxed);
            depth.store(juce::jlimit(0.0f, 1.0f,
                        old + 0.05f * static_cast<float>(direction)),
                        std::memory_order_relaxed);
        });
    cells[1][1].onInsert = [this](double value)
    {
        depth.store(juce::jlimit(0.0f, 1.0f, static_cast<float>(value)),
                    std::memory_order_relaxed);
    };
    return cells;
}
```

`UIBox` callbacks are used by the keyboard UI and generic MCP machine control.
Labels and positions contribute to generated control IDs, so changing them can
break an external controller even though there is no bespoke MCP schema.

Use `MachineUi.h` helpers for normal labels, floats, toggles, Hz, and dB. Set
`isDisabled`, `isActive`, value scales, and custom colors deliberately. A cell
with no callback is presentation-only.

### 3. Store versioned, validated state

```cpp
void TremoloFxMachine::getStateInformation(juce::MemoryBlock& destination)
{
    auto object = juce::DynamicObject::Ptr(new juce::DynamicObject());
    object->setProperty("version", kStateVersion);
    object->setProperty("rateHz", rateHz.load(std::memory_order_relaxed));
    object->setProperty("depth", depth.load(std::memory_order_relaxed));
    writeMachineStateJson(destination, juce::var(object.get()));
}

void TremoloFxMachine::setStateInformation(const void* data, int size)
{
    const auto object = parseMachineStateJson(data, size);
    if (!object.isObject())
    {
        rateHz.store(kDefaultRate, std::memory_order_relaxed);
        depth.store(kDefaultDepth, std::memory_order_relaxed);
        clearTransientState();
        return;
    }

    float restoredRate = static_cast<float>(object.getProperty("rateHz", kDefaultRate));
    float restoredDepth = static_cast<float>(object.getProperty("depth", kDefaultDepth));
    if (!std::isfinite(restoredRate)) restoredRate = kDefaultRate;
    if (!std::isfinite(restoredDepth)) restoredDepth = kDefaultDepth;
    rateHz.store(juce::jlimit(kMinRate, kMaxRate, restoredRate), std::memory_order_relaxed);
    depth.store(juce::jlimit(0.0f, 1.0f, restoredDepth), std::memory_order_relaxed);
    clearTransientState();
}
```

Use semantic defaults, not whatever value happens to be left in a reused
machine. `setStateInformation()` is not called when the top-level machine blob
is absent, so also provide a parameter-default reset and invoke it in processor
restore before decoding the optional new entry (DRUM demonstrates this pattern).
A malformed song must not inherit tremolo settings from the song that was open
before it. Machine version fields are advisory: readable blobs are interpreted
with the current layout, while malformed values still need safe handling.

### 4. Add the source to CMake

Add the `.cpp` under `target_sources(myk-tracker-plug PRIVATE ...)`:

```cmake
src/machines/TremoloFxMachine.cpp
```

Headers are included normally and do not have to be listed. If a focused test
compiles the implementation directly, add it to that test's sources as well.

## Registering an ordinary stack machine

Registration is deliberately explicit. Complete every applicable item.

### 1. Append a stable `CommandType`

`CommandType` values are serialized in sequence rows and slot state. Never
renumber, reorder, or reuse them. At the time of writing the final value is:

```cpp
DrumSynth = 16
```

A new type would be appended with the next value, followed by an update to the
single canonical `kLastCommandType` endpoint. Recheck the live enum before
editing; do not trust this document as an allocator.

### 2. Add centralized traits

Add a `machineTraits()` case. A tremolo is an audio effect, is not a sequencer
step command, is stack-routable, and has a slot-cycle index:

```cpp
case CommandType::TremoloFx:
    return { "TREM", "tremolo", true, false, true, 10 };
```

Append it to `kSlotCycleTypes` and update that array's declared size. Traits are
consumed by the processor, editor, renderer descriptions, and control service;
avoid duplicating classification elsewhere.

### 3. Add persistent ownership

In `TrackerMainProcessor::MachineStack`:

```cpp
std::unique_ptr<TremoloFxMachine> tremoloFx;
```

Then update:

- the forward declaration/include;
- `initialiseMachines()` construction;
- `MachineStack::forEachMachine()`;
- `getMachineForStackType()`;
- `getMachineCount()`;
- `stackMachineStateEntries()` and its `std::array` size.

The state key becomes part of saved files:

```cpp
{ "tremoloFx", stack.tremoloFx.get() }
```

Keep it stable after release.

### 4. Make processor switches exhaustive

Search for the canonical endpoint and for switches over `CommandType`:

```sh
rg -n 'kLastCommandType|switch \(.*CommandType|switch \(type\)|switch \(slot' src tests
```

Review every result. Common sites include:

- persisted command validation in `Sequencer.cpp` and processor restore;
- `TrackerMainUI` label/type iteration;
- `TrackerControlService` command-name parsing and command validation;
- aux-bus/type lookup switches;
- current slot-order validation;
- note description and held-note routing;
- stack state normalization;
- performance-stage classification.

The canonical endpoint handles numeric range checks. Explicit cases that
describe DRUM routing or behavior remain DRUM-specific.

For a normal effect:

- `machineTraits(type).isAudioEffect` makes it eligible for serial processing.
- `getMachineForStackType` must return an `AudioEffectMachine` object.
- `refreshStackProcessingState` already treats enabled audio-effect traits as an
  audio path, but still inspect it if the machine has special activity flags or
  modulation destinations.
- `dispatchNoteThroughStack` must have an exhaustive pass-through case even if
  the effect ignores notes.
- the process timing switch should give it a meaningful performance stage
  rather than silently attributing it to another effect.

### 5. Decide whether it is a step command

An ordinary effect is not a step command. Do not add a `Command` merely because
the effect is stack-routable.

Only add `CommandProcessor` registration if a sequence row can explicitly store
and execute that command. Current examples include note terminals and control
commands such as AUX toggles and filter cutoff. A new step command must preserve
the four-value row layout and be accepted by restore/API validation.

### 6. UI and MCP behavior

The Machine page gets controls from `getUIBoxes()`. For a simple effect, add its
type to any detail-page allowlist/special rendering switch in `TrackerMainUI`.
Do not hard-code a second parameter grid in the editor.

Direct MCP controls are generated from the same cells. Usually no new MCP tool
is needed. If users/API calls need to name the new type when editing a stack or
step, add aliases to `commandTypeFromName()` in `TrackerControlService.cpp` and
update exposed capabilities/documentation.

## Adding a terminal instrument

A terminal needs everything above plus a sample-accurate note path. Follow WAVE
or DRUM end to end.

### Processor storage

Add, reserve, and clear:

- a pending/scratch `NoteEventQueue`;
- one `std::vector<MachineScheduledNote>` per stack;
- a `*ProcessingActive` flag when useful.

Reserve capacities before playback. Event collection and stable ordering in
the callback must not grow storage.

### Dispatch and release

Update all of:

- `dispatchNoteThroughStack()` terminal detection and enqueue;
- `beginHeldNote()` and `endHeldNote()`;
- `allNotesOff()` and `allNotesOffForStack()`;
- current-block distribution;
- source rendering in `processBlock()`;
- `describeStepNote()` if the instrument maps notes to named sounds;
- `refreshStackProcessingState()`.

Define whether more than one enabled terminal may respond. The current walk can
trigger each enabled terminal it encounters. A disabled terminal still counts
as an explicit terminal route for fallback decisions, preventing accidental
external MIDI output.

### Timing and gates

`MachineScheduledNote` includes sample offset, MIDI note, velocity, duration in
tracker ticks, and explicit note-off. Use `setSecondsPerTick()` when converting
duration to samples. Preserve fractional durations used by HALF/QUARTER
performance gating, and clamp any resulting positive gate to at least one
sample.

If the instrument is normally one-shot, explicitly define how note-off,
retrigger, and `allNotesOff` behave. DRUM supports per-engine one-shot/gated
behavior; SAMPLER has special performance deadlines; WAVE uses envelope release.

### CPU behavior

Voice count must control actual work. A preallocated maximum is good, but do not
process every inactive voice. Idle engines should exit cheaply after envelope
and filter tails finish. Changing a voice count or enable flag must not allocate
or initialize large resources in the callback.

## Adding a modulation destination

This is separate from adding a machine control. Inspect:

- `src/ModulationTypes.h`
- `ModulationMatrixMachine.*`
- `TrackerMainProcessor::refreshStackProcessingState()`
- the destination consumer (WAVE, FILTER, SAMPLER, DELAY, or stack mixer)

The matrix uses fixed source/destination identifiers, preallocated block
buffers, compiled nonzero routes, and active-destination masks. A new
destination generally requires:

1. Adding a destination ID and updating the tracker schema version.
2. Defining its full-scale mapping and clamp/wrap behavior.
3. Updating labels and installed-machine visibility masks.
4. Extending route storage and bumping the exact machine-state version.
5. Producing the destination without allocation or route discovery.
6. Applying it at the documented point in the target DSP.
7. Testing positive/negative/full depth, summing, bounds, bypass, and state round trips.

Use lookup/interpolation for expensive audio-rate transforms. Preserve the
unmodulated fast path. Trigger sources (NOTE/CLK) may have different semantics
from continuous LFO/PITCH sources, so define both.

Do not expose a destination merely because a parameter exists. The correct
application point matters—for example filter cutoff modulation is applied
before its envelope, delay-time modulation is ignored in step-synced mode, and
stack pan is applied at the mono-to-stereo mixer boundary.

## Shared aux and fixed-block changes

### Shared aux return

An aux send slot does not own the reverb it names. The processor owns two
`SharedAuxBus` objects containing a stereo input buffer, one `AuxReverbMachine`,
gain, balance pan, mute, and stereo meters. Adding another bus means changing:

- processor ownership and preparation;
- per-block stereo input clearing;
- stack send routing;
- return processing and final sum;
- Mixer columns and `MachineHost` accessors;
- current-version persistence and safe malformed-input handling;
- all editor/control stubs and tests.

### Fixed MOD-style block

A fixed block needs a stable pseudo-slot ID, dedicated Machine-page row,
per-sequence-set enabled recall if applicable, explicit pre/post position in the
callback, and generic control discovery. It should not be inserted into
`kSlotCycleTypes` unless it truly becomes reorderable serial DSP.

## State and format versions

There are three related formats:

1. Each machine emits a versioned JSON blob.
2. `TrackerMainProcessor` Base64-encodes those blobs into its JSON document,
   stores that JSON in an APVTS `ValueTree`, and writes the tree as XML.
3. Explicit `WITH AUDIO` save wraps that same XML as `state.xml` in a ZIP with
   `manifest.json` and 24-bit FLAC sampler assets. The extension remains
   `.myktracker`.

`getStateInformation()` must remain lightweight and path-based because hosts,
automatic standalone state, MCP state, and `STATE ONLY` saves use it. Do not
embed sample buffers in ordinary plug-in state.

Current-format rules:

- write the current `stateVersion` in top-level tracker JSON and machine blobs;
- treat missing or mismatched versions as the current version-1 layout;
- do not maintain alternate older-layout migration branches;
- clamp restored numbers and reject non-finite values;
- validate external data in Release, not only with `jassert`;
- reset transient DSP separately from parameters;
- do not serialize phases, meter values, UI glows, or other transient state
  unless the feature specifically requires it.

Test current-version, unversioned, and mismatched-version loads plus malformed
data handling.

## Real-time and concurrency checklist

Before considering DSP complete, answer all of these:

- Does `processAudioBuffer` or `processBlock` allocate, resize, sort using new
  storage, acquire a mutex, log, read a file, or call the message thread?
- Are lookup tables, delay buffers, wavetables, voices, events, and scratch
  buffers prepared before playback?
- Are atomics loaded once per block where possible?
- Are expensive coefficients recalculated only when their inputs change?
- Does disabled/idle state skip meaningful work without causing a surprise
  allocation on re-enable?
- Are deliberate delay/reverb/release tails preserved?
- Can a UI edit race with a vector/index the callback is traversing?
- Are all indices validated before `operator[]`?
- Does sample-rate/block-size change rebuild all dependent state safely?
- Do `allNotesOff`, transport reset, held-key release, and route changes clean up
  every pending queue?

The current stack buffer is mono. Aux returns and the master are stereo. Be
explicit about the channel domain in tests.

## UI and control checklist

- Return `cells[column][row]`, not row-major data.
- Keep labels short, uppercase, and legible at low resolution.
- Give selectable cells at least one supported callback.
- Support `[`/`]` through `onAdjust`; use `onInsert` for direct numeric entry
  when the parameter should accept it.
- Clamp inside callbacks, not only in rendering.
- Use `width` for long values instead of adding fake cells.
- Avoid setting maximum glow merely because a page was opened. Dynamic glow
  should reflect real activity and disappear on bypass.
- Cache waveform/envelope geometry when its source state is unchanged.
- If the machine has a horizontally windowed editor, implement
  `navigateLeft()`/`navigateRight()` and keep cursor coordinates valid.
- Check generated machine controls through MCP after changing labels/layout.

## Tests and verification

### Focused automated tests

Create a focused JUCE console target when the behavior is substantial. Current
examples are:

- `sequence-read-head-tests`
- `sampler-tuning-tests`
- `modulation-matrix-tests`
- `wavetable-synth-tests`
- `drum-synth-tests`
- `drum-processor-tests`

Good machine tests cover:

- defaults and parameter bounds;
- bracket adjustment and numeric insertion;
- deterministic audio or state transitions;
- note-on/off/retrigger/all-notes-off where applicable;
- sample-rate and block-size changes;
- bypass/idle behavior and tails;
- current-version and unversioned state round trips, plus malformed input;
- modulation application and inactive fast path;
- no callback allocation where the harness can observe it.

Run:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --target myk-tracker-plug_Standalone -j2
ctest --test-dir build --output-on-failure
```

Build VST3 when processor/state/host behavior changes:

```sh
cmake --build build --target myk-tracker-plug_VST3 -j2
```

### Manual audio/UI checks

At minimum:

1. Add, remove, move, enable, and bypass the machine.
2. Verify effect order audibly.
3. Edit every parameter while stopped and playing.
4. Try more than one sample rate and buffer size.
5. Exercise note-off, retrigger, route changes, transport reset, and tails.
6. Save/reload current state and confirm an unversioned state is attempted.
7. Switch pages repeatedly, resize, zoom, and scroll wide grids.
8. Confirm the machine costs little while disabled/idle and does not spike when
   its detail page opens.

### MCP checks

With the Debug Standalone running:

1. Check `/health`.
2. Call modern `server/discover` and `tools/list`.
3. Read `myktracker://view` or use `tracker_get_state`.
4. Query `scope: "machine_controls"`.
5. Address the new control by `stackId`, persisted `slotId`, and `controlId`.
6. Adjust or set it without changing the visible GUI cursor.
7. Use `tracker_screenshot` to inspect the active page if visual behavior changed.

Do not add a bespoke MCP operation when `tracker_machine_control` already
expresses the action.

## Common mistakes

### Renumbering `CommandType`

This corrupts the meaning of saved sequence rows and slots. Append only.

### Updating the enum but not its canonical endpoint

Numeric range checks use `kLastCommandType` and `kCommandTypeCount`. Update the
endpoint when appending a type, then inspect explicit switches for routing and
behavior changes.

### Forgetting `forEachMachine`

The machine then misses prepare, release, tick-duration, or all-notes-off style
lifecycle operations.

### Forgetting `stackMachineStateEntries`

The UI may work, but parameters silently disappear on save/load. Update the
fixed `std::array` size too.

### Treating slots as owners

Reordering or deleting a slot must not destroy machine parameters. The stack
owns persistent instances.

### Making a serial effect stereo upstream

Stacks are mono until mixer panning. Process the supplied channels correctly;
do not double every source/effect merely to add stereo behavior.

### Allocating on first use

This creates exactly the live CPU surprise the architecture tries to avoid.
Prepare maximum-capacity resources, then process only active voices/routes.

### Using UI coordinates as external identity

Grid coordinates move. MCP uses stable slot IDs and generated machine-local
control IDs; MOD uses `mod-matrix`.

### Editing shared data without audio exclusion

A bounds check does not cure a vector resize race. Use the processor's existing
exclusive mutation path and keep the protected section small.

### Reintroducing arpeggiator machines

Arpeggiation belongs to sequence read heads. Extend that system instead.

### Assuming a successful build is enough

DSP can compile while being silent, out of order, unrestorable, unreachable by
MCP, or unsafe during live edits. Perform the relevant focused and manual checks.

## Final implementation checklist

- [ ] Correct machine category and signal position chosen
- [ ] Callback allocation/lock/file/log free
- [ ] Parameters safe between UI and audio threads
- [ ] Transient resources prepared and reset correctly
- [ ] Column-major `UIBox` grid with normal controls
- [ ] Versioned, validated state with permissive current-layout loading
- [ ] Production `.cpp` listed in CMake
- [ ] Stable `CommandType` appended
- [ ] Traits and slot cycle updated
- [ ] Stack ownership, construction, lifecycle, lookup, and state updated
- [ ] Processing flags and special routing updated
- [ ] Canonical command endpoint, restore allowlists, and switches audited
- [ ] Terminal queues/dispatch/release/rendering added if applicable
- [ ] Modulation visibility/state/consumer added if applicable
- [ ] Generic MCP controls verified; aliases/docs updated if needed
- [ ] Focused tests added and full CTest run
- [ ] Debug Standalone built and manually auditioned
- [ ] VST3 built when shared processor/state behavior changed
- [ ] `README.md`, cheat sheet, and this tutorial updated as appropriate
- [ ] Only intended source/document files staged
