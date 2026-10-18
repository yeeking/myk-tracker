#include "PortableTrackerState.h"

namespace
{
constexpr auto kStateEntry = "state.xml";
constexpr auto kManifestEntry = "manifest.json";
constexpr int kFormatVersion = 1;

juce::String assetPath(const PortableSampleAsset& asset)
{
    return "audio/stack-" + juce::String(asset.stackId) + "/player-" + juce::String(asset.playerId) + ".flac";
}

bool encodeFlac(const PortableSampleAsset& asset, juce::MemoryBlock& result, juce::String& error)
{
    if (asset.audio.getNumChannels() < 1 || asset.audio.getNumChannels() > 2
        || asset.audio.getNumSamples() < 1 || asset.sampleRate <= 0.0)
    {
        error = "INVALID SAMPLE DATA";
        return false;
    }

    std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::MemoryOutputStream>(result, false);
    juce::FlacAudioFormat format;
    auto writer = format.createWriterFor(stream,
        juce::AudioFormatWriterOptions{}.withSampleRate(asset.sampleRate)
            .withNumChannels(asset.audio.getNumChannels()).withBitsPerSample(24));
    if (writer == nullptr || !writer->writeFromAudioSampleBuffer(asset.audio, 0, asset.audio.getNumSamples()))
    {
        error = "COULD NOT ENCODE SAMPLE AUDIO";
        return false;
    }
    writer.reset();
    return result.getSize() > 0;
}

bool decodeFlac(const juce::MemoryBlock& encoded, PortableSampleAsset& asset, juce::String& error)
{
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    auto stream = std::make_unique<juce::MemoryInputStream>(encoded, false);
    std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(std::move(stream)));
    if (reader == nullptr || reader->sampleRate <= 0.0 || reader->lengthInSamples <= 0
        || reader->lengthInSamples > std::numeric_limits<int>::max() || reader->numChannels < 1)
    {
        error = "INVALID EMBEDDED AUDIO";
        return false;
    }
    const int channels = juce::jlimit(1, 2, static_cast<int>(reader->numChannels));
    asset.sampleRate = reader->sampleRate;
    asset.audio.setSize(channels, static_cast<int>(reader->lengthInSamples));
    if (!reader->read(&asset.audio, 0, asset.audio.getNumSamples(), 0, true, channels > 1))
    {
        error = "COULD NOT DECODE EMBEDDED AUDIO";
        return false;
    }
    return true;
}

bool readEntry(juce::ZipFile& archive, const juce::String& path, juce::MemoryBlock& data)
{
    const auto* entry = archive.getEntry(path);
    if (entry == nullptr) return false;
    std::unique_ptr<juce::InputStream> stream(archive.createStreamForEntry(*entry));
    return stream != nullptr && stream->readIntoMemoryBlock(data) > 0;
}
}

bool portable_tracker_state::isPortableFile(const juce::File& source)
{
    juce::FileInputStream input(source);
    return input.openedOk() && input.readInt() == static_cast<int>(0x04034b50);
}

bool portable_tracker_state::write(const juce::File& destination, const juce::MemoryBlock& state,
                                   const std::vector<PortableSampleAsset>& samples, juce::String& error)
{
    if (state.getSize() == 0) { error = "EMPTY TRACKER STATE"; return false; }
    juce::ZipFile::Builder builder;
    builder.addEntry(std::make_unique<juce::MemoryInputStream>(state, true), 9, kStateEntry, juce::Time::getCurrentTime());

    juce::Array<juce::var> manifestSamples;
    for (const auto& sample : samples)
    {
        juce::MemoryBlock encoded;
        if (!encodeFlac(sample, encoded, error)) return false;
        const auto path = assetPath(sample);
        builder.addEntry(std::make_unique<juce::MemoryInputStream>(encoded, true), 0, path, juce::Time::getCurrentTime());
        auto object = juce::DynamicObject::Ptr(new juce::DynamicObject());
        object->setProperty("stackId", sample.stackId);
        object->setProperty("playerId", sample.playerId);
        object->setProperty("path", path);
        object->setProperty("filePath", sample.filePath);
        object->setProperty("fileName", sample.fileName);
        manifestSamples.add(object.get());
    }
    auto manifest = juce::DynamicObject::Ptr(new juce::DynamicObject());
    manifest->setProperty("format", "myk-tracker-portable");
    manifest->setProperty("version", kFormatVersion);
    manifest->setProperty("stateEntry", kStateEntry);
    manifest->setProperty("samples", manifestSamples);
    const auto manifestText = juce::JSON::toString(juce::var(manifest.get()));
    builder.addEntry(std::make_unique<juce::MemoryInputStream>(manifestText.toRawUTF8(), manifestText.getNumBytesAsUTF8(), true),
                     9, kManifestEntry, juce::Time::getCurrentTime());

    juce::TemporaryFile temporary(destination, juce::TemporaryFile::useHiddenFile);
    std::unique_ptr<juce::FileOutputStream> output(temporary.getFile().createOutputStream());
    if (output == nullptr || !builder.writeToStream(*output, nullptr))
    {
        error = "COULD NOT WRITE PORTABLE STATE";
        return false;
    }
    output->flush();
    if (!temporary.overwriteTargetFileWithTemporary())
    {
        error = "COULD NOT WRITE PORTABLE STATE";
        return false;
    }
    return true;
}

bool portable_tracker_state::read(const juce::File& source, PortableTrackerDocument& document, juce::String& error)
{
    document = {};
    juce::ZipFile archive(source);
    juce::MemoryBlock manifestData;
    if (!readEntry(archive, kManifestEntry, manifestData)) { error = "MISSING PORTABLE MANIFEST"; return false; }
    const auto manifest = juce::JSON::fromString(manifestData.toString());
    if (!manifest.isObject() || manifest.getProperty("format", {}).toString() != "myk-tracker-portable"
        || static_cast<int>(manifest.getProperty("version", 0)) != kFormatVersion)
    { error = "UNSUPPORTED PORTABLE STATE"; return false; }
    const auto stateEntry = manifest.getProperty("stateEntry", kStateEntry).toString();
    if (!readEntry(archive, stateEntry, document.state)) { error = "MISSING PORTABLE STATE"; return false; }
    const auto samples = manifest.getProperty("samples", juce::var());
    if (!samples.isArray()) { error = "INVALID PORTABLE MANIFEST"; return false; }
    for (const auto& value : *samples.getArray())
    {
        if (!value.isObject()) { error = "INVALID PORTABLE MANIFEST"; return false; }
        PortableSampleAsset asset;
        asset.stackId = static_cast<int>(value.getProperty("stackId", -1));
        asset.playerId = static_cast<int>(value.getProperty("playerId", -1));
        asset.filePath = value.getProperty("filePath", {}).toString();
        asset.fileName = value.getProperty("fileName", {}).toString();
        if (asset.stackId < 0 || asset.playerId < 0) { error = "INVALID PORTABLE SAMPLE"; return false; }
        juce::MemoryBlock encoded;
        // Audio is optional at restore time: the state has already retained the
        // original path, so a damaged entry deliberately falls back to that
        // path rather than preventing the rest of a song from opening.
        if (readEntry(archive, value.getProperty("path", {}).toString(), encoded)
            && decodeFlac(encoded, asset, error))
            document.samples.push_back(std::move(asset));
        error.clear();
    }
    return true;
}
