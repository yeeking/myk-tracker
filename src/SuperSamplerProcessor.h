#pragma once
#include <JuceHeader.h>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "MachineInterface.h"

class SuperSamplePlayer;


//==============================================================================
// Audio processor that hosts multiple sample players and exposes sampler controls.
class SuperSamplerProcessor final : public juce::AudioProcessor, public MachineInterface
{
public:
    //==============================================================================
    SuperSamplerProcessor();
    ~SuperSamplerProcessor() override;

    //==============================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;

    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    //==============================================================================
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    //==============================================================================
    const juce::String getName() const override;

    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;

    //==============================================================================
    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int index, const juce::String& newName) override;

    //==============================================================================
    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // things for the api server to call
    // really you should make an 'interface' for this 
    // but for simplicity in this demo
    void messageReceivedFromWebAPI(std::string msg);
    void addSamplePlayerFromWeb();
    void removeSamplePlayer (int playerId);
    void sendSamplerStateToUI();
    void requestSampleLoadFromWeb (int playerId);
    juce::var getSamplerState() const;
    juce::String getWaveformSVGForPlayer (int playerId) const;
    std::vector<float> getWaveformPointsForPlayer (int playerId) const;
    std::string getVuStateJson() const;
    void setSampleRangeFromWeb (int playerId, int low, int high);
    void triggerFromWeb (int playerId);
    void setGainFromUI (int playerId, float gain);

    std::vector<std::vector<UIBox>> getUIBoxes(const MachineUiContext& context) override;
    bool handleIncomingNote(unsigned short note,
                            unsigned short velocity,
                            unsigned short durationTicks,
                            MachineNoteEvent& outEvent) override;
    void applyLearnedNote(int midiNote) override;
    void addEntry() override;
    void removeEntry(int entryIndex) override;

private:
    struct UiPlayerState
    {
        int id = 0;
        int midiLow = 0;
        int midiHigh = 127;
        float gain = 1.0f;
        bool isPlaying = false;
        float vuDb = -60.0f;
        std::string status;
        std::string fileName;
    };

    juce::AudioProcessorValueTreeState apvts;
    juce::File lastSampleDirectory;

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    void broadcastMessage (const juce::String& msg);
    void processSamplerBlock (juce::AudioBuffer<float>& buffer, const juce::MidiBuffer& midi);
    int addSamplePlayer();
    bool removeSamplePlayerInternal (int playerId);
    juce::var toVar() const;
    void loadSampleAsync (int playerId, const juce::File& file, std::function<void (bool, juce::String)> onComplete);
    bool setMidiRange (int playerId, int low, int high);
    bool setGain (int playerId, float gain);
    bool trigger (int playerId);
    juce::String getWaveformSVG (int playerId) const;
    std::vector<float> getWaveformPoints (int playerId) const;
    std::shared_ptr<std::string> getVuJson() const;
    juce::ValueTree exportToValueTree() const;
    void importFromValueTree (const juce::ValueTree& tree);
    bool loadSampleInternal (int playerId, const juce::File& file, juce::String& error);
    SuperSamplePlayer* getPlayer (int playerId) const;

    std::vector<std::unique_ptr<SuperSamplePlayer>> players;
    mutable std::mutex playerMutex;
    int nextId { 1 };
    juce::AudioFormatManager formatManager;
    std::string vuJson;
    mutable juce::SpinLock vuLock;

    std::vector<UiPlayerState> uiPlayers;
    std::vector<float> uiGlowLevels;
    int learningPlayerId { -1 };

    //==============================================================================
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SuperSamplerProcessor)
};
