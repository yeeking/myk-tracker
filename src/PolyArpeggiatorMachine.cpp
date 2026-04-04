#include "PolyArpeggiatorMachine.h"

#include <JuceHeader.h>
#include <algorithm>
#include <random>
#include <tuple>

#include "MachineUtilsAbs.h"

namespace
{
constexpr double kStateVersion = 2.0;

int nextQuarterBeatDivisor(int currentDivisor, int direction)
{
    constexpr int divisors[] = { 1, 2, 4, 8, 16 };
    int currentIndex = 0;
    for (int i = 0; i < 5; ++i)
    {
        if (divisors[i] == currentDivisor)
        {
            currentIndex = i;
            break;
        }
    }

    currentIndex = (currentIndex + direction + 5) % 5;
    return divisors[currentIndex];
}

bool isValidQuarterBeatDivisor(int divisor)
{
    switch (divisor)
    {
        case 1:
        case 2:
        case 4:
        case 8:
        case 16:
            return true;
        default:
            return false;
    }
}
}

PolyArpeggiatorMachine::PolyArpeggiatorMachine()
{
    slots.resize(kMaxLength);
    readHeads.resize(kMaxReadHeads);
    clampState();
}

void PolyArpeggiatorMachine::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    juce::ignoreUnused(sampleRate, samplesPerBlock);
}

void PolyArpeggiatorMachine::releaseResources()
{
}

void PolyArpeggiatorMachine::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ignoreUnused(buffer, midi);
}

