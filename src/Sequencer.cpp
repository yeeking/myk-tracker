#include "Sequencer.h"
#include "MachineUtilsAbs.h"
#include <algorithm>
#include <sstream>
#include <iomanip>
#include <cmath>
#include <limits>

Step::Step() : rw_mutex{std::make_unique<std::shared_mutex>()}, active{true}

{
  data.push_back(std::vector<double>());
  for (std::size_t i=0;i<=Step::maxInd;++i){
    data[0].push_back(0.0);
  }
  data[0][Step::cmdInd] = static_cast<double>(CommandType::MidiNote);
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

std::string Step::toStringFlat(const SequenceReadOnly* sequenceContext) const
{
  if (sequenceContext == nullptr)
    return "----";

  if (std::abs(this->data[0][Step::noteInd]) < std::numeric_limits<double>::epsilon()){
    return "----";
  }

  std::string disp = CommandProcessor::describeStepNote(sequenceContext, this->data[0][Step::noteInd]);
  int velInt = static_cast<int>(this->data[0][Step::velInd]);
  if (velInt < 0)
    velInt = 0;
  std::size_t power = static_cast<std::size_t>(velInt / 32);
  for (std::size_t p=0;p<power;++p){
    disp += "+";
  }
  return disp; 
}

std::vector<std::vector<std::string>> Step::toStringGrid(const SequenceReadOnly* sequenceContext) const 
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
        colData.push_back(cmd.parameters[col - 1].shortName + std::to_string((int)data[row][col]));
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
    data[row][col] = 0;
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
  {
    const auto command = static_cast<CommandType>(static_cast<std::size_t>(std::max(0.0, value)));
    if (command != CommandType::MidiNote && command != CommandType::Log
        && command != CommandType::Sampler && command != CommandType::WavetableSynth)
      value = static_cast<double>(CommandType::MidiNote);
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
void Step::trigger(std::size_t row, const SequenceReadOnly* sequenceContext) 
{
  // shared lock as reading
  // std::shared_lock<std::shared_mutex> lock(*rw_mutex);
  // std::cout << "Step::trigger" << std::endl;
  if (active)
  {
    if (row < data.size())
    { // only trigger one row
      // note that the command decides if 
      // the data is valid and therefore, if it should do anything, not the step 
      CommandProcessor::executeCommand(data[row][Step::cmdInd], &data[row], sequenceContext);
    }
    else
    {
      for (std::vector<double> &dataRow : data)
      {
      // note that the command decides if 
      // the data is valid and therefore, if it should do anything, not the step 
        CommandProcessor::executeCommand(dataRow[Step::cmdInd], &dataRow, sequenceContext);
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

bool Step::hasTriggerableEvent() const
{
  for (const auto& row : data)
  {
    if (row.size() <= Step::noteInd || row[Step::noteInd] <= 0.0)
      continue;
    const auto command = static_cast<CommandType>(static_cast<std::size_t>(std::max(0.0, row[Step::cmdInd])));
    if (command == CommandType::MidiNote || command == CommandType::Log
        || command == CommandType::Sampler || command == CommandType::WavetableSynth)
      return true;
  }
  return false;
}

std::string Step::dblToString(double val, std::size_t dps)
{
  // quite verbose C++ way to make a string of a double with 2sf
  std::ostringstream oss;
  oss << std::fixed << std::setprecision(static_cast<int>(dps)) << val;
  return oss.str();
}


Sequence::Sequence(Sequencer *_sequencer,
                   std::size_t seqLength,
                   unsigned short _machineId)
    : sequencer{_sequencer},
      currentLength{seqLength},
      currentStep{0},
      machineId{static_cast<double>(_machineId)},
      type{SequenceType::midiNote},
      machineType{static_cast<double>(CommandType::MidiNote)},
      transpose{0},
      lengthAdjustment{0},
      ticksPerStep{4},
      originalTicksPerStep{4},
      nextTicksPerStep{0},
      rewindAtNextZeroTick{false},
      ticksElapsed{0},
      tickOfFour{0},
       muted{false},
        pendingQuarterBeatResync{std::make_unique<std::atomic<bool>>(false)},
        triggerEventCount{std::make_unique<std::atomic<std::uint64_t>>(0)},
        rw_mutex{std::make_unique<std::shared_mutex>()}
// , midiScaleToDrum{MachineUtilsAbs::getScaleMidiToDrumMidi()}
{
  readHeads.emplace_back();
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
void Sequence::tick(bool trigger, bool isQuarterNoteBoundary)
{
  if (pendingQuarterBeatResync != nullptr && pendingQuarterBeatResync->load(std::memory_order_acquire))
  {
    if (!isQuarterNoteBoundary)
      return;

    pendingQuarterBeatResync->store(false, std::memory_order_release);
    resetReadHeads(true);
    rewindAtNextZeroTick = false;
    nextTicksPerStep = 0;
    tickOfFour = 3;
  }

  tickOfFour = (tickOfFour + 1) % 4;
  if (rewindAtNextZeroTick && tickOfFour == 0)
  {
    resetReadHeads(true);
    rewindAtNextZeroTick = false;
  }

  if (nextTicksPerStep > 0 && tickOfFour == 0)
  {
    auto config = readHeads.front().config;
    config.ticksPerStep = nextTicksPerStep;
    setReadHeadConfig(0, config, false);
    nextTicksPerStep = 0;
  }

  SequenceReadOnly context = getReadOnlyContext();
  std::uniform_real_distribution<double> chance(0.0, 1.0);
  bool anyStepTriggered = false;
  for (auto& head : readHeads)
  {
    ++head.ticksElapsed;
    if (head.ticksElapsed < head.config.ticksPerStep)
      continue;
    head.ticksElapsed = 0;

    const bool rhythmTrigger = head.config.rhythm[head.rhythmIndex] == '1';
    head.rhythmIndex = (head.rhythmIndex + 1) % head.config.rhythm.size();
    if (!rhythmTrigger)
      continue;

    selectPositions(head);
    if (head.positionCount == 0)
      continue;
    currentStep = head.positions[0];

    // Probability is a head-level gate. Selection still advances on a miss.
    if (!trigger || muted || chance(head.random) >= head.config.probability)
      continue;
    for (std::size_t position = 0; position < head.positionCount; ++position)
    {
      const auto stepIndex = head.positions[position];
      if (stepIndex < steps.size() && steps[stepIndex].isActive()
          && steps[stepIndex].hasTriggerableEvent())
      {
        steps[stepIndex].trigger(steps[stepIndex].howManyDataRows(), &context);
        anyStepTriggered = true;
      }
    }
  }

  // The UI polls this relaxed counter to render a brief trigger flash.
  if (anyStepTriggered && triggerEventCount != nullptr)
    triggerEventCount->fetch_add(1, std::memory_order_relaxed);

  ticksElapsed = readHeads.empty() ? 0 : readHeads.front().ticksElapsed;
}

std::size_t Sequence::getTicksElapsed() const
{
  return ticksElapsed;
}

std::size_t Sequence::getTickOfFour() const
{
  return tickOfFour;
}

void Sequence::triggerStep(std::size_t step, std::size_t row)
{
  SequenceReadOnly context = getReadOnlyContext();
  steps[step].trigger(row, &context);
}

void Sequence::deactivateProcessors()
{
  transpose = 0;
  lengthAdjustment = 0;
  ticksPerStep = getTicksPerStep();
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
  const auto clamped = std::min<std::size_t>(lenAdjust,
                                              static_cast<std::size_t>(std::numeric_limits<int>::max()));
  const int newValue = static_cast<int>(clamped);
  const bool changed = newValue != lengthAdjustment;
  this->lengthAdjustment = newValue;
  if (changed)
    requestQuarterBeatResyncIfPlaying();
}

void Sequence::setTicksPerStep(std::size_t tps)
{
  if (readHeads.empty()) readHeads.emplace_back();
  auto config = readHeads.front().config;
  config.ticksPerStep = std::max<std::size_t>(1, std::min<std::size_t>(16, tps));
  setReadHeadConfig(0, config);
  originalTicksPerStep = config.ticksPerStep;
  ticksPerStep = config.ticksPerStep;
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
  setTicksPerStep(tps);
}

std::size_t Sequence::getTicksPerStep() const
{
  // std::shared_lock<std::shared_mutex> lock(*rw_mutex);
  return readHeads.empty() ? 4 : readHeads.front().config.ticksPerStep;
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
  if (step >= steps.size())
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
      s.setDataAt(0, Step::cmdInd, machineType);
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

  const bool changed = length != currentLength;
  currentLength = length;
  for (auto& head : readHeads)
  {
    head.linearStep %= currentLength;
    for (std::size_t i = 0; i < head.positionCount; ++i)
      head.positions[i] %= currentLength;
  }
  if (changed)
    requestQuarterBeatResyncIfPlaying();
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

  const long long adjustedLength =
      static_cast<long long>(currentLength) + static_cast<long long>(lengthAdjustment);
  return adjustedLength > 0 ? static_cast<std::size_t>(adjustedLength) : 1u;
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
void Sequence::setType(SequenceType _type)
{
  this->type = _type;
}
SequenceType Sequence::getType() const
{
  return this->type;
}

void Sequence::setMachineType(double newMachineType)
{
  const auto command = static_cast<CommandType>(static_cast<std::size_t>(std::max(0.0, newMachineType)));
  if (command != CommandType::MidiNote && command != CommandType::Log
      && command != CommandType::Sampler && command != CommandType::WavetableSynth)
    newMachineType = static_cast<double>(CommandType::MidiNote);
  this->machineType = newMachineType;
}

double Sequence::getMachineType() const
{
  return this->machineType;
}

void Sequence::setMachineId(double newMachineId)
{
  if (newMachineId < 0) newMachineId = 0;
  if (newMachineId > 15) newMachineId = 15;
  this->machineId = newMachineId;
}

double Sequence::getMachineId() const
{
  return this->machineId;
}

std::size_t Sequence::getReadHeadCount() const
{
  return readHeads.size();
}

void Sequence::setReadHeadCount(std::size_t count)
{
  count = std::max<std::size_t>(1, std::min<std::size_t>(maxReadHeads, count));
  const bool changed = count != readHeads.size();
  if (count < readHeads.size())
    readHeads.resize(count);
  else
    while (readHeads.size() < count)
      readHeads.emplace_back();
  resetReadHeads(false);
  if (changed)
    requestQuarterBeatResyncIfPlaying();
}

const SequenceReadHeadConfig& Sequence::getReadHeadConfig(std::size_t head) const
{
  assert(head < readHeads.size());
  return readHeads[head].config;
}

bool Sequence::setReadHeadConfig(std::size_t head, const SequenceReadHeadConfig& requested, bool requestResync)
{
  if (head >= readHeads.size())
    return false;
  auto config = requested;
  config.ticksPerStep = std::max<std::size_t>(1, std::min<std::size_t>(16, config.ticksPerStep));
  config.polyphony = std::max<std::size_t>(1, std::min<std::size_t>(maxChordPolyphony, config.polyphony));
  config.probability = std::max(0.0, std::min(1.0, config.probability));
  const auto& presets = getRhythmPresets();
  if (std::find(presets.begin(), presets.end(), config.rhythm) == presets.end())
    return false;
  const auto oldConfig = readHeads[head].config;
  const bool resetPhase = oldConfig.ticksPerStep != config.ticksPerStep
      || oldConfig.mode != config.mode || oldConfig.rhythm != config.rhythm;
  readHeads[head].config = std::move(config);
  if (resetPhase)
  {
    readHeads[head].random.seed(0x4d594b31u + static_cast<std::uint32_t>(head * 0x9e3779b9u));
    resetReadHeadRuntime(readHeads[head], false);
    if (requestResync)
      requestQuarterBeatResyncIfPlaying();
  }
  if (head == 0)
  {
    ticksPerStep = readHeads[head].config.ticksPerStep;
    originalTicksPerStep = ticksPerStep;
  }
  return true;
}

std::vector<SequenceReadHeadSnapshot> Sequence::getReadHeadSnapshots() const
{
  std::vector<SequenceReadHeadSnapshot> result;
  result.reserve(readHeads.size());
  for (const auto& head : readHeads)
  {
    SequenceReadHeadSnapshot snapshot;
    snapshot.config = head.config;
    snapshot.positions.assign(head.positions.begin(), head.positions.begin() + static_cast<long>(head.positionCount));
    result.push_back(std::move(snapshot));
  }
  return result;
}

std::uint64_t Sequence::getTriggerEventCount() const noexcept
{
  return triggerEventCount != nullptr
      ? triggerEventCount->load(std::memory_order_relaxed)
      : 0;
}

const std::vector<std::string>& Sequence::getRhythmPresets()
{
  static const std::vector<std::string> presets = []
  {
    std::vector<std::string> values;
    for (int width = 1; width <= 4; ++width)
      for (int bits = 1; bits < (1 << width); ++bits)
      {
        std::string value(static_cast<std::size_t>(width), '0');
        for (int bit = 0; bit < width; ++bit)
          if ((bits & (1 << (width - bit - 1))) != 0)
            value[static_cast<std::size_t>(bit)] = '1';
        values.push_back(std::move(value));
      }
    return values;
  }();
  return presets;
}

const char* Sequence::readModeName(SequenceReadMode mode)
{
  switch (mode)
  {
    case SequenceReadMode::linear: return "linear";
    case SequenceReadMode::random: return "random";
    case SequenceReadMode::randomChord: return "rand_chord";
  }
  return "linear";
}

bool Sequence::parseReadMode(const std::string& name, SequenceReadMode& mode)
{
  if (name == "linear") mode = SequenceReadMode::linear;
  else if (name == "random") mode = SequenceReadMode::random;
  else if (name == "rand_chord") mode = SequenceReadMode::randomChord;
  else return false;
  return true;
}

void Sequence::resetReadHeadRuntime(ReadHeadState& head, bool immediate)
{
  head.ticksElapsed = immediate ? head.config.ticksPerStep - 1 : 0;
  head.rhythmIndex = 0;
  head.linearStep = 0;
  head.positionCount = 0;
}

void Sequence::resetReadHeads(bool immediate)
{
  currentStep = 0;
  for (std::size_t index = 0; index < readHeads.size(); ++index)
  {
    readHeads[index].random.seed(0x4d594b31u + static_cast<std::uint32_t>(index * 0x9e3779b9u));
    resetReadHeadRuntime(readHeads[index], immediate);
  }
  ticksElapsed = readHeads.empty() ? 0 : readHeads.front().ticksElapsed;
}

void Sequence::requestQuarterBeatResyncIfPlaying()
{
  if (pendingQuarterBeatResync == nullptr)
    pendingQuarterBeatResync = std::make_unique<std::atomic<bool>>(false);
  pendingQuarterBeatResync->store(sequencer != nullptr && sequencer->isPlaying(), std::memory_order_release);
}

void Sequence::cancelQuarterBeatResync()
{
  if (pendingQuarterBeatResync != nullptr)
    pendingQuarterBeatResync->store(false, std::memory_order_release);
}

bool Sequence::isWaitingForQuarterBeatResync() const
{
  return pendingQuarterBeatResync != nullptr && pendingQuarterBeatResync->load(std::memory_order_acquire);
}

std::size_t Sequence::eligibleRandomSteps(std::array<std::size_t, 128>& eligible) const
{
  const auto length = std::min({currentLength, steps.size(), eligible.size()});
  std::size_t count = 0;
  for (std::size_t stepIndex = 0; stepIndex < length; ++stepIndex)
  {
    if (!steps[stepIndex].isActive())
      continue;
    const auto data = steps[stepIndex].getData();
    const bool hasNote = std::any_of(data.begin(), data.end(), [](const auto& row)
    {
      if (row.size() <= Step::noteInd || row[Step::noteInd] <= 0.0) return false;
      const auto command = static_cast<CommandType>(static_cast<std::size_t>(row[Step::cmdInd]));
      return command == CommandType::MidiNote || command == CommandType::Log
          || command == CommandType::Sampler || command == CommandType::WavetableSynth;
    });
    if (hasNote) eligible[count++] = stepIndex;
  }
  return count;
}

void Sequence::selectPositions(ReadHeadState& head)
{
  head.positionCount = 0;
  const auto length = std::max<std::size_t>(1, std::min(currentLength, steps.size()));
  if (head.config.mode == SequenceReadMode::linear)
  {
    head.positions[0] = head.linearStep % length;
    head.positionCount = 1;
    head.linearStep = (head.linearStep + 1) % length;
    return;
  }

  std::array<std::size_t, 128> eligible{};
  const auto eligibleCount = eligibleRandomSteps(eligible);
  if (eligibleCount == 0)
    return;
  for (std::size_t index = eligibleCount; index > 1; --index)
  {
    std::uniform_int_distribution<std::size_t> distribution(0, index - 1);
    std::swap(eligible[index - 1], eligible[distribution(head.random)]);
  }
  const auto requested = head.config.mode == SequenceReadMode::randomChord ? head.config.polyphony : 1u;
  head.positionCount = std::min<std::size_t>(requested, eligibleCount);
  for (std::size_t index = 0; index < head.positionCount; ++index)
    head.positions[index] = eligible[index];
}

SequenceReadOnly Sequence::getReadOnlyContext() const
{
  return SequenceReadOnly{machineType, machineId};
}

void Sequence::setTranspose(double _transpose)
{
  this->transpose = _transpose;
}

std::string Sequence::stepToStringFlat(std::size_t step)
{
  if (isMuted()) return "";
  SequenceReadOnly context = getReadOnlyContext();
  return steps[step].toStringFlat(&context);
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
    step.setDataAt(0, Step::cmdInd, machineType);
  }
}
std::vector<std::vector<std::string>> Sequence::stepAsGridOfStrings(std::size_t step)
{
  SequenceReadOnly context = getReadOnlyContext();
  return steps[step].toStringGrid(&context);
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

void Sequence::primeForImmediateTrigger()
{
  deactivateProcessors();
  resetReadHeads(true);
  rewindAtNextZeroTick = false;
  nextTicksPerStep = 0;
  tickOfFour = 3;
  cancelQuarterBeatResync();
}

void Sequence::resetForTransportStart()
{
  deactivateProcessors();
  resetReadHeads(true);
  rewindAtNextZeroTick = false;
  nextTicksPerStep = 0;
  tickOfFour = 3;
  cancelQuarterBeatResync();
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
  // set default machine ids to something useful
  std::size_t seqCount = sequences.size();
  for (std::size_t seq = 0; seq < seqCount; ++seq)
  {
    double machineId = floor(seq / 2);
    sequences[seq].setMachineId(machineId);
  }
  // no lock held here, so request via the locking entry point
  requestStrUpdate();
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
    this->sequences[seq].setMachineId(otherSeq->sequences[seq].getMachineId());
    this->sequences[seq].setMachineType(otherSeq->sequences[seq].getMachineType());
    this->sequences[seq].setReadHeadCount(otherSeq->sequences[seq].getReadHeadCount());
    for (std::size_t head = 0; head < this->sequences[seq].getReadHeadCount(); ++head)
      this->sequences[seq].setReadHeadConfig(head, otherSeq->sequences[seq].getReadHeadConfig(head));
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
void Sequencer::tick(bool isQuarterNoteBoundary)
{
  // The string grid is a UI/MCP display cache, so it is rebuilt lazily by the
  // reader instead of here - building it on the audio thread allocated per edit
  std::unique_lock<std::shared_mutex> lock(*rw_mutex);
  if (playing)
  {
    for (auto& seq : sequences)
    {
      seq.tick(triggerOnTick, isQuarterNoteBoundary);
    }
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
  stringUpdateRequested = true;
}

void Sequencer::shrinkSequence(std::size_t sequence)
{
  std::unique_lock<std::shared_mutex> lock(*rw_mutex);

  sequences[sequence].setLength(sequences[sequence].getLength() - 1);
  stringUpdateRequested = true;
}
void Sequencer::extendSequence(std::size_t sequence)
{
  std::unique_lock<std::shared_mutex> lock(*rw_mutex);

  sequences[sequence].ensureEnoughStepsForLength(sequences[sequence].getLength() + 1);
  sequences[sequence].setLength(sequences[sequence].getLength() + 1);
  stringUpdateRequested = true;
}

/** update the data stored at a step in the sequencer */
void Sequencer::setStepData(std::size_t sequence, std::size_t step, std::vector<std::vector<double>> data)
{
  std::unique_lock<std::shared_mutex> lock(*rw_mutex);

  if (!assertSeqAndStep(sequence, step))
    return;
  double machineType = sequences[sequence].getMachineType();
  for (auto& row : data)
  {
    if (row.size() < Step::maxInd + 1)
      row.resize(Step::maxInd + 1, 0.0);
    row[Step::cmdInd] = machineType;
  }
  sequences[sequence].setStepData(step, data);
  // mark the display string grid stale so the UI rebuilds it on its next frame
  stringUpdateRequested = true;
}
/** update a single value in the  data
 * stored at a step in the sequencer */
void Sequencer::setStepDataAt(std::size_t sequence, std::size_t step, std::size_t row, std::size_t col, double value)
{
  std::unique_lock<std::shared_mutex> lock(*rw_mutex);

  if (!assertSeqAndStep(sequence, step))
    return;
  if (col == Step::cmdInd)
    value = sequences[sequence].getMachineType();
  sequences[sequence].setStepDataAt(step, row, col, value);
  stringUpdateRequested = true;
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
  stringUpdateRequested = true;
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
  stringUpdateRequested = true;
}

void Sequencer::resetStepRow(std::size_t sequence, std::size_t step, std::size_t row)
{
  std::unique_lock<std::shared_mutex> lock(*rw_mutex);

  if (!assertSeqAndStep(sequence, step))
    return;
  sequences[sequence].resetStepRow(step, row);
  // mark the display string grid stale so the UI rebuilds it on its next frame
  stringUpdateRequested = true;
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

  if (sequence >= sequences.size())
  {
    return false;
  }
  return true;
}

void Sequencer::updateSeqStringGrid()
{
  std::unique_lock<std::shared_mutex> lock(*rw_mutex);
  updateSeqStringGridLocked();
}

void Sequencer::updateSeqStringGridLocked()
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
  stringUpdateRequested = false;
}

std::vector<std::vector<std::string>> &Sequencer::getSequenceAsGridOfStrings()
{
  // rebuild lazily if an edit requested it; readers run on the message thread,
  // so this keeps the string construction off the audio path
  std::unique_lock<std::shared_mutex> lock(*rw_mutex);
  if (stringUpdateRequested)
    updateSeqStringGridLocked();
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

std::vector<std::vector<std::string>> Sequencer::getSequenceConfigsAsGridOfStrings(std::size_t selectedHead)
{
  // std::shared_lock<std::shared_mutex> lock(*rw_mutex);// read lock

  // editable config items for a sequence:
// - set channel for all steps
// - set ticks per beat
// - set transpose maybe? 
  // each col is a sequence
  std::vector<std::vector<std::string>>confGrid;
  for (std::size_t seq =0;seq<howManySequences();++seq){
    confGrid.push_back(std::vector<std::string>());
    Sequence* sequence = getSequence(seq);
    const auto headCount = sequence->getReadHeadCount();
    const auto headIndex = std::min(selectedHead, headCount - 1);
    const auto& head = sequence->getReadHeadConfig(headIndex);
    confGrid[seq].push_back("SEND:" + std::to_string(static_cast<int>(sequence->getMachineId())));
    confGrid[seq].push_back("HEADS:" + std::to_string(headCount));
    confGrid[seq].push_back("HEAD:" + std::to_string(headIndex + 1) + "/" + std::to_string(headCount));
    confGrid[seq].push_back("TPS:" + std::to_string(head.ticksPerStep));
    confGrid[seq].push_back("MODE:" + std::string(Sequence::readModeName(head.mode)));
    confGrid[seq].push_back("POLY:" + std::to_string(head.polyphony));
    confGrid[seq].push_back("RHY:" + head.rhythm);
    confGrid[seq].push_back("P:" + Step::dblToString(head.probability, 2));
  }    
  return confGrid;
}

void Sequencer::toggleSequenceMute(std::size_t sequence)
{
  std::unique_lock<std::shared_mutex> lock(*rw_mutex);
  sequences[sequence].toggleMuteState();
  // muted sequences render empty cells, so the grid must be rebuilt
  stringUpdateRequested = true;
}


std::vector<Parameter>& Sequencer::getSeqConfigSpecs()  
{
  return seqConfigSpecs; 
}
void Sequencer::setupSeqConfigSpecs()
{
  seqConfigSpecs.resize(Sequence::configCount);
  seqConfigSpecs[Sequence::sendConfig] = Parameter("Send", "SEND", 0, 15, 1, 0, -1);
  seqConfigSpecs[Sequence::headCountConfig] = Parameter("Heads", "HEADS", 1, 3, 1, 1, -1);
  seqConfigSpecs[Sequence::headConfig] = Parameter("Head", "HEAD", 0, 2, 1, 0, -1);
  seqConfigSpecs[Sequence::tpsConfig] = Parameter("Ticks per step", "TPS", 1, 16, 1, 4, -1);
  seqConfigSpecs[Sequence::modeConfig] = Parameter("Mode", "MODE", 0, 2, 1, 0, -1);
  seqConfigSpecs[Sequence::polyphonyConfig] = Parameter("Polyphony", "POLY", 1, 5, 1, 3, -1);
  seqConfigSpecs[Sequence::rhythmConfig] = Parameter("Rhythm", "RHY", 0, 25, 1, 0, -1);
  seqConfigSpecs[Sequence::probabilityConfig] = Parameter("Probability", "P", 0, 1, 0.1, 1, -1, 2);
}


void Sequencer::incrementSeqParam(std::size_t seq, std::size_t paramIndex, std::size_t headIndex)
{
  assert(paramIndex < getSeqConfigSpecs().size());
  Sequence* sequence = getSequence(seq);
  if (paramIndex == Sequence::sendConfig)
  {
    sequence->setMachineId(std::min(15.0, sequence->getMachineId() + 1.0));
    return;
  }
  if (paramIndex == Sequence::headCountConfig)
  {
    sequence->setReadHeadCount(sequence->getReadHeadCount() + 1);
    return;
  }
  if (paramIndex == Sequence::headConfig) return;
  headIndex = std::min(headIndex, sequence->getReadHeadCount() - 1);
  auto config = sequence->getReadHeadConfig(headIndex);
  if (paramIndex == Sequence::tpsConfig) config.ticksPerStep = std::min<std::size_t>(16, config.ticksPerStep + 1);
  else if (paramIndex == Sequence::modeConfig) config.mode = static_cast<SequenceReadMode>((static_cast<int>(config.mode) + 1) % 3);
  else if (paramIndex == Sequence::polyphonyConfig) config.polyphony = std::min(Sequence::maxChordPolyphony, config.polyphony + 1);
  else if (paramIndex == Sequence::rhythmConfig)
  {
    const auto& presets = Sequence::getRhythmPresets();
    const auto it = std::find(presets.begin(), presets.end(), config.rhythm);
    config.rhythm = presets[(static_cast<std::size_t>(std::distance(presets.begin(), it)) + 1) % presets.size()];
  }
  else if (paramIndex == Sequence::probabilityConfig) config.probability = std::min(1.0, config.probability + 0.1);
  sequence->setReadHeadConfig(headIndex, config);
}
void Sequencer::decrementSeqParam(std::size_t seq, std::size_t paramIndex, std::size_t headIndex)
{
  assert(paramIndex < getSeqConfigSpecs().size());
  Sequence* sequence = getSequence(seq);
  if (paramIndex == Sequence::sendConfig)
  {
    sequence->setMachineId(std::max(0.0, sequence->getMachineId() - 1.0));
    return;
  }
  if (paramIndex == Sequence::headCountConfig)
  {
    sequence->setReadHeadCount(sequence->getReadHeadCount() - 1);
    return;
  }
  if (paramIndex == Sequence::headConfig) return;
  headIndex = std::min(headIndex, sequence->getReadHeadCount() - 1);
  auto config = sequence->getReadHeadConfig(headIndex);
  if (paramIndex == Sequence::tpsConfig) config.ticksPerStep = std::max<std::size_t>(1, config.ticksPerStep - 1);
  else if (paramIndex == Sequence::modeConfig) config.mode = static_cast<SequenceReadMode>((static_cast<int>(config.mode) + 2) % 3);
  else if (paramIndex == Sequence::polyphonyConfig) config.polyphony = std::max<std::size_t>(1, config.polyphony - 1);
  else if (paramIndex == Sequence::rhythmConfig)
  {
    const auto& presets = Sequence::getRhythmPresets();
    const auto it = std::find(presets.begin(), presets.end(), config.rhythm);
    const auto index = static_cast<std::size_t>(std::distance(presets.begin(), it));
    config.rhythm = presets[(index + presets.size() - 1) % presets.size()];
  }
  else if (paramIndex == Sequence::probabilityConfig) config.probability = std::max(0.0, config.probability - 0.1);
  sequence->setReadHeadConfig(headIndex, config);
}



void Sequencer::incrementStepDataAt(std::size_t sequence, std::size_t step, std::size_t row, std::size_t col)
{
  double val = getStepDataAt(sequence, step, row, col);

  // check if they are changing the step command. 
  // if so, do not use param config stuff to edit. 
  if (col == Step::cmdInd) {
    val = sequences[sequence].getMachineType();
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
    val = sequences[sequence].getMachineType();
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
  for (auto& seq : sequences)
    seq.cancelQuarterBeatResync();
  playing = false; 
}
/** start playing */
void Sequencer::play()
{
  playing = true; 
}

bool Sequencer::isPlaying() const
{
  return playing; 
}

void Sequencer::rewindAtNextZero()
{
  for (Sequence& seq : sequences){seq.rewindAtNextZero();}
}

void Sequencer::primeForImmediateTrigger()
{
  for (Sequence& seq : sequences){seq.primeForImmediateTrigger();}
}

void Sequencer::resetForTransportStart()
{
  for (Sequence& seq : sequences){seq.resetForTransportStart();}
}

std::size_t Sequencer::getTicksElapsed(std::size_t sequence) const
{
  return sequences[sequence].getTicksElapsed();
}

std::size_t Sequencer::getTickOfFour(std::size_t sequence) const
{
  return sequences[sequence].getTickOfFour();
}


void Sequencer::requestStrUpdate()
{
  std::unique_lock<std::shared_mutex> lock(*rw_mutex);
  this->stringUpdateRequested = true;
}
