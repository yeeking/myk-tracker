#include "WavetableSynthMachine.h"
#include "MachineStateCodec.h"

#include <cmath>

namespace
{
constexpr double kStateVersion = 1.0;
constexpr float kMaxAttackSeconds = 2.0f;
constexpr float kMaxDecaySeconds = 2.0f;
constexpr float kMaxReleaseSeconds = 3.0f;
constexpr float kMinCyclerRate = 0.1f;
constexpr float kMaxCyclerRate = 10.0f;
/** Minimum attack+decay time used in AD mode so an empty envelope cannot
    produce an infinite cycler speed. */
constexpr double kMinAdTimeSeconds = 0.005;
/** Oscillator detune range in cents (fine tuning around the octave offset
    pitch), shared by the main and sub oscillators. */
constexpr float kMinDetuneCents = -50.0f;
constexpr float kMaxDetuneCents = 50.0f;
constexpr float kMinEnvelopeBend = -2.0f;
constexpr float kMaxEnvelopeBend = 2.0f;
}

WavetableSynthMachine::WavetableSynthMachine()
{
    initialiseTables();
    updateVoiceEnvelopeParameters();
    updateMainDetuneMultiplier();
    updateSubDetuneMultiplier();
}

void WavetableSynthMachine::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    juce::ignoreUnused(samplesPerBlock);
    const std::lock_guard<std::mutex> lock(stateMutex);
    currentSampleRate = sanitizedSampleRate(sampleRate);
    updateVoiceEnvelopeParameters();
}

void WavetableSynthMachine::releaseResources()
{
    allNotesOff();
}

void WavetableSynthMachine::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ignoreUnused(midi);
    const std::lock_guard<std::mutex> lock(stateMutex);

    const int numSamples = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();
    if (numSamples <= 0 || numChannels <= 0)
        return;

    const std::vector<MachineScheduledNote>* blockNotes = scheduledBlockNotes;
    const int numScheduledNotes = (blockNotes != nullptr) ? static_cast<int>(blockNotes->size()) : 0;
    int nextScheduledNote = 0;

    // stateMutex is held for the whole block, so cycler settings and envelope
    // times cannot change mid-block; the per-sample phase delta is computed
    // once here.
    double cyclerDelta = 0.0;
    if (cyclerMode == CyclerMode::cps)
        cyclerDelta = static_cast<double>(cyclerRate) / currentSampleRate;
    else
    {
        const double adTime = juce::jmax(kMinAdTimeSeconds,
            static_cast<double>(attackSeconds) + static_cast<double>(decaySeconds));
        cyclerDelta = static_cast<double>(cyclerRate) / (adTime * currentSampleRate);
    }

    for (int sample = 0; sample < numSamples; ++sample)
    {
        // Advance the shared cycler only while a voice is sounding; the phase
        // is kept across silences so the next note resumes mid-cycle.
        bool anyVoiceActive = false;
        for (const auto& voice : voices)
            if (voice.active)
            {
                anyVoiceActive = true;
                break;
            }
        if (anyVoiceActive)
        {
            cyclerPhase += cyclerDelta;
            cyclerPhase -= std::floor(cyclerPhase);
        }

        // Apply sequencer note events scheduled for this sample before rendering it,
        // so triggered voices start exactly on their tick's sample.
        while (nextScheduledNote < numScheduledNotes
            && (*blockNotes)[static_cast<std::size_t>(nextScheduledNote)].sampleOffset <= sample)
        {
            const MachineScheduledNote& event = (*blockNotes)[static_cast<std::size_t>(nextScheduledNote)];
            auto& voice = allocateVoice();
            voice.midiNote = static_cast<int>(event.note);
            voice.velocity = juce::jlimit(0.0f, 1.0f, static_cast<float>(event.velocity) / 127.0f);
            voice.phase = 0.0;
            // Main oscillator pitch: note frequency scaled by the main octave
            // offset and cached detune multiplier (both unity by default).
            voice.phaseDelta = juce::MidiMessage::getMidiNoteInHertz(static_cast<int>(event.note))
                * std::ldexp(1.0, mainOctaveOffset)
                * mainDetuneMultiplier
                / currentSampleRate;
            // The sub rides the same voice, so its allocation, stealing and
            // release are governed by the same round-robin pool; its base
            // pitch is the main oscillator's offset+detuned pitch, further
            // scaled by the sub octave offset and detune.
            voice.subPhase = 0.0;
            voice.subPhaseDelta = voice.phaseDelta * std::ldexp(1.0, subOctaveOffset)
                * subDetuneMultiplier;
            voice.ageSamples = 0;
            voice.noteDurationSamples = juce::jmax(1, static_cast<int>(std::lround(currentSampleRate
                * currentSecondsPerTick
                * static_cast<double>(juce::jmax(1, static_cast<int>(event.durationTicks))))));
            voice.samplesUntilRelease = voice.noteDurationSamples;
            voice.releaseStarted = false;
            voice.active = true;
            voice.envelope.reset();
            voice.envelope.noteOn();
            ++nextScheduledNote;
        }

        float outputSample = 0.0f;

        for (auto& voice : voices)
        {
            if (!voice.active)
                continue;

            if (!voice.releaseStarted && voice.samplesUntilRelease <= 0)
            {
                voice.envelope.noteOff();
                voice.releaseStarted = true;
            }

            outputSample += sampleVoice(voice);

            ++voice.ageSamples;
            if (!voice.releaseStarted)
                --voice.samplesUntilRelease;

            if (!voice.envelope.isActive())
            {
                voice.active = false;
                voice.midiNote = -1;
            }
        }

        outputSample *= 0.2f;
        for (int channel = 0; channel < numChannels; ++channel)
            buffer.addSample(channel, sample, outputSample);
    }
}

