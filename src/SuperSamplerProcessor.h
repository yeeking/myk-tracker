#pragma once
#include <JuceHeader.h>
#include <vector>
#include "SamplerEngine.h"


//==============================================================================
class SuperSamplerProcessor final : public juce::AudioProcessor
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
    void sendSamplerStateToUI();
    void requestSampleLoadFromWeb (int playerId);
    juce::var getSamplerState() const;
    juce::String getWaveformSVGForPlayer (int playerId) const;
    std::vector<float> getWaveformPointsForPlayer (int playerId) const;
    std::string getVuStateJson() const;
    void setSampleRangeFromWeb (int playerId, int low, int high);
    void triggerFromWeb (int playerId);
    void setGainFromUI (int playerId, float gain);

private:
    SamplerEngine sampler;
    juce::AudioProcessorValueTreeState apvts;
    juce::File lastSampleDirectory;

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    void broadcastMessage (const juce::String& msg);

    //==============================================================================
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SuperSamplerProcessor)
};
