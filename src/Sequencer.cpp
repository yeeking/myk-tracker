#include "Sequencer.h"
Step::Step() : active{true}, rw_mutex{std::make_unique<std::shared_mutex>()}
{
  data.push_back(std::vector<double>());
  data[0].push_back(0.0);
  data[0].push_back(0.0);
  data[0].push_back(0.0);
  data[0].push_back(0.0);
  data[0].push_back(0.0);
}
/** returns a copy of the data stored in this step*/
std::vector<std::vector<double>> Step::getData()
{
  std::shared_lock<std::shared_mutex> lock(*rw_mutex);
  return data; 
}
double Step::getDataAt(int row, int col) 
{
  // this lock allows multiple concorrent reads
  std::shared_lock<std::shared_mutex> lock(*rw_mutex);
  return data[row][col];
}
int Step::howManyDataRows()
{
  std::shared_lock<std::shared_mutex> lock(*rw_mutex);
  return data.size();
}
int Step::howManyDataCols()
{
  std::shared_lock<std::shared_mutex> lock(*rw_mutex);
  return data[0].size();
}

// std::vector<std::vector<double>>* Step::getDataDirect()
// {
//   return &data; 
// }


std::string Step::toStringFlat() 
{
  std::shared_lock<std::shared_mutex> lock(*rw_mutex);
  return std::to_string((int)this->data[0][Step::p2Ind]);
}

std::vector<std::vector<std::string>> Step::toStringGrid() 
{ 

  std::shared_lock<std::shared_mutex> lock(*rw_mutex);

  // each data sub vector should be on its own row 
  //
  std::vector<std::vector<std::string>> grid;
  assert (data.size() > 0);
  // a row is
  for (int col=0;col<data[0].size();++col){
    std::vector<std::string> colData;
    //colData.resize(data.size());
    for (int row=0;row<data.size();++row){
      //  colData.push_back(std::to_string(row) + ":" + std::to_string(col) + ":" + std::to_string((int)data[row][col]));
      std::string field = "";
      switch (col){
        case Step::p2Ind:
          field = "n: ";
          break;
        case Step::p3Ind:
          field = "v: ";
          break;
        case Step::p4Ind:
          field = "d: ";
          break;
        case Step::p1Ind:
          field = "c: ";
          break;
      }
      colData.push_back(field + std::to_string((int)data[row][col]));
    }
    grid.push_back(colData); 
  }
  return grid;
} 

/** sets the data stored in this step */
void Step::setData(const std::vector<std::vector<double>>& _data)
{
  // uni lock as writing data 
  std::unique_lock<std::shared_mutex> lock(*rw_mutex);
  this->data = _data; // copy it over 
}

/** update one value in the data vector for this step*/
void Step::setDataAt(unsigned int row, unsigned int col, double value)
{
  // uni lock as writing data 
  std::unique_lock<std::shared_mutex> lock(*rw_mutex);
  assert(row < data.size());
  assert(col < data[row].size());

  // apply data constraints based on current command
  if (col == Step::cmdInd){// changing the command
     int maxCmds = CommandRegistry::countCommands();
     if (value >= maxCmds) value = maxCmds - 1;
     if (value < 0) value = 0;
  }
  else if (col > Step::cmdInd){// it is one of the parameter columns
    Command cmd = CommandRegistry::getCommand(data[row][Step::cmdInd]);
    int pInd = col - 1;
    Parameter& p = cmd.parameters[pInd];
    // now constrain the value to the range of the parameter
    if (value > p.max) value = p.max;
    if (value < p.min) value = p.min;
    if(col < data[row].size()) data[row][col] = value;
  }

}
/** set the callback function called when this step is triggered*/
void Step::setCallback(std::function<void(std::vector<std::vector<double>>*)> callback)
{
  this->stepCallback = callback;
}
std::function<void(std::vector<std::vector<double>>*)> Step::getCallback()
{
  return this->stepCallback;
}