std::vector<std::vector<UIBox>> WavetableSynthMachine::getUIBoxes(const MachineUiContext& context)
{
    juce::ignoreUnused(context);
    const std::lock_guard<std::mutex> lock(stateMutex);

    // Three parallel blocks: the main oscillator (columns 0-1), the
    // sub-oscillator (columns 2-3) and the ADSR envelope (columns 4-5).
    // Control rows line up across the main and sub blocks so the W1..Wn and
    // SW1..SWs wave steps sit directly beside each other from row 5 down,
    // followed by the two cycler rows (MODE, CYC) below the main waves.
    const std::size_t rows = static_cast<std::size_t>(
        juce::jmax(7 + waveStepCount, 5 + subWaveStepCount));
    std::vector<std::vector<UIBox>> boxes(6, std::vector<UIBox>(rows));

    auto makeValueCell = [this](float* target, float step, float minValue, float maxValue, int decimals)
    {
        return makeFloatCell(*target, minValue, maxValue, decimals,
            [this, target, step, minValue, maxValue](int direction)
            {
                const std::lock_guard<std::mutex> guard(stateMutex);
                *target = juce::jlimit(minValue, maxValue, *target + (step * static_cast<float>(direction)));
                updateVoiceEnvelopeParameters();
            });
    };

    boxes[0][0].kind = UIBox::Kind::TrackerCell;
    boxes[0][0].text = "SOURCE";
    boxes[1][0].kind = UIBox::Kind::None;
    boxes[1][0].isDisabled = true;
    boxes[0][1].kind = UIBox::Kind::TrackerCell;
    boxes[0][1].text = "STEPS";
    boxes[1][1].kind = UIBox::Kind::TrackerCell;
    boxes[1][1].text = std::to_string(waveStepCount);
    boxes[1][1].onAdjust = [this](int direction)
    {
        const std::lock_guard<std::mutex> guard(stateMutex);
        waveStepCount = juce::jlimit(1, kMaxWaveSteps, waveStepCount + direction);
    };
    boxes[1][1].hasValueScale = true;
    boxes[1][1].valueNorm = juce::jlimit(0.0f, 1.0f,
        static_cast<float>(waveStepCount - 1) / static_cast<float>(kMaxWaveSteps - 1));

    for (std::size_t col = 0; col < boxes.size(); ++col)
        for (std::size_t row = 2; row < rows; ++row)
        {
            boxes[col][row].kind = UIBox::Kind::None;
            boxes[col][row].isDisabled = true;
        }

    // Main oscillator level / octave / detune, mirroring the sub-osc block
    // so the control rows line up across the two blocks.
    boxes[0][2].kind = UIBox::Kind::TrackerCell;
    boxes[0][2].text = "LEV";
    boxes[1][2] = makeValueCell(&mainLevel, 0.05f, 0.0f, 1.0f, 2);

    boxes[0][3].kind = UIBox::Kind::TrackerCell;
    boxes[0][3].text = "OCT";
    boxes[1][3].isDisabled = false;
    boxes[1][3].kind = UIBox::Kind::TrackerCell;
    boxes[1][3].text = (mainOctaveOffset > 0) ? ("+" + std::to_string(mainOctaveOffset))
                                              : std::to_string(mainOctaveOffset);
    boxes[1][3].onAdjust = [this](int direction)
    {
        const std::lock_guard<std::mutex> guard(stateMutex);
        int next = mainOctaveOffset + direction;
        if (next > 3)
            next = -3;
        if (next < -3)
            next = 3;
        mainOctaveOffset = next;
    };
    boxes[1][3].hasValueScale = true;
    boxes[1][3].valueNorm = static_cast<float>(mainOctaveOffset + 3) / 6.0f;

    boxes[0][4].kind = UIBox::Kind::TrackerCell;
    boxes[0][4].text = "DET";
    boxes[1][4].isDisabled = false;
    boxes[1][4].kind = UIBox::Kind::TrackerCell;
    const int mainDetuneCentsDisplay = static_cast<int>(std::lround(mainDetuneCents));
    boxes[1][4].text = std::string(mainDetuneCentsDisplay > 0 ? "+" : "") + std::to_string(mainDetuneCentsDisplay);
    boxes[1][4].onAdjust = [this](int direction)
    {
        const std::lock_guard<std::mutex> guard(stateMutex);
        mainDetuneCents = juce::jlimit(kMinDetuneCents, kMaxDetuneCents,
            mainDetuneCents + (1.0f * static_cast<float>(direction)));
        updateMainDetuneMultiplier();
    };
    boxes[1][4].hasValueScale = true;
    boxes[1][4].valueNorm = juce::jlimit(0.0f, 1.0f,
        (mainDetuneCents - kMinDetuneCents) / (kMaxDetuneCents - kMinDetuneCents));

    for (int stepIndex = 0; stepIndex < waveStepCount; ++stepIndex)
    {
        const std::size_t row = static_cast<std::size_t>(stepIndex + 5);
        // The pre-pass above marks these rows disabled; re-enable them so the
        // cursor can land on the waveform selector cells.
        boxes[0][row].isDisabled = false;
        boxes[0][row].kind = UIBox::Kind::TrackerCell;
        boxes[0][row].text = "W" + std::to_string(stepIndex + 1);
        boxes[1][row].isDisabled = false;
        boxes[1][row].kind = UIBox::Kind::TrackerCell;
        boxes[1][row].text = getWaveformName(waveSteps[static_cast<std::size_t>(stepIndex)]);
        boxes[1][row].onAdjust = [this, stepIndex](int direction)
        {
            const std::lock_guard<std::mutex> guard(stateMutex);
            int next = static_cast<int>(waveSteps[static_cast<std::size_t>(stepIndex)]) + direction;
            if (next < 0)
                next = static_cast<int>(Waveform::count) - 1;
            if (next >= static_cast<int>(Waveform::count))
                next = 0;
            waveSteps[static_cast<std::size_t>(stepIndex)] = static_cast<Waveform>(next);
        };
    }

    // Cycler controls: shared-phase clock mode and rate (cycles/second in
    // CPS mode, cycles per attack+decay time in AD mode).
    const std::size_t modeRow = static_cast<std::size_t>(waveStepCount + 5);
    const std::size_t cycRow = static_cast<std::size_t>(waveStepCount + 6);

    boxes[0][modeRow].isDisabled = false;
    boxes[0][modeRow].kind = UIBox::Kind::TrackerCell;
    boxes[0][modeRow].text = "MODE";
    boxes[1][modeRow].isDisabled = false;
    boxes[1][modeRow].kind = UIBox::Kind::TrackerCell;
    boxes[1][modeRow].text = (cyclerMode == CyclerMode::ad) ? "AD" : "CPS";
    boxes[1][modeRow].onAdjust = [this](int direction)
    {
        juce::ignoreUnused(direction);
        const std::lock_guard<std::mutex> guard(stateMutex);
        cyclerMode = (cyclerMode == CyclerMode::ad) ? CyclerMode::cps : CyclerMode::ad;
    };
    boxes[1][modeRow].hasValueScale = true;
    boxes[1][modeRow].valueNorm = (cyclerMode == CyclerMode::ad) ? 0.0f : 1.0f;

    boxes[0][cycRow].isDisabled = false;
    boxes[0][cycRow].kind = UIBox::Kind::TrackerCell;
    boxes[0][cycRow].text = "CYC";
    boxes[1][cycRow] = makeValueCell(&cyclerRate, 0.1f, kMinCyclerRate, kMaxCyclerRate, 1);

    // ADSR block (columns 4-5).
    boxes[4][0].kind = UIBox::Kind::TrackerCell;
    boxes[4][0].text = "ENV";
    boxes[5][0].kind = UIBox::Kind::None;
    boxes[5][0].isDisabled = true;

    constexpr std::array<const char*, 4> envLabels { "A", "D", "S", "R" };
    const std::array<UIBox, 4> envValueCells {
        makeValueCell(&attackSeconds, 0.01f, 0.0f, kMaxAttackSeconds, 2),
        makeValueCell(&decaySeconds, 0.01f, 0.0f, kMaxDecaySeconds, 2),
        makeValueCell(&sustainLevel, 0.05f, 0.0f, 1.0f, 2),
        makeValueCell(&releaseSeconds, 0.01f, 0.0f, kMaxReleaseSeconds, 2)
    };

    for (std::size_t envIndex = 0; envIndex < envLabels.size(); ++envIndex)
    {
        const std::size_t row = envIndex + 1;
        boxes[4][row].kind = UIBox::Kind::TrackerCell;
        boxes[4][row].text = envLabels[envIndex];
        boxes[5][row] = envValueCells[envIndex];
    }

    boxes[4][5].kind = UIBox::Kind::TrackerCell;
    boxes[4][5].text = "BEND";
    boxes[5][5] = makeValueCell(&envelopeBend, 0.05f, kMinEnvelopeBend, kMaxEnvelopeBend, 2);

    // Sub-oscillator block (columns 2-3): header, step count, mix level,
    // octave offset, detune, then one row per sub wave step. The control
    // rows mirror the main oscillator's LEV/OCT/DET and the wave step rows
    // start at row 5, directly beside the main W1..Wn rows.
    boxes[2][0].kind = UIBox::Kind::TrackerCell;
    boxes[2][0].text = "SUB";
    boxes[3][0].kind = UIBox::Kind::None;
    boxes[3][0].isDisabled = true;

    boxes[2][1].kind = UIBox::Kind::TrackerCell;
    boxes[2][1].text = "STEPS";
    boxes[3][1].isDisabled = false;
    boxes[3][1].kind = UIBox::Kind::TrackerCell;
    boxes[3][1].text = std::to_string(subWaveStepCount);
    boxes[3][1].onAdjust = [this](int direction)
    {
        const std::lock_guard<std::mutex> guard(stateMutex);
        subWaveStepCount = juce::jlimit(1, kMaxWaveSteps, subWaveStepCount + direction);
    };
    boxes[3][1].hasValueScale = true;
    boxes[3][1].valueNorm = juce::jlimit(0.0f, 1.0f,
        static_cast<float>(subWaveStepCount - 1) / static_cast<float>(kMaxWaveSteps - 1));

    boxes[2][2].kind = UIBox::Kind::TrackerCell;
    boxes[2][2].text = "SLEV";
    boxes[3][2] = makeValueCell(&subLevel, 0.05f, 0.0f, 1.0f, 2);

    boxes[2][3].kind = UIBox::Kind::TrackerCell;
    boxes[2][3].text = "SOCT";
    boxes[3][3].isDisabled = false;
    boxes[3][3].kind = UIBox::Kind::TrackerCell;
    boxes[3][3].text = (subOctaveOffset > 0) ? ("+" + std::to_string(subOctaveOffset))
                                              : std::to_string(subOctaveOffset);
    boxes[3][3].onAdjust = [this](int direction)
    {
        const std::lock_guard<std::mutex> guard(stateMutex);
        int next = subOctaveOffset + direction;
        if (next > 3)
            next = -3;
        if (next < -3)
            next = 3;
        subOctaveOffset = next;
    };
    boxes[3][3].hasValueScale = true;
    boxes[3][3].valueNorm = static_cast<float>(subOctaveOffset + 3) / 6.0f;

    boxes[2][4].kind = UIBox::Kind::TrackerCell;
    boxes[2][4].text = "SDET";
    boxes[3][4].isDisabled = false;
    boxes[3][4].kind = UIBox::Kind::TrackerCell;
    const int detuneCents = static_cast<int>(std::lround(subDetuneCents));
    boxes[3][4].text = std::string(detuneCents > 0 ? "+" : "") + std::to_string(detuneCents);
    boxes[3][4].onAdjust = [this](int direction)
    {
        const std::lock_guard<std::mutex> guard(stateMutex);
        subDetuneCents = juce::jlimit(kMinDetuneCents, kMaxDetuneCents,
            subDetuneCents + (1.0f * static_cast<float>(direction)));
        updateSubDetuneMultiplier();
    };
    boxes[3][4].hasValueScale = true;
    boxes[3][4].valueNorm = juce::jlimit(0.0f, 1.0f,
        (subDetuneCents - kMinDetuneCents) / (kMaxDetuneCents - kMinDetuneCents));

    for (int stepIndex = 0; stepIndex < subWaveStepCount; ++stepIndex)
    {
        const std::size_t row = static_cast<std::size_t>(stepIndex + 5);
        boxes[2][row].isDisabled = false;
        boxes[2][row].kind = UIBox::Kind::TrackerCell;
        boxes[2][row].text = "SW" + std::to_string(stepIndex + 1);
        boxes[3][row].isDisabled = false;
        boxes[3][row].kind = UIBox::Kind::TrackerCell;
        boxes[3][row].text = getWaveformName(subWaveSteps[static_cast<std::size_t>(stepIndex)]);
        boxes[3][row].onAdjust = [this, stepIndex](int direction)
        {
            const std::lock_guard<std::mutex> guard(stateMutex);
            int next = static_cast<int>(subWaveSteps[static_cast<std::size_t>(stepIndex)]) + direction;
            if (next < 0)
                next = static_cast<int>(Waveform::count) - 1;
            if (next >= static_cast<int>(Waveform::count))
                next = 0;
            subWaveSteps[static_cast<std::size_t>(stepIndex)] = static_cast<Waveform>(next);
        };
    }

    return boxes;
}