std::vector<std::vector<UIBox>> PolyArpeggiatorMachine::getUIBoxes(const MachineUiContext& context)
{
    juce::ignoreUnused(context);

    const std::lock_guard<std::mutex> lock(stateMutex);
    clampState();

    const std::size_t noteRows = static_cast<std::size_t>((length + kMaxWidth - 1) / kMaxWidth);
    const std::size_t headRows = static_cast<std::size_t>(readHeadCount);
    const std::size_t rows = juce::jmax<std::size_t>(static_cast<std::size_t>(1), noteRows) + headRows + 1;
    const std::size_t cols = static_cast<std::size_t>(kMaxWidth);
    std::vector<std::vector<UIBox>> boxes(cols, std::vector<UIBox>(rows));

    std::vector<int> headCounts(static_cast<std::size_t>(length), 0);
    std::vector<int> firstHeadIndex(static_cast<std::size_t>(length), -1);
    for (int headIndex = 0; headIndex < readHeadCount; ++headIndex)
    {
        const auto& head = readHeads[static_cast<std::size_t>(headIndex)];
        if (head.playHead < 0 || head.playHead >= length)
            continue;
        ++headCounts[static_cast<std::size_t>(head.playHead)];
        if (firstHeadIndex[static_cast<std::size_t>(head.playHead)] < 0)
            firstHeadIndex[static_cast<std::size_t>(head.playHead)] = headIndex;
    }

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

                if (headCounts[static_cast<std::size_t>(index)] > 0)
                {
                    noteCell.isHighlighted = true;
                    noteCell.useCustomFillColour = true;
                    noteCell.customFillArgb = getHeadFillColour(static_cast<std::size_t>(firstHeadIndex[static_cast<std::size_t>(index)]));
                    noteCell.useCustomTextColour = true;
                    noteCell.customTextArgb = 0xfff4fff7u;
                    noteCell.glow = juce::jmin(1.0f, 0.25f * static_cast<float>(headCounts[static_cast<std::size_t>(index)]));
                }

                if (!recordEnabled)
                {
                    noteCell.onInsert = [this, index](double value)
                    {
                        const std::lock_guard<std::mutex> guard(stateMutex);
                        clampState();
                        if (index < 0 || index >= length)
                            return;

                        auto& slot = slots[static_cast<std::size_t>(index)];
                        slot.note = juce::jlimit(0, 127, static_cast<int>(std::lround(value)));
                        slot.velocity = slot.velocity > 0 ? slot.velocity : 100;
                        slot.durationTicks = slot.durationTicks > 0 ? slot.durationTicks : 1;
                        slot.hasNote = true;

                        if (anyOrderedHeadActive())
                            sortActiveSlots();
                    };
                }
            }
            else
            {
                noteCell.kind = UIBox::Kind::None;
                noteCell.isDisabled = true;
            }

            boxes[col][row] = std::move(noteCell);
        }
    }

    for (int headIndex = 0; headIndex < readHeadCount; ++headIndex)
    {
        const std::size_t row = noteRows + static_cast<std::size_t>(headIndex);
        auto makeLabelCell = [headIndex](const std::string& text)
        {
            UIBox cell;
            cell.kind = UIBox::Kind::TrackerCell;
            cell.text = text;
            cell.useCustomFillColour = true;
            cell.customFillArgb = getHeadFillColour(static_cast<std::size_t>(headIndex));
            cell.useCustomTextColour = true;
            cell.customTextArgb = 0xfff4fff7u;
            return cell;
        };

        boxes[0][row] = makeLabelCell("H" + std::to_string(headIndex + 1));
        boxes[1][row].kind = UIBox::Kind::TrackerCell;
        boxes[1][row].text = "RATE";

        boxes[2][row].kind = UIBox::Kind::TrackerCell;
        boxes[2][row].text = formatQuarterBeatDivisor(readHeads[static_cast<std::size_t>(headIndex)].quarterBeatDivisor);
        boxes[2][row].onAdjust = [this, headIndex](int direction)
        {
            const std::lock_guard<std::mutex> guard(stateMutex);
            auto& head = readHeads[static_cast<std::size_t>(headIndex)];
            head.quarterBeatDivisor = nextQuarterBeatDivisor(head.quarterBeatDivisor, direction < 0 ? -1 : 1);
            head.ticksSinceStep = juce::jmax(0, head.quarterBeatDivisor - 1);
        };

        boxes[3][row].kind = UIBox::Kind::TrackerCell;
        boxes[3][row].text = "MODE";

        boxes[4][row].kind = UIBox::Kind::TrackerCell;
        boxes[4][row].text = formatPlayMode(readHeads[static_cast<std::size_t>(headIndex)].playMode);
        boxes[4][row].onAdjust = [this, headIndex](int direction)
        {
            const std::lock_guard<std::mutex> guard(stateMutex);
            auto& head = readHeads[static_cast<std::size_t>(headIndex)];
            int modeIndex = static_cast<int>(head.playMode) + direction;
            if (modeIndex < 0)
                modeIndex = static_cast<int>(PlayMode::random);
            else if (modeIndex > static_cast<int>(PlayMode::random))
                modeIndex = static_cast<int>(PlayMode::pingPong);
            head.playMode = static_cast<PlayMode>(modeIndex);
            if (anyOrderedHeadActive())
                sortActiveSlots();
            else
                resetReadHeads();
        };

        boxes[5][row].kind = UIBox::Kind::TrackerCell;
        boxes[5][row].text = "OCT";

        boxes[6][row].kind = UIBox::Kind::TrackerCell;
        boxes[6][row].text = std::to_string(readHeads[static_cast<std::size_t>(headIndex)].octaveSpan);
        boxes[6][row].onAdjust = [this, headIndex](int direction)
        {
            const std::lock_guard<std::mutex> guard(stateMutex);
            auto& head = readHeads[static_cast<std::size_t>(headIndex)];
            head.octaveSpan = juce::jlimit(1, 4, head.octaveSpan + direction);
            head.currentOctaveIndex = juce::jlimit(0, head.octaveSpan - 1, head.currentOctaveIndex);
        };

        for (std::size_t col = 7; col < cols; ++col)
        {
            boxes[col][row].kind = UIBox::Kind::None;
            boxes[col][row].isDisabled = true;
        }
    }

    const std::size_t globalRow = rows - 1;
    boxes[0][globalRow].kind = UIBox::Kind::TrackerCell;
    boxes[0][globalRow].text = "WRITE";
    boxes[0][globalRow].isArmed = recordEnabled;
    boxes[0][globalRow].onActivate = [this]()
    {
        const std::lock_guard<std::mutex> guard(stateMutex);
        recordEnabled = !recordEnabled;
    };

    boxes[1][globalRow].kind = UIBox::Kind::TrackerCell;
    boxes[1][globalRow].text = "LEN";

    boxes[2][globalRow].kind = UIBox::Kind::TrackerCell;
    boxes[2][globalRow].text = std::to_string(length);
    boxes[2][globalRow].onAdjust = [this](int direction)
    {
        const std::lock_guard<std::mutex> guard(stateMutex);
        length = juce::jlimit(1, kMaxLength, length + direction);
        clampState();
    };

    boxes[3][globalRow].kind = UIBox::Kind::TrackerCell;
    boxes[3][globalRow].text = "HEADS";

    boxes[4][globalRow].kind = UIBox::Kind::TrackerCell;
    boxes[4][globalRow].text = std::to_string(readHeadCount);
    boxes[4][globalRow].onAdjust = [this](int direction)
    {
        const std::lock_guard<std::mutex> guard(stateMutex);
        readHeadCount = juce::jlimit(1, kMaxReadHeads, readHeadCount + direction);
        clampState();
    };

    boxes[5][globalRow].kind = UIBox::Kind::TrackerCell;
    boxes[5][globalRow].text = "SHUF";
    boxes[5][globalRow].onActivate = [this]()
    {
        const std::lock_guard<std::mutex> guard(stateMutex);
        shuffleSlots();
    };

    for (std::size_t col = 6; col < cols; ++col)
    {
        boxes[col][globalRow].kind = UIBox::Kind::None;
        boxes[col][globalRow].isDisabled = true;
    }

    return boxes;
}

