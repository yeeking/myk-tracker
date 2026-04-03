#pragma once

#include <JuceHeader.h>
#include <memory>

namespace tracker::standalone
{

class TrackerStandaloneProcessorPlayer;

class StandalonePluginHolder : private juce::ChangeListener,
                               private juce::Value::Listener
{
public:
    struct PluginInOuts
    {
        short numIns = 0;
        short numOuts = 0;
    };

    StandalonePluginHolder(juce::PropertySet* settingsToUse,
                           bool takeOwnershipOfSettings = true,
                           const juce::String& preferredDefaultDeviceName = {},
                           const juce::AudioDeviceManager::AudioDeviceSetup* preferredSetupOptions = nullptr,
                           const juce::Array<PluginInOuts>& channels = {});
    ~StandalonePluginHolder() override;

    static StandalonePluginHolder* getInstance();

    juce::AudioProcessor* getAudioProcessor() const noexcept;
    void showAudioSettingsDialog();
    void savePluginState();
    bool getProcessorHasPotentialFeedbackLoop() const noexcept;
    juce::Value& getMuteInputValue() noexcept;

private:
    void valueChanged(juce::Value& value) override;
    void changeListenerCallback(juce::ChangeBroadcaster*) override;

    void handleCreatePlugin();
    void handleDeletePlugin();
    void startPlaying();
    void stopPlaying();
    void reloadAudioDeviceState(const juce::String& preferredDefaultDeviceName,
                                const juce::AudioDeviceManager::AudioDeviceSetup* preferredSetupOptions);
    void reloadPluginState();
    void saveAudioDeviceState();
    void setupAudioDevices(const juce::String& preferredDefaultDeviceName,
                           const juce::AudioDeviceManager::AudioDeviceSetup* preferredSetupOptions);
    void shutDownAudioDevices();
    void updateMidiOutput();
    int getNumInputChannels() const;
    int getNumOutputChannels() const;

    juce::OptionalScopedPointer<juce::PropertySet> settings;
    std::unique_ptr<juce::AudioProcessor> processor;
    juce::AudioDeviceManager deviceManager;
    std::unique_ptr<TrackerStandaloneProcessorPlayer> player;
    juce::Array<PluginInOuts> channelConfiguration;
    juce::MidiOutput* midiOutput = nullptr;
    bool processorHasPotentialFeedbackLoop = true;
    std::atomic<bool> muteInput { true };
    juce::Value shouldMuteInput;
    std::unique_ptr<juce::AudioDeviceManager::AudioDeviceSetup> options;

    inline static StandalonePluginHolder* currentInstance = nullptr;
};

} // namespace tracker::standalone
