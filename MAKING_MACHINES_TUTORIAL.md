# Making Machines Tutorial

This tutorial explains how to add a new machine to MYK Tracker GL. It is written
for two audiences:

- Humans who want to understand where machines live and how they are wired in.
- Language-model agents that need a precise, checkable runbook.

The primary path is adding a new stack-owned audio-effect machine, such as a
tremolo, phaser, gate, or other in-place audio processor. Advanced sections
cover note-consuming terminal instruments and shared bus machines.

Use `TremoloFx` / `TremoloFxMachine` as the running example. The example is
illustrative; adapt the names and DSP to the real machine being added.

## When to use this tutorial

Use this tutorial when you need to:

- Add a new machine type that can appear in a machine stack slot.
- Add a new audio effect that processes stack audio.
- Add a new terminal instrument that consumes sequencer notes.
- Register a new machine type with the UI, MCP control service, and saved state.

Do not use this tutorial to:

- Resurrect or extend `ArpeggiatorMachine` or `PolyArpeggiatorMachine`.
- Renumber existing `CommandType` values.
- Change persisted state formats without a migration plan.
- Modify `libs/JUCE`.

## Human reading path

1. Read “Machine model”.
2. Fill in the “Machine decision table”.
3. Follow the numbered steps for the relevant machine kind.
4. Run the verification checklist.
5. Update user-facing docs only if the change is user-visible.

## Agent fast path

1. Jump to “Agent runbook”.
2. Use the grep patterns to locate the current integration points.
3. Make only the required changes.
4. Build the affected targets.
5. Run `sequence-read-head-tests` when sequencing or command code is touched.
6. Stop and report if any stop condition is hit.

## Machine model

MYK Tracker has 16 fixed machine stacks. Each stack owns persistent machine
instances and an ordered list of slots.

Important distinction:

- A slot is a routing/chain entry.
- A slot does not own a machine.
- A slot references one of the stack’s fixed machine instances.

Current stack-owned machines are:

```text
sampler
wavetableSynth
distortionFx
delayFx
channelStripFx
filterFx
```

Shared bus machines are special:

```text
AuxSend1Fx -> auxBus1
AuxSend2Fx -> auxBus2
```

Notes enter a stack through `dispatchNoteThroughStack`. They walk the enabled
slots in order:

```text
sequence note
    -> dispatchNoteThroughStack
        -> effect slots: pass through / retrigger filter chain
        -> terminal slot: consume note
```

Audio is rendered per stack:

```text
stack sources
    -> sampler
    -> wavetable synth
    -> effect slots in slot order
    -> stack gain
    -> shared aux buses
    -> plugin output
```

For audio-effect machines, the normal audio path is driven by:

- `machineTraits(type).isAudioEffect`
- `getMachineForStackType()`
- `getAudioEffectForStackType()`
- `refreshStackProcessingState()`
- the effect-slot loop in `TrackerMainProcessor::processBlock()`

For terminal instruments, note routing must also be wired explicitly.

## Machine decision table

Before editing code, decide and record these facts:

```text
Class name:
Header file:
Source file:
CommandType name:
CommandType value:
State key:
Short UI label:
Long UI name:
Base class:
Audio-only effect?
Terminal instrument?
Shared bus machine?
Sequence step command?
Stack routable?
Can be cycled into a slot?
Slot cycle position:
Send level supported?
Return level supported?
Clock-driven?
Needs allNotesOff()?
Needs custom machine detail page?
MCP command name:
Aliases:
```

For a normal new audio effect, typical answers are:

```text
Base class: AudioEffectMachine
Audio-only effect? yes
Terminal instrument? no
Shared bus machine? no
Sequence step command? no
Stack routable? yes
Can be cycled into a slot? yes
Slot cycle position: append at end
Send level supported? yes
Return level supported? yes
Clock-driven? no
Needs allNotesOff()? only if it has tails or active notes
Needs custom machine detail page? no, if getUIBoxes() is enough
```

## Choosing the base class

### Audio-only effect

Use:

```text
src/machines/AudioEffectMachine.h
```

This is the recommended path for most new machines.

`AudioEffectMachine` finalizes:

```text
prepareToPlay()
releaseResources()
processBlock()
handleIncomingNote()
```

Subclasses implement:

```text
processAudioBuffer(juce::AudioBuffer<float>&)
prepareDsp(double, int)
```

and optionally:

```text
clearTransientState()
allNotesOff()
scheduleBlockNotes(...)
setSecondsPerTick(...)
handleClockTick(...)
```

