#include "TrackerStandaloneHost.h"
#include "../TrackerMainProcessor.h"

#include <juce_audio_plugin_client/detail/juce_CreatePluginFilter.h>

juce::JUCEApplicationBase* juce_CreateApplication();

namespace tracker::standalone
{
namespace
{
bool shouldAutoStartSongPlayback()
{
    return juce::SystemStats::getEnvironmentVariable("MYK_TRACKER_AUTOPLAY", {}) == "1";
}

void initialiseIoBuffers(juce::Span<const float* const> ins,
                         juce::Span<float* const> outs,
                         int numSamples,
                         std::size_t processorIns,
                         std::size_t processorOuts,
                         juce::AudioBuffer<float>& tempBuffer,
                         std::vector<float*>& channels,
                         bool shouldMuteInput)
{
    const auto totalNumChannels = juce::jmax(processorIns, processorOuts);

    jassert(channels.capacity() >= totalNumChannels);
    jassert(static_cast<std::size_t>(tempBuffer.getNumChannels()) >= totalNumChannels);
    jassert(tempBuffer.getNumSamples() >= numSamples);

    channels.resize(totalNumChannels);

    const auto numBytes = static_cast<std::size_t>(numSamples) * sizeof(float);
    std::size_t tempBufferIndex = 0;

    for (std::size_t i = 0; i < totalNumChannels; ++i)
    {
        auto*& channelPtr = channels[i];
        channelPtr = i < outs.size()
            ? outs[i]
            : tempBuffer.getWritePointer(static_cast<int>(tempBufferIndex++));

        if (shouldMuteInput && i < processorIns)
        {
            juce::zeromem(channelPtr, numBytes);
        }
        else if (ins.size() == 1 && i < processorIns)
        {
            memcpy(channelPtr, ins.front(), numBytes);
        }
        else if (i < ins.size())
        {
            memcpy(channelPtr, ins[i], numBytes);
        }
        else
        {
            juce::zeromem(channelPtr, numBytes);
        }
    }

    for (std::size_t i = totalNumChannels; i < outs.size(); ++i)
        juce::zeromem(outs[i], numBytes);
}
} // namespace

class TrackerStandaloneProcessorPlayer final : public juce::AudioIODeviceCallback,
                                               public juce::MidiInputCallback
{
public:
    explicit TrackerStandaloneProcessorPlayer(std::atomic<bool>& muteInputFlag)
        : muteInput(muteInputFlag)
    {
    }

    void setProcessor(juce::AudioProcessor* processorToPlay)
    {
        const juce::ScopedLock sl(lock);

        if (processor == processorToPlay)
            return;

        sampleCount = 0;
        currentWorkgroup.reset();

        if (processorToPlay != nullptr && sampleRate > 0 && blockSize > 0)
        {
            defaultProcessorChannels = NumChannels { processorToPlay->getBusesLayout() };
            actualProcessorChannels = findMostSuitableLayout(*processorToPlay);

            if (processorToPlay->isMidiEffect())
                processorToPlay->setRateAndBufferSizeDetails(sampleRate, blockSize);
            else
                processorToPlay->setPlayConfigDetails(actualProcessorChannels.ins,
                                                      actualProcessorChannels.outs,
                                                      sampleRate,
                                                      blockSize);

            processorToPlay->setProcessingPrecision(juce::AudioProcessor::singlePrecision);
            processorToPlay->prepareToPlay(sampleRate, blockSize);
        }

        auto* oldOne = isPrepared ? processor : nullptr;
        processor = processorToPlay;
        isPrepared = true;
        resizeChannels();

        if (oldOne != nullptr)
            oldOne->releaseResources();
    }

    juce::AudioProcessor* getCurrentProcessor() const noexcept
    {
        return processor;
    }

    juce::MidiMessageCollector& getMidiMessageCollector() noexcept
    {
        return messageCollector;
    }

    void setMidiOutput(juce::MidiOutput* midiOutputToUse)
    {
        if (midiOutput != midiOutputToUse)
        {
            const juce::ScopedLock sl(lock);
            midiOutput = midiOutputToUse;
        }
    }

    void audioDeviceIOCallbackWithContext(const float* const* inputChannelData,
                                          int numInputChannels,
                                          float* const* outputChannelData,
                                          int numOutputChannels,
                                          int numSamples,
                                          const juce::AudioIODeviceCallbackContext& context) override
    {
        const juce::ScopedLock sl(lock);

        jassert(currentDevice != nullptr);
        jassert(sampleRate > 0 && blockSize > 0);

        incomingMidi.clear();
        messageCollector.removeNextBlockOfMessages(incomingMidi, numSamples);

        initialiseIoBuffers({ inputChannelData, static_cast<std::size_t>(numInputChannels) },
                            { outputChannelData, static_cast<std::size_t>(numOutputChannels) },
                            numSamples,
                            static_cast<std::size_t>(actualProcessorChannels.ins),
                            static_cast<std::size_t>(actualProcessorChannels.outs),
                            tempBuffer,
                            channels,
                            muteInput.load(std::memory_order_relaxed));

        const auto totalNumChannels = juce::jmax(actualProcessorChannels.ins, actualProcessorChannels.outs);
        juce::AudioBuffer<float> buffer(channels.data(), totalNumChannels, numSamples);

        if (processor != nullptr)
        {
            const juce::ScopedLock processorLock(processor->getCallbackLock());

            if (std::exchange(currentWorkgroup, currentDevice->getWorkgroup()) != currentDevice->getWorkgroup())
                processor->audioWorkgroupContextChanged(currentWorkgroup);

            class PlayHead final : private juce::AudioPlayHead
            {
            public:
                PlayHead(juce::AudioProcessor& proc,
                         juce::Optional<uint64_t> hostTimeIn,
                         uint64_t sampleCountIn,
                         double sampleRateIn)
                    : processor(proc),
                      hostTimeNs(hostTimeIn),
                      sampleCount(sampleCountIn),
                      seconds(static_cast<double>(sampleCountIn) / sampleRateIn)
                {
                    if (useThisPlayhead)
                        processor.setPlayHead(this);
                }

                ~PlayHead() override
                {
                    if (useThisPlayhead)
                        processor.setPlayHead(nullptr);
                }

            private:
                juce::Optional<juce::AudioPlayHead::PositionInfo> getPosition() const override
                {
                    juce::AudioPlayHead::PositionInfo info;
                    info.setHostTimeNs(hostTimeNs);
                    info.setTimeInSamples(static_cast<int64_t>(sampleCount));
                    info.setTimeInSeconds(seconds);
                    return info;
                }

                juce::AudioProcessor& processor;
                juce::Optional<uint64_t> hostTimeNs;
                uint64_t sampleCount;
                double seconds;
                bool useThisPlayhead = processor.getPlayHead() == nullptr;
            };

            PlayHead playHead { *processor,
                                context.hostTimeNs != nullptr ? juce::makeOptional(*context.hostTimeNs) : juce::nullopt,
                                sampleCount,
                                sampleRate };

            sampleCount += static_cast<uint64_t>(numSamples);

            if (!processor->isSuspended())
            {
                processor->processBlock(buffer, incomingMidi);

                if (midiOutput != nullptr)
                {
                    if (midiOutput->isBackgroundThreadRunning())
                    {
                        const auto standaloneOutputOffsetMs = (1000.0 * numSamples) / sampleRate;
                        midiOutput->sendBlockOfMessages(incomingMidi,
                                                        juce::Time::getMillisecondCounterHiRes() + standaloneOutputOffsetMs,
                                                        sampleRate);
                    }
                    else
                    {
                        midiOutput->sendBlockOfMessagesNow(incomingMidi);
                    }
                }

                return;
            }
        }

        for (int i = 0; i < numOutputChannels; ++i)
            juce::FloatVectorOperations::clear(outputChannelData[i], numSamples);
    }

    void audioDeviceAboutToStart(juce::AudioIODevice* device) override
    {
        currentDevice = device;
        const auto newSampleRate = device->getCurrentSampleRate();
        const auto newBlockSize = device->getCurrentBufferSizeSamples();
        const auto numChansIn = device->getActiveInputChannels().countNumberOfSetBits();
        const auto numChansOut = device->getActiveOutputChannels().countNumberOfSetBits();

        const juce::ScopedLock sl(lock);

        sampleRate = newSampleRate;
        blockSize = newBlockSize;
        deviceChannels = { numChansIn, numChansOut };

        resizeChannels();
        messageCollector.reset(sampleRate);
        currentWorkgroup.reset();

        if (processor != nullptr)
        {
            if (isPrepared)
                processor->releaseResources();

            auto* oldProcessor = processor;
            setProcessor(nullptr);
            setProcessor(oldProcessor);
        }
    }

    void audioDeviceStopped() override
    {
        const juce::ScopedLock sl(lock);

        if (processor != nullptr && isPrepared)
            processor->releaseResources();

        sampleRate = 0.0;
        blockSize = 0;
        isPrepared = false;
        tempBuffer.setSize(1, 1);
        currentDevice = nullptr;
        currentWorkgroup.reset();
    }

    void handleIncomingMidiMessage(juce::MidiInput*, const juce::MidiMessage& message) override
    {
        messageCollector.addMessageToQueue(message);
    }

private:
    struct NumChannels
    {
        NumChannels() = default;
        NumChannels(int numIns, int numOuts) : ins(numIns), outs(numOuts) {}
        explicit NumChannels(const juce::AudioProcessor::BusesLayout& layout)
            : ins(layout.getNumChannels(true, 0)),
              outs(layout.getNumChannels(false, 0))
        {
        }

        juce::AudioProcessor::BusesLayout toLayout() const
        {
            return { { juce::AudioChannelSet::canonicalChannelSet(ins) },
                     { juce::AudioChannelSet::canonicalChannelSet(outs) } };
        }

        int ins = 0;
        int outs = 0;
    };

    NumChannels findMostSuitableLayout(const juce::AudioProcessor& proc) const
    {
        if (proc.isMidiEffect())
            return {};

        std::vector<NumChannels> layouts { deviceChannels };

        if (deviceChannels.ins == 0 || deviceChannels.ins == 1)
        {
            layouts.emplace_back(defaultProcessorChannels.ins, deviceChannels.outs);
            layouts.emplace_back(deviceChannels.outs, deviceChannels.outs);
        }

        const auto it = std::find_if(layouts.begin(), layouts.end(), [&] (const NumChannels& chans)
        {
            return proc.checkBusesLayoutSupported(chans.toLayout());
        });

        return it == layouts.end() ? defaultProcessorChannels : *it;
    }

    void resizeChannels()
    {
        const auto maxChannels = juce::jmax(deviceChannels.ins,
                                            deviceChannels.outs,
                                            actualProcessorChannels.ins,
                                            actualProcessorChannels.outs);
        channels.resize(static_cast<std::size_t>(maxChannels));
        tempBuffer.setSize(maxChannels, blockSize);
    }

    std::atomic<bool>& muteInput;
    juce::AudioProcessor* processor = nullptr;
    juce::CriticalSection lock;
    double sampleRate = 0.0;
    int blockSize = 0;
    bool isPrepared = false;
    NumChannels deviceChannels, defaultProcessorChannels, actualProcessorChannels;
    std::vector<float*> channels;
    juce::AudioBuffer<float> tempBuffer;
    juce::MidiBuffer incomingMidi;
    juce::MidiMessageCollector messageCollector;
    juce::MidiOutput* midiOutput = nullptr;
    uint64_t sampleCount = 0;
    juce::AudioIODevice* currentDevice = nullptr;
    juce::AudioWorkgroup currentWorkgroup;
};

class SettingsComponent final : public juce::Component
{
public:
    SettingsComponent(StandalonePluginHolder& pluginHolder,
                      juce::AudioDeviceManager& deviceManagerToUse,
                      int maxAudioInputChannels,
                      int maxAudioOutputChannels)
        : owner(pluginHolder),
          deviceSelector(deviceManagerToUse,
                         0,
                         maxAudioInputChannels,
                         0,
                         maxAudioOutputChannels,
                         true,
                         owner.getAudioProcessor() != nullptr && owner.getAudioProcessor()->producesMidi(),
                         true,
                         false),
          shouldMuteLabel("Feedback Loop:", "Feedback Loop:"),
          shouldMuteButton("Mute audio input")
    {
        setOpaque(true);

        shouldMuteButton.setClickingTogglesState(true);
        shouldMuteButton.getToggleStateValue().referTo(owner.getMuteInputValue());

        addAndMakeVisible(deviceSelector);

        if (owner.getProcessorHasPotentialFeedbackLoop())
        {
            addAndMakeVisible(shouldMuteButton);
            addAndMakeVisible(shouldMuteLabel);
            shouldMuteLabel.attachToComponent(&shouldMuteButton, true);
        }
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(getLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId));
    }