int WavetableSynthMachine::getWaveStepCount() const
{
    const std::lock_guard<std::mutex> lock(stateMutex);
    return waveStepCount;
}

int WavetableSynthMachine::getWaveStepWaveform(int stepIndex) const
{
    const std::lock_guard<std::mutex> lock(stateMutex);
    if (stepIndex < 0 || stepIndex >= waveStepCount)
        return 0;
    return static_cast<int>(waveSteps[static_cast<std::size_t>(stepIndex)]);
}

int WavetableSynthMachine::getSubWaveStepCount() const
{
    const std::lock_guard<std::mutex> lock(stateMutex);
    return subWaveStepCount;
}

int WavetableSynthMachine::getSubWaveStepWaveform(int stepIndex) const
{
    const std::lock_guard<std::mutex> lock(stateMutex);
    if (stepIndex < 0 || stepIndex >= subWaveStepCount)
        return 0;
    return static_cast<int>(subWaveSteps[static_cast<std::size_t>(stepIndex)]);
}

int WavetableSynthMachine::getSubOctaveOffset() const
{
    const std::lock_guard<std::mutex> lock(stateMutex);
    return subOctaveOffset;
}

float WavetableSynthMachine::sampleWaveformForUi(int waveformIndex, double phase) const
{
    if (waveformIndex < 0 || waveformIndex >= static_cast<int>(Waveform::count))
        return 0.0f;
    // Waveform tables are immutable after construction; the lock only keeps the
    // step selection coherent with concurrent UI edits.
    return sampleWaveform(static_cast<Waveform>(waveformIndex), phase);
}