bool PolyArpeggiatorMachine::handleIncomingNote(unsigned short note,
                                                unsigned short velocity,
                                                unsigned short durationTicks,
                                                MachineNoteEvent& outEvent)
{
    juce::ignoreUnused(outEvent);
    const std::lock_guard<std::mutex> lock(stateMutex);
    clampState();

    if (recordEnabled)
    {
        auto& slot = slots[static_cast<std::size_t>(recordHead)];
        slot.note = static_cast<int>(note);
        slot.velocity = static_cast<int>(velocity);
        slot.durationTicks = static_cast<int>(durationTicks);
        slot.hasNote = true;
        recordHead = (recordHead + 1) % juce::jmax(1, length);
        if (anyOrderedHeadActive())
            sortActiveSlots();
    }

    return false;
}

void PolyArpeggiatorMachine::addEntry()
{
    const std::lock_guard<std::mutex> lock(stateMutex);
    readHeadCount = juce::jlimit(1, kMaxReadHeads, readHeadCount + 1);
    clampState();
}

void PolyArpeggiatorMachine::removeEntry(int entryIndex)
{
    juce::ignoreUnused(entryIndex);
    const std::lock_guard<std::mutex> lock(stateMutex);
    readHeadCount = juce::jlimit(1, kMaxReadHeads, readHeadCount - 1);
    clampState();
}

void PolyArpeggiatorMachine::allNotesOff()
{
    const std::lock_guard<std::mutex> lock(stateMutex);
    resetReadHeads();
}

void PolyArpeggiatorMachine::tick(int quarterBeat)
{
    std::function<void(const MachineNoteEvent&)> callbackCopy;
    std::vector<MachineNoteEvent> outEvents;

    {
        const std::lock_guard<std::mutex> lock(stateMutex);
        clampState();
        if (!clockActive || length <= 0 || countActiveSlots() == 0)
            return;
        juce::ignoreUnused(quarterBeat);

        callbackCopy = clockEventCallback;
        if (callbackCopy == nullptr)
            return;

        for (int headIndex = 0; headIndex < readHeadCount; ++headIndex)
        {
            auto& head = readHeads[static_cast<std::size_t>(headIndex)];
            if (quarterBeat == 1)
                head.ticksSinceStep = juce::jmax(0, head.quarterBeatDivisor - 1);

            ++head.ticksSinceStep;
            if (head.ticksSinceStep < juce::jmax(1, head.quarterBeatDivisor))
                continue;
            head.ticksSinceStep = 0;

            const int nextIndex = advancePlayHead(head);
            if (nextIndex < 0)
                continue;

            const auto& slot = slots[static_cast<std::size_t>(nextIndex)];
            if (!slot.hasNote)
                continue;

            MachineNoteEvent event;
            event.note = static_cast<unsigned short>(getPlaybackNoteForSlot(head, slot.note));
            event.velocity = static_cast<unsigned short>(slot.velocity);
            event.durationTicks = static_cast<unsigned short>(slot.durationTicks);
            outEvents.push_back(event);
        }
    }

    for (const auto& event : outEvents)
        callbackCopy(event);
}

void PolyArpeggiatorMachine::reset()
{
    allNotesOff();
}