Rules:

- Do not override `prepareToPlay()` in an `AudioEffectMachine` subclass.
- Do not override `releaseResources()` in an `AudioEffectMachine` subclass.
- `prepareToPlay()` calls `prepareDsp()`, then `clearTransientState()`.
- `releaseResources()` calls `clearTransientState()`.
- `processBlock()` forwards to `processAudioBuffer()`.
- `handleIncomingNote()` always returns `false` for audio effects.
- If the machine has transient DSP state, override `clearTransientState()`.
- If the machine uses a mutex, `clearTransientState()` should self-lock.
- If the machine uses atomics, follow the existing scalar-parameter style.

### Note-consuming terminal instrument

Use `MachineInterface` directly, like:

```text
WavetableSynthMachine
SuperSamplerProcessor
```

This path is more involved. The machine must handle:

- incoming sequencer notes
- sample-accurate scheduling
- audio rendering
- note-off and `allNotesOff()` cleanup
- state persistence
- UI cells

Terminal machines also require processor routing changes. See “Advanced:
terminal instruments”.

### Shared bus machine

Shared bus machines are larger changes. Examples:

```text
AuxSend1Fx
AuxSend2Fx
```

They are not normal stack-owned machines. They require:

- a shared bus member in `TrackerMainProcessor`
- mapping in `getAuxBusForType()`
- special parallel processing in `processBlock()`
- shared state save/restore
- UI and control-service treatment as a shared resource

Only use this path when the machine must be shared across stacks.

## Worked example: TremoloFx

This example adds a simple audio-only tremolo effect.

Planned names:

```text
Class: TremoloFxMachine
Header: src/machines/TremoloFxMachine.h
Source: src/machines/TremoloFxMachine.cpp
CommandType: TremoloFx
State key: "tremoloFx"
Short label: "TREM"
Long name: "tremolo"
MCP name: "tremolo"
MCP alias: "tremolo_fx"
```

### Step 1: Create the machine files

Create:

```text
src/machines/TremoloFxMachine.h
src/machines/TremoloFxMachine.cpp
```

Example header:

```cpp
#pragma once

#include <array>
#include <atomic>
#include <string>
#include <vector>

#include <JuceHeader.h>

#include "AudioEffectMachine.h"

class TremoloFxMachine final : public AudioEffectMachine
{
public:
    TremoloFxMachine() = default;

    void clearTransientState() override;
    std::vector<std::vector<UIBox>> getUIBoxes(const MachineUiContext& context) override;
    void processAudioBuffer(juce::AudioBuffer<float>& buffer) override;
    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

protected:
    void prepareDsp(double sampleRate, int samplesPerBlock) override;

private:
    static constexpr float kMinRateHz = 0.1f;
    static constexpr float kMaxRateHz = 30.0f;
    static constexpr float kMinDepth = 0.0f;
    static constexpr float kMaxDepth = 1.0f;
    static constexpr float kMinMix = 0.0f;
    static constexpr float kMaxMix = 1.0f;

    std::atomic<double> currentSampleRate { 44100.0 };
    std::atomic<float> rateHz { 4.0f };
    std::atomic<float> depth { 1.0f };
    std::atomic<float> mix { 1.0f };

    std::array<float, 2> phase {};
};
```

Example source:

```cpp
#include "TremoloFxMachine.h"
#include "MachineStateCodec.h"
#include "MachineUi.h"

#include <cmath>

namespace
{
constexpr double kTremoloStateVersion = 1.0;
}

void TremoloFxMachine::prepareDsp(double sampleRate, int samplesPerBlock)
{
    juce::ignoreUnused(samplesPerBlock);
    currentSampleRate.store(sanitizedSampleRate(sampleRate), std::memory_order_relaxed);
}

void TremoloFxMachine::clearTransientState()
{
    phase.fill(0.0f);
}

std::vector<std::vector<UIBox>> TremoloFxMachine::getUIBoxes(const MachineUiContext& context)
{
    juce::ignoreUnused(context);

    std::vector<std::vector<UIBox>> boxes(2, std::vector<UIBox>(4));

    auto makeValueCell = [](std::atomic<float>& target, float step, float minValue, float maxValue, int decimals)
    {
        return makeFloatCell(target.load(std::memory_order_relaxed), minValue, maxValue, decimals,
            [&target, step, minValue, maxValue](int direction)
            {
                const float currentValue = target.load(std::memory_order_relaxed);
                const float nextValue = juce::jlimit(minValue, maxValue, currentValue + step * static_cast<float>(direction));
                target.store(nextValue, std::memory_order_relaxed);
            });
    };

    boxes[0][0].kind = UIBox::Kind::TrackerCell;
    boxes[0][0].text = "RATE";
    boxes[1][0] = makeValueCell(rateHz, 0.5f, kMinRateHz, kMaxRateHz, 1);

    boxes[0][1].kind = UIBox::Kind::TrackerCell;
    boxes[0][1].text = "DEPTH";
    boxes[1][1] = makeValueCell(depth, 0.05f, kMinDepth, kMaxDepth, 2);

    boxes[0][2].kind = UIBox::Kind::TrackerCell;
    boxes[0][2].text = "MIX";
    boxes[1][2] = makeValueCell(mix, 0.05f, kMinMix, kMaxMix, 2);

    return boxes;
}

void TremoloFxMachine::processAudioBuffer(juce::AudioBuffer<float>& buffer)
{
    const float rateValue = rateHz.load(std::memory_order_relaxed);
    const float depthValue = depth.load(std::memory_order_relaxed);
    const float mixValue = mix.load(std::memory_order_relaxed);
    const float sampleRateValue = static_cast<float>(currentSampleRate.load(std::memory_order_relaxed));

    const float phaseStep = juce::jlimit(0.0f, 0.25f, rateValue / juce::jmax(1.0f, sampleRateValue));

    for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
    {
        auto* samples = buffer.getWritePointer(channel);
        float channelPhase = phase[static_cast<std::size_t>(juce::jlimit(0, 1, channel))];

        for (int sampleIndex = 0; sampleIndex < buffer.getNumSamples(); ++sampleIndex)
        {
            channelPhase += phaseStep;
            if (channelPhase >= 1.0f)
                channelPhase -= 1.0f;

            const float cosine = std::cos(2.0f * juce::MathConstants<float>::pi * channelPhase);
            const float gain = 1.0f - depthValue * 0.5f * (1.0f - cosine);
            const float dry = samples[sampleIndex];
            samples[sampleIndex] = juce::jmap(mixValue, dry, dry * gain);
        }

        phase[static_cast<std::size_t>(juce::jlimit(0, 1, channel))] = channelPhase;
    }
}

void TremoloFxMachine::getStateInformation(juce::MemoryBlock& destData)
{
    juce::DynamicObject::Ptr root = new juce::DynamicObject();
    root->setProperty("version", kTremoloStateVersion);
    root->setProperty("rateHz", rateHz.load(std::memory_order_relaxed));
    root->setProperty("depth", depth.load(std::memory_order_relaxed));
    root->setProperty("mix", mix.load(std::memory_order_relaxed));

    writeMachineStateJson(destData, juce::var(root.get()));
}

void TremoloFxMachine::setStateInformation(const void* data, int sizeInBytes)
{
    const juce::var parsed = parseMachineStateJson(data, sizeInBytes);
    if (parsed.isVoid())
        return;

    const float rateValue = getFloatProperty(parsed, "rateHz", rateHz.load(std::memory_order_relaxed), kMinRateHz, kMaxRateHz);
    const float depthValue = getFloatProperty(parsed, "depth", depth.load(std::memory_order_relaxed), kMinDepth, kMaxDepth);
    const float mixValue = getFloatProperty(parsed, "mix", mix.load(std::memory_order_relaxed), kMinMix, kMaxMix);

    rateHz.store(rateValue, std::memory_order_relaxed);
    depth.store(depthValue, std::memory_order_relaxed);
    mix.store(mixValue, std::memory_order_relaxed);

    phase.fill(0.0f);
}
```

Notes:

- The UI grid is column-major: the outer vector is columns, each inner vector
  is a column’s rows.
- Labels are compact and uppercase.
- `MachineUi.h` helpers only present cells; the adjust callback owns state
  updates.
- `MachineStateCodec.h` helpers validate and clamp persisted values.
- `processAudioBuffer()` must not allocate, log, touch files, sleep, or take
  per-sample locks.

### Step 2: Add the source file to CMake

Edit:

```text
CMakeLists.txt
```

Add the new `.cpp` to the plugin target’s machine source list, near the other
machine sources:

```cmake
target_sources(myk-tracker-plug
    PRIVATE
    ...
    src/machines/FilterFxMachine.cpp
    src/machines/TremoloFxMachine.cpp
)
```

Do not add legacy arpeggiator sources.

### Step 3: Register the `CommandType`

