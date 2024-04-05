#include "Sequencer.h"

Step::Step() : active{true}
{
  data.push_back(0.0);
  data.push_back(0.0);
  data.push_back(0.0);
  data.push_back(0.0);
}
/** returns a copy of the data stored in this step*/
std::vector<double> Step::getData() const
{
  return data; 
}
std::vector<double>* Step::getDataDirect()
{
  return &data; 
}


/** sets the data stored in this step */
void Step::setData(std::vector<double> _data)
{
  this->data = _data; 
}

/** update one value in the data vector for this step*/
void Step::updateData(unsigned int dataInd, double value)
{
  if(dataInd < data.size()) data[dataInd] = value;
}
/** set the callback function called when this step is triggered*/
void Step::setCallback(std::function<void(std::vector<double>*)> callback)
{
  this->stepCallback = callback;
}
std::function<void(std::vector<double>*)> Step::getCallback()
{
  return this->stepCallback;
}

/** trigger this step, causing it to pass its data to its callback*/
void Step::trigger() 
{ 
  //std::cout << "Step::trigger" << std::endl;
  if (active && data[Step::note1Ind] != 0) stepCallback(&data);
}
/** toggle the activity status of this step*/
void Step::toggleActive()
{
  active = !active; 
}
/** returns the activity status of this step */
bool Step::isActive() const
{
  return active; 
}

Sequence::Sequence(Sequencer* _sequencer, 
                  unsigned int seqLength, 
                  unsigned short _midiChannel) 
: sequencer{_sequencer}, currentStep{0},  
  midiChannel{_midiChannel}, type{SequenceType::midiNote}, 
  transpose{0}, lengthAdjustment{0}, ticksPerStep{4}, originalTicksPerStep{4}, ticksElapsed{0}
  // , midiScaleToDrum{MidiUtils::getScaleMidiToDrumMidi()}
{
  currentLength = seqLength;
  for (unsigned int i=0;i<seqLength;i++)
  {
    Step s;
    s.setCallback([i](std::vector<double>* data){
      if (data->size() > 0){
        //std::cout << "Sequence::Sequence default step callback " << i << " triggered " << std::endl;
      }
    });
    steps.push_back(s);
  }
}

/** go to the next step */
void Sequence::tick(bool trigger)
{
  //std::cout << "Sequence::tick" << std::endl;
  ++ticksElapsed;

  if (ticksElapsed == ticksPerStep)
    {
      ticksElapsed = 0;
      if (trigger) {
      switch (type){
        case SequenceType::midiNote:
          triggerMidiNoteType();
          break;
        case SequenceType::drumMidi:
          triggerMidiDrumType();
          break;
        // case SequenceType::chordMidi:
        //   triggerMidiChordType();
        //   break;
          
        case SequenceType::transposer:
          triggerTransposeType();
          break;
        case SequenceType::lengthChanger:
          triggerLengthType();
          break;
        case SequenceType::tickChanger:
          triggerTickType();
          break;
        case SequenceType::chordMidi:
          break;
        case SequenceType::samplePlayer:
          break;
        default:
          std::cout << "Sequnce::tick warning unkown seq type" << std::endl;
          break;
      }
      if (currentLength + lengthAdjustment < 1) currentStep = 0;
      else currentStep = (++currentStep) % (currentLength + lengthAdjustment);
      if (currentStep >= steps.size()) currentStep = 0;
      assert(currentStep >= 0 && currentStep < steps.size());
      // switch off any adjusters when we are at step 0
      if (currentStep == 0) deactivateProcessors();

    } // end of if trigger

  }
  
}

void Sequence::deactivateProcessors()
{
    transpose = 0;
    lengthAdjustment = 0;
    ticksPerStep = originalTicksPerStep; 
    ticksElapsed = 0;
}

void Sequence::triggerMidiNoteType()
{
  // make a local copy
  Step s = steps[currentStep];
  // apply changes to local copy if needed      
  if(transpose > 0) 
  {
    std::vector<double>* data = s.getDataDirect();//  s.getData();
    if (data->at(Step::note1Ind) > 0 ) // only transpose non-zero steps
    {
      data->at(Step::note1Ind) = fmod(data->at(Step::note1Ind) + transpose, 127);
    }
  }
  // trigger the local, adjusted copy of the step
  s.trigger();
}

