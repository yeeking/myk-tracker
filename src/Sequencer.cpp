#include "Sequencer.h"
#include "MidiUtils.h"

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
  if (this->data[0][Step::noteInd] == 0){
    return "----";
  }
  else {
    std::string disp = "";
    int note = (int)this->data[0][Step::noteInd];
    char nchar = MidiUtils::getIntToNoteMap()[note % 12];
    int oct = note / 12; 
    disp.push_back(nchar);
    disp += "-" + std::to_string(oct) + " ";
    int power = (int)this->data[0][Step::velInd] / 32; 
    for (int p=0;p<power;++p){
      disp += "]";
    }
    return disp; 
    // return std::string(1, note);
  }
  // return std::to_string((int)this->data[0][Step::noteInd]);
}

std::vector<std::vector<std::string>> Step::toStringGrid()
{

  std::shared_lock<std::shared_mutex> lock(*rw_mutex);

  // each data sub vector should be on its own row
  //
  std::vector<std::vector<std::string>> grid;
  // assert (data.size() > 0);
  // a row is
  for (int col = 0; col < data[0].size(); ++col)
  {
    std::vector<std::string> colData;
    // colData.resize(data.size());
    for (int row = 0; row < data.size(); ++row)
    {
      //  colData.push_back(std::to_string(row) + ":" + std::to_string(col) + ":" + std::to_string((int)data[row][col]));
      std::string field = "";
      Command cmd = CommandProcessor::getCommand(data[row][Step::cmdInd]);
      // command col
      if (col == Step::cmdInd)
      {
        colData.push_back(cmd.shortName);
      }
      else
      {
        colData.push_back(cmd.parameters[col - 1].shortName + std::to_string((int)data[row][col]));
      }
    }
    grid.push_back(colData);
  }
  return grid;
}

void Step::activate()
{
  std::unique_lock<std::shared_mutex> lock(*rw_mutex);
  active = true;
}
void Step::deactivate()
{
  std::unique_lock<std::shared_mutex> lock(*rw_mutex);
  active = false;
}

/** sets the data stored in this step */
void Step::setData(const std::vector<std::vector<double>> &_data)
{
  // uni lock as writing data
  std::unique_lock<std::shared_mutex> lock(*rw_mutex);
  this->data = std::move(_data); // copy it over
}

void Step::resetRow(int row)
{
  std::unique_lock<std::shared_mutex> lock(*rw_mutex);
  assert(row < data.size());
  for (int col = 0; col < data[row].size(); ++col)
  {
    data[row][col] = 0;
  }
}

/** update one value in the data vector for this step*/
void Step::setDataAt(unsigned int row, unsigned int col, double value)
{
  // uni lock as writing data
  std::unique_lock<std::shared_mutex> lock(*rw_mutex);
  assert(row < data.size());
  assert(col < data[row].size());

  // apply data constraints based on current command
  if (col == Step::cmdInd)
  { // changing the command - only allowed a value 0->no. commands
    int maxCmds = CommandProcessor::countCommands();
    if (value >= maxCmds)
      value = maxCmds - 1;
    if (value < 0)
      value = 0;
  }
  else if (col > Step::cmdInd)
  { // it is one of the parameter columns - use parameter spec constraints
    Command cmd = CommandProcessor::getCommand(data[row][Step::cmdInd]);
    int pInd = col - 1;
    Parameter &p = cmd.parameters[pInd];
    // now constrain the value to the range of the parameter
    if (value > p.max)
      value = p.max;
    if (value < p.min)
      value = p.min;
  }
  if (col < data[row].size())
    data[row][col] = value;
}
/** set the callback function called when this step is triggered*/
void Step::setCallback(std::function<void(std::vector<std::vector<double>> *)> callback)
{
  this->stepCallback = callback;
}
std::function<void(std::vector<std::vector<double>> *)> Step::getCallback()
{
  return this->stepCallback;
}