Edit:

```text
src/SequencerCommands.h
```

Append the new type after the current last value. Do not renumber existing
values.

Current example:

```cpp
enum class CommandType : std::size_t {
    MidiNote = 0,
    Log = 1,
    Sampler = 2,
    LegacyArpeggiator = 3,
    WavetableSynth = 4,
    LegacyPolyArpeggiator = 5,
    DistortionFx = 6,
    DelayFx = 7,
    ChannelStripFx = 8,
    AuxSend1Fx = 9,
    AuxSend2Fx = 10,
    FilterFx = 11,
    TremoloFx = 12,
};
```

Rules:

- Never change an existing value.
- Never reuse `LegacyArpeggiator` or `LegacyPolyArpeggiator`.
- Prefer appending to preserve persisted integer compatibility.
- After appending, search for hard-coded upper bounds such as
  `CommandType::FilterFx` and update the ones that assume it is the last value.

Important places to check:

```text
src/TrackerMainUI.cpp
src/TrackerMainProcessor.cpp
src/SequencerCommands.h
src/SequencerCommands.cpp
src/TrackerControlService.cpp
```

A hard-coded upper bound is a problem when it is used to mean “last known
command type”, not when it specifically refers to the filter machine.

### Step 4: Add machine traits

Edit:

```text
src/SequencerCommands.h
machineTraits(CommandType)
```

For a normal audio effect appended to the slot cycle:

```cpp
case CommandType::TremoloFx: return { "TREM", "tremolo", true, false, true, 9 };
```

Field meanings:

```text
shortLabel       stack-grid label
longName         detail/MCP-facing name
isAudioEffect    true for audio-only stack effects
isStepCommandType true only for sequence step commands
isStackRoutable  true for machines that can route notes through a stack
slotCycleIndex   index in kSlotCycleTypes, or -1 if not cycleable
```

For a non-cycleable helper type, use:

```cpp
case CommandType::TremoloFx: return { "TREM", "tremolo", true, false, true, -1 };
```

and do not add it to `kSlotCycleTypes`.

### Step 5: Update slot cycling

If the new machine can be cycled into a stack slot, edit:

```text
src/SequencerCommands.h
kSlotCycleTypes
```

Append it at the end for the least disruptive change:

```cpp
constexpr std::array<CommandType, 10> kSlotCycleTypes = {
    CommandType::MidiNote,
    CommandType::WavetableSynth,
    CommandType::Sampler,
    CommandType::DistortionFx,
    CommandType::DelayFx,
    CommandType::ChannelStripFx,
    CommandType::AuxSend1Fx,
    CommandType::AuxSend2Fx,
    CommandType::FilterFx,
    CommandType::TremoloFx
};
```

If inserting in the middle:

- update the array size
- update every affected `slotCycleIndex`
- manually verify the UI slot cycle order
- manually verify MCP `cycle_type` behavior

### Step 6: Register a step command only if required

Audio-only effects normally are not sequence step commands. For `TremoloFx`,
skip this step.

If the new machine is a step command:

1. Set `isStepCommandType = true` in `machineTraits()`.
2. Register a `Command` in:

   ```text
   src/SequencerCommands.cpp
   CommandProcessor::initialiseCommands()
   ```

3. Insert it into both command lookup structures used by the sequencer.
4. Update any persisted-step validation that rejects unknown or non-step
   command types.

`CommandProcessor::getCommand()` asserts that a command exists, so a step
command type must be fully registered.

### Step 7: Add machine ownership to `MachineStack`

Edit:

```text
src/TrackerMainProcessor.h
```

Include the new machine header if the processor header does not already get it
transitively.

Add a member to `MachineStack`:

```cpp
std::unique_ptr<TremoloFxMachine> tremoloFx;
```

Add the machine to `forEachMachine()`:

```cpp
template <typename F>
void forEachMachine(F&& f)
{
    f(*sampler);
    f(*wavetableSynth);
    f(*distortionFx);
    f(*delayFx);
    f(*channelStripFx);
    f(*filterFx);
    f(*tremoloFx);
}
```

This is required so the machine receives:

```text
setSecondsPerTick()
prepareToPlay()
releaseResources()
allNotesOff()
```

### Step 8: Construct the machine

Edit:

```text
src/TrackerMainProcessor.cpp
TrackerMainProcessor::initialiseMachines()
```

Construct the machine for every stack:

```cpp
stack.tremoloFx = std::make_unique<TremoloFxMachine>();
```