void WavetableSynthMachine::getEnvelopeSettings(float& attack, float& decay, float& sustain,
                                                float& release, float& bend,
                                                float& maxAttack, float& maxDecay,
                                                float& maxRelease) const
{
    const std::lock_guard<std::mutex> lock(stateMutex);
    attack = attackSeconds;
    decay = decaySeconds;
    sustain = sustainLevel;
    release = releaseSeconds;
    bend = envelopeBend;
    maxAttack = kMaxAttackSeconds;
    maxDecay = kMaxDecaySeconds;
    maxRelease = kMaxReleaseSeconds;
}

bool WavetableSynthMachine::handleIncomingNote(unsigned short note,
                                               unsigned short velocity,
                                               unsigned short durationTicks,
                                               MachineNoteEvent& outEvent)
{
    // Sequencer notes now arrive via scheduleBlockNotes and are applied
    // sample-accurately inside processBlock; this direct path is unused.
    juce::ignoreUnused(note, velocity, durationTicks, outEvent);
    return false;
}

void WavetableSynthMachine::scheduleBlockNotes(const std::vector<MachineScheduledNote>& notes)
{
    // Called on the audio thread immediately before processBlock; the referenced
    // vector is processor-owned per-stack storage that stays valid until the
    // next block's distribution.
    scheduledBlockNotes = &notes;
}