/** trigger this step, causing it to pass its data to its callback*/
void Step::trigger() 
{ 
  // shared lock as reading 
  std::shared_lock<std::shared_mutex> lock(*rw_mutex);
  //std::cout << "Step::trigger" << std::endl;
  if (active) {
    for (std::vector<double>& dataRow: data){
      Command cmd = CommandRegistry::getCommand(dataRow[Step::cmdInd]);
      // if (dataRow[Step::note1Ind] != 0){
      //   if (command.execute) {
      //     command.execute(&dataRow);
      //   }      
      // }
    } 
  }
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
    s.setCallback([i](std::vector<std::vector<double>>* data){
      if (data->size() > 0){
        //std::cout << "Sequence::Sequence default step callback " << i << " triggered " << std::endl;
      }
    });
    steps.push_back(std::move(s));
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
  // // make a local copy
  // Step s = steps[currentStep];
  // // apply changes to local copy if needed      
  // if(transpose > 0) 
  // {
  //   std::vector<std::vector<double>>* data = s.getDataDirect();//  s.getData();
  //   if (data->at(0).at(Step::note1Ind) > 0 ) // only transpose non-zero steps
  //   {
  //     data->at(0).at(Step::note1Ind) = fmod(data->at(0).at(Step::note1Ind) + transpose, 127);
  //   }
  // }
  // // trigger the local, adjusted copy of the step
  // s.trigger();
  steps[currentStep].trigger();
}

void Sequence::triggerMidiDrumType()
{
  // // make a local copy
  // Step s = steps[currentStep];
  // // transpose the midi note into the drum domain
  // std::vector<std::vector<double>>* data = s.getDataDirect();//  s.getData();
  // data->at(0).at(Step::note1Ind) = midiScaleToDrum[(int) data->at(0).at(Step::note1Ind)];
  // // apply changes to local copy if needed      
  // if(transpose > 0) 
  // {
  //   if (data->at(0).at(Step::note1Ind) > 0 ) // only transpose non-zero steps
  //   {
  //     data->at(0).at(Step::note1Ind) = fmod(data->at(0).at(Step::note1Ind) + transpose, 127);
  //   }
  // }
  // s.trigger();
  steps[currentStep].trigger();
}

void Sequence::triggerMidiChordType()
{
 
}


void Sequence::triggerTransposeType()
{
  // if (steps[currentStep].isActive() )
  // {
  //   std::vector<std::vector<double>>* data = steps[currentStep].getDataDirect();
  //   if (data->at(0).at(Step::note1Ind) > 0 ) // only transpose non-zero steps
  //   {
  //     sequencer->getSequence(data->at(0).at(Step::channelInd))->setTranspose(
  //       fmod(data->at(0).at(Step::note1Ind), 12) );
  //   }
  // }
} 

void Sequence::triggerLengthType()
{
  // //return; 
  // if (steps[currentStep].isActive())
  // {
  //   std::vector<std::vector<double>>* data = steps[currentStep].getDataDirect();
  //   if (data->at(0).at(Step::note1Ind) > 0 ) // only transpose non-zero steps
  //   {
  //     sequencer->getSequence(data->at(0).at(Step::channelInd))->setLengthAdjustment(
  //       fmod(data->at(0).at(Step::note1Ind), 12) );
  //   }
  // }   
}

