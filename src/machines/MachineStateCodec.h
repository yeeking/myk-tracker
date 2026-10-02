#pragma once

#include <JuceHeader.h>

// Shared plumbing for the machine state codecs. The JSON property names and
// layout are part of the persisted plugin state; these helpers only factor
// out the validation/serialisation boilerplate and clamped property reads.

/** Parses a machine state blob into a JSON object, returning a void `var`
    when the blob is missing, not valid UTF-8, not valid JSON, or not an
    object. Callers check `isVoid()`. */
inline juce::var parseMachineStateJson(const void* data, int sizeInBytes)
{
    if (data == nullptr || sizeInBytes <= 0)
        return {};
    if (!juce::CharPointer_UTF8::isValidString(static_cast<const char*>(data), sizeInBytes))
        return {};
    const juce::String json = juce::String::fromUTF8(static_cast<const char*>(data), sizeInBytes);
    if (json.isEmpty())
        return {};
    const juce::var parsed = juce::JSON::fromString(json);
    return parsed.isObject() ? parsed : juce::var();
}

/** Serialises a machine state object using the canonical juce::JSON byte
    format and replaces the destination contents. */
inline void writeMachineStateJson(juce::MemoryBlock& destData, juce::var object)
{
    const juce::String json = juce::JSON::toString(object);
    destData.reset();
    destData.append(json.toRawUTF8(), json.getNumBytesAsUTF8());
}

/** Reads a float property, clamped to [min, max], falling back to the current
    value when the property is missing. */
inline float getFloatProperty(const juce::var& object, const juce::String& key, float defaultValue,
                              float minValue, float maxValue)
{
    return juce::jlimit(minValue, maxValue, static_cast<float>(object.getProperty(key, defaultValue)));
}

/** Reads an integer property, clamped to [min, max], falling back to the
    current value when the property is missing. */
inline int getIntProperty(const juce::var& object, const juce::String& key, int defaultValue,
                          int minValue, int maxValue)
{
    return juce::jlimit(minValue, maxValue, static_cast<int>(object.getProperty(key, defaultValue)));
}

/** Reads a boolean property stored as 0/1, falling back to the current value
    when the property is missing. */
inline bool getBoolProperty(const juce::var& object, const juce::String& key, bool defaultValue)
{
    return static_cast<int>(object.getProperty(key, defaultValue ? 1 : 0)) != 0;
}
