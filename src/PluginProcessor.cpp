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
                       ), 
                       sequencer{16, 8}, seqEditor{&sequencer}, 
                       // seq, clock, editor
                       trackerController{&sequencer, this, &seqEditor},
                       elapsedSamples{0},maxHorizon{44100 * 3600}, 
                       samplesPerTick{44100/(120/60)/8}, bpm{120}, 
                       outstandingNoteOffs{0}
#endif
{
    
    CommandProcessor::assignMasterClock(this);
    CommandProcessor::assignMidiUtils(this);


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
    bool receivedMidi = false; 
    for (const MidiMessageMetadata metadata : midiMessages){
        if (metadata.getMessage().isNoteOn()) {
            DBG("Got a note " << metadata.getMessage().getNoteNumber());
            // add a note to the current sequence 
            // and move it on a step 

            // get the armed sequence 

            // seqEditor.enterStepData(metadata.getMessage().getNoteNumber(), Step::noteInd);
            receivedMidi = true; 
        }
    }
    if (receivedMidi) {
        // tell the 
    }

    int blockStartSample = elapsedSamples;
    int blockEndSample = (elapsedSamples + getBlockSize()) % maxHorizon;
    // int blockSize = getBlockSize();
    for (int i=0;i<blockSize; ++i){
        // weird but since juce midi sample offsets are int not unsigned long, 
        // I set a maximum elapsedSamples and mod on that, instead of just elapsedSamples ++; forever
        // otherwise, behaviour after 13 hours is undefined (samples @441k you can fit in an int)
        elapsedSamples = (++elapsedSamples) % maxHorizon;
        if (elapsedSamples % samplesPerTick == 0){
            // tick is from the clockabs class and it keeps track of the absolute tick 
            this->tick(); 
            // this will cause any pending messages to be added to 'midiToSend'
            sequencer.tick();
        }
    }
    // to get sample-accurate midi as opposed to block-accurate midi (!)
    // now add any midi that should have occurred within this block
    // to midiMessages, but with an offset value within this block
    
    juce::MidiBuffer futureMidi;    // store messages from midiToSend from the future here. 
    juce::MidiMessage message;
    // int samplePosition;
    for (const MidiMessageMetadata metadata : midiToSend){
        if (blockEndSample < blockStartSample){// we wrapped block end back around 
            // DBG("processBlock wrapped events");

            // after block start or before block end (as block end is before block start due to wrap around)
            if ( metadata.samplePosition >= blockStartSample ||  
                metadata.samplePosition < blockEndSample) {
                midiMessages.addEvent(metadata.getMessage(),  metadata.samplePosition - blockStartSample);
            }
            else{// it is in the future            
                futureMidi.addEvent(metadata.getMessage(),  metadata.samplePosition);
            }
            if (metadata.getMessage().isNoteOff()) outstandingNoteOffs --;
        }
        if (blockStartSample < blockEndSample){
            // normal case where block start is before block end as no wrap has occurred. 
            if ( metadata.samplePosition >= blockStartSample && 
                metadata.samplePosition < blockEndSample) {
                // DBG("Event this block " << metadata.samplePosition - blockStartSample);
                midiMessages.addEvent(metadata.getMessage(),  metadata.samplePosition - blockStartSample);
            }
            else{// it is in the future            

                futureMidi.addEvent(metadata.getMessage(),  metadata.samplePosition);
            }
            if (metadata.getMessage().isNoteOff()) outstandingNoteOffs --;

        }
    }
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

TrackerController* PluginProcessor::getTrackerController()
{
    return &trackerController;
}

////////////// MIDIUtils interface 

void PluginProcessor::allNotesOff()
{
    midiToSend.clear();// remove anything that's hanging around. 
    for (int chan = 1; chan < 17; ++chan){
        midiToSend.addEvent(MidiMessage::allNotesOff(chan), static_cast<int>(elapsedSamples));
    }
}
void PluginProcessor::playSingleNote(unsigned short channel, unsigned short note, unsigned short velocity, unsigned short durInTicks)
{
    channel ++; // channels come in 0-15 but we want 1-16
    // offtick is an absolute tick from the start of time 
    // but we have a max horizon which is how far in the future we can set things 
    int offSample =  elapsedSamples +  (samplesPerTick * static_cast<int>(durInTicks)) % maxHorizon;
    // DBG("playSingleNote note start/ end " << elapsedSamples << " -> " << offSample << " tick length " << durInTicks << " hor " << maxHorizon);
    // generate a note on and a note off 
    // note on is right now 
    midiToSend.addEvent(MidiMessage::noteOn((int)channel, (int)note, (uint8)velocity), elapsedSamples);
    // note off is now + length 
    midiToSend.addEvent(MidiMessage::noteOff((int)channel, (int)note, (uint8)velocity), offSample);
    // assert()
    outstandingNoteOffs ++ ;
}
void PluginProcessor::sendQueuedMessages(long tick)
{
    // this is blank as midi gets sent by moving it from midiToSend to the processBlock's midi buffer

}

////////////// end MIDIUtils interface 


void PluginProcessor::setBPM(double _bpm)
{   
    assert(_bpm > 0);
    // update tick interval in samples 
    samplesPerTick = getSampleRate() *  (60/_bpm) /8;
    bpm = _bpm;
}

double PluginProcessor::getBPM()
{
    return bpm;
}


void PluginProcessor::clearPendingEvents()
{
    midiToSend.clear();
}