void Sequence::triggerMidiDrumType()
{
  // make a local copy
  Step s = steps[currentStep];
  // transpose the midi note into the drum domain
  std::vector<double>* data = s.getDataDirect();//  s.getData();
  data->at(Step::note1Ind) = midiScaleToDrum[(int) data->at(Step::note1Ind)];
  // apply changes to local copy if needed      
  if(transpose > 0) 
  {
    if (data->at(Step::note1Ind) > 0 ) // only transpose non-zero steps
    {
      data->at(Step::note1Ind) = fmod(data->at(Step::note1Ind) + transpose, 127);
    }
  }
  s.trigger();
}

void Sequence::triggerMidiChordType()
{
  // // this is a nice tricksy one
  // // we make several local copies of the step
  // // one for each note in the chord
  // std::vector<double>* data = steps[currentStep].getDataDirect();
  // assert(data->size() > 0);
  // if (data->at(Step::note1Ind) > 0)
  // {
  // std::vector<double> notes = ChordUtils::getChord(
  //  (unsigned int) data->at(Step::note1Ind),
  //  (unsigned int) data->at(Step::velInd)
  // );
  // std::cout << "Sequencer::note " << notes[0] << ":" << data->at(Step::note1Ind) << std::endl;

 
  // }   
}


void Sequence::triggerTransposeType()
{
  if (steps[currentStep].isActive() )
  {
    std::vector<double>* data = steps[currentStep].getDataDirect();
    if (data->at(Step::note1Ind) > 0 ) // only transpose non-zero steps
    {
      sequencer->getSequence(data->at(Step::channelInd))->setTranspose(
        fmod(data->at(Step::note1Ind), 12) );
    }
  }
} 

void Sequence::triggerLengthType()
{
  //return; 
  if (steps[currentStep].isActive())
  {
    std::vector<double>* data = steps[currentStep].getDataDirect();
    if (data->at(Step::note1Ind) > 0 ) // only transpose non-zero steps
    {
      sequencer->getSequence(data->at(Step::channelInd))->setLengthAdjustment(
        fmod(data->at(Step::note1Ind), 12) );
    }
  }   
}

void Sequence::triggerTickType()
{
  if (steps[currentStep].isActive() )
  {
    std::vector<double>* data = steps[currentStep].getDataDirect();
    if (data->at(Step::note1Ind) > 0 ) // only transpose non-zero steps
    {
      sequencer->getSequence(data->at(Step::channelInd))->setTicksPerStep(
        fmod(data->at(Step::note1Ind), 6) );
    }
  }
} 


void Sequence::setLengthAdjustment(signed int lenAdjust)
{
  // make sure we have enough steps
  this->ensureEnoughStepsForLength(currentLength + lenAdjust);
  this->lengthAdjustment = lenAdjust;
}

void Sequence::setTicksPerStep(int tps)
{
  if (tps < 1 || tps > 16) return; 
  this->originalTicksPerStep = tps;
  this->ticksElapsed = 0;
}

void Sequence::setTicksPerStepAdjustment(int tps)
{
    if (tps < 1 || tps > 16) return; 
  this->ticksPerStep = tps;
}

int Sequence::getTicksPerStep() const
{
  return this->originalTicksPerStep;
}

unsigned int Sequence::getCurrentStep() const
{
  return currentStep; 
} 
bool Sequence::assertStep(unsigned int step) const
{
  if (step >= steps.size() || step < 0) return false;
  return true; 
}
std::vector<double> Sequence::getStepData(int step) const
{
  return steps[step].getData();
}
std::vector<double>* Sequence::getStepDataDirect(int step)
{
  return steps[step].getDataDirect();
}
Step* Sequence::getStep(int step)
{
  return &steps[step];
}


std::vector<double> Sequence::getCurrentStepData() const
{
  return steps[currentStep].getData();
}
unsigned int Sequence::getLength() const
{
  return currentLength; 
}

void Sequence::ensureEnoughStepsForLength(int length)
{
  if (length > steps.size()) // bad need more steps
  {
    int toAdd = length - steps.size();
    for (int i=0; i < toAdd; ++i)
    {
      Step s;
      s.setCallback(
        steps[0].getCallback()
      );
      // set the channel
      s.getDataDirect()[Step::channelInd] = steps[0].getDataDirect()[Step::channelInd];
      steps.push_back(s);
    }
  }
}
void Sequence::setLength(int length)
{

  if (length < 1) return;
  if (length > steps.size()) return; 
  
  currentLength = length;
}