All machines referenced by `forEachMachine()` must be constructed before any
use.

### Step 9: Map type to machine

Edit:

```text
src/TrackerMainProcessor.cpp
TrackerMainProcessor::getMachineForStackType()
```

Add:

```cpp
case CommandType::TremoloFx: return stack.tremoloFx.get();
```

The const overload forwards to the non-const overload, so one case is usually
enough.

### Step 10: Verify audio-effect lookup

For audio effects, check:

```text
TrackerMainProcessor::getAudioEffectForStackType()
```

It should work once:

- `machineTraits(type).isAudioEffect` is true
- `getMachineForStackType()` returns the new machine
- the machine inherits `AudioEffectMachine`

No extra cast table is normally needed.

### Step 11: Add state persistence entry

Edit:

```text
src/TrackerMainProcessor.h
src/TrackerMainProcessor.cpp
TrackerMainProcessor::stackMachineStateEntries()
```

Increase the array size in both the declaration and definition.

Example header:

```cpp
static std::array<std::pair<const char*, MachineInterface*>, 7> stackMachineStateEntries(MachineStack& stack);
```

Example definition:

```cpp
std::array<std::pair<const char*, MachineInterface*>, 7> TrackerMainProcessor::stackMachineStateEntries(MachineStack& stack)
{
    std::array<std::pair<const char*, MachineInterface*>, 7> entries;
    entries[0] = { "sampler", stack.sampler.get() };
    entries[1] = { "wavetableSynth", stack.wavetableSynth.get() };
    entries[2] = { "distortionFx", stack.distortionFx.get() };
    entries[3] = { "delayFx", stack.delayFx.get() };
    entries[4] = { "channelStripFx", stack.channelStripFx.get() };
    entries[5] = { "filterFx", stack.filterFx.get() };
    entries[6] = { "tremoloFx", stack.tremoloFx.get() };
    return entries;
}
```

State rules:

- The state key is part of the persisted plugin state format.
- Old saved states may not contain the new key.
- Missing state must restore to defaults.
- Missing fields must use safe defaults.
- Invalid values must be clamped or ignored.
- Do not depend on new fields being present in old state.

### Step 12: Check processing-state refresh

Check:

```text
src/TrackerMainProcessor.cpp
TrackerMainProcessor::refreshStackProcessingState()
```

For a normal audio effect, the existing `isAudioEffectType()` path should mark
the stack as having an audio path.

Add special logic only if the machine needs:

- a dedicated processing flag
- special tail handling
- special block-note scheduling
- disabled-slot drain behavior

For example, `DelayFx` has special disabled-tail behavior. A simple tremolo
does not.

### Step 13: Wire note dispatch behavior

Edit:

```text
src/TrackerMainProcessor.cpp
TrackerMainProcessor::dispatchNoteThroughStack()
```

For a normal audio-effect slot, add the new type to the existing audio-effect
case group:

```cpp
case CommandType::DistortionFx:
case CommandType::DelayFx:
case CommandType::ChannelStripFx:
case CommandType::AuxSend1Fx:
case CommandType::AuxSend2Fx:
case CommandType::FilterFx:
case CommandType::TremoloFx:
    enqueueStackNote(filterEvents, stackIndex, note, velocity, durInTicks);
    break;
```

This matches the current behavior where notes passing through stack effect
slots can retrigger the stack filter chain.

If the new effect should not participate in that behavior, use a different
case and document why.

### Step 14: Clock wiring, only if needed

If the machine is clock-driven, register it in:

```text
src/TrackerMainProcessor.cpp
TrackerMainProcessor::configureClockListeners()
```

Only delay machines currently use `ClockAbs`. Treat clock-driven machines as a
special case and verify:

- registration
- unregistration
- event re-entry through the correct slot
- behavior when the machine is disabled or removed

A sample-rate-based tremolo does not need clock wiring.

### Step 15: UI integration

Edit:

```text
src/TrackerMainUI.cpp
```

If the machine can use the generic machine detail page, add it to:

```text
isSimpleMachineDetail()
```

Example:

```cpp
bool isSimpleMachineDetail(CommandType type)
{
    return type == CommandType::DistortionFx
        || type == CommandType::DelayFx
        || type == CommandType::FilterFx
        || type == CommandType::ChannelStripFx
        || type == CommandType::AuxSend1Fx
        || type == CommandType::AuxSend2Fx
        || type == CommandType::MidiNote
        || type == CommandType::TremoloFx;
}
```

