#include "WavetableSynthMachine.h"

#include <cmath>

namespace
{
constexpr double kStateVersion = 1.0;
constexpr float kMaxAttackSeconds = 2.0f;
constexpr float kMaxDecaySeconds = 2.0f;
constexpr float kMaxReleaseSeconds = 3.0f;
}

WavetableSynthMachine::WavetableSynthMachine()
{
    initialiseTables();
    updateVoiceEnvelopeParameters();
}

void WavetableSynthMachine::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    juce::ignoreUnused(samplesPerBlock);
    const std::lock_guard<std::mutex> lock(stateMutex);
    currentSampleRate = sampleRate > 0.0 ? sampleRate : 44100.0;
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

    for (int sample = 0; sample < numSamples; ++sample)
    {
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

    const std::size_t rows = static_cast<std::size_t>(juce::jmax(5, waveStepCount + 2));
    std::vector<std::vector<UIBox>> boxes(5, std::vector<UIBox>(rows));

    auto makeValueCell = [this](float* target, float step, float minValue, float maxValue, int decimals)
    {
        UIBox cell;
        cell.kind = UIBox::Kind::TrackerCell;
        cell.text = formatFloat(*target, decimals);
        cell.onAdjust = [this, target, step, minValue, maxValue](int direction)
        {
            const std::lock_guard<std::mutex> guard(stateMutex);
            *target = juce::jlimit(minValue, maxValue, *target + (step * static_cast<float>(direction)));
            updateVoiceEnvelopeParameters();
        };
        return cell;
    };

    boxes[0][0].kind = UIBox::Kind::TrackerCell;
    boxes[0][0].text = "SOURCE";
    boxes[0][1].kind = UIBox::Kind::TrackerCell;
    boxes[0][1].text = "STEPS";
    boxes[1][1].kind = UIBox::Kind::TrackerCell;
    boxes[1][1].text = std::to_string(waveStepCount);
    boxes[1][1].onAdjust = [this](int direction)
    {
        const std::lock_guard<std::mutex> guard(stateMutex);
        waveStepCount = juce::jlimit(1, kMaxWaveSteps, waveStepCount + direction);
    };
    boxes[3][0].kind = UIBox::Kind::TrackerCell;
    boxes[3][0].text = "ENV";
    boxes[4][0].kind = UIBox::Kind::None;
    boxes[4][0].isDisabled = true;

    for (std::size_t row = 2; row < rows; ++row)
    {
        boxes[0][row].kind = UIBox::Kind::None;
        boxes[0][row].isDisabled = true;
        boxes[1][row].kind = UIBox::Kind::None;
        boxes[1][row].isDisabled = true;
        boxes[2][row].kind = UIBox::Kind::None;
        boxes[2][row].isDisabled = true;
        boxes[3][row].kind = UIBox::Kind::None;
        boxes[3][row].isDisabled = true;
        boxes[4][row].kind = UIBox::Kind::None;
        boxes[4][row].isDisabled = true;
    }

    for (int stepIndex = 0; stepIndex < waveStepCount; ++stepIndex)
    {
        const std::size_t row = static_cast<std::size_t>(stepIndex + 2);
        boxes[0][row].kind = UIBox::Kind::TrackerCell;
        boxes[0][row].text = "W" + std::to_string(stepIndex + 1);
        boxes[1][row].kind = UIBox::Kind::TrackerCell;
        boxes[1][row].text = getWaveformName(waveSteps[static_cast<std::size_t>(stepIndex)]);
        boxes[1][row].onAdjust = [this, stepIndex](int direction)
        {
            const std::lock_guard<std::mutex> guard(stateMutex);
            int next = static_cast<int>(waveSteps[static_cast<std::size_t>(stepIndex)]) + direction;
            if (next < 0)
                next = static_cast<int>(Waveform::square);
            if (next > static_cast<int>(Waveform::square))
                next = 0;
            waveSteps[static_cast<std::size_t>(stepIndex)] = static_cast<Waveform>(next);
        };
    }

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
        boxes[3][row].kind = UIBox::Kind::TrackerCell;
        boxes[3][row].text = envLabels[envIndex];
        boxes[4][row] = envValueCells[envIndex];
    }

    return boxes;
}