void Sequence::triggerTickType()
{
  // if (steps[currentStep].isActive() )
  // {
  //   std::vector<std::vector<double>>* data = steps[currentStep].getDataDirect();
  //   if (data->at(0).at(Step::note1Ind) > 0 ) // only transpose non-zero steps
  //   {
  //     sequencer->getSequence(data->at(0).at(Step::channelInd))->setTicksPerStep(
  //       fmod(data->at(0).at(Step::note1Ind), 6) );
  //   }
  // }
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
std::vector<std::vector<double>> Sequence::getStepData(int step)
{
  return steps[step].getData();
}

double Sequence::getStepDataAt(int step, int row, int col)
{
  return steps[step].getDataAt(row, col);
}


// std::vector<std::vector<double>>* Sequence::getStepDataDirect(int step)
// {
//   return steps[step].getDataDirect();
// }
// Step* Sequence::getStep(int step)
// {
//   return &steps[step];
// }

std::vector<std::vector<double>> Sequence::getCurrentStepData()
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
      int channel = steps[0].getDataAt(0, Step::p1Ind);
      s.setDataAt(0, Step::p1Ind, channel);
      steps.push_back(std::move(s));
    }
  }
}
void Sequence::setLength(int length)
{

  if (length < 1) return;
  if (length > steps.size()) return; 
  
  currentLength = length;
}

void Sequence::setStepData(unsigned int step, std::vector<std::vector<double>> data)
{
  steps[step].setData(data);
}
/** update a single data value in a given step*/
void Sequence::setStepDataAt(unsigned int step, unsigned int row, unsigned int col, double value)
{
  steps[step].setDataAt(row, col, value);
}