    void resized() override
    {
        const juce::ScopedValueSetter<bool> scope(isResizing, true);
        auto r = getLocalBounds();

        if (owner.getProcessorHasPotentialFeedbackLoop())
        {
            const auto itemHeight = deviceSelector.getItemHeight();
            auto extra = r.removeFromTop(itemHeight);
            const auto separatorHeight = itemHeight >> 1;
            shouldMuteButton.setBounds({ extra.proportionOfWidth(0.35f),
                                         separatorHeight,
                                         extra.proportionOfWidth(0.60f),
                                         deviceSelector.getItemHeight() });
            r.removeFromTop(separatorHeight);
        }

        deviceSelector.setBounds(r);
    }

    void childBoundsChanged(juce::Component* childComp) override
    {
        if (!isResizing && childComp == &deviceSelector)
            setToRecommendedSize();
    }

    void setToRecommendedSize()
    {
        const auto extraHeight = [&]
        {
            if (!owner.getProcessorHasPotentialFeedbackLoop())
                return 0;

            const auto itemHeight = deviceSelector.getItemHeight();
            const auto separatorHeight = itemHeight >> 1;
            return itemHeight + separatorHeight;
        }();

        setSize(getWidth(), deviceSelector.getHeight() + extraHeight);
    }

private:
    StandalonePluginHolder& owner;
    juce::AudioDeviceSelectorComponent deviceSelector;
    juce::Label shouldMuteLabel;
    juce::ToggleButton shouldMuteButton;
    bool isResizing = false;
};