void PolyArpeggiatorMachine::setClockEventCallback(std::function<void(const MachineNoteEvent&)> callback)
{
    const std::lock_guard<std::mutex> lock(stateMutex);
    clockEventCallback = std::move(callback);
}

void PolyArpeggiatorMachine::setClockActive(bool shouldBeActive)
{
    const std::lock_guard<std::mutex> lock(stateMutex);
    if (clockActive != shouldBeActive && shouldBeActive)
        resetReadHeads();
    clockActive = shouldBeActive;
}

bool PolyArpeggiatorMachine::shiftNoteAtCell(int row, int col, int semitones)
{
    const std::lock_guard<std::mutex> lock(stateMutex);
    clampState();
    if (recordEnabled)
        return false;

    const int index = row * kMaxWidth + col;
    if (row < 0 || col < 0 || index < 0 || index >= length)
        return false;

    auto& slot = slots[static_cast<std::size_t>(index)];
    if (!slot.hasNote)
        return false;

    slot.note = juce::jlimit(0, 127, slot.note + semitones);
    if (anyOrderedHeadActive())
        sortActiveSlots();
    return true;
}

bool PolyArpeggiatorMachine::clearCell(int row, int col)
{
    const std::lock_guard<std::mutex> lock(stateMutex);
    clampState();
    if (recordEnabled)
        return false;

    const int index = row * kMaxWidth + col;
    if (row < 0 || col < 0 || index < 0 || index >= length)
        return false;

    auto& slot = slots[static_cast<std::size_t>(index)];
    slot = NoteSlot{};
    if (anyOrderedHeadActive())
        sortActiveSlots();
    return true;
}

void PolyArpeggiatorMachine::getStateInformation(juce::MemoryBlock& destData)
{
    const std::lock_guard<std::mutex> lock(stateMutex);
    juce::DynamicObject::Ptr root = new juce::DynamicObject();
    root->setProperty("version", kStateVersion);
    root->setProperty("length", length);
    root->setProperty("recordEnabled", recordEnabled);
    root->setProperty("recordHead", recordHead);
    root->setProperty("readHeadCount", readHeadCount);

    juce::Array<juce::var> slotsVar;
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

    juce::Array<juce::var> headsVar;
    for (const auto& head : readHeads)
    {
        juce::DynamicObject::Ptr obj = new juce::DynamicObject();
        obj->setProperty("quarterBeatDivisor", head.quarterBeatDivisor);
        obj->setProperty("playHead", head.playHead);
        obj->setProperty("pingPongDirection", head.pingPongDirection);
        obj->setProperty("octaveSpan", head.octaveSpan);
        obj->setProperty("currentOctaveIndex", head.currentOctaveIndex);
        obj->setProperty("playMode", static_cast<int>(head.playMode));
        headsVar.add(juce::var(obj));
    }
    root->setProperty("heads", headsVar);

    const juce::String json = juce::JSON::toString(juce::var(root.get()));
    destData.reset();
    destData.append(json.toRawUTF8(), json.getNumBytesAsUTF8());
}

