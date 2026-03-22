#include "ArpeggiatorMachine.h"

#include <JuceHeader.h>
#include <algorithm>
#include <cstring>
#include <tuple>

#include "MachineUtilsAbs.h"

namespace
{
constexpr double kStateVersion = 2.0;
}

ArpeggiatorMachine::ArpeggiatorMachine()
{
    slots.resize(kMaxLength);
}

void ArpeggiatorMachine::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    juce::ignoreUnused(sampleRate, samplesPerBlock);
}

void ArpeggiatorMachine::releaseResources()
{
}

void ArpeggiatorMachine::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ignoreUnused(buffer, midi);
}

std::vector<std::vector<UIBox>> ArpeggiatorMachine::getUIBoxes(const MachineUiContext& context)
{
    juce::ignoreUnused(context);

    const std::lock_guard<std::mutex> lock(stateMutex);
    clampLength();

    const std::size_t noteRows = static_cast<std::size_t>((length + kMaxWidth - 1) / kMaxWidth);
    const std::size_t rows = juce::jmax<std::size_t>(static_cast<std::size_t>(1), noteRows) + 1;
    const std::size_t cols = static_cast<std::size_t>(kMaxWidth);
    std::vector<std::vector<UIBox>> boxes(cols, std::vector<UIBox>(rows));

    for (std::size_t row = 0; row < noteRows; ++row)
    {
        for (std::size_t col = 0; col < cols; ++col)
        {
            const int index = static_cast<int>(row * cols + col);
            UIBox noteCell;
            noteCell.kind = UIBox::Kind::TrackerCell;

            if (index < length)
            {
                if (slots[static_cast<std::size_t>(index)].hasNote)
                {
                    noteCell.text = formatNote(slots[static_cast<std::size_t>(index)].note);
                    noteCell.hasNote = true;
                }
                else
                {
                    noteCell.text = "--";
                }

                if (index == playHead)
                    noteCell.isHighlighted = true;
                if (recordEnabled && index == recordHead)
                    noteCell.isArmed = true;
            }
            else
            {
                noteCell.kind = UIBox::Kind::None;
                noteCell.isDisabled = true;
            }

            boxes[col][row] = std::move(noteCell);
        }
    }

    const std::size_t controlRow = rows - 1;
    for (std::size_t col = 0; col < cols; ++col)
    {
        UIBox controlCell;
        if (col == 0)
        {
            controlCell.kind = UIBox::Kind::TrackerCell;
            controlCell.text = "REC";
            controlCell.isArmed = recordEnabled;
            controlCell.onActivate = [this]()
            {
                const std::lock_guard<std::mutex> guard(stateMutex);
                recordEnabled = !recordEnabled;
            };
        }
        else if (col == 1)
        {
            controlCell.kind = UIBox::Kind::TrackerCell;
            controlCell.text = "LEN";
        }
        else if (col == 2)
        {
            controlCell.kind = UIBox::Kind::TrackerCell;
            controlCell.text = std::to_string(length);
            controlCell.onAdjust = [this](int direction)
            {
                const std::lock_guard<std::mutex> guard(stateMutex);
                length = juce::jlimit(1, kMaxLength, length + direction);
                clampLength();
            };
        }
        else if (col == 3)
        {
            controlCell.kind = UIBox::Kind::TrackerCell;
            controlCell.text = "TPB";
        }
        else if (col == 4)
        {
            controlCell.kind = UIBox::Kind::TrackerCell;
            controlCell.text = std::to_string(ticksPerBeat);
            controlCell.onAdjust = [this](int direction)
            {
                const std::lock_guard<std::mutex> guard(stateMutex);
                ticksPerBeat = juce::jlimit(1, kMaxTicksPerBeat, ticksPerBeat + direction);
                tickAccumulator = 0;
            };
        }
        else if (col == 5)
        {
            controlCell.kind = UIBox::Kind::TrackerCell;
            controlCell.text = "MODE";
        }
        else if (col == 6)
        {
            controlCell.kind = UIBox::Kind::TrackerCell;
            controlCell.text = formatPlayMode(playMode);
            controlCell.onAdjust = [this](int direction)
            {
                const std::lock_guard<std::mutex> guard(stateMutex);
                int modeIndex = static_cast<int>(playMode) + direction;
                if (modeIndex < 0)
                    modeIndex = static_cast<int>(PlayMode::random);
                else if (modeIndex > static_cast<int>(PlayMode::random))
                    modeIndex = static_cast<int>(PlayMode::pingPong);
                playMode = static_cast<PlayMode>(modeIndex);
                if (playMode == PlayMode::up || playMode == PlayMode::down)
                    sortActiveSlots();
                resetPlaybackState();
            };
        }
        else
        {
            controlCell.kind = UIBox::Kind::None;
            controlCell.text = "";
            controlCell.isDisabled = true;
        }
        boxes[col][controlRow] = std::move(controlCell);
    }

    return boxes;
}

