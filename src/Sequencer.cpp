#include "Sequencer.h"
#include "MidiUtils.h"
#include <sstream>
#include <iomanip>

Step::Step() : rw_mutex{std::make_unique<std::shared_mutex>()}, active{true}

{
  data.push_back(std::vector<double>());
  for (std::size_t i=0;i<=Step::maxInd;++i){
    data[0].push_back(0.0);
  }
}
/** returns a copy of the data stored in this step*/
std::vector<std::vector<double>> Step::getData() const
{
  // std::shared_lock<std::shared_mutex> lock(*rw_mutex);
  return data;
}
double Step::getDataAt(std::size_t row, std::size_t col) const
{
  // this lock allows multiple concorrent reads
  // std::shared_lock<std::shared_mutex> lock(*rw_mutex);
  return data[row][col];
}
std::size_t Step::howManyDataRows() const 
{
  // std::shared_lock<std::shared_mutex> lock(*rw_mutex);
  return data.size();
}
std::size_t Step::howManyDataCols() const
{
  // std::shared_lock<std::shared_mutex> lock(*rw_mutex);
  return data[0].size();
}

// std::vector<std::vector<double>>* Step::getDataDirect()
// {
//   return &data;
// }

std::string Step::toStringFlat() const
{
  // std::shared_lock<std::shared_mutex> lock(*rw_mutex);
  if (this->data[0][Step::noteInd] == 0){
    return "----";
  }
  else {
    std::string disp = "";
    std::size_t note = (int)this->data[0][Step::noteInd];
    char nchar = MidiUtils::getIntToNoteMap()[note % 12];
    std::size_t oct = note / 12; 
    disp.push_back(nchar);
    disp += "-" + std::to_string(oct) + " ";
    std::size_t power = (int)this->data[0][Step::velInd] / 32; 
    for (std::size_t p=0;p<power;++p){
      disp += "]";
    }
    return disp; 
    // return std::string(1, note);
  }
  // return std::to_string((int)this->data[0][Step::noteInd]);
}

std::vector<std::vector<std::string>> Step::toStringGrid() const 
{

  // std::shared_lock<std::shared_mutex> lock(*rw_mutex);

  // each data sub vector should be on its own row
  //
  std::vector<std::vector<std::string>> grid;
  // assert (data.size() > 0);
  // a row is
  for (std::size_t col = 0; col < data[0].size(); ++col)
  {
    std::vector<std::string> colData;
    // colData.resize(data.size());
    for (std::size_t row = 0; row < data.size(); ++row)
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
        // decide how to display the step 
        // based on column index
        if (col == Step::probInd){
          colData.push_back(cmd.parameters[col - 1].shortName + Step::dblToString(data[row][col], 2));
        }
        else{
          colData.push_back(cmd.parameters[col - 1].shortName + std::to_string((int)data[row][col]));
        }
      }
    }
    grid.push_back(colData);
  }
  return grid;
}

void Step::activate()
{
  // std::unique_lock<std::shared_mutex> lock(*rw_mutex);
  active = true;
}
void Step::deactivate()
{
  // std::unique_lock<std::shared_mutex> lock(*rw_mutex);
  active = false;
}

/** sets the data stored in this step */
void Step::setData(const std::vector<std::vector<double>> &_data)
{
  // uni lock as writing data
  // std::unique_lock<std::shared_mutex> lock(*rw_mutex);
  this->data = std::move(_data); // copy it over
}

void Step::resetRow(std::size_t row)
{
  // std::unique_lock<std::shared_mutex> lock(*rw_mutex);
  assert(row < data.size());
  for (std::size_t col = 0; col < data[row].size(); ++col)
  {
    if (col != Step::chanInd){// do not reset channel
      data[row][col] = 0;
    }
  }
}