Also update any enum-range logic that assumes the old last command type. For
example, `describeStackCursorAction()` currently walks command types up to a
hard-coded upper bound. Extend that upper bound to include the new type.

If the machine needs a custom detail page, add a dedicated branch in
`TrackerMainUI.cpp` instead of using `isSimpleMachineDetail()`.

The stack grid label comes from:

```text
machineTraits(type).shortLabel
```

so do not hard-code the new label in the editor.

### Step 16: MCP / control-service name

Edit:

```text
src/TrackerControlService.cpp
commandTypeFromName()
```

Add the canonical name and any useful aliases:

```cpp
{ "tremolo", CommandType::TremoloFx },
{ "tremolo_fx", CommandType::TremoloFx },
```

Direct machine control usually works generically once:

- the slot type resolves through `getMachineForStackType()`
- the machine returns correct `UIBox` cells
- the `UIBox` callbacks work
- the control service can resolve the stable slot id

A new MCP tool is usually not required unless the machine introduces a new
command category.

If the new machine changes advertised command names, resources, or control
scopes, update:

```text
TrackerControlService::capabilities()
```

and any MCP tool descriptions that list machine names.

### Step 17: Documentation

Update user-facing docs only when the change is user-visible.

Update:

```text
README.md
```

if the change affects:

- slot cycle order
- machine page behavior
- machine detail page behavior
- keyboard controls
- build commands or artifact locations

Update:

```text
AGENTS.md
```

if the change affects architecture invariants such as:

- stack-owned machine list
- slot-cycle order
- `AudioEffectMachine` lifecycle
- `allNotesOff` behavior
- state compatibility rules

Do not update docs for internal-only refactors.

## Advanced: terminal instruments

Use this path when the machine consumes notes and generates audio, like:

```text
WavetableSynthMachine
SuperSamplerProcessor
```

A terminal instrument usually requires all of the normal machine steps plus:

1. `MachineInterface` implementation, not `AudioEffectMachine`.
2. Note handling:
   - `handleIncomingNote()`
   - `scheduleBlockNotes()`
   - `processBlock()`
3. Stack note queue or event pipeline.
4. Terminal-route recognition in:

   ```text
   TrackerMainProcessor::dispatchNoteThroughStack()
   ```

5. Note-dispatch switch behavior in:

   ```text
   TrackerMainProcessor::dispatchNoteThroughStack()
   ```

6. A processing flag in `MachineStack` if the source needs special activation.
7. Source processing in:

   ```text
   TrackerMainProcessor::processBlock()
   ```

8. `refreshStackProcessingState()` logic for the new source.
9. `allNotesOff()` cleanup.
10. Step-command registration if sequence rows can store the machine directly.

Terminal instruments are more likely to need:

- sample-accurate note scheduling
- per-stack note buffers
- note-off tracking
- transport stop/reset behavior
- state for learned notes, sample selection, or synthesis patches

Treat terminal instruments as a separate design review before implementation.

## Advanced: shared bus machines

Use this path when the machine is shared by many stacks, like aux reverb.

Shared bus machines require:

- a shared bus member in `TrackerMainProcessor`
- construction in `initialiseMachines()`
- mapping in:

  ```text
  getMachineForStackType()
  getAuxBusForType()
  ```

- parallel bus processing in:

  ```text
  TrackerMainProcessor::processBlock()
  ```

- shared state save/restore
- UI treatment that makes shared ownership clear
- MCP naming that makes shared ownership clear

Shared bus machines are not stack-owned. Do not add them to `MachineStack`
unless the design explicitly changes.

## Real-time and state rules

### Audio-thread rules

The audio path is real-time-sensitive.

In `processBlock()`, `processAudioBuffer()`, and related audio-path helpers:

- Do not allocate.
- Do not log.
- Do not touch files.
- Do not use network/OSC.
- Do not sleep.
- Do not use contended synchronization per sample.
- Do not create or destroy DSP objects.
- Allocate buffers and prepare DSP in `prepareToPlay()` / `prepareDsp()`.

Acceptable patterns:

- Atomics for simple scalar parameters.
- One uncontended mutex lock per block to snapshot parameters, as done by
  `FilterFxMachine`.
- Preallocated vectors and buffers.
- Lock-free per-sample processing after a per-block snapshot.

If using a mutex:

- `clearTransientState()` should acquire the lock itself.
- Call sites that already hold the lock must not call `clearTransientState()`
  directly; they should call the private reset helper instead.

### State rules

Persisted state must remain loadable.

Required behavior:

- New state loads in the new build.
- Old state missing the new machine key loads with defaults.
- Old state missing new machine fields loads with defaults.
- Invalid numeric values are clamped.
- Corrupt or non-JSON state does not crash.
- Slot ids remain stable.
- Existing `CommandType` values keep their numeric values.

Machine state rules:

- Each machine serializes its own versioned JSON.
- Use `MachineStateCodec.h` helpers.
- Use `juce::var::isVoid()` for parsed-state emptiness checks.
- Keep property names stable once shipped.
- Add new fields additively.
- Increment a state version only when adding a real migration need.

## Verification checklist

Run the narrowest relevant build first. For a new machine, build both plugin
formats when available:

```sh
cmake --build build --target myk-tracker-plug_Standalone -j2
cmake --build build --target myk-tracker-plug_VST3 -j2
```

Run sequence tests when touching:

```text
src/Sequencer.cpp
src/SequencerCommands.cpp
src/SequencerCommands.h
read-head behavior
step-command validation
```

Command:

```sh
cmake --build build --target sequence-read-head-tests -j2
ctest --test-dir build
```

Even when only adding an audio effect, running the sequence tests is a cheap
sanity check because `CommandType` and traits affect command validation.

### Manual checks

For a new slot machine:

- Cycle a stack slot into the new machine type.
- Add, remove, move up, move down, enable, and disable the slot.
- Verify the short label appears correctly in the stack grid.
- Verify the machine detail page renders the `getUIBoxes()` cells.
- Adjust at least one parameter while paused and while playing.
- Verify send and return levels behave correctly if supported.
- Verify the machine processes audio when enabled.
- Verify disabling the machine stops normal processing.
- Verify notes route correctly through the stack.
- Verify `allNotesOff` clears tails or active notes if applicable.
- Verify transport stop/reset does not leave stuck audio.
- Save state, reload it, and verify machine parameters remain.
- Load an old state without the new machine key and verify defaults.
- Load an old state without the new slot type and verify no slot is created
  unless the user adds it.
- Check MCP discovery/control if `commandTypeFromName()` or capabilities
  changed.

### Real-time audit

Before considering the machine done, audit the audio path:

```text
No new heap allocation in processBlock or processAudioBuffer.
No new DBG/log calls in the audio path.
No file, network, or OSC access in the audio path.
No sleeps or waits in the audio path.
No per-sample mutex locking.
Buffers allocated in prepareToPlay/prepareDsp where possible.
Transient state cleared on prepare and release.
allNotesOff clears any active notes or tails.
```

### State audit

```text
New save contains the new machine key.
Old save without the new machine key loads.
Missing fields restore to defaults.
Out-of-range values are clamped.
Corrupt machine JSON is ignored safely.
Slot id remains stable across save/load.
CommandType values were not renumbered.
```

## Agent runbook

Use this runbook for a new stack-owned audio-effect machine.

### 0. Inspect the repository

```sh
git status --short
git branch --show-current
```

Preserve unrelated local changes. Do not stage or commit unless explicitly
instructed.

### 1. Locate integration points

Useful greps:

```sh
rg "CommandType::FilterFx" src
rg "kSlotCycleTypes|machineTraits" src
rg "forEachMachine|initialiseMachines|getMachineForStackType" src
rg "stackMachineStateEntries" src
rg "refreshStackProcessingState|getAudioEffectForStackType" src
rg "dispatchNoteThroughStack" src
rg "isSimpleMachineDetail|describeStackCursorAction" src/TrackerMainUI.cpp
rg "commandTypeFromName" src/TrackerControlService.cpp
rg "target_sources\\(myk-tracker-plug" CMakeLists.txt
```

### 2. Create the machine

Create:

```text
src/machines/<MachineName>.h
src/machines/<MachineName>.cpp
```

For audio effects:

- inherit `AudioEffectMachine`
- implement `processAudioBuffer()`
- implement `prepareDsp()`
- implement `getUIBoxes()`
- implement `getStateInformation()`
- implement `setStateInformation()`
- implement `clearTransientState()` if there is transient DSP state
- implement `allNotesOff()` if there are tails or active notes

Do not override final `AudioEffectMachine` lifecycle methods.

### 3. Add CMake source

Add:

```text
src/machines/<MachineName>.cpp
```

to:

```text
target_sources(myk-tracker-plug ...)
```

### 4. Register type metadata

In:

```text
src/SequencerCommands.h
```