bool ArpeggiatorMachine::handleIncomingNote(unsigned short note,
                                            unsigned short velocity,
                                            unsigned short durationTicks,
                                            MachineNoteEvent& outEvent)
{
    juce::ignoreUnused(outEvent);

    const std::lock_guard<std::mutex> lock(stateMutex);
    clampLength();

    if (recordEnabled)
    {
        auto& slot = slots[static_cast<std::size_t>(recordHead)];
        slot.note = static_cast<int>(note);
        slot.velocity = static_cast<int>(velocity);
        slot.durationTicks = static_cast<int>(durationTicks);
        slot.hasNote = true;
        recordHead = (recordHead + 1) % juce::jmax(1, length);
        if (playMode == PlayMode::up || playMode == PlayMode::down)
            sortActiveSlots();
    }

    return false;
}

bool ArpeggiatorMachine::handleClockTick(MachineNoteEvent& outEvent)
{
    const std::lock_guard<std::mutex> lock(stateMutex);
    clampLength();
    if (length <= 0 || countActiveSlots() == 0)
        return false;

    tickAccumulator += ticksPerBeat;
    if (tickAccumulator < kMaxTicksPerBeat)
        return false;

    tickAccumulator -= kMaxTicksPerBeat;
    const int nextIndex = advancePlayHead();
    if (nextIndex < 0)
        return false;

    const auto& slot = slots[static_cast<std::size_t>(nextIndex)];
    if (!slot.hasNote)
        return false;

    outEvent.note = static_cast<unsigned short>(slot.note);
    outEvent.velocity = static_cast<unsigned short>(slot.velocity);
    outEvent.durationTicks = static_cast<unsigned short>(slot.durationTicks);
    return true;
}

void ArpeggiatorMachine::resetPlayback()
{
    const std::lock_guard<std::mutex> lock(stateMutex);
    resetPlaybackState();
}

void ArpeggiatorMachine::getStateInformation(juce::MemoryBlock& destData)
{
    const std::lock_guard<std::mutex> lock(stateMutex);
    juce::DynamicObject::Ptr root = new juce::DynamicObject();
    root->setProperty("version", kStateVersion);
    root->setProperty("length", length);
    root->setProperty("ticksPerBeat", ticksPerBeat);
    root->setProperty("recordEnabled", recordEnabled);
    root->setProperty("recordHead", recordHead);
    root->setProperty("playHead", playHead);
    root->setProperty("pingPongDirection", pingPongDirection);
    root->setProperty("tickAccumulator", tickAccumulator);
    root->setProperty("playMode", static_cast<int>(playMode));

    juce::Array<juce::var> slotsVar;
    slotsVar.ensureStorageAllocated(kMaxLength);
    for (const auto& slot : slots)
    {
        juce::DynamicObject::Ptr obj = new juce::DynamicObject();
        obj->setProperty("note", slot.note);
        obj->setProperty("velocity", slot.velocity);
        obj->setProperty("durationTicks", slot.durationTicks);
        obj->setProperty("hasNote", slot.hasNote);
        slotsVar.add(juce::var(obj));
    }
    root->setProperty("slots", slotsVar);

    const juce::String json = juce::JSON::toString(juce::var(root.get()));
    destData.reset();
    destData.append(json.toRawUTF8(), json.getNumBytesAsUTF8());
}