void Sequence::setStepData(unsigned int step, std::vector<double> data)
{
  steps[step].setData(data);
}
/** update a single data value in a given step*/
void Sequence::updateStepData(unsigned int step, unsigned int dataInd, double value)
{
  steps[step].updateData(dataInd, value);
}

void Sequence::setStepCallback(unsigned int step, 
                  std::function<void (std::vector<double>*)> callback)
{
  steps[step].setCallback(callback);
}
std::string Sequence::stepToString(int step) const
{
  std::vector<double> data = getStepData(step);
  if (data.size() > 0)
    return std::to_string(data[0]);
  else
    return "---";
}

unsigned int Sequence::howManySteps() const 
{
  //return steps.size();
  // case where length adjust is too high
  //if (currentLength + lengthAdjustment >= steps.size()) return currentLength;
  
  return currentLength + lengthAdjustment > 0 ? currentLength + lengthAdjustment : 1;
}

void Sequence::toggleActive(unsigned int step)
{
  steps[step].toggleActive();
}
bool Sequence::isStepActive(unsigned int step) const
{
  return steps[step].isActive();
}
void Sequence::setType(SequenceType type)
{
  this->type = type;
}
SequenceType Sequence::getType() const
{
  return this->type;
}

void Sequence::setTranspose(double transpose)
{
  this->transpose = transpose;
}

void Sequence::reset()
{
  for (Step& step : steps)
  {
    // activate the step
    if (!step.isActive()) step.toggleActive();
    // reset the data
    int dSize = step.getDataDirect()->size();
    for (auto i = 0; i < dSize; i++)
    {
      step.updateData(i, 0.0);
    }
  }
}

/////////////////////// Sequencer 

Sequencer::Sequencer(unsigned int seqCount, unsigned int seqLength) 
{
  for (auto i=0;i<seqCount;++i)
  {
    sequences.push_back(Sequence{this, seqLength});
  }
}

Sequencer::~Sequencer()
{
}

void Sequencer::copyChannelAndTypeSettings(Sequencer* otherSeq)
{
  // ignore if diff sizes
  assert (otherSeq->sequences.size() == this->sequences.size()); 
  //if (otherSeq->sequences.size() != this->sequences.size()) return; 
  for (int seq = 0; seq < this->sequences.size(); ++seq)
  { 
    this->sequences[seq].setType(otherSeq->sequences[seq].getType());
    // assign the same channel
    double channel = otherSeq->sequences[seq].getStepDataDirect(0)->at(Step::channelInd);
    for (int step=0; step < this->sequences[seq].howManySteps(); ++step)
    {
      this->updateStepData(seq, step, Step::channelInd, channel);
    } 
  }
}


unsigned int Sequencer::howManySequences() const 
{
  assert(sequences.size() < 128);
  return sequences.size();
}
unsigned int Sequencer::howManySteps(unsigned int sequence) const 
{
  if (assertSequence(sequence)) return sequences[sequence].howManySteps();
  else return 0;
}
unsigned int Sequencer::getCurrentStep(unsigned int sequence) const
{
  return sequences[sequence].getCurrentStep();
}

SequenceType Sequencer::getSequenceType(unsigned int sequence) const 
{
  return sequences[sequence].getType();
}

unsigned int Sequencer::getSequenceTicksPerStep(unsigned int sequence) const
{
  return sequences[sequence].getTicksPerStep();
}

/** move the sequencer along by one tick */
void Sequencer::tick(bool trigger)
{
  for (Sequence& seq : sequences)
  {
      seq.tick(trigger);
  }
}

Sequence* Sequencer::getSequence(unsigned int sequence)
{
  return &(sequences[sequence]);
}

void Sequencer::setSequenceType(unsigned int sequence, SequenceType type)
{
  sequences[sequence].setType(type);
}

void Sequencer::setSequenceLength(unsigned int sequence, unsigned int length)
{
  sequences[sequence].setLength(length);
}