/** update one value in the data vector for this step*/
void Step::setDataAt(std::size_t row, std::size_t col, double value)
{
  // uni lock as writing data
  // std::unique_lock<std::shared_mutex> lock(*rw_mutex);
  assert(row < data.size());
  assert(col < data[row].size());

  // apply data constraints based on current command
  if (col == Step::cmdInd)
  { // changing the command - only allowed a value 0->no. commands
    std::size_t maxCmds = CommandProcessor::countCommands();
    if (value >= maxCmds)
      value = maxCmds - 1;
    if (value < 0)
      value = 0;
  }
  else if (col > Step::cmdInd)
  { // it is one of the parameter columns - use parameter spec constraints
    Command cmd = CommandProcessor::getCommand(data[row][Step::cmdInd]);
    std::size_t pInd = col - 1;
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
void Step::trigger(std::size_t row) 
{
  // shared lock as reading
  // std::shared_lock<std::shared_mutex> lock(*rw_mutex);
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

std::string Step::dblToString(double val, std::size_t dps)
{
  // quite verbose C++ way to make a string of a double with 2sf
  std::ostringstream oss;
  oss << std::fixed << std::setprecision(dps) << val;
  return oss.str();
}


Sequence::Sequence(Sequencer *_sequencer,
                   std::size_t seqLength,
                   unsigned short _midiChannel)
    : sequencer{_sequencer}, currentStep{0},
      midiChannel{_midiChannel}, type{SequenceType::midiNote},
      transpose{0}, lengthAdjustment{0}, ticksPerStep{4}, originalTicksPerStep{4}, nextTicksPerStep{0}, ticksElapsed{0}, tickOfFour{0}, muted{false}, 
      rw_mutex{std::make_unique<std::shared_mutex>()}, rewindAtNextZeroTick{false}
// , midiScaleToDrum{MidiUtils::getScaleMidiToDrumMidi()}
{
  currentLength = seqLength;
  for (std::size_t i = 0; i < seqLength; i++)
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
  // std::unique_lock<std::shared_mutex> lock(*rw_mutex);
  
  ++ticksElapsed;
  tickOfFour = ++tickOfFour % 4;
  
  // jump to the top 
  if (rewindAtNextZeroTick && tickOfFour == 0){
    tickOfFour = 0;
    ticksElapsed = 0;
    currentStep = 0; 
    rewindAtNextZeroTick = false; 
  }
  // std::cout << "elap " <<ticksElapsed << " t/4 " << tickOfFour << " tps " << ticksPerStep <<" next tps " << nextTicksPerStep << std::endl; 
  if (nextTicksPerStep  > 0 && tickOfFour == 0){// update to this tps on next zero of tickOfFour
      // std::cout << "changing tps " << nextTicksPerStep << std::endl;
      this->originalTicksPerStep = this->nextTicksPerStep;
      this->ticksElapsed = ticksPerStep;
      currentStep = 0;
      deactivateProcessors();

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
      steps[currentStep].trigger(0);
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

void Sequence::triggerStep(std::size_t step, std::size_t row)
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
void Sequence::resetStepRow(std::size_t step, std::size_t row)
{
  steps[step].resetRow(row);
}


void Sequence::setLengthAdjustment(std::size_t lenAdjust)
{
  // make sure we have enough steps
  this->ensureEnoughStepsForLength(currentLength + lenAdjust);
  this->lengthAdjustment = lenAdjust;
}

void Sequence::setTicksPerStep(std::size_t tps)
{
  // std::unique_lock<std::shared_mutex> lock(*rw_mutex);
  this->originalTicksPerStep = tps;
  this->ticksElapsed = 0;
}

void Sequence::onZeroSetTicksPerStep(std::size_t _nextTicksPerStep)
{
  // std::unique_lock<std::shared_mutex> lock(*rw_mutex);
  this->nextTicksPerStep = _nextTicksPerStep;
}

void Sequence::setTicksPerStepAdjustment(std::size_t tps)
{
  if (tps < 1 || tps > 16)
    return;
  this->ticksPerStep = tps;
}

std::size_t Sequence::getTicksPerStep() const
{
  // std::shared_lock<std::shared_mutex> lock(*rw_mutex);
  return this->originalTicksPerStep;
}

std::size_t Sequence::getNextTicksPerStep() const
{
  // std::shared_lock<std::shared_mutex> lock(*rw_mutex);
  if (this->nextTicksPerStep == 0){
    return this->originalTicksPerStep;
  }
  else {
    return this->nextTicksPerStep;
  }
}
    
std::size_t Sequence::getCurrentStep() const
{
  // std::shared_lock<std::shared_mutex> lock(*rw_mutex);
  return currentStep;
}
bool Sequence::assertStep(std::size_t step) const
{
  if (step >= steps.size() || step < 0)
    return false;
  return true;
}
std::vector<std::vector<double>> Sequence::getStepData(std::size_t step)
{
  return steps[step].getData();
}

double Sequence::getStepDataAt(std::size_t step, std::size_t row, std::size_t col)
{
  return steps[step].getDataAt(row, col);
}

// std::vector<std::vector<double>>* Sequence::getStepDataDirect(std::size_t step)
// {
//   return steps[step].getDataDirect();
// }
// Step* Sequence::getStep(std::size_t step)
// {
//   return &steps[step];
// }

std::vector<std::vector<double>> Sequence::getCurrentStepData()
{
  return steps[currentStep].getData();
}
std::size_t Sequence::getLength() const
{
  // std::shared_lock<std::shared_mutex> lock(*rw_mutex);
  return currentLength;
}

void Sequence::ensureEnoughStepsForLength(std::size_t length)
{
  // std::unique_lock<std::shared_mutex> lock(*rw_mutex);
  if (length > steps.size()) // bad need more steps
  {
    std::size_t toAdd = length - steps.size();
    for (std::size_t i = 0; i < toAdd; ++i)
    {
      Step s;
      s.setCallback(
          steps[0].getCallback());
      // set the channel
      std::size_t channel = steps[0].getDataAt(0, Step::chanInd);
      s.setDataAt(0, Step::chanInd, channel);
      steps.push_back(std::move(s));
    }
  }
}
void Sequence::setLength(std::size_t length)
{
// std::unique_lock<std::shared_mutex> lock(*rw_mutex);
  if (length < 1)
    return;
  if (length > steps.size())
    return;

  currentLength = length;
}

void Sequence::setStepData(std::size_t step, std::vector<std::vector<double>> data)
{
  steps[step].setData(data);
}
/** update a single data value in a given step*/
void Sequence::setStepDataAt(std::size_t step, std::size_t row, std::size_t col, double value)
{
  steps[step].setDataAt(row, col, value);
}

void Sequence::setStepCallback(std::size_t step,
                               std::function<void(std::vector<std::vector<double>> *)> callback)
{
  steps[step].setCallback(callback);
}
std::string Sequence::stepToString(std::size_t step)
{
  std::vector<std::vector<double>> data = getStepData(step);
  if (data.size() > 0)
    return std::to_string(data[0][0]);
  else
    return "-";
}

std::size_t Sequence::howManySteps() const
{
  // std::shared_lock<std::shared_mutex> lock(*rw_mutex);
  // return steps.size();
  //  case where length adjust is too high
  // if (currentLength + lengthAdjustment >= steps.size()) return currentLength;

  return currentLength + lengthAdjustment > 0 ? currentLength + lengthAdjustment : 1;
}

std::size_t Sequence::howManyStepDataRows(std::size_t step)
{
  return steps[step].howManyDataRows();
}
std::size_t Sequence::howManyStepDataCols(std::size_t step)
{
  return steps[step].howManyDataCols();
}

void Sequence::toggleActive(std::size_t step)
{
  steps[step].toggleActive();
}
bool Sequence::isStepActive(std::size_t step) const
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

std::string Sequence::stepToStringFlat(std::size_t step)
{
  if (isMuted()) return "";
  
  return steps[step].toStringFlat();
}

void Sequence::reset()
{
  // std::unique_lock<std::shared_mutex> lock(*rw_mutex);

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
std::vector<std::vector<std::string>> Sequence::stepAsGridOfStrings(std::size_t step)
{
  return steps[step].toStringGrid();
}

bool Sequence::isMuted() const
{
  // std::shared_lock<std::shared_mutex> lock(*rw_mutex);
  return muted;
}
/** change mote state to its opposite */
void Sequence::toggleMuteState()
{
  // std::unique_lock<std::shared_mutex> lock(*rw_mutex);
  muted = !muted;
}

void Sequence::rewindAtNextZero()
{
  rewindAtNextZeroTick = true;  
}


/////////////////////// Sequencer

Sequencer::Sequencer(std::size_t seqCount, std::size_t seqLength) : rw_mutex{std::make_unique<std::shared_mutex>()}, playing{true}, triggerOnTick{true}, stringUpdateRequested{false}
{
  for (std::size_t i = 0; i < seqCount; ++i)
  {
    sequences.push_back(Sequence{this, seqLength});
  }

  
  updateSeqStringGrid();
  setupSeqConfigSpecs();
  // std::vector<Parameter> paramSpecs;
  // //    Parameter(const std::string& name, const std::string& shortName, double min, double max, double step, double defaultValue);
  // paramSpecs.push_back(Parameter("Channel", "ch", 0, 15, 1, 1, Step::));
  // paramSpecs.push_back(Parameter("Ticks per step", "tps", 0, 15, 1, 1));
}

void Sequencer::setDefaultMIDIChannels()
{
  // set MIDI channels on steps to something useful
  std::size_t seqCount = sequences.size();
  for (std::size_t seq = 0; seq < seqCount; ++seq)
  {
    double ch = floor(seq / 2);
    std::size_t seqLength = sequences[seq].getLength();
    for (std::size_t step = 0; step < seqLength; ++step){
      setStepDataAt(seq, step, 0, Step::chanInd, ch);
    }
  }
}


Sequencer::~Sequencer()
{
}

void Sequencer::copyChannelAndTypeSettings(Sequencer *otherSeq)
{
  std::unique_lock<std::shared_mutex> lock(*rw_mutex);

  // ignore if diff sizes
  assert(otherSeq->sequences.size() == this->sequences.size());
  // if (otherSeq->sequences.size() != this->sequences.size()) return;
  for (std::size_t seq = 0; seq < this->sequences.size(); ++seq)
  {
    this->sequences[seq].setType(otherSeq->sequences[seq].getType());
    // assign the same channel
    double channel = otherSeq->sequences[seq].getStepDataAt(0, 0, Step::chanInd);
    for (std::size_t step = 0; step < this->sequences[seq].howManySteps(); ++step)
    {
      // note this assumes a single row step
      this->setStepDataAt(seq, step, 0, Step::chanInd, channel);
    }
  }
}

std::size_t Sequencer::howManySequences() const
{
  assert(sequences.size() < 128);
  return sequences.size();
}
std::size_t Sequencer::howManySteps(std::size_t sequence) const
{
  if (assertSequence(sequence))
    return sequences[sequence].howManySteps();
  else
    return 0;
}
std::size_t Sequencer::getCurrentStep(std::size_t sequence) const
{
  return sequences[sequence].getCurrentStep();
}

SequenceType Sequencer::getSequenceType(std::size_t sequence) const
{
  return sequences[sequence].getType();
}

std::size_t Sequencer::getSequenceTicksPerStep(std::size_t sequence) const
{
  return sequences[sequence].getTicksPerStep();
}

std::size_t Sequencer::getSequencerNextTicksPerStep(std::size_t sequence) const
{
  return sequences[sequence].getNextTicksPerStep();
}


/** move the sequencer along by one tick */
void Sequencer::tick()
{
  std::unique_lock<std::shared_mutex> lock(*rw_mutex);

  if (!playing) return; 
  for (Sequence &seq : sequences)
  {
    seq.tick(triggerOnTick);
  }
  if (this->stringUpdateRequested){
    this->updateSeqStringGrid();
    stringUpdateRequested = false; 
  }
}

void Sequencer::triggerStep(std::size_t seq, std::size_t step, std::size_t row)
{
  std::unique_lock<std::shared_mutex> lock(*rw_mutex);

  sequences[seq].triggerStep(step, row);
}

Sequence *Sequencer::getSequence(std::size_t sequence)
{
  return &(sequences[sequence]);
}

void Sequencer::setSequenceType(std::size_t sequence, SequenceType type)
{
  std::unique_lock<std::shared_mutex> lock(*rw_mutex);

  sequences[sequence].setType(type);
}

void Sequencer::setSequenceLength(std::size_t sequence, std::size_t length)
{
  std::unique_lock<std::shared_mutex> lock(*rw_mutex);

  sequences[sequence].setLength(length);
}

void Sequencer::shrinkSequence(std::size_t sequence)
{
  std::unique_lock<std::shared_mutex> lock(*rw_mutex);

  sequences[sequence].setLength(sequences[sequence].getLength() - 1);
}
void Sequencer::extendSequence(std::size_t sequence)
{
  std::unique_lock<std::shared_mutex> lock(*rw_mutex);

  sequences[sequence].ensureEnoughStepsForLength(sequences[sequence].getLength() + 1);
  sequences[sequence].setLength(sequences[sequence].getLength() + 1);
}

/** update the data stored at a step in the sequencer */
void Sequencer::setStepData(std::size_t sequence, std::size_t step, std::vector<std::vector<double>> data)
{
  std::unique_lock<std::shared_mutex> lock(*rw_mutex);

  if (!assertSeqAndStep(sequence, step))
    return;
  sequences[sequence].setStepData(step, data);
}
/** update a single value in the  data
 * stored at a step in the sequencer */
void Sequencer::setStepDataAt(std::size_t sequence, std::size_t step, std::size_t row, std::size_t col, double value)
{
  std::unique_lock<std::shared_mutex> lock(*rw_mutex);

  if (!assertSeqAndStep(sequence, step))
    return;
  sequences[sequence].setStepDataAt(step, row, col, value);
}

std::size_t Sequencer::howManyStepDataRows(std::size_t seq, std::size_t step)
{
  std::shared_lock<std::shared_mutex> lock(*rw_mutex);
  return sequences[seq].howManyStepDataRows(step);
}
std::size_t Sequencer::howManyStepDataCols(std::size_t seq, std::size_t step)
{
  std::shared_lock<std::shared_mutex> lock(*rw_mutex);
  return sequences[seq].howManyStepDataCols(step);
}

/** retrieve a copy of the data for a specific step */
std::vector<std::vector<double>> Sequencer::getStepData(std::size_t sequence, std::size_t step)
{
  std::shared_lock<std::shared_mutex> lock(*rw_mutex);
  if (!assertSeqAndStep(sequence, step))
    return std::vector<std::vector<double>>{};
  return sequences[sequence].getStepData(step);
}

// /** retrieve the data for a specific step */
// std::vector<std::vector<double>>* Sequencer::getStepDataDirect(std::size_t sequence, std::size_t step)
// {
//   assert(sequence < sequences.size());
//   assert(step < sequences[sequence].howManySteps());
//   return sequences[sequence].getStepDataDirect(step);
// }

void Sequencer::toggleStepActive(std::size_t sequence, std::size_t step)
{
  std::unique_lock<std::shared_mutex> lock(*rw_mutex);

  // if (!assertSeqAndStep(sequence, step))
    // return;
  sequences[sequence].toggleActive(step);
}
bool Sequencer::isStepActive(std::size_t sequence, std::size_t step) const
{
  std::shared_lock<std::shared_mutex> lock(*rw_mutex);
  // if (!assertSeqAndStep(sequence, step))
    // return false;
  return sequences[sequence].isStepActive(step);
}
void Sequencer::addStepListener()
{
}


void Sequencer::resetSequence(std::size_t sequence)
{
  std::unique_lock<std::shared_mutex> lock(*rw_mutex);
  sequences[sequence].reset();
}

void Sequencer::resetStepRow(std::size_t sequence, std::size_t step, std::size_t row)
{
  sequences[sequence].resetStepRow(step, row);
}


bool Sequencer::assertSeqAndStep(std::size_t sequence, std::size_t step) const
{

  if (!assertSequence(sequence))
    return false;
  if (!sequences[sequence].assertStep(step))
    return false;
  return true;
}

bool Sequencer::assertSequence(std::size_t sequence) const
{
// std::shared_lock<std::shared_mutex> lock(*rw_mutex);

  if (sequence >= sequences.size() || sequence < 0)
  {
    return false;
  }
  return true;
}

void Sequencer::updateSeqStringGrid()
{
  std::unique_lock<std::shared_mutex> lock(*rw_mutex);

  std::vector<std::vector<std::string>> gridView;
  // need to get the data in the sequences, convert it to strings and
  // store it into the sent grid view
  if (gridView.size() != howManySequences())
  {
    gridView.resize(howManySequences());
  }

  assert(gridView.size() >= howManySequences());

  // first. find the longest sequence
  std::size_t maxSteps = 0;
  for (std::size_t seq = 0; seq < howManySequences(); ++seq)
  {
    if (howManySteps(seq) > maxSteps)
      maxSteps = howManySteps(seq);
  }

  for (std::size_t seq = 0; seq < howManySequences() && seq < gridView.size(); ++seq)
  {
    // check we have the length
    // if (gridView[seq].size() != howManySteps(seq)){
    if (gridView[seq].size() != maxSteps)
    {

      // std::cout << "resizing " << seq << std::endl;
      // gridView[seq].resize(howManySteps(seq));
      gridView[seq].resize(maxSteps);
      for (std::size_t i = 0; i < gridView[seq].size(); ++i)
        gridView[seq][i] = "";
    }
    assert(gridView[seq].size() >= howManySteps(seq));
    for (std::size_t step = 0; step < howManySteps(seq) && step < gridView[seq].size(); ++step)
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
  std::shared_lock<std::shared_mutex> lock(*rw_mutex);// read lock
  return seqAsStringGrid;
}
std::vector<std::vector<std::string>> Sequencer::getStepAsGridOfStrings(std::size_t seq, std::size_t step)
{
  std::shared_lock<std::shared_mutex> lock(*rw_mutex);// read lock
  return sequences[seq].stepAsGridOfStrings(step);
}

double Sequencer::getStepDataAt(std::size_t seq, std::size_t step, std::size_t row, std::size_t col)
{
  std::shared_lock<std::shared_mutex> lock(*rw_mutex);// read lock
  return sequences[seq].getStepDataAt(step, row, col);
}

std::vector<std::vector<std::string>> Sequencer::getSequenceConfigsAsGridOfStrings()
{
  // std::shared_lock<std::shared_mutex> lock(*rw_mutex);// read lock

  // editable config items for a sequence:
// - set channel for all steps
// - set ticks per beat
// - set transpose maybe? 
  // each col is a sequence
  std::vector<std::vector<std::string>>confGrid;
  std::vector<Parameter> params = getSeqConfigSpecs();

  for (std::size_t seq =0;seq<howManySequences();++seq){
    confGrid.push_back(std::vector<std::string>());
    for (Parameter& p : params){
      if (p.stepCol == Step::chanInd ||
        p.stepCol == Step::probInd){ 
        // display the setting on the first step in this sequence
        double val = getStepDataAt(seq, 0, 0, p.stepCol);
        confGrid[seq].push_back(p.shortName + ":" + Step::dblToString(val, p.decPlaces));
      }
      if (p.stepCol == -1){ // config is sequence level not step
        // ticks
        confGrid[seq].push_back(p.shortName + ":" + std::to_string(getSequencerNextTicksPerStep(seq)));    
      }
    }
  }    
  return confGrid;
}

void Sequencer::toggleSequenceMute(std::size_t sequence)
{
  std::unique_lock<std::shared_mutex> lock(*rw_mutex);
  sequences[sequence].toggleMuteState();
}


std::vector<Parameter>& Sequencer::getSeqConfigSpecs()  
{
  return seqConfigSpecs; 
}
void Sequencer::setupSeqConfigSpecs()
{
  seqConfigSpecs.resize(3);
  seqConfigSpecs[Sequence::chanConfig] = Parameter("Channel", "Ch", 0, 15, 1, 1, Step::chanInd); // affects step channel value
  seqConfigSpecs[Sequence::tpsConfig] = Parameter("Ticks per step", "TPS", 1, 16, 1, 4, -1); // -1 as no step level option
  seqConfigSpecs[Sequence::probConfig] = Parameter("Probability %", "P", 0.0, 1.0, 0.1, 1.0, Step::probInd, 2); // affects step prob value
  // TODO
  // seqParamSpecs.push_back(Parameter("Velocity variation plus/minus %", "velvary", 0.0, 1.0, 0.1, 0.0));
  // seqParamSpecs.push_back(Parameter("Shuffle +/- ticks", "shuf", 0, 3, 1, 0.0));
  
  
}


void Sequencer::incrementSeqParam(std::size_t seq, std::size_t paramIndex)
{
  // std::unique_lock<std::shared_mutex> lock(*rw_mutex);// write lock - this function edits sequencer data

  assert(paramIndex < getSeqConfigSpecs().size() &&
         paramIndex >= 0);
  Parameter p = seqConfigSpecs[paramIndex];
  // deal with 'step-level' where we are overriding at sequence level
  if (paramIndex == Sequence::chanConfig || 
     paramIndex == Sequence::probConfig){
    // meed to update all steps to the new parameter     sequences[seq].
    double val = getStepDataAt(seq, 0, 0, p.stepCol);
    val += p.step;
    if (val >= p.max) val = p.max;
    for (std::size_t step = 0; step < howManySteps(seq); ++ step){
      for (std::size_t row =0; row < howManyStepDataRows(seq, step); ++row){
        setStepDataAt(seq, step, row, p.stepCol, val);
      }
    }
  }
  // extra behaviour - if they are setting the sequence's channel, 
  // remember it for use for new notes
  if (paramIndex == Sequence::chanConfig){
    
  }


  if (paramIndex == Sequence::tpsConfig){
    std::size_t tps = sequences[seq].getTicksPerStep();
    tps += p.step;
    if (tps > p.max) tps = p.max;
    sequences[seq].onZeroSetTicksPerStep(tps);
  }

}
void Sequencer::decrementSeqParam(std::size_t seq, std::size_t paramIndex)
{
  // std::unique_lock<std::shared_mutex> lock(*rw_mutex);// write lock - this function edits sequencer data

  assert(paramIndex < getSeqConfigSpecs().size() &&
         paramIndex >= 0);

  Parameter p = seqConfigSpecs[paramIndex];
  if (paramIndex == Sequence::chanConfig || 
     paramIndex == Sequence::probConfig){
    // meed to update all steps to the new parameter     sequences[seq].
    double val = getStepDataAt(seq, 0, 0, p.stepCol);
    val -= p.step;
    if (val < p.min) val = p.min;
    for (std::size_t step = 0; step < howManySteps(seq); ++ step){
      for (std::size_t row =0; row < howManyStepDataRows(seq, step); ++row){
        setStepDataAt(seq, step, row, p.stepCol, val);
      }
    }
  }
  if (paramIndex == Sequence::tpsConfig){
    std::size_t tps = sequences[seq].getTicksPerStep();
    tps -= p.step;
    if (tps < p.min) tps = p.min;
    sequences[seq].onZeroSetTicksPerStep(tps);
  }
}



void Sequencer::incrementStepDataAt(std::size_t sequence, std::size_t step, std::size_t row, std::size_t col)
{
  double val = getStepDataAt(sequence, step, row, col);

  // check if they are changing the step command. 
  // if so, do not use param config stuff to edit. 
  if (col == Step::cmdInd) {
    val ++;
    if (val >= CommandProcessor::countCommands()) val = CommandProcessor::countCommands();
  }
  else {
    double stepCmd = getStepDataAt(sequence, step, row, Step::cmdInd);
    // param dictates the step, min and max for this column
    Parameter param = CommandProcessor::getCommand(stepCmd).parameters[col-1]; // -1 as the first col is the command which has no parameter
    val += param.step;
    if (val > param.max) val = param.max;
  }
  setStepDataAt(sequence, step, row, col, val);
}

void Sequencer::decrementStepDataAt(std::size_t sequence, std::size_t step, std::size_t row, std::size_t col)
{
  double val = getStepDataAt(sequence, step, row, col);
  if (col == Step::cmdInd) {
    val --;
    if (val < 0) val = 0;
  }
  else {
    // get the step param config for the step's 
    double stepCmd = getStepDataAt(sequence, step, row, Step::cmdInd);
    // param dictates the step, min and max for this column
    Parameter param = CommandProcessor::getCommand(stepCmd).parameters[col-1]; // -1 as the first col is the command which has no parameter
    val -= param.step;
    if (val < param.min) val = param.min;
  }
  setStepDataAt(sequence, step, row, col, val);
}

void Sequencer::setStepDataToDefault(std::size_t sequence, std::size_t step, std::size_t row, std::size_t col)
{
  double stepCmd = getStepDataAt(sequence, step, row, Step::cmdInd);
  // param dictates the step, min and max for this column
  Parameter param = CommandProcessor::getCommand(stepCmd).parameters[col-1]; // -1 as the first col is the command which has no parameter
  setStepDataAt(sequence, step, row, col, param.defaultValue);
}


void Sequencer::disableAllTriggers()
{
  // std::unique_lock<std::shared_mutex> lock(*rw_mutex);// write lock - this function edits sequencer data
  triggerOnTick = false;   
}
void Sequencer::enableAllTriggers()
{
  // std::unique_lock<std::shared_mutex> lock(*rw_mutex);// write lock - this function edits sequencer data
  triggerOnTick = true; 
}

/** stop playing*/
void Sequencer::stop()
{
  playing = false; 
}
/** start playing */
void Sequencer::play()
{
  playing = true; 
}

bool Sequencer::isPlaying()
{
  return playing; 
}

void Sequencer::rewindAtNextZero()
{
  for (Sequence& seq : sequences){seq.rewindAtNextZero();}
}


void Sequencer::requestStrUpdate()
{
  this->stringUpdateRequested = true; 
}