void ArpeggiatorMachine::setStateInformation(const void* data, int sizeInBytes)
{
    if (data == nullptr || sizeInBytes <= 0)
        return;

    const std::lock_guard<std::mutex> lock(stateMutex);
    juce::String json(static_cast<const char*>(data), static_cast<size_t>(sizeInBytes));
    const auto parsed = juce::JSON::fromString(json);
    if (!parsed.isObject())
        return;

    const auto* obj = parsed.getDynamicObject();
    const auto lengthVar = obj->getProperty("length");
    if (!lengthVar.isVoid())
        length = static_cast<int>(lengthVar);
    const auto ticksVar = obj->getProperty("ticksPerBeat");
    if (!ticksVar.isVoid())
        ticksPerBeat = static_cast<int>(ticksVar);
    const auto recordVar = obj->getProperty("recordEnabled");
    if (!recordVar.isVoid())
        recordEnabled = static_cast<bool>(recordVar);
    const auto recordHeadVar = obj->getProperty("recordHead");
    if (!recordHeadVar.isVoid())
        recordHead = static_cast<int>(recordHeadVar);
    const auto playHeadVar = obj->getProperty("playHead");
    if (!playHeadVar.isVoid())
        playHead = static_cast<int>(playHeadVar);
    const auto directionVar = obj->getProperty("pingPongDirection");
    if (!directionVar.isVoid())
        pingPongDirection = static_cast<int>(directionVar);
    const auto accumulatorVar = obj->getProperty("tickAccumulator");
    if (!accumulatorVar.isVoid())
        tickAccumulator = static_cast<int>(accumulatorVar);
    const auto modeVar = obj->getProperty("playMode");
    if (!modeVar.isVoid())
    {
        const int modeIndex = juce::jlimit(0, static_cast<int>(PlayMode::random), static_cast<int>(modeVar));
        playMode = static_cast<PlayMode>(modeIndex);
    }

    const auto slotsVar = obj->getProperty("slots");
    if (slotsVar.isArray())
    {
        const auto* array = slotsVar.getArray();
        if (array != nullptr)
        {
            slots.assign(kMaxLength, NoteSlot{});
            for (int i = 0; i < array->size() && i < kMaxLength; ++i)
            {
                const auto& entry = (*array)[i];
                if (!entry.isObject())
                    continue;
                const auto* slotObj = entry.getDynamicObject();
                NoteSlot slot;
                const auto noteVar = slotObj->getProperty("note");
                if (!noteVar.isVoid())
                    slot.note = static_cast<int>(noteVar);
                const auto velVar = slotObj->getProperty("velocity");
                if (!velVar.isVoid())
                    slot.velocity = static_cast<int>(velVar);
                const auto durVar = slotObj->getProperty("durationTicks");
                if (!durVar.isVoid())
                    slot.durationTicks = static_cast<int>(durVar);
                const auto hasVar = slotObj->getProperty("hasNote");
                if (!hasVar.isVoid())
                    slot.hasNote = static_cast<bool>(hasVar);
                slots[static_cast<std::size_t>(i)] = slot;
            }
        }
    }

    clampLength();
}

void ArpeggiatorMachine::clampLength()
{
    length = juce::jlimit(1, kMaxLength, length);
    ticksPerBeat = juce::jlimit(1, kMaxTicksPerBeat, ticksPerBeat);
    if (recordHead < 0 || recordHead >= length)
        recordHead = 0;
    if (playHead < -1 || playHead >= length)
        playHead = -1;
    if (playHead >= 0 && !slots[static_cast<std::size_t>(playHead)].hasNote)
        playHead = -1;
    if (pingPongDirection == 0)
        pingPongDirection = 1;
    tickAccumulator = juce::jlimit(0, kMaxTicksPerBeat - 1, tickAccumulator);
}

void ArpeggiatorMachine::resetPlaybackState()
{
    playHead = -1;
    pingPongDirection = 1;
    tickAccumulator = 0;
}