void Sequencer::shrinkSequence(unsigned int sequence)
{
  sequences[sequence].setLength(sequences[sequence].getLength()-1);
}
void Sequencer::extendSequence(unsigned int sequence)
{
  sequences[sequence].ensureEnoughStepsForLength(sequences[sequence].getLength()+1);
  sequences[sequence].setLength(sequences[sequence].getLength()+1);
}


void Sequencer::setAllCallbacks(std::function<void (std::vector<double>*)> callback)
{
    for (int seq = 0; seq < sequences.size(); ++seq)
    {
      setSequenceCallback(seq, callback);
    }
}

/** set a callback for all steps in a sequence*/
void Sequencer::setSequenceCallback(unsigned int sequence, std::function<void (std::vector<double>*)> callback)
{
  for (int step = 0; step<sequences[sequence].howManySteps(); ++step)
  {
    sequences[sequence].setStepCallback(step, callback);
  }
}

/** set a lambda to call when a particular step in a particular sequence happens */
void Sequencer::setStepCallback(unsigned int sequence, unsigned int step, std::function<void (std::vector<double>*)> callback)
{
    sequences[sequence].setStepCallback(step, callback); 
}

/** update the data stored at a step in the sequencer */
void Sequencer::setStepData(unsigned int sequence, unsigned int step, std::vector<double> data)
{
  if (!assertSeqAndStep(sequence, step)) return;
  sequences[sequence].setStepData(step, data);
}
/** update a single value in the  data 
 * stored at a step in the sequencer */
void Sequencer::updateStepData(unsigned int sequence, unsigned int step, unsigned int dataInd, double value)
{
  if (!assertSeqAndStep(sequence, step)) return;
  sequences[sequence].updateStepData(step, dataInd, value);
}

/** retrieve the data for the current step */
std::vector<double> Sequencer::getCurrentStepData(int sequence) const
{
  if (sequence >= sequences.size() || sequence < 0) return std::vector<double>{};
  return sequences[sequence].getCurrentStepData();
}

/** retrieve the data for a specific step */
std::vector<double> Sequencer::getStepData(int sequence, int step) const
{
  if (!assertSeqAndStep(sequence, step)) return std::vector<double>{};
  return sequences[sequence].getStepData(step);
}
/** retrieve the data for a specific step */
std::vector<double>* Sequencer::getStepDataDirect(int sequence, int step)
{
  assert(sequence < sequences.size());
  assert(step < sequences[sequence].howManySteps());
  return sequences[sequence].getStepDataDirect(step);
}

void Sequencer::toggleActive(int sequence, int step)
{
  if (!assertSeqAndStep(sequence, step)) return;
  sequences[sequence].toggleActive(step);
}
bool Sequencer::isStepActive(int sequence, int step) const
{
  if (!assertSeqAndStep(sequence, step))  return false; 
  return sequences[sequence].isStepActive(step);
}
void Sequencer::addStepListener()
{

}

/** print out a tracker style view of the sequence */
std::string Sequencer::toString()
{
std::string s{""};
for (int step = 0; step < 32; ++step)
{
  s += std::to_string(step) + "\t: ";
  for (Sequence& seq : sequences)
  {
    // s += seq.stepToString(step) + "\t";
  }
  s += "\n";
}
return s;
}

void Sequencer::resetSequence(int sequence)
{
  if (!assertSequence(sequence)) return;
  sequences[sequence].reset();
}


bool Sequencer::assertSeqAndStep(unsigned int sequence, unsigned int step) const
{
  if (!assertSequence(sequence)) return false;
  if (!sequences[sequence].assertStep(step)) return false; 
  return true;
}

bool Sequencer::assertSequence(unsigned int sequence) const
{
if (sequence >= sequences.size() || sequence < 0)
  {
    return false;
  } 
return true;
}

void Sequencer::prepareGridView(std::vector<std::vector<std::string>>& gridView)
{
  // need to get the data in the sequences, convert it to strings and 
  // store it into the sent grid view
  assert(gridView.size() >= howManySequences());
  for (int seq=0; seq<howManySequences(); ++ seq){
    // check we have the length 
    assert(gridView[seq].size() >= howManySteps(seq));
    for (int step=0; step<howManySteps(seq); ++ step){
      // step then seq, i.e. col then row
      gridView[seq][step] = std::to_string((int)getStepDataDirect(seq, step)->at(Step::note1Ind));
    }
  }
}