void WavetableSynthMachine::setSecondsPerTick(double secondsPerTick)
{
    const std::lock_guard<std::mutex> lock(stateMutex);
    if (secondsPerTick > 0.0)
        currentSecondsPerTick = secondsPerTick;
}

void WavetableSynthMachine::allNotesOff()
{
    const std::lock_guard<std::mutex> lock(stateMutex);
    for (auto& voice : voices)
    {
        voice.envelope.reset();
        voice.midiNote = -1;
        voice.velocity = 0.0f;
        voice.phase = 0.0;
        voice.phaseDelta = 0.0;
        voice.subPhase = 0.0;
        voice.subPhaseDelta = 0.0;
        voice.ageSamples = 0;
        voice.noteDurationSamples = 0;
        voice.samplesUntilRelease = 0;
        voice.releaseStarted = false;
        voice.active = false;
    }
}

void WavetableSynthMachine::getStateInformation(juce::MemoryBlock& destData)
{
    const std::lock_guard<std::mutex> lock(stateMutex);

    juce::DynamicObject::Ptr root = new juce::DynamicObject();
    root->setProperty("version", kStateVersion);
    root->setProperty("attack", attackSeconds);
    root->setProperty("decay", decaySeconds);
    root->setProperty("sustain", sustainLevel);
    root->setProperty("release", releaseSeconds);
    root->setProperty("bend", envelopeBend);
    root->setProperty("waveStepCount", waveStepCount);
    root->setProperty("cyclerMode", static_cast<int>(cyclerMode));
    root->setProperty("cyclerRate", cyclerRate);
    root->setProperty("mainLevel", mainLevel);
    root->setProperty("mainOctave", mainOctaveOffset);
    root->setProperty("mainDetune", mainDetuneCents);

    juce::Array<juce::var> waveArray;
    for (int i = 0; i < kMaxWaveSteps; ++i)
        waveArray.add(static_cast<int>(waveSteps[static_cast<std::size_t>(i)]));
    root->setProperty("waves", waveArray);

    root->setProperty("subWaveStepCount", subWaveStepCount);
    root->setProperty("subOctave", subOctaveOffset);
    root->setProperty("subLevel", subLevel);
    root->setProperty("subDetune", subDetuneCents);

    juce::Array<juce::var> subWaveArray;
    for (int i = 0; i < kMaxWaveSteps; ++i)
        subWaveArray.add(static_cast<int>(subWaveSteps[static_cast<std::size_t>(i)]));
    root->setProperty("subWaves", subWaveArray);

    writeMachineStateJson(destData, juce::var(root.get()));
}

