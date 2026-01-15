#include "ArpeggiatorMachine.h"

#include <JuceHeader.h>
#include <cstring>

#include "MachineUtilsAbs.h"

namespace
{
constexpr double kStateVersion = 1.0;
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

    const std::size_t rows = 2;
    const std::size_t cols = static_cast<std::size_t>(kMaxLength);
    std::vector<std::vector<UIBox>> boxes(cols, std::vector<UIBox>(rows));

    for (std::size_t col = 0; col < cols; ++col)
    {
        const int index = static_cast<int>(col);
        UIBox noteCell;
        noteCell.kind = UIBox::Kind::TrackerCell;
        if (index < length && slots[static_cast<std::size_t>(index)].hasNote)
        {
            noteCell.text = formatNote(slots[static_cast<std::size_t>(index)].note);
            noteCell.hasNote = true;
        }
        else
        {
            noteCell.text = "--";
            noteCell.hasNote = false;
        }
        if (index == playHead && index < length)
            noteCell.isHighlighted = true;
        if (recordEnabled && index == recordHead && index < length)
            noteCell.isArmed = true;
        if (index >= length)
            noteCell.isDisabled = true;
        boxes[col][0] = std::move(noteCell);
    }

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
        else
        {
            controlCell.kind = UIBox::Kind::None;
            controlCell.text = "";
            controlCell.isDisabled = true;
        }
        boxes[col][1] = std::move(controlCell);
    }

    return boxes;
}

bool ArpeggiatorMachine::handleIncomingNote(unsigned short note,
                                            unsigned short velocity,
                                            unsigned short durationTicks,
                                            MachineNoteEvent& outEvent)
{
    const std::lock_guard<std::mutex> lock(stateMutex);
    clampLength();
    if (length <= 0)
        return false;

    if (recordEnabled)
    {
        auto& slot = slots[static_cast<std::size_t>(recordHead)];
        slot.note = static_cast<int>(note);
        slot.velocity = static_cast<int>(velocity);
        slot.durationTicks = static_cast<int>(durationTicks);
        slot.hasNote = true;
        recordHead = (recordHead + 1) % length;
    }

    playHead = (playHead + 1) % length;
    const auto& slot = slots[static_cast<std::size_t>(playHead)];
    if (!slot.hasNote)
        return false;

    outEvent.note = static_cast<unsigned short>(slot.note);
    outEvent.velocity = static_cast<unsigned short>(slot.velocity);
    outEvent.durationTicks = static_cast<unsigned short>(slot.durationTicks);
    return true;
}

void ArpeggiatorMachine::getStateInformation(juce::MemoryBlock& destData)
{
    const std::lock_guard<std::mutex> lock(stateMutex);
    juce::DynamicObject::Ptr root = new juce::DynamicObject();
    root->setProperty("version", kStateVersion);
    root->setProperty("length", length);
    root->setProperty("recordEnabled", recordEnabled);
    root->setProperty("recordHead", recordHead);
    root->setProperty("playHead", playHead);

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
    const auto recordVar = obj->getProperty("recordEnabled");
    if (!recordVar.isVoid())
        recordEnabled = static_cast<bool>(recordVar);
    const auto recordHeadVar = obj->getProperty("recordHead");
    if (!recordHeadVar.isVoid())
        recordHead = static_cast<int>(recordHeadVar);
    const auto playHeadVar = obj->getProperty("playHead");
    if (!playHeadVar.isVoid())
        playHead = static_cast<int>(playHeadVar);

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
    if (recordHead < 0 || recordHead >= length)
        recordHead = 0;
    if (playHead < -1 || playHead >= length)
        playHead = -1;
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