class StandaloneFilterWindow final : public juce::DocumentWindow
{
public:
    StandaloneFilterWindow(const juce::String& title,
                           juce::Colour backgroundColour,
                           std::unique_ptr<StandalonePluginHolder> pluginHolderIn)
        : juce::DocumentWindow(title, backgroundColour, juce::DocumentWindow::minimiseButton | juce::DocumentWindow::closeButton),
          pluginHolder(std::move(pluginHolderIn))
    {
        if (auto* processor = pluginHolder->getAudioProcessor())
        {
            auto* editor = processor->createEditorIfNeeded();

            if (editor != nullptr)
            {
                setContentOwned(editor, true);
                setResizable(editor->isResizable(), false);
            }
        }

        centreWithSize(getWidth(), getHeight());
    }

    ~StandaloneFilterWindow() override
    {
        if (auto* content = getContentComponent())
            if (auto* editor = dynamic_cast<juce::AudioProcessorEditor*>(content))
                if (auto* processor = pluginHolder != nullptr ? pluginHolder->getAudioProcessor() : nullptr)
                    processor->editorBeingDeleted(editor);

        clearContentComponent();
    }

    void closeButtonPressed() override
    {
        if (auto* app = juce::JUCEApplicationBase::getInstance())
            app->systemRequestedQuit();
    }