- Append a new `CommandType` value.
- Add a `machineTraits()` case.
- If cycleable, append to `kSlotCycleTypes`.
- If cycleable, set the correct `slotCycleIndex`.
- If not cycleable, set `slotCycleIndex = -1`.

### 5. Update hard-coded enum bounds

Search for:

```text
CommandType::FilterFx
```

Update bounds that mean “last known command type”. Do not change uses that
specifically mean the filter machine.

### 6. Wire processor ownership

In:

```text
src/TrackerMainProcessor.h
```

- Add the machine member to `MachineStack`.
- Add the machine to `forEachMachine()`.

In:

```text
src/TrackerMainProcessor.cpp
```

- Construct it in `initialiseMachines()`.
- Add a `getMachineForStackType()` case.
- Add a `stackMachineStateEntries()` entry and update the array size.
- Add the type to the audio-effect case group in
  `dispatchNoteThroughStack()` if it should behave like the other stack
  effects.
- Check `refreshStackProcessingState()` for special activation needs.
- Check `processBlock()` for special scheduling or tail needs.

### 7. Wire UI

In:

```text
src/TrackerMainUI.cpp
```

- Add the type to `isSimpleMachineDetail()` if using the generic detail page.
- Update enum-range logic such as `describeStackCursorAction()`.
- Add a custom detail branch only if the generic page is insufficient.

### 8. Wire control service

In:

```text
src/TrackerControlService.cpp
```

- Add the machine name and aliases to `commandTypeFromName()`.
- Update capabilities or tool descriptions if machine names are advertised.

### 9. Build and test

```sh
cmake --build build --target myk-tracker-plug_Standalone -j2
cmake --build build --target myk-tracker-plug_VST3 -j2
cmake --build build --target sequence-read-head-tests -j2
ctest --test-dir build
```

Report exactly which builds and tests were run. Do not say tests passed
without running them.

### 10. Do not commit

Unless explicitly instructed:

- Do not run `git add`.
- Do not run `git commit`.
- Do not use `git add -A`.
- Do not use `git commit -a`.
- Leave `build/`, `libs/JUCE`, and unrelated user changes untouched.

## Common mistakes

### Renumbering `CommandType`

This breaks persisted sequence rows and slot state.

### Forgetting `forEachMachine()`

The machine will not receive prepare, release, BPM, or `allNotesOff` updates.

### Forgetting `stackMachineStateEntries()`

The machine state will not be saved or restored.

### Forgetting hard-coded enum upper bounds

The new type may exist but UI descriptions, validation, or command handling
may still stop at the old last type.

### Overriding final `AudioEffectMachine` methods

This breaks the shared lifecycle:

```text
prepareToPlay() -> prepareDsp() -> clearTransientState()
releaseResources() -> clearTransientState()
```

### Double-locking in `clearTransientState()`

If `clearTransientState()` locks `stateMutex`, a caller that already holds
`stateMutex` must not call it. Use a private unlocked reset helper instead.

### Allocating in the audio path

Avoid vector growth, string construction, object creation, logging, file I/O,
or lock contention in `processBlock()` / `processAudioBuffer()`.

### Assuming old state has the new fields

Old saved states are valid input. Missing keys and missing machines must
restore safely.

### Using row-major UI grids

Tracker machine UI grids are column-major:

```text
outer vector = columns
inner vector = rows for that column
```

### Hard-coding labels in the editor

Use:

```text
machineTraits(type).shortLabel
machineTraits(type).longName
```

### Adding a new MCP tool too early

Most machines work with the existing generic machine-control path. Add a new
tool only for a genuinely new command category.

### Resurrecting legacy arpeggiators

Do not add:

```text
src/machines/ArpeggiatorMachine.cpp
src/machines/PolyArpeggiatorMachine.cpp
```

to the build, and do not extend legacy arpeggiator command types.

## Stop conditions

Stop and report if any of these are true:

- The task requires renumbering an existing `CommandType`.
- The task requires reusing a legacy arpeggiator value.
- The task requires adding legacy arpeggiator sources to CMake.
- The task requires changing the meaning of an existing persisted field.
- The task requires incompatible saved-state migration.
- The task requires allocation or blocking in the audio path.
- The task requires changing `AudioEffectMachine` final lifecycle in a way
  that breaks existing machines.
- The task requires modifying `libs/JUCE`.
- The task is really a shared bus redesign, not a normal stack machine.
- The task requires changing slot identity rules.

When stopping, report:

```text
What was completed.
What blocked the work.
Which files were touched.
Which builds/tests were run.
What decision is needed.
```