void PolyArpeggiatorMachine::setStateInformation(const void* data, int sizeInBytes)
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
    const double version = static_cast<double>(parsed.getProperty("version", 0.0));
    length = static_cast<int>(parsed.getProperty("length", length));
    recordEnabled = static_cast<bool>(parsed.getProperty("recordEnabled", recordEnabled));
    recordHead = static_cast<int>(parsed.getProperty("recordHead", recordHead));
    readHeadCount = static_cast<int>(parsed.getProperty("readHeadCount", readHeadCount));

    if (const auto slotsVar = obj->getProperty("slots"); slotsVar.isArray())
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
                auto& slot = slots[static_cast<std::size_t>(i)];
                slot.note = static_cast<int>(entry.getProperty("note", slot.note));
                slot.velocity = static_cast<int>(entry.getProperty("velocity", slot.velocity));
                slot.durationTicks = static_cast<int>(entry.getProperty("durationTicks", slot.durationTicks));
                slot.hasNote = static_cast<bool>(entry.getProperty("hasNote", slot.hasNote));
            }
        }
    }

    if (const auto headsVar = obj->getProperty("heads"); headsVar.isArray())
    {
        const auto* array = headsVar.getArray();
        if (array != nullptr)
        {
            readHeads.assign(kMaxReadHeads, ReadHead{});
            for (int i = 0; i < array->size() && i < kMaxReadHeads; ++i)
            {
                const auto& entry = (*array)[i];
                if (!entry.isObject())
                    continue;
                auto& head = readHeads[static_cast<std::size_t>(i)];
                if (entry.hasProperty("quarterBeatDivisor"))
                    head.quarterBeatDivisor = static_cast<int>(entry.getProperty("quarterBeatDivisor", head.quarterBeatDivisor));
                else
                    head.quarterBeatDivisor = mapLegacyTicksPerStepToQuarterBeatDivisor(static_cast<int>(entry.getProperty("ticksPerStep", head.quarterBeatDivisor)));
                head.playHead = static_cast<int>(entry.getProperty("playHead", head.playHead));
                head.pingPongDirection = static_cast<int>(entry.getProperty("pingPongDirection", head.pingPongDirection));
                head.octaveSpan = static_cast<int>(entry.getProperty("octaveSpan", head.octaveSpan));
                head.currentOctaveIndex = static_cast<int>(entry.getProperty("currentOctaveIndex", head.currentOctaveIndex));
                int modeIndex = static_cast<int>(entry.getProperty("playMode", static_cast<int>(head.playMode)));
                if (version < 2.0 && modeIndex >= static_cast<int>(PlayMode::linear))
                    ++modeIndex;
                modeIndex = juce::jlimit(0, static_cast<int>(PlayMode::random), modeIndex);
                head.playMode = static_cast<PlayMode>(modeIndex);
            }
        }
    }

    clampState();
}

void PolyArpeggiatorMachine::clampState()
{
    length = juce::jlimit(1, kMaxLength, length);
    readHeadCount = juce::jlimit(1, kMaxReadHeads, readHeadCount);
    if (recordHead < 0 || recordHead >= length)
        recordHead = 0;

    for (auto& head : readHeads)
    {
        if (!isValidQuarterBeatDivisor(head.quarterBeatDivisor))
            head.quarterBeatDivisor = mapLegacyTicksPerStepToQuarterBeatDivisor(head.quarterBeatDivisor);
        head.octaveSpan = juce::jlimit(1, 4, head.octaveSpan);
        if (head.playHead < -1 || head.playHead >= length)
            head.playHead = -1;
        if (head.pingPongDirection == 0)
            head.pingPongDirection = 1;
        head.currentOctaveIndex = juce::jlimit(0, head.octaveSpan - 1, head.currentOctaveIndex);
    }
}

void PolyArpeggiatorMachine::resetReadHeads()
{
    for (auto& head : readHeads)
    {
        head.playHead = -1;
        head.pingPongDirection = 1;
        head.currentOctaveIndex = 0;
        head.ticksSinceStep = juce::jmax(0, head.quarterBeatDivisor - 1);
    }
}

int PolyArpeggiatorMachine::countActiveSlots() const
{
    int active = 0;
    for (int i = 0; i < length; ++i)
        if (slots[static_cast<std::size_t>(i)].hasNote)
            ++active;
    return active;
}

void PolyArpeggiatorMachine::sortActiveSlots()
{
    std::vector<NoteSlot> activeSlots;
    activeSlots.reserve(static_cast<std::size_t>(length));
    for (int i = 0; i < length; ++i)
    {
        const auto& slot = slots[static_cast<std::size_t>(i)];
        if (slot.hasNote)
            activeSlots.push_back(slot);
    }

    std::sort(activeSlots.begin(), activeSlots.end(),
              [](const NoteSlot& lhs, const NoteSlot& rhs)
              {
                  return std::tie(lhs.note, lhs.velocity, lhs.durationTicks)
                      < std::tie(rhs.note, rhs.velocity, rhs.durationTicks);
              });

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
    resetReadHeads();
}

void PolyArpeggiatorMachine::shuffleSlots()
{
    std::mt19937 rng(static_cast<std::mt19937::result_type>(juce::Random::getSystemRandom().nextInt()));
    std::shuffle(slots.begin(), slots.begin() + length, rng);
    resetReadHeads();
}

bool PolyArpeggiatorMachine::anyOrderedHeadActive() const
{
    for (int headIndex = 0; headIndex < readHeadCount; ++headIndex)
    {
        const auto mode = readHeads[static_cast<std::size_t>(headIndex)].playMode;
        if (mode == PlayMode::up || mode == PlayMode::down)
            return true;
    }
    return false;
}