    std::unique_ptr<StandalonePluginHolder> pluginHolder;
};

class StandaloneFilterApp final : public juce::JUCEApplication
{
public:
    StandaloneFilterApp()
    {
        juce::PropertiesFile::Options appOptions;
        appOptions.applicationName = juce::CharPointer_UTF8(JucePlugin_Name);
        appOptions.filenameSuffix = ".settings";
        appOptions.osxLibrarySubFolder = "Application Support";
       #if JUCE_LINUX || JUCE_BSD
        appOptions.folderName = "~/.config";
       #else
        appOptions.folderName = "";
       #endif
        appProperties.setStorageParameters(appOptions);
    }

    const juce::String getApplicationName() override { return juce::CharPointer_UTF8(JucePlugin_Name); }
    const juce::String getApplicationVersion() override { return JucePlugin_VersionString; }
    bool moreThanOneInstanceAllowed() override { return true; }
    void anotherInstanceStarted(const juce::String&) override {}

    void initialise(const juce::String&) override
    {
        if (juce::Desktop::getInstance().getDisplays().displays.isEmpty())
        {
            pluginHolder = createPluginHolder();
            return;
        }

        mainWindow = std::make_unique<StandaloneFilterWindow>(getApplicationName(),
                                                              juce::LookAndFeel::getDefaultLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId),
                                                              createPluginHolder());
        mainWindow->setVisible(true);
    }

    void shutdown() override
    {
        pluginHolder = nullptr;
        mainWindow = nullptr;
        appProperties.saveIfNeeded();
    }

    void systemRequestedQuit() override
    {
        if (pluginHolder != nullptr)
            pluginHolder->savePluginState();

        if (mainWindow != nullptr)
            mainWindow->pluginHolder->savePluginState();

        if (juce::ModalComponentManager::getInstance()->cancelAllModalComponents())
        {
            juce::Timer::callAfterDelay(100, []()
            {
                if (auto* app = juce::JUCEApplicationBase::getInstance())
                    app->systemRequestedQuit();
            });
        }
        else
        {
            quit();
        }
    }