int ArpeggiatorMachine::countActiveSlots() const
{
    int active = 0;
    for (int i = 0; i < length; ++i)
        if (slots[static_cast<std::size_t>(i)].hasNote)
            ++active;
    return active;
}

void ArpeggiatorMachine::sortActiveSlots()
{
    std::vector<NoteSlot> activeSlots;
    activeSlots.reserve(static_cast<std::size_t>(length));
    for (int i = 0; i < length; ++i)
    {
        const auto& slot = slots[static_cast<std::size_t>(i)];
        if (slot.hasNote)
            activeSlots.push_back(slot);
    }

    const auto comparator = [this](const NoteSlot& lhs, const NoteSlot& rhs)
    {
        return playMode == PlayMode::down
            ? std::tie(lhs.note, lhs.velocity, lhs.durationTicks) > std::tie(rhs.note, rhs.velocity, rhs.durationTicks)
            : std::tie(lhs.note, lhs.velocity, lhs.durationTicks) < std::tie(rhs.note, rhs.velocity, rhs.durationTicks);
    };
    std::sort(activeSlots.begin(), activeSlots.end(), comparator);

    for (int i = 0; i < length; ++i)
    {
        if (i < static_cast<int>(activeSlots.size()))
            slots[static_cast<std::size_t>(i)] = activeSlots[static_cast<std::size_t>(i)];
        else
            slots[static_cast<std::size_t>(i)] = NoteSlot{};
    }

    recordHead = activeSlots.size() < static_cast<std::size_t>(length)
        ? static_cast<int>(activeSlots.size())
        : 0;
    resetPlaybackState();
}

int ArpeggiatorMachine::advancePlayHead()
{
    if (countActiveSlots() == 0)
        return -1;

    if (playMode == PlayMode::random)
    {
        playHead = getRandomPlayableIndex();
        return playHead;
    }

    for (int attempts = 0; attempts < length; ++attempts)
    {
        if (playMode == PlayMode::pingPong)
        {
            if (playHead < 0)
            {
                playHead = 0;
                pingPongDirection = 1;
            }
            else if (pingPongDirection > 0)
            {
                if (playHead + 1 < length)
                    ++playHead;
                else
                {
                    pingPongDirection = -1;
                    playHead = length > 1 ? playHead - 1 : 0;
                }
            }
            else
            {
                if (playHead > 0)
                    --playHead;
                else
                {
                    pingPongDirection = 1;
                    playHead = length > 1 ? 1 : 0;
                }
            }
        }
        else
        {
            playHead = (playHead + 1 + length) % length;
        }

        if (slots[static_cast<std::size_t>(playHead)].hasNote)
            return playHead;
    }

    return -1;
}

int ArpeggiatorMachine::getRandomPlayableIndex() const
{
    std::vector<int> activeIndices;
    activeIndices.reserve(static_cast<std::size_t>(length));
    for (int i = 0; i < length; ++i)
        if (slots[static_cast<std::size_t>(i)].hasNote)
            activeIndices.push_back(i);

    if (activeIndices.empty())
        return -1;

    const int randomIndex = juce::Random::getSystemRandom().nextInt(static_cast<int>(activeIndices.size()));
    return activeIndices[static_cast<std::size_t>(randomIndex)];
}

const char* ArpeggiatorMachine::formatPlayMode(PlayMode mode)
{
    switch (mode)
    {
        case PlayMode::pingPong:
            return "PING";
        case PlayMode::up:
            return "UP";
        case PlayMode::down:
            return "DOWN";
        case PlayMode::random:
            return "RAND";
    }

    return "--";
}

std::string ArpeggiatorMachine::formatNote(int midiNote)
{
    if (midiNote < 0)
        return "--";
    const int noteIndex = midiNote % 12;
    const int octave = midiNote / 12;
    const auto map = MachineUtilsAbs::getIntToNoteMap();
    const auto it = map.find(noteIndex);
    const char nchar = it != map.end() ? it->second : '-';
    std::string text;
    text.reserve(4);
    text.push_back(nchar);
    text.push_back('-');
    text += std::to_string(octave);
    return text;
}