void WavetableSynthMachine::setStateInformation(const void* data, int sizeInBytes)
{
    const std::lock_guard<std::mutex> lock(stateMutex);
    const juce::var parsed = parseMachineStateJson(data, sizeInBytes);
    if (parsed.isVoid())
        return;

    const auto* obj = parsed.getDynamicObject();
    attackSeconds = getFloatProperty(parsed, "attack", attackSeconds, 0.0f, kMaxAttackSeconds);
    decaySeconds = getFloatProperty(parsed, "decay", decaySeconds, 0.0f, kMaxDecaySeconds);
    sustainLevel = getFloatProperty(parsed, "sustain", sustainLevel, 0.0f, 1.0f);
    releaseSeconds = getFloatProperty(parsed, "release", releaseSeconds, 0.0f, kMaxReleaseSeconds);
    envelopeBend = getFloatProperty(parsed, "bend", envelopeBend, kMinEnvelopeBend, kMaxEnvelopeBend);
    if (!std::isfinite(envelopeBend))
        envelopeBend = 0.0f;
    waveStepCount = getIntProperty(parsed, "waveStepCount", waveStepCount, 1, kMaxWaveSteps);
    // Only the two known mode values are accepted; anything else falls back to
    // CPS so corrupted or foreign state cannot pick an invalid cycler clock.
    const int cyclerModeValue = static_cast<int>(parsed.getProperty("cyclerMode", static_cast<int>(cyclerMode)));
    cyclerMode = (cyclerModeValue == static_cast<int>(CyclerMode::ad)) ? CyclerMode::ad : CyclerMode::cps;
    cyclerRate = getFloatProperty(parsed, "cyclerRate", cyclerRate, kMinCyclerRate, kMaxCyclerRate);
    mainLevel = getFloatProperty(parsed, "mainLevel", mainLevel, 0.0f, 1.0f);
    // Only the seven known octave offsets (-3..+3) are accepted; anything
    // else falls back to the note's own octave.
    const int mainOctaveValue = static_cast<int>(parsed.getProperty("mainOctave", mainOctaveOffset));
    mainOctaveOffset = (mainOctaveValue >= -3 && mainOctaveValue <= 3) ? mainOctaveValue : 0;
    mainDetuneCents = getFloatProperty(parsed, "mainDetune", mainDetuneCents, kMinDetuneCents, kMaxDetuneCents);

    const auto wavesVar = obj->getProperty("waves");
    if (wavesVar.isArray())
    {
        const auto* array = wavesVar.getArray();
        if (array != nullptr)
        {
            for (int i = 0; i < array->size() && i < kMaxWaveSteps; ++i)
            {
                const int waveIndex = juce::jlimit(0, static_cast<int>(Waveform::count) - 1, static_cast<int>((*array)[i]));
                waveSteps[static_cast<std::size_t>(i)] = static_cast<Waveform>(waveIndex);
            }
        }
    }

    subWaveStepCount = getIntProperty(parsed, "subWaveStepCount", subWaveStepCount, 1, kMaxWaveSteps);
    // Only the seven known octave offsets (-3..+3) are accepted; anything
    // else falls back to the default octave-down position.
    const int subOctaveValue = static_cast<int>(parsed.getProperty("subOctave", subOctaveOffset));
    subOctaveOffset = (subOctaveValue >= -3 && subOctaveValue <= 3) ? subOctaveValue : -1;
    subLevel = getFloatProperty(parsed, "subLevel", subLevel, 0.0f, 1.0f);
    subDetuneCents = getFloatProperty(parsed, "subDetune", subDetuneCents, kMinDetuneCents, kMaxDetuneCents);

    const auto subWavesVar = obj->getProperty("subWaves");
    if (subWavesVar.isArray())
    {
        const auto* array = subWavesVar.getArray();
        if (array != nullptr)
        {
            for (int i = 0; i < array->size() && i < kMaxWaveSteps; ++i)
            {
                const int waveIndex = juce::jlimit(0, static_cast<int>(Waveform::count) - 1, static_cast<int>((*array)[i]));
                subWaveSteps[static_cast<std::size_t>(i)] = static_cast<Waveform>(waveIndex);
            }
        }
    }

    updateMainDetuneMultiplier();
    updateSubDetuneMultiplier();
    updateVoiceEnvelopeParameters();
}

