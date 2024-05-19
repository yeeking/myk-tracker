/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin processor.

  ==============================================================================
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
PluginProcessor::PluginProcessor()
#ifndef JucePlugin_PreferredChannelConfigurations
     : AudioProcessor (BusesProperties()
                     #if ! JucePlugin_IsMidiEffect
                      #if ! JucePlugin_IsSynth
                       .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                      #endif
                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)
                     #endif
                       ), elapsedSamples{0}, sequencer{4, 4}, seqEditor{&sequencer}, samplesPerTick{44100/(120/60)/4}
#endif
{
    
    CommandProcessor::assignMasterClock(&ticker);
    CommandProcessor::initialiseMIDI(this);

    // sequencer.decrementSeqParam(0, 1);
    // sequencer.decrementSeqParam(0, 1);

    // put some test notes into the sequencer to see if they flow through



    
}

PluginProcessor::~PluginProcessor()
{
}




//==============================================================================
const juce::String PluginProcessor::getName() const
{
    return JucePlugin_Name;
}

bool PluginProcessor::acceptsMidi() const
{
   #if JucePlugin_WantsMidiInput
    return true;
   #else
    return false;
   #endif
}

bool PluginProcessor::producesMidi() const
{
   #if JucePlugin_ProducesMidiOutput
    return true;
   #else
    return false;
   #endif
}

bool PluginProcessor::isMidiEffect() const
{
   #if JucePlugin_IsMidiEffect
    return true;
   #else
    return false;
   #endif
}

double PluginProcessor::getTailLengthSeconds() const
{
    return 0.0;
}

int PluginProcessor::getNumPrograms()
{
    return 1;   // NB: some hosts don't cope very well if you tell them there are 0 programs,
                // so this should be at least 1, even if you're not really implementing programs.
}

int PluginProcessor::getCurrentProgram()
{
    return 0;
}

void PluginProcessor::setCurrentProgram (int index)
{
}

const juce::String PluginProcessor::getProgramName (int index)
{
    return {};
}

void PluginProcessor::changeProgramName (int index, const juce::String& newName)
{
}

//==============================================================================
void PluginProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    // Use this method as the place to do any pre-playback
    // initialisation that you need..
}

void PluginProcessor::releaseResources()
{
    // When playback stops, you can use this as an opportunity to free up any
    // spare memory, etc.
}

#ifndef JucePlugin_PreferredChannelConfigurations
bool PluginProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
  #if JucePlugin_IsMidiEffect
    juce::ignoreUnused (layouts);
    return true;
  #else
    // This is the place where you check if the layout is supported.
    // In this template code we only support mono or stereo.
    // Some plugin hosts, such as certain GarageBand versions, will only
    // load plugins that support stereo bus layouts.
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::mono()
     && layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

    // This checks if the input layout matches the output layout
   #if ! JucePlugin_IsSynth
    if (layouts.getMainOutputChannelSet() != layouts.getMainInputChannelSet())
        return false;
   #endif

    return true;
  #endif
}
#endif

void PluginProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;
    unsigned long blockStartSample = elapsedSamples;
    unsigned long blockEndSample = elapsedSamples + (unsigned long) getBlockSize();
    for (int i=0;i<getBlockSize(); ++i){
        elapsedSamples ++;
        if (elapsedSamples % samplesPerTick == 0){
            // tick is from the clockabs interface and it keeps track of the absolute tick 
            ticker.tick(); // 
            // this will cause any pending messages to be added to 'midiToSend'
            sequencer.tick();
        }
    }
    // to get sample-accurate midi as opposed to block-accurate midi (!)
    // now add any midi that should have occured within this block
    // to midiMessages, but with an offset value within this block
    
    juce::MidiBuffer futureMidi;    // store messages from midiToSend from the future here. 
    juce::MidiBuffer::Iterator iterator(midiToSend);
    juce::MidiMessage message;
    int samplePosition;
    while (iterator.getNextEvent(message, samplePosition)) {
        // std::cout << "msg samplepos " << samplePosition << " block end " << blockEndSample << std::endl;
        if ((unsigned long)samplePosition <= blockEndSample) {
            // fix it to be an offset within the current block
            // 
            // std::cout << "play now" << std::endl;
            midiMessages.addEvent(message, (unsigned long) samplePosition - blockStartSample);
        }
        else{// it is in the future 
            // std::cout << "adding future event at " << message.getTimeStamp() << ":--- " << message.getDescription() << std::endl;
            futureMidi.addEvent(message, samplePosition);
        }
    }
    // std::cout << "future " << futureMidi.getNumEvents() << " present " << midiMessages.getNumEvents() << std::endl;
    // now put the future midi temp buffer into midiToSend for future sending!
    midiToSend.clear();
    midiToSend.swapWith(futureMidi);

}

//==============================================================================
bool PluginProcessor::hasEditor() const
{
    return true; // (change this to false if you choose to not supply an editor)
}

juce::AudioProcessorEditor* PluginProcessor::createEditor()
{
    return new PluginEditor (*this);
}

//==============================================================================
void PluginProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    // You should use this method to store your parameters in the memory block.
    // You could do that either as raw data, or use the XML or ValueTree classes
    // as intermediaries to make it easy to save and load complex data.
}

void PluginProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    // You should use this method to restore your parameters from this memory block,
    // whose contents will have been created by the getStateInformation() call.
}

//==============================================================================
// This creates new instances of the plugin..
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new PluginProcessor();
}

Sequencer* PluginProcessor::getSequencer()
{
    return &sequencer;
}

SequencerEditor* PluginProcessor::getSequenceEditor()
{
    return &seqEditor;
}
////////////// MIDIUtils interface 

void PluginProcessor::allNotesOff()
{
    for (int chan = 1; chan < 17; ++chan){
        midiToSend.addEvent(MidiMessage::allNotesOff(chan), elapsedSamples);
    }
}
void PluginProcessor::playSingleNote(unsigned short channel, unsigned short note, unsigned short velocity, long offTick)
{
    long offSample =  samplesPerTick * offTick;
    // std::cout << "playSingleNote " << channel << "," << note << "," << velocity << " on " << elapsedSamples << " offtick " << offTick << " off sample " << offSample <<  std::endl;  

    // generate a note on and a note off 
    midiToSend.addEvent(MidiMessage::noteOn((int)channel, (int)note, (uint8)velocity), elapsedSamples);
    midiToSend.addEvent(MidiMessage::noteOff((int)channel, (int)note, (uint8)velocity), offSample);
    
}
void PluginProcessor::sendQueuedMessages(long tick)
{
    // this is blank as midi gets sent by moving it from midiToSend to the processBlock's midi buffer

}

////////////// end MIDIUtils interface 