void Sequence::setStepCallback(unsigned int step, 
                  std::function<void (std::vector<std::vector<double>>*)> callback)
{
  steps[step].setCallback(callback);
}
std::string Sequence::stepToString(int step) 
{
  std::vector<std::vector<double>> data = getStepData(step);
  if (data.size() > 0)
    return std::to_string(data[0][0]);
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

int Sequence::howManyStepDataRows(int step)
{
  return steps[step].howManyDataRows();
}
int Sequence::howManyStepDataCols(int step)
{
  return steps[step].howManyDataCols();
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

std::string Sequence::stepToStringFlat(int step)
{
  return steps[step].toStringFlat();
}

void Sequence::reset()
{
  for (Step& step : steps)
  {
    // activate the step
    if (!step.isActive()) step.toggleActive();
    // reset the data
    Step cleanStep{};
    step.setData(cleanStep.getData());
  }
}
std::vector<std::vector<std::string>> Sequence::stepAsGridOfStrings(int step)
{
  return steps[step].toStringGrid();
}


/////////////////////// Sequencer 

Sequencer::Sequencer(unsigned int seqCount, unsigned int seqLength) 
{
  for (auto i=0;i<seqCount;++i)
  {
    sequences.push_back(Sequence{this, seqLength});
  }
  updateGridOfStrings();
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
    double channel = otherSeq->sequences[seq].getStepDataAt(0, 0, Step::p1Ind);
    for (int step=0; step < this->sequences[seq].howManySteps(); ++step)
    {
      // note this assumes a single row step
      this->setStepDataAt(seq, step, 0, Step::p1Ind, channel);
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
  updateGridOfStrings();
}
void Sequencer::extendSequence(unsigned int sequence)
{
  sequences[sequence].ensureEnoughStepsForLength(sequences[sequence].getLength()+1);
  sequences[sequence].setLength(sequences[sequence].getLength()+1);
  updateGridOfStrings();
}


void Sequencer::setAllCallbacks(std::function<void (std::vector<std::vector<double>>*)> callback)
{
    for (int seq = 0; seq < sequences.size(); ++seq)
    {
      setSequenceCallback(seq, callback);
    }
}

/** set a callback for all steps in a sequence*/
void Sequencer::setSequenceCallback(unsigned int sequence, std::function<void (std::vector<std::vector<double>>*)> callback)
{
  for (int step = 0; step<sequences[sequence].howManySteps(); ++step)
  {
    sequences[sequence].setStepCallback(step, callback);
  }
}

/** set a lambda to call when a particular step in a particular sequence happens */
void Sequencer::setStepCallback(unsigned int sequence, unsigned int step, std::function<void (std::vector<std::vector<double>>*)> callback)
{
    sequences[sequence].setStepCallback(step, callback); 
}

/** update the data stored at a step in the sequencer */
void Sequencer::setStepData(unsigned int sequence, unsigned int step, std::vector<std::vector<double>> data)
{
  if (!assertSeqAndStep(sequence, step)) return;
  sequences[sequence].setStepData(step, data);
  updateGridOfStrings();
}
/** update a single value in the  data 
 * stored at a step in the sequencer */
void Sequencer::setStepDataAt(unsigned int sequence, unsigned int step, unsigned int row, unsigned int col, double value)
{
  if (!assertSeqAndStep(sequence, step)) return;
  sequences[sequence].setStepDataAt(step, row, col, value);
  updateGridOfStrings();
}

/** retrieve the data for the current step */
std::vector<std::vector<double>> Sequencer::getCurrentStepData(int sequence)
{
  if (sequence >= sequences.size() || sequence < 0) return std::vector<std::vector<double>>{};
  
  return sequences[sequence].getCurrentStepData();
}



int Sequencer::howManyStepDataRows(int seq, int step)
{
  return sequences[seq].howManyStepDataRows(step);
}
int Sequencer::howManyStepDataCols(int seq, int step)
{
  return sequences[seq].howManyStepDataCols(step);
}

// Step* Sequencer::getStep(int seq, int step)
// {
//   if (!assertSeqAndStep(seq, step)) assert(false); // hard crash for that, sorry 

//   return sequences[seq].getStep(step); 
// }


/** retrieve the data for a specific step */
std::vector<std::vector<double>> Sequencer::getStepData(int sequence, int step)
{
  if (!assertSeqAndStep(sequence, step)) return std::vector<std::vector<double>>{};
  return sequences[sequence].getStepData(step);
}

// /** retrieve the data for a specific step */
// std::vector<std::vector<double>>* Sequencer::getStepDataDirect(int sequence, int step)
// {
//   assert(sequence < sequences.size());
//   assert(step < sequences[sequence].howManySteps());
//   return sequences[sequence].getStepDataDirect(step);
// }

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


void Sequencer::updateGridOfStrings()
{
  std::vector<std::vector<std::string>> gridView;
  // need to get the data in the sequences, convert it to strings and 
  // store it into the sent grid view
  if (gridView.size() != howManySequences()){
    gridView.resize(howManySequences());
  }
  

  assert (gridView.size() >= howManySequences());


  // first. find the longest sequence
  int maxSteps = 0;
  for (int seq=0; seq<howManySequences(); ++ seq){
    if (howManySteps(seq) > maxSteps) maxSteps = howManySteps(seq);
  }

  for (int seq=0; seq<howManySequences() && seq<gridView.size(); ++ seq){
    // check we have the length 
    // if (gridView[seq].size() != howManySteps(seq)){
    if (gridView[seq].size() != maxSteps){
      
      // std::cout << "resizing " << seq << std::endl;
      // gridView[seq].resize(howManySteps(seq));
      gridView[seq].resize(maxSteps);
      for (int i = 0; i<gridView[seq].size(); ++i) gridView[seq][i] = "";
    }
    assert(gridView[seq].size() >= howManySteps(seq));
    for (int step=0; step<howManySteps(seq) && step<gridView[seq].size(); ++ step){
      // step then seq, i.e. col then row
      gridView[seq][step] = std::to_string(seq) + ":" + std::to_string(step) + ":" + sequences[seq].stepToStringFlat(step);
    }
  }
  seqAsStringGrid = gridView; 
}

std::vector<std::vector<std::string>>& Sequencer::getGridOfStrings()
{
  return seqAsStringGrid;
}
std::vector<std::vector<std::string>> Sequencer::stepAsGridOfStrings(int seq, int step)
{
  return sequences[seq].stepAsGridOfStrings(step);
}

double Sequencer::getStepDataAt(int seq, int step, int row, int col)
{
  return sequences[seq].getStepDataAt(step, row, col);
}


// void Sequencer::setStepDataAt(int seq, int step, int row, int col, double val)
// {
//   sequences[seq].setStepDataAt(step, row, col, val);
// }