void WavetableSynthMachine::initialiseTables()
{
    for (int i = 0; i < kTableSize; ++i)
    {
        const float phase = static_cast<float>(i) / static_cast<float>(kTableSize);
        tables[static_cast<std::size_t>(Waveform::sine)][static_cast<std::size_t>(i)] = std::sin(juce::MathConstants<float>::twoPi * phase);
        tables[static_cast<std::size_t>(Waveform::triangle)][static_cast<std::size_t>(i)] = 1.0f - (4.0f * std::abs(phase - 0.5f));
        tables[static_cast<std::size_t>(Waveform::saw)][static_cast<std::size_t>(i)] = (2.0f * phase) - 1.0f;
        tables[static_cast<std::size_t>(Waveform::square)][static_cast<std::size_t>(i)] = phase < 0.5f ? 1.0f : -1.0f;
    }

    // Additive waveforms: sums of equal-amplitude sine partials. Each table
    // is peak-normalised so every waveform plays at a comparable level to
    // the basic four.
    auto fillAdditiveTable = [this](Waveform waveform, const int* harmonics, int harmonicCount)
    {
        auto& table = tables[static_cast<std::size_t>(waveform)];
        float peak = 0.0f;
        for (int i = 0; i < kTableSize; ++i)
        {
            const float phase = static_cast<float>(i) / static_cast<float>(kTableSize);
            float sum = 0.0f;
            for (int h = 0; h < harmonicCount; ++h)
                sum += std::sin(juce::MathConstants<float>::twoPi * phase * static_cast<float>(harmonics[h]));
            table[static_cast<std::size_t>(i)] = sum;
            peak = std::max(peak, std::abs(sum));
        }
        if (peak > 0.0f)
            for (int i = 0; i < kTableSize; ++i)
                table[static_cast<std::size_t>(i)] /= peak;
    };

    static const int add1Harmonics[] = { 1, 2 };
    static const int add2Harmonics[] = { 1, 2, 3 };
    static const int add10Harmonics[] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10 };
    static const int eve5Harmonics[] = { 1, 2, 4, 6, 8 };
    static const int odd5Harmonics[] = { 1, 3, 5, 7, 9 };
    fillAdditiveTable(Waveform::add1, add1Harmonics, 2);
    fillAdditiveTable(Waveform::add2, add2Harmonics, 3);
    fillAdditiveTable(Waveform::add10, add10Harmonics, 10);
    fillAdditiveTable(Waveform::eve5, eve5Harmonics, 5);
    fillAdditiveTable(Waveform::odd5, odd5Harmonics, 5);
}

void WavetableSynthMachine::updateSubDetuneMultiplier()
{
    subDetuneMultiplier = std::pow(2.0, static_cast<double>(subDetuneCents) / 1200.0);
}

void WavetableSynthMachine::updateMainDetuneMultiplier()
{
    mainDetuneMultiplier = std::pow(2.0, static_cast<double>(mainDetuneCents) / 1200.0);
}