private:
    std::unique_ptr<StandalonePluginHolder> createPluginHolder()
    {
       #ifdef JucePlugin_PreferredChannelConfigurations
        constexpr StandalonePluginHolder::PluginInOuts channels[] { JucePlugin_PreferredChannelConfigurations };
        const juce::Array<StandalonePluginHolder::PluginInOuts> channelConfig(channels, juce::numElementsInArray(channels));
       #else
        const juce::Array<StandalonePluginHolder::PluginInOuts> channelConfig;
       #endif

        return std::make_unique<StandalonePluginHolder>(appProperties.getUserSettings(),
                                                        false,
                                                        juce::String{},
                                                        nullptr,
                                                        channelConfig);
    }

    juce::ApplicationProperties appProperties;
    std::unique_ptr<StandaloneFilterWindow> mainWindow;
    std::unique_ptr<StandalonePluginHolder> pluginHolder;
};
static juce::JUCEApplicationBase* createStandaloneApplication()
{
    return new StandaloneFilterApp();
}

StandalonePluginHolder::StandalonePluginHolder(juce::PropertySet* settingsToUse,
                                               bool takeOwnershipOfSettings,
                                               const juce::String& preferredDefaultDeviceName,
                                               const juce::AudioDeviceManager::AudioDeviceSetup* preferredSetupOptions,
                                               const juce::Array<PluginInOuts>& channels)
    : settings(settingsToUse, takeOwnershipOfSettings),
      player(std::make_unique<TrackerStandaloneProcessorPlayer>(muteInput)),
      channelConfiguration(channels)
{
    jassert(currentInstance == nullptr);
    currentInstance = this;

    shouldMuteInput.addListener(this);
    shouldMuteInput = true;

    handleCreatePlugin();

    if (preferredSetupOptions != nullptr)
        options = std::make_unique<juce::AudioDeviceManager::AudioDeviceSetup>(*preferredSetupOptions);

    setupAudioDevices(preferredDefaultDeviceName, options.get());
    reloadPluginState();
    startPlaying();
}

StandalonePluginHolder::~StandalonePluginHolder()
{
    if (midiOutput != nullptr && midiOutput->isBackgroundThreadRunning())
        midiOutput->stopBackgroundThread();

    stopPlaying();
    handleDeletePlugin();
    shutDownAudioDevices();
    currentInstance = nullptr;
}

StandalonePluginHolder* StandalonePluginHolder::getInstance()
{
    return currentInstance;
}

juce::AudioProcessor* StandalonePluginHolder::getAudioProcessor() const noexcept
{
    return processor.get();
}

bool StandalonePluginHolder::getProcessorHasPotentialFeedbackLoop() const noexcept
{
    return processorHasPotentialFeedbackLoop;
}

juce::Value& StandalonePluginHolder::getMuteInputValue() noexcept
{
    return shouldMuteInput;
}

void StandalonePluginHolder::valueChanged(juce::Value& value)
{
    muteInput.store(static_cast<bool>(value.getValue()), std::memory_order_relaxed);
}

void StandalonePluginHolder::changeListenerCallback(juce::ChangeBroadcaster*)
{
    updateMidiOutput();
}

void StandalonePluginHolder::handleCreatePlugin()
{
    processor = juce::createPluginFilterOfType(juce::AudioProcessor::wrapperType_Standalone);
    processor->disableNonMainBuses();
    processor->setRateAndBufferSizeDetails(44100, 512);
    processorHasPotentialFeedbackLoop = (getNumInputChannels() > 0 && getNumOutputChannels() > 0);
    if (shouldAutoStartSongPlayback())
        if (auto* trackerProcessor = dynamic_cast<TrackerMainProcessor*>(processor.get()))
        {
            trackerProcessor->setSongPlayMode(SongPlayMode::song);
            trackerProcessor->toggleSongPlayback();
        }
}

void StandalonePluginHolder::handleDeletePlugin()
{
    processor = nullptr;
}

void StandalonePluginHolder::startPlaying()
{
    player->setProcessor(processor.get());
}

void StandalonePluginHolder::stopPlaying()
{
    player->setProcessor(nullptr);
}

int StandalonePluginHolder::getNumInputChannels() const
{
    if (processor == nullptr)
        return 0;

    return channelConfiguration.size() > 0
        ? channelConfiguration[0].numIns
        : processor->getMainBusNumInputChannels();
}