bool WavetableSynthMachine::handleIncomingNote(unsigned short note,
                                               unsigned short velocity,
                                               unsigned short durationTicks,
                                               MachineNoteEvent& outEvent)
{
    juce::ignoreUnused(outEvent);
    const std::lock_guard<std::mutex> lock(stateMutex);

    auto& voice = allocateVoice();
    voice.midiNote = static_cast<int>(note);
    voice.velocity = juce::jlimit(0.0f, 1.0f, static_cast<float>(velocity) / 127.0f);
    voice.phase = 0.0;
    voice.phaseDelta = juce::MidiMessage::getMidiNoteInHertz(static_cast<int>(note)) / currentSampleRate;
    voice.ageSamples = 0;
    voice.noteDurationSamples = juce::jmax(1, static_cast<int>(std::lround(currentSampleRate
        * currentSecondsPerTick
        * static_cast<double>(juce::jmax(1, static_cast<int>(durationTicks))))));
    voice.samplesUntilRelease = voice.noteDurationSamples;
    voice.releaseStarted = false;
    voice.active = true;
    voice.envelope.reset();
    voice.envelope.noteOn();
    return false;
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
    root->setProperty("waveStepCount", waveStepCount);

    juce::Array<juce::var> waveArray;
    for (int i = 0; i < kMaxWaveSteps; ++i)
        waveArray.add(static_cast<int>(waveSteps[static_cast<std::size_t>(i)]));
    root->setProperty("waves", waveArray);

    const juce::String json = juce::JSON::toString(juce::var(root.get()));
    destData.reset();
    destData.append(json.toRawUTF8(), json.getNumBytesAsUTF8());
}

void WavetableSynthMachine::setStateInformation(const void* data, int sizeInBytes)
{
    if (data == nullptr || sizeInBytes <= 0)
        return;
    if (!juce::CharPointer_UTF8::isValidString(static_cast<const char*>(data), sizeInBytes))
        return;

    const std::lock_guard<std::mutex> lock(stateMutex);
    const auto json = juce::String::fromUTF8(static_cast<const char*>(data), sizeInBytes);
    if (json.isEmpty())
        return;

    const auto parsed = juce::JSON::fromString(json);
    if (!parsed.isObject())
        return;

    const auto* obj = parsed.getDynamicObject();
    attackSeconds = juce::jlimit(0.0f, kMaxAttackSeconds, static_cast<float>(parsed.getProperty("attack", attackSeconds)));
    decaySeconds = juce::jlimit(0.0f, kMaxDecaySeconds, static_cast<float>(parsed.getProperty("decay", decaySeconds)));
    sustainLevel = juce::jlimit(0.0f, 1.0f, static_cast<float>(parsed.getProperty("sustain", sustainLevel)));
    releaseSeconds = juce::jlimit(0.0f, kMaxReleaseSeconds, static_cast<float>(parsed.getProperty("release", releaseSeconds)));
    waveStepCount = juce::jlimit(1, kMaxWaveSteps, static_cast<int>(parsed.getProperty("waveStepCount", waveStepCount)));

    const auto wavesVar = obj->getProperty("waves");
    if (wavesVar.isArray())
    {
        const auto* array = wavesVar.getArray();
        if (array != nullptr)
        {
            for (int i = 0; i < array->size() && i < kMaxWaveSteps; ++i)
            {
                const int waveIndex = juce::jlimit(0, static_cast<int>(Waveform::square), static_cast<int>((*array)[i]));
                waveSteps[static_cast<std::size_t>(i)] = static_cast<Waveform>(waveIndex);
            }
        }
    }

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
}

void WavetableSynthMachine::updateVoiceEnvelopeParameters()
{
    juce::ADSR::Parameters parameters;
    parameters.attack = attackSeconds;
    parameters.decay = decaySeconds;
    parameters.sustain = sustainLevel;
    parameters.release = releaseSeconds;

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
    const double progress = voice.noteDurationSamples > 0
        ? juce::jlimit(0.0, 1.0, static_cast<double>(voice.ageSamples) / static_cast<double>(voice.noteDurationSamples))
        : 0.0;
    const int lastStep = juce::jmax(0, waveStepCount - 1);
    const double stepPosition = progress * static_cast<double>(lastStep);
    const int baseStep = juce::jlimit(0, lastStep, static_cast<int>(std::floor(stepPosition)));
    const int nextStep = juce::jlimit(0, lastStep, baseStep + 1);
    const float morph = voice.releaseStarted ? 1.0f : static_cast<float>(juce::jlimit(0.0, 1.0, stepPosition - static_cast<double>(baseStep)));

    const auto currentWave = waveSteps[static_cast<std::size_t>(voice.releaseStarted ? lastStep : baseStep)];
    const auto nextWave = waveSteps[static_cast<std::size_t>(voice.releaseStarted ? lastStep : nextStep)];
    const float sampleA = sampleWaveform(currentWave, voice.phase);
    const float sampleB = sampleWaveform(nextWave, voice.phase);
    const float oscillatorSample = juce::jmap(morph, sampleA, sampleB);
    const float envelopeSample = voice.envelope.getNextSample();

    voice.phase += voice.phaseDelta;
    voice.phase -= std::floor(voice.phase);

    return oscillatorSample * envelopeSample * voice.velocity;
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
        default: return "---";
    }
}

std::string WavetableSynthMachine::formatFloat(float value, int decimals)
{
    return juce::String(value, decimals).toStdString();
}