void WavetableSynthMachine::updateVoiceEnvelopeParameters()
{
    CurvedAdsr::Parameters parameters;
    parameters.attack = attackSeconds;
    parameters.decay = decaySeconds;
    parameters.sustain = sustainLevel;
    parameters.release = releaseSeconds;
    parameters.bend = envelopeBend;

    for (auto& voice : voices)
    {
        voice.envelope.setSampleRate(currentSampleRate);
        voice.envelope.setParameters(parameters);
    }
}

WavetableSynthMachine::Voice& WavetableSynthMachine::allocateVoice()
{
    for (int attempt = 0; attempt < kVoiceCount; ++attempt)
    {
        auto& voice = voices[static_cast<std::size_t>(nextVoiceIndex)];
        nextVoiceIndex = (nextVoiceIndex + 1) % kVoiceCount;
        if (!voice.active)
            return voice;
    }

    auto& stolenVoice = voices[static_cast<std::size_t>(nextVoiceIndex)];
    nextVoiceIndex = (nextVoiceIndex + 1) % kVoiceCount;
    stolenVoice.envelope.reset();
    return stolenVoice;
}

float WavetableSynthMachine::sampleVoice(Voice& voice) const
{
    // All voices read the same shared cycler phase, so polyphonic notes stay
    // on the same waveform step and crossfade together. The cycle wraps from
    // the last step back to the first; released notes keep cycling.
    const int stepCount = juce::jmax(1, waveStepCount);
    const double stepPosition = cyclerPhase * static_cast<double>(stepCount);
    const int baseStep = static_cast<int>(std::floor(stepPosition)) % stepCount;
    const int nextStep = (baseStep + 1) % stepCount;
    const float morph = static_cast<float>(stepPosition - std::floor(stepPosition));

    const auto currentWave = waveSteps[static_cast<std::size_t>(baseStep)];
    const auto nextWave = waveSteps[static_cast<std::size_t>(nextStep)];
    const float sampleA = sampleWaveform(currentWave, voice.phase);
    const float sampleB = sampleWaveform(nextWave, voice.phase);
    float mixedSample = juce::jmap(morph, sampleA, sampleB) * mainLevel;
    const float envelopeSample = voice.envelope.getNextSample();

    voice.phase += voice.phaseDelta;
    voice.phase -= std::floor(voice.phase);

    if (subLevel > 0.0f)
    {
        // The sub shares the cycler phase, normalised over its own step
        // count, so it tracks the main oscillator's morph position.
        const int subStepCount = juce::jmax(1, subWaveStepCount);
        const double subStepPosition = cyclerPhase * static_cast<double>(subStepCount);
        const int subBaseStep = static_cast<int>(std::floor(subStepPosition)) % subStepCount;
        const int subNextStep = (subBaseStep + 1) % subStepCount;
        const float subMorph = static_cast<float>(subStepPosition - std::floor(subStepPosition));

        const float subSampleA = sampleWaveform(subWaveSteps[static_cast<std::size_t>(subBaseStep)], voice.subPhase);
        const float subSampleB = sampleWaveform(subWaveSteps[static_cast<std::size_t>(subNextStep)], voice.subPhase);
        mixedSample += juce::jmap(subMorph, subSampleA, subSampleB) * subLevel;

        voice.subPhase += voice.subPhaseDelta;
        voice.subPhase -= std::floor(voice.subPhase);
    }

    return mixedSample * envelopeSample * voice.velocity;
}

float WavetableSynthMachine::sampleWaveform(WavetableSynthMachine::Waveform waveform, double phase) const
{
    const auto& table = tables[static_cast<std::size_t>(waveform)];
    const double wrappedPhase = phase - std::floor(phase);
    const double tablePosition = wrappedPhase * static_cast<double>(kTableSize);
    const int indexA = static_cast<int>(tablePosition) % kTableSize;
    const int indexB = (indexA + 1) % kTableSize;
    const float mix = static_cast<float>(tablePosition - std::floor(tablePosition));
    return juce::jmap(mix, table[static_cast<std::size_t>(indexA)], table[static_cast<std::size_t>(indexB)]);
}

const char* WavetableSynthMachine::getWaveformName(WavetableSynthMachine::Waveform waveform)
{
    switch (waveform)
    {
        case Waveform::sine: return "SIN";
        case Waveform::triangle: return "TRI";
        case Waveform::saw: return "SAW";
        case Waveform::square: return "SQR";
        case Waveform::add1: return "ADD1";
        case Waveform::add2: return "ADD2";
        case Waveform::add10: return "ADD10";
        case Waveform::eve5: return "EVE5";
        case Waveform::odd5: return "ODD5";
        default: return "---";
    }
}