int PolyArpeggiatorMachine::advancePlayHead(ReadHead& head)
{
    if (countActiveSlots() == 0)
        return -1;

    if (head.playMode == PlayMode::random)
    {
        head.playHead = juce::Random::getSystemRandom().nextInt(juce::jmax(1, length));
        head.currentOctaveIndex = juce::Random::getSystemRandom().nextInt(juce::jmax(1, head.octaveSpan));
        return head.playHead;
    }

    const int previousPlayHead = head.playHead;
    const int previousDirection = head.pingPongDirection;

    if (head.playMode == PlayMode::pingPong)
    {
        if (head.playHead < 0)
        {
            head.playHead = 0;
            head.pingPongDirection = 1;
        }
        else if (head.pingPongDirection > 0)
        {
            if (head.playHead + 1 < length)
                ++head.playHead;
            else
            {
                head.pingPongDirection = -1;
                head.playHead = length > 1 ? head.playHead - 1 : 0;
            }
        }
        else
        {
            if (head.playHead > 0)
                --head.playHead;
            else
            {
                head.pingPongDirection = 1;
                head.playHead = length > 1 ? 1 : 0;
            }
        }
    }
    else if (head.playMode == PlayMode::down)
    {
        if (head.playHead < 0)
            head.playHead = length;
        head.playHead = (head.playHead - 1 + length) % length;
    }
    else
    {
        head.playHead = (head.playHead + 1 + length) % length;
    }

    bool advanceOctave = false;
    if (head.playMode == PlayMode::linear || head.playMode == PlayMode::up)
        advanceOctave = previousPlayHead >= 0 && head.playHead <= previousPlayHead;
    else if (head.playMode == PlayMode::down)
        advanceOctave = previousPlayHead >= 0 && head.playHead >= previousPlayHead;
    else if (head.playMode == PlayMode::pingPong)
        advanceOctave = previousPlayHead == 0 && previousDirection < 0 && head.pingPongDirection > 0;

    if (advanceOctave && head.octaveSpan > 1)
        head.currentOctaveIndex = (head.currentOctaveIndex + 1) % head.octaveSpan;

    return head.playHead;
}

int PolyArpeggiatorMachine::getPlaybackNoteForSlot(const ReadHead& head, int slotNote)
{
    return juce::jlimit(0, 127, slotNote + (12 * head.currentOctaveIndex));
}

const char* PolyArpeggiatorMachine::formatPlayMode(PlayMode mode)
{
    switch (mode)
    {
        case PlayMode::pingPong: return "PING";
        case PlayMode::linear: return "LINE";
        case PlayMode::up: return "UP";
        case PlayMode::down: return "DOWN";
        case PlayMode::random: return "RAND";
    }

    return "PING";
}

const char* PolyArpeggiatorMachine::formatQuarterBeatDivisor(int divisor)
{
    switch (divisor)
    {
        case 1: return "1/16";
        case 2: return "1/8";
        case 4: return "1/4";
        case 8: return "1/2";
        case 16: return "1BAR";
        default: return "1/16";
    }
}

int PolyArpeggiatorMachine::mapLegacyTicksPerStepToQuarterBeatDivisor(int ticksPerStep)
{
    if (ticksPerStep <= 1)
        return 16;
    if (ticksPerStep <= 2)
        return 8;
    if (ticksPerStep <= 4)
        return 4;
    if (ticksPerStep <= 6)
        return 2;
    return 1;
}

std::string PolyArpeggiatorMachine::formatNote(int midiNote)
{
    if (midiNote < 0)
        return "--";

    const auto noteMap = MachineUtilsAbs::getIntToNoteMap();
    const int noteIndex = midiNote % 12;
    const int octave = midiNote / 12;
    std::string text;
    text.push_back(noteMap.at(noteIndex));
    text += std::to_string(octave);
    return text;
}

std::uint32_t PolyArpeggiatorMachine::getHeadFillColour(std::size_t headIndex)
{
    static constexpr std::uint32_t colours[kMaxReadHeads] = {
        0xff2fbf71u,
        0xff3b82f6u,
        0xfff59e0bu,
        0xffef4444u,
        0xff14b8a6u,
        0xfff97316u,
        0xffe879f9u,
        0xff84cc16u
    };
    return colours[headIndex % kMaxReadHeads];
}