/** trigger this step, causing it to pass its data to its callback*/
void Step::trigger(int row)
{
  // shared lock as reading
  std::shared_lock<std::shared_mutex> lock(*rw_mutex);
  // std::cout << "Step::trigger" << std::endl;
  if (active)
  {
    if (row > -1)
    { // only trigger one row
      assert(row < data.size());
      // note that the command decides if 
      // the data is valid and therefore, if it should do anything, not the step 
      CommandProcessor::executeCommand(data[row][Step::cmdInd], &data[row]);
    }
    else
    {
      for (std::vector<double> &dataRow : data)
      {
      // note that the command decides if 
      // the data is valid and therefore, if it should do anything, not the step 
        CommandProcessor::executeCommand(dataRow[Step::cmdInd], &dataRow);
      }
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

Sequence::Sequence(Sequencer *_sequencer,
                   unsigned int seqLength,
                   unsigned short _midiChannel)
    : sequencer{_sequencer}, currentStep{0},
      midiChannel{_midiChannel}, type{SequenceType::midiNote},
      transpose{0}, lengthAdjustment{0}, ticksPerStep{4}, originalTicksPerStep{4}, nextTicksPerStep{0}, ticksElapsed{0}, tickOfFour{0}, muted{false}, 
      rw_mutex{std::make_unique<std::shared_mutex>()}
// , midiScaleToDrum{MidiUtils::getScaleMidiToDrumMidi()}
{
  currentLength = seqLength;
  for (unsigned int i = 0; i < seqLength; i++)
  {
    Step s;
    s.setCallback([i](std::vector<std::vector<double>> *data)
                  {
      if (data->size() > 0){
        //std::cout << "Sequence::Sequence default step callback " << i << " triggered " << std::endl;
      } });
    steps.push_back(std::move(s));
  }
}

/** go to the next step */
void Sequence::tick(bool trigger)
{
  // write lock
  std::unique_lock<std::shared_mutex> lock(*rw_mutex);
  // std::cout << "Sequence::tick" << std::endl;
  ++ticksElapsed;
  tickOfFour = ++tickOfFour % 4;
  
  if (nextTicksPerStep  > 0 && tickOfFour == 0){// update to this tps on next zero of tickOfFour
      this->originalTicksPerStep = this->nextTicksPerStep;
      this->ticksElapsed = ticksPerStep;
      currentStep = 0; 
      // can't call this as it asks for another lock which
      // causes a crash
      //setTicksPerStep(nextTicksPerStep);
      nextTicksPerStep = 0;// don't trigger it again
  }

  if (ticksElapsed == ticksPerStep)
  {
    ticksElapsed = 0;
    if (trigger && !muted)
    {
      steps[currentStep].trigger();
    }

    if (currentLength + lengthAdjustment < 1)
      currentStep = 0;
    else
      currentStep = (++currentStep) % (currentLength + lengthAdjustment);
    if (currentStep >= steps.size())
      currentStep = 0;
    assert(currentStep >= 0 && currentStep < steps.size());
    // switch off any adjusters when we are at step 0
    if (currentStep == 0)
      deactivateProcessors();
  }
}

void Sequence::triggerStep(int step, int row)
{
  steps[step].trigger(row);
}

void Sequence::deactivateProcessors()
{
  transpose = 0;
  lengthAdjustment = 0;
  ticksPerStep = originalTicksPerStep;
  ticksElapsed = 0;
}

/** set this step, row values to zero */
void Sequence::resetStepRow(int step, int row)
{
  steps[step].resetRow(row);
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
  std::unique_lock<std::shared_mutex> lock(*rw_mutex);
  this->originalTicksPerStep = tps;
  this->ticksElapsed = 0;
}

void Sequence::onZeroSetTicksPerStep(int _nextTicksPerStep)
{
  std::unique_lock<std::shared_mutex> lock(*rw_mutex);
  this->nextTicksPerStep = _nextTicksPerStep;
}

void Sequence::setTicksPerStepAdjustment(int tps)
{
  if (tps < 1 || tps > 16)
    return;
  this->ticksPerStep = tps;
}

int Sequence::getTicksPerStep() const
{
  std::shared_lock<std::shared_mutex> lock(*rw_mutex);
  return this->originalTicksPerStep;
}

unsigned int Sequence::getCurrentStep() const
{
  std::shared_lock<std::shared_mutex> lock(*rw_mutex);
  return currentStep;
}
bool Sequence::assertStep(unsigned int step) const
{
  if (step >= steps.size() || step < 0)
    return false;
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
  std::shared_lock<std::shared_mutex> lock(*rw_mutex);
  return currentLength;
}

void Sequence::ensureEnoughStepsForLength(int length)
{
  std::unique_lock<std::shared_mutex> lock(*rw_mutex);
  if (length > steps.size()) // bad need more steps
  {
    int toAdd = length - steps.size();
    for (int i = 0; i < toAdd; ++i)
    {
      Step s;
      s.setCallback(
          steps[0].getCallback());
      // set the channel
      int channel = steps[0].getDataAt(0, Step::chanInd);
      s.setDataAt(0, Step::chanInd, channel);
      steps.push_back(std::move(s));
    }
  }
}
void Sequence::setLength(int length)
{
std::unique_lock<std::shared_mutex> lock(*rw_mutex);
  if (length < 1)
    return;
  if (length > steps.size())
    return;

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
                               std::function<void(std::vector<std::vector<double>> *)> callback)
{
  steps[step].setCallback(callback);
}
std::string Sequence::stepToString(int step)
{
  std::vector<std::vector<double>> data = getStepData(step);
  if (data.size() > 0)
    return std::to_string(data[0][0]);
  else
    return "-";
}

unsigned int Sequence::howManySteps() const
{
  std::shared_lock<std::shared_mutex> lock(*rw_mutex);
  // return steps.size();
  //  case where length adjust is too high
  // if (currentLength + lengthAdjustment >= steps.size()) return currentLength;

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
  if (isMuted()) return "";
  
  return steps[step].toStringFlat();
}

void Sequence::reset()
{
  std::unique_lock<std::shared_mutex> lock(*rw_mutex);

  for (Step &step : steps)
  {
    // activate the step
    if (!step.isActive())
      step.toggleActive();
    // reset the data
    Step cleanStep{};
    step.setData(cleanStep.getData());
  }
}
std::vector<std::vector<std::string>> Sequence::stepAsGridOfStrings(int step)
{
  return steps[step].toStringGrid();
}

bool Sequence::isMuted()
{
  std::shared_lock<std::shared_mutex> lock(*rw_mutex);
  return muted;
}
/** change mote state to its opposite */
void Sequence::toggleMuteState()
{
  std::unique_lock<std::shared_mutex> lock(*rw_mutex);
  muted = !muted;
}

/////////////////////// Sequencer

Sequencer::Sequencer(unsigned int seqCount, unsigned int seqLength)
{
  for (auto i = 0; i < seqCount; ++i)
  {
    sequences.push_back(Sequence{this, seqLength});
  }
  updateSeqStringGrid();
  setupSeqConfigSpecs();
  std::vector<Parameter> paramSpecs;
  //    Parameter(const std::string& name, const std::string& shortName, double min, double max, double step, double defaultValue);
  paramSpecs.push_back(Parameter("Channel", "ch", 0, 15, 1, 1));
  paramSpecs.push_back(Parameter("Ticks per step", "tps", 0, 15, 1, 1));
}

Sequencer::~Sequencer()
{
}

void Sequencer::copyChannelAndTypeSettings(Sequencer *otherSeq)
{
  // ignore if diff sizes
  assert(otherSeq->sequences.size() == this->sequences.size());
  // if (otherSeq->sequences.size() != this->sequences.size()) return;
  for (int seq = 0; seq < this->sequences.size(); ++seq)
  {
    this->sequences[seq].setType(otherSeq->sequences[seq].getType());
    // assign the same channel
    double channel = otherSeq->sequences[seq].getStepDataAt(0, 0, Step::chanInd);
    for (int step = 0; step < this->sequences[seq].howManySteps(); ++step)
    {
      // note this assumes a single row step
      this->setStepDataAt(seq, step, 0, Step::chanInd, channel);
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
  if (assertSequence(sequence))
    return sequences[sequence].howManySteps();
  else
    return 0;
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
  for (Sequence &seq : sequences)
  {
    seq.tick(trigger);
  }
}

void Sequencer::triggerStep(int seq, int step, int row)
{
  sequences[seq].triggerStep(step, row);
}

Sequence *Sequencer::getSequence(unsigned int sequence)
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
  sequences[sequence].setLength(sequences[sequence].getLength() - 1);
}
void Sequencer::extendSequence(unsigned int sequence)
{
  sequences[sequence].ensureEnoughStepsForLength(sequences[sequence].getLength() + 1);
  sequences[sequence].setLength(sequences[sequence].getLength() + 1);
}

void Sequencer::setAllCallbacks(std::function<void(std::vector<std::vector<double>> *)> callback)
{
  for (int seq = 0; seq < sequences.size(); ++seq)
  {
    setSequenceCallback(seq, callback);
  }
}

/** set a callback for all steps in a sequence*/
void Sequencer::setSequenceCallback(unsigned int sequence, std::function<void(std::vector<std::vector<double>> *)> callback)
{
  for (int step = 0; step < sequences[sequence].howManySteps(); ++step)
  {
    sequences[sequence].setStepCallback(step, callback);
  }
}

/** set a lambda to call when a particular step in a particular sequence happens */
void Sequencer::setStepCallback(unsigned int sequence, unsigned int step, std::function<void(std::vector<std::vector<double>> *)> callback)
{
  sequences[sequence].setStepCallback(step, callback);
}

/** update the data stored at a step in the sequencer */
void Sequencer::setStepData(unsigned int sequence, unsigned int step, std::vector<std::vector<double>> data)
{
  if (!assertSeqAndStep(sequence, step))
    return;
  sequences[sequence].setStepData(step, data);
}
/** update a single value in the  data
 * stored at a step in the sequencer */
void Sequencer::setStepDataAt(unsigned int sequence, unsigned int step, unsigned int row, unsigned int col, double value)
{
  if (!assertSeqAndStep(sequence, step))
    return;
  sequences[sequence].setStepDataAt(step, row, col, value);
}

/** retrieve the data for the current step */
std::vector<std::vector<double>> Sequencer::getCurrentStepData(int sequence)
{
  if (sequence >= sequences.size() || sequence < 0)
    return std::vector<std::vector<double>>{};

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
  if (!assertSeqAndStep(sequence, step))
    return std::vector<std::vector<double>>{};
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
  if (!assertSeqAndStep(sequence, step))
    return;
  sequences[sequence].toggleActive(step);
}
bool Sequencer::isStepActive(int sequence, int step) const
{
  if (!assertSeqAndStep(sequence, step))
    return false;
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
    for (Sequence &seq : sequences)
    {
      // s += seq.stepToString(step) + "\t";
    }
    s += "\n";
  }
  return s;
}

void Sequencer::resetSequence(int sequence)
{
  if (!assertSequence(sequence))
    return;
  sequences[sequence].reset();
}

void Sequencer::resetStepRow(int sequence, int step, int row)
{
  sequences[sequence].resetStepRow(step, row);
}


bool Sequencer::assertSeqAndStep(unsigned int sequence, unsigned int step) const
{
  if (!assertSequence(sequence))
    return false;
  if (!sequences[sequence].assertStep(step))
    return false;
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

void Sequencer::updateSeqStringGrid()
{
  std::vector<std::vector<std::string>> gridView;
  // need to get the data in the sequences, convert it to strings and
  // store it into the sent grid view
  if (gridView.size() != howManySequences())
  {
    gridView.resize(howManySequences());
  }

  assert(gridView.size() >= howManySequences());

  // first. find the longest sequence
  int maxSteps = 0;
  for (int seq = 0; seq < howManySequences(); ++seq)
  {
    if (howManySteps(seq) > maxSteps)
      maxSteps = howManySteps(seq);
  }

  for (int seq = 0; seq < howManySequences() && seq < gridView.size(); ++seq)
  {
    // check we have the length
    // if (gridView[seq].size() != howManySteps(seq)){
    if (gridView[seq].size() != maxSteps)
    {

      // std::cout << "resizing " << seq << std::endl;
      // gridView[seq].resize(howManySteps(seq));
      gridView[seq].resize(maxSteps);
      for (int i = 0; i < gridView[seq].size(); ++i)
        gridView[seq][i] = "";
    }
    assert(gridView[seq].size() >= howManySteps(seq));
    for (int step = 0; step < howManySteps(seq) && step < gridView[seq].size(); ++step)
    {
      // step then seq, i.e. col then row
      //gridView[seq][step] = std::to_string(seq) + ":" + std::to_string(step) + ":" + sequences[seq].stepToStringFlat(step);
      gridView[seq][step] = sequences[seq].stepToStringFlat(step);
    }
  }
  seqAsStringGrid = gridView;
}

std::vector<std::vector<std::string>> &Sequencer::getSequenceAsGridOfStrings()
{
  return seqAsStringGrid;
}
std::vector<std::vector<std::string>> Sequencer::getStepAsGridOfStrings(int seq, int step)
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

void Sequencer::toggleSequenceMute(int sequence)
{
  sequences[sequence].toggleMuteState();

}

std::vector<std::vector<std::string>> Sequencer::getSequenceConfigsAsGridOfStrings()
{
  // editable config items for a sequence:
// - set channel for all steps
// - set ticks per beat
// - set transpose maybe? 
  // each col is a sequence
  std::vector<std::vector<std::string>>confGrid;
  std::vector<Parameter> params = getSeqConfigParamSpecs();

  for (int seq =0;seq<howManySequences();++seq){
    confGrid.push_back(std::vector<std::string>());
    // channel 
    confGrid[seq].push_back(params[0].shortName + ":" + std::to_string(getStepDataAt(seq, 0, 0, Step::chanInd)));
    // ticks
    confGrid[seq].push_back(params[1].shortName + ":" + std::to_string(getSequenceTicksPerStep(seq)));    
  }
  return confGrid;
}


std::vector<Parameter>& Sequencer::getSeqConfigParamSpecs()
{
  return seqParamSpecs; 
}
void Sequencer::setupSeqConfigSpecs()
{
  seqParamSpecs.push_back(Parameter("Channel", "ch", 0, 15, 1, 1));
  seqParamSpecs.push_back(Parameter("Ticks per step", "tps", 1, 16, 1, 4));
}

void Sequencer::incrementSeqParam(int seq, int paramIndex)
{
  assert(paramIndex < getSeqConfigParamSpecs().size() &&
         paramIndex >= 0);
  Parameter p = seqParamSpecs[paramIndex];
  if (p.shortName == "ch"){
    // meed to update all steps to the new channel     sequences[seq].
    double ch = getStepDataAt(seq, 0, 0, Step::chanInd);
    ch += p.step;
    if (ch >= p.max) ch = p.max;
    for (int step = 0; step < howManySteps(seq); ++ step){
      for (int row =0; row < howManyStepDataRows(seq, step); ++row){
        setStepDataAt(seq, step, row, Step::chanInd, ch);
      }
    }
  }
  if (p.shortName == "tps"){
    int tps = sequences[seq].getTicksPerStep();
    tps += p.step;
    if (tps > p.max) tps = p.max;
    sequences[seq].onZeroSetTicksPerStep(tps);
  }

}
void Sequencer::decrementSeqParam(int seq, int paramIndex)
{
  assert(paramIndex < getSeqConfigParamSpecs().size() &&
         paramIndex >= 0);

  Parameter p = seqParamSpecs[paramIndex];
  if (p.shortName == "ch"){
    // meed to update all steps to the new channel     sequences[seq].
    double ch = getStepDataAt(seq, 0, 0, Step::chanInd);
    ch -= p.step;
    if (ch < p.min) ch = p.min;
    for (int step = 0; step < howManySteps(seq); ++ step){
      for (int row =0; row < howManyStepDataRows(seq, step); ++row){
        setStepDataAt(seq, step, row, Step::chanInd, ch);
      }
    }
  }
  if (p.shortName == "tps"){
    int tps = sequences[seq].getTicksPerStep();
    tps -= p.step;
    if (tps < p.min) tps = p.min;
    sequences[seq].onZeroSetTicksPerStep(tps);
  }
}