int StandalonePluginHolder::getNumOutputChannels() const
{
    if (processor == nullptr)
        return 0;

    return channelConfiguration.size() > 0
        ? channelConfiguration[0].numOuts
        : processor->getMainBusNumOutputChannels();
}

void StandalonePluginHolder::reloadAudioDeviceState(const juce::String& preferredDefaultDeviceName,
                                                    const juce::AudioDeviceManager::AudioDeviceSetup* preferredSetupOptions)
{
    std::unique_ptr<juce::XmlElement> savedState;

    if (settings != nullptr)
    {
        savedState = settings->getXmlValue("audioSetup");
        shouldMuteInput.setValue(settings->getBoolValue("shouldMuteInput", true));
    }

    auto inputChannels = getNumInputChannels();
    auto outputChannels = getNumOutputChannels();

    if (inputChannels == 0 && outputChannels == 0 && processor->isMidiEffect())
        outputChannels = 1;

    deviceManager.initialise(inputChannels,
                             outputChannels,
                             savedState.get(),
                             true,
                             preferredDefaultDeviceName,
                             preferredSetupOptions);
}

void StandalonePluginHolder::reloadPluginState()
{
    if (settings == nullptr)
        return;

    juce::MemoryBlock data;
    if (data.fromBase64Encoding(settings->getValue("filterState")) && data.getSize() > 0)
        processor->setStateInformation(data.getData(), static_cast<int>(data.getSize()));
}

void StandalonePluginHolder::savePluginState()
{
    if (settings == nullptr || processor == nullptr)
        return;

    juce::MemoryBlock data;
    processor->getStateInformation(data);
    settings->setValue("filterState", data.toBase64Encoding());
}

void StandalonePluginHolder::saveAudioDeviceState()
{
    if (settings == nullptr)
        return;

    auto xml = deviceManager.createStateXml();
    settings->setValue("audioSetup", xml.get());
    settings->setValue("shouldMuteInput", static_cast<bool>(shouldMuteInput.getValue()));
}

void StandalonePluginHolder::setupAudioDevices(const juce::String& preferredDefaultDeviceName,
                                               const juce::AudioDeviceManager::AudioDeviceSetup* preferredSetupOptions)
{
    deviceManager.addAudioCallback(player.get());
    deviceManager.addMidiInputDeviceCallback({}, player.get());
    deviceManager.addChangeListener(this);

    reloadAudioDeviceState(preferredDefaultDeviceName, preferredSetupOptions);
    updateMidiOutput();
}

void StandalonePluginHolder::shutDownAudioDevices()
{
    saveAudioDeviceState();
    deviceManager.removeChangeListener(this);
    deviceManager.removeMidiInputDeviceCallback({}, player.get());
    deviceManager.removeAudioCallback(player.get());
}

void StandalonePluginHolder::updateMidiOutput()
{
    auto* defaultMidiOutput = deviceManager.getDefaultMidiOutput();

    if (midiOutput != defaultMidiOutput)
    {
        if (midiOutput != nullptr && midiOutput->isBackgroundThreadRunning())
            midiOutput->stopBackgroundThread();

        midiOutput = defaultMidiOutput;
    }

    if (midiOutput != nullptr && !midiOutput->isBackgroundThreadRunning())
        midiOutput->startBackgroundThread();

    player->setMidiOutput(midiOutput);
}

void StandalonePluginHolder::showAudioSettingsDialog()
{
    juce::DialogWindow::LaunchOptions launchOptions;

    int maxNumInputs = getNumInputChannels();
    int maxNumOutputs = getNumOutputChannels();

    if (auto* bus = processor->getBus(true, 0))
        maxNumInputs = juce::jmax(0, bus->getDefaultLayout().size());

    if (auto* bus = processor->getBus(false, 0))
        maxNumOutputs = juce::jmax(0, bus->getDefaultLayout().size());

    auto content = std::make_unique<SettingsComponent>(*this, deviceManager, maxNumInputs, maxNumOutputs);
    content->setSize(500, 550);
    content->setToRecommendedSize();

    launchOptions.content.setOwned(content.release());
    launchOptions.dialogTitle = TRANS("Audio/MIDI Settings");
    launchOptions.dialogBackgroundColour = launchOptions.content->getLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId);
    launchOptions.escapeKeyTriggersCloseButton = true;
    launchOptions.useNativeTitleBar = true;
    launchOptions.resizable = false;
    launchOptions.launchAsync();
}

} // namespace tracker::standalone

juce::JUCEApplicationBase* juce_CreateApplication()
{
    return tracker::standalone::createStandaloneApplication();
}
