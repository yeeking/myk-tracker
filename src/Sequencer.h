/**
 * Sequencer stores a set of sequences each of which stores a step
 * By Matthew Yee-King 2020
 * Probably should separate into header and cpp at some point
 */

#pragma once 

#include <vector>
#include <iostream>
#include <string>
#include <functional>
#include <map>
#include <cmath> // fmod
#include <assert.h>  
#include <mutex> 
#include <shared_mutex>
#include <memory>
#include <unordered_map>

#include "SequencerCommands.h"
//#include "ChordUtils.h"
// #include "SequencerUtils.h"
// #include "MidiUtils.h"


/** default spec for a Step's data, 
 * so data[0] specifies length, 
 * data[1] specifies velocity 
 * and data[2] is the first note
 * 
*/
class Step{
  
  public:
  // these should be in an enum probably
    const static std::size_t cmdInd{0}; // maps to CommandRegistry::getCommand
    const static std::size_t chanInd{1};
    const static std::size_t noteInd{2};
    const static std::size_t velInd{3};
    const static std::size_t lengthInd{4};
    const static std::size_t probInd{5};
    /** when populating an empty step, use this */
    const static std::size_t maxInd{5};
    
    
    Step();

  // deleting the copy constructors as the unique_ptr 
  // in the Step class is not copyable. This means
  // you need to use std::move to 'copy' instances of step around. 
    Step(const Step& other) = delete;
    Step& operator=(const Step& other) = delete;
    Step(Step&& other) noexcept = default;
    Step& operator=(Step&& other) noexcept = default;


    /** returns a copy of the data stored in this step*/
    std::vector<std::vector<double>> getData() const;
    /** get the memory address of the data in this step for direct access*/
    // std::vector<std::vector<double>>* getDataDirect();
    /** returns a one line string representation of the step's data */
    std::string toStringFlat() const ;
    /** returns a grid representation of the step's data*/
    std::vector<std::vector<std::string>> toStringGrid() const;
    
    /** sets the data stored in this step to a copy of the sent data and updates stored string representations */
    void setData(const std::vector<std::vector<double>>& data);
    /** get the value of the step data at the sent row and column, where each row is a distinct event and the col is the data vector for that event  */
    double getDataAt(std::size_t row, std::size_t col) const;
    /** how many rows of data for this step? */
    std::size_t howManyDataRows() const;
    /** how many cols of data for this step? */
    std::size_t howManyDataCols() const;

    /** update one value in the data vector for this step and updates stored string representations*/
    void setDataAt( std::size_t row, std::size_t col, double value);
    /** set the callback function called when this step is triggered*/
    void setCallback(std::function<void(std::vector<std::vector<double>>*)> callback);
    /** return the callback for this step*/
    std::function<void(std::vector<std::vector<double>>*)> getCallback();
    /** trigger this step, causing it to pass its data to its callback. if row > -1, only trigger that row */
    void trigger(std::size_t row);
    void resetRow(std::size_t row);
    /** toggle the activity status of this step*/
    void toggleActive();
    void activate();
    void deactivate();
    /** returns the activity status of this step */
    bool isActive() const;
    /** convert double to string with sent no. decimal places*/
      static std::string dblToString(double val, std::size_t dps);
  private: 

  // clever mutex that allows multiple concurrent reads but a block-all write 
  // it has to be a shared pointer as the mutex constrains how this object can be used
  // which is also why I have set the constructors up how I have above.
    std::unique_ptr<std::shared_mutex> rw_mutex;
    /** the data for the step: rows and columns*/
    std::vector<std::vector<double>> data;
    bool active;
    std::function<void(std::vector<std::vector<double>>*)> stepCallback;

};

/** need this so can have a Sequencer data member in Sequence*/
class Sequencer;

/** use to define the type of a sequence. 
 * midiNote sends midi notes out
 * samplePlayer triggers internal samples
 * transposer transposes another sequence 
 **/
enum class SequenceType {midiNote, drumMidi, chordMidi, samplePlayer, transposer, lengthChanger, tickChanger};

class Sequence{
  public:
    /** param index for */
    const static std::size_t chanConfig{0}; 
    const static std::size_t tpsConfig{1};
    const static std::size_t probConfig{2};
     
    


    Sequence(Sequencer* sequencer, std::size_t seqLength = 16, unsigned short midiChannel = 1);

    // stop copying so mutex is ok 
    Sequence(const Sequence& other) = delete;
    Sequence& operator=(const Sequence& other) = delete;
    Sequence(Sequence&& other) noexcept = default;
    Sequence& operator=(Sequence&& other) noexcept = default;


    /** go to the next step. If trigger is false, just move along without triggering. */
    void tick(bool trigger = true);
    /** trigger a step's callback right now */
    void triggerStep(std::size_t step, std::size_t row);
    /** which step are you on? */
    std::size_t getCurrentStep() const;
    /** is this step number valid? */
    bool assertStep(std::size_t step) const;
    /** retrieve a copy of the step data for the sent step */
    std::vector<std::vector<double>> getStepData(std::size_t step);
    /** returns the value of the sent step's data at the sent index*/
    double getStepDataAt(std::size_t step, std::size_t row, std::size_t col);
    std::string stepToStringFlat(std::size_t step);
    std::size_t howManyStepDataRows(std::size_t step);
    /** returns the number of columns of data at the sent step (data is a rectangle)*/
    std::size_t howManyStepDataCols(std::size_t step);
    // /** get the memory address of the step data for the requested step*/
    // std::vector<std::vector<double>>* getStepDataDirect(std::size_t step);
    // Step* getStep(std::size_t step);
    /** set the data for the sent step */
    void setStepData(std::size_t step, std::vector<std::vector<double>> data);
    /** retrieve a copy of the step data for the current step */
    std::vector<std::vector<double>> getCurrentStepData();
    /** what is the length of the sequence? Length is a temporary property used
     * to define the playback length. There might be more steps than this
    */
    std::size_t getLength() const;
    /** set the length of the sequence 
     * if there are not enough steps currently for this length, 
     * an assert will crash the program. So you should also call
     * ensureEnoughStepsForLength
    */
    void setLength(std::size_t length);
    /** call this before calling setLength to make sure that many steps are 
     * available
    */
    void ensureEnoughStepsForLength(std::size_t length);
  
    /**
     * Set the permanent tick per step. To apply a temporary
     * change, call setTicksPerStepAdjustment
     */
    void setTicksPerStep(std::size_t ticksPerStep);
    void onZeroSetTicksPerStep(std::size_t nextTicksPerStep);
    /** set a new ticks per step until the sequence hits step 0*/
    void setTicksPerStepAdjustment(std::size_t ticksPerStep);
    /** return my permanent ticks per step (not the adjusted one)*/
    std::size_t getTicksPerStep() const;
    /** returns the upcoming ticks per step, in case you want the value sent to onZeroSetTicksPerStep */
    std::size_t getNextTicksPerStep() const;
    /** apply a transpose to the sequence, which is reset when the sequence
     * hits step 0 again
     */
    void setTranspose(double transpose);
    /** apply a length adjustment to the sequence. This immediately changes the length.
     * It is reset when the sequence
     * hits step 0 again
     */
    void setLengthAdjustment(std::size_t lengthAdjust);

    /** how many steps does this sequence have it total. This is independent of the length. Length can be lower than how many steps*/
    std::size_t howManySteps() const ;
    
    /** update a single data value in a given step*/
    void setStepDataAt(std::size_t step, std::size_t row, std::size_t col, double value);
    /** set the callback for the sent step */
    void setStepCallback(std::size_t step, 
                  std::function<void (std::vector<std::vector<double>>*)> callback);
    std::string stepToString(std::size_t step);
    /** activate/ deactivate the sent step */
    void toggleActive(std::size_t step);
    /** check if the sent step is active */
    bool isStepActive(std::size_t step) const;
    /** set the sequence type */
    void setType(SequenceType type);
    SequenceType getType() const;
  /** add a transpose processor to this sequence. 
     * Normally, a transposer type sequence will call this on a midiNote type sequence
     * to apply a transpose to it 
    */
//    void setStepProcessorTranspose(StepDataTranspose transpose);
    /** deactivate all data processors, e.g. transposer, length adjusters */
    void deactivateProcessors();
    /** clear the data from this sequence. Does not clear step event functions*/
    void reset();
    /** set this step, row values to zero */
    void resetStepRow(std::size_t step, std::size_t row);

    std::vector<std::vector<std::string>> stepAsGridOfStrings(std::size_t step);
    /** returns the mute state of this sequence*/
    bool isMuted() const;
    /** change mote state to its opposite */
    void toggleMuteState();
    /** tell the sequence to reset its position counter at next tick. Useful for rewinding*/
    void rewindToStart();
  private:

    /** provides access to the sequencer so this sequence can change things*/
    Sequencer* sequencer;
    /** current length. This is a std::size_tas we apply length adjustments to it that might be negative*/
    std::size_t currentLength;
    /** Current seq length. This is signed in case we are computing current step using currentLength*/
    std::size_t currentStep;
    unsigned short midiChannel;
    std::vector<Step> steps;
    SequenceType type;
    // temporary sequencer adjustment parameters that get reset at step 0
    double transpose; 
    signed int lengthAdjustment;
    std::size_t ticksPerStep;
    /** stores the current default for this sequence, whereas ticksperstep 
     * is the temporarily adjusted one 
     */
    std::size_t originalTicksPerStep;
    /** used to store a ticks per step update that will be applied next time tickoffour == 0*/
    std::size_t nextTicksPerStep; 
    bool resetAtNextTick; 
    
    std::size_t ticksElapsed;
    /** used to keep in sync with the '1'*/
    std::size_t tickOfFour;
    bool muted; 
    /** maps from linear midi scale to general midi drum notes*/
    std::map<int,int> midiScaleToDrum;

    std::unique_ptr<std::shared_mutex> rw_mutex;

};

/** represents a sequencer which is used to store a grid of data and to step through it */
class Sequencer  {
    public:
    /** create a sequencer: channels,stepsPerChannel*/
      Sequencer(std::size_t seqCount = 4, std::size_t seqLength = 16);
      ~Sequencer();

      /** set seq channels and seq types of this sequence to the same as the sent sequence*/
      void copyChannelAndTypeSettings(Sequencer* otherSeq);
      std::size_t howManySequences() const ;
      std::size_t howManySteps(std::size_t sequence) const ;
      std::size_t getCurrentStep(std::size_t sequence) const;
      SequenceType getSequenceType(std::size_t sequence) const;
      std::size_t getSequenceTicksPerStep(std::size_t sequence) const;
      std::size_t getSequencerNextTicksPerStep(std::size_t sequence) const;

      /** go to the next step. if disableAllTriggers has been called, will send false trigger to 
       * sequence objects, meaning they step without firing. 
      */
      void tick();
      /** trigger a step's callback right now */
      void triggerStep(std::size_t seq, std::size_t step, std::size_t row);
      /** return a pointer to the sequence with sent id*/
      Sequence* getSequence(std::size_t sequence);
      /** the the type of sequence to type*/
      void setSequenceType(std::size_t sequence, SequenceType type);
       /** set the length of the sequence 
       * If it is higher than the current max length, new steps will be created
       * using callbacks that are copies of the one at the last, previously existant
       * step
       */
      void setSequenceLength(std::size_t sequence, std::size_t length);
      /** reduce the playback (as opposed to total possible) length of the sequence by 1 */
      void shrinkSequence(std::size_t sequence);
      /** increase the length of the sequence by 1, adding new steps in memory if needed, as per setSequenceLength*/
      void extendSequence(std::size_t sequence);
      /** set all callbacks on all sequences to the sent lambda*/
      void setAllCallbacks(std::function<void (std::vector<std::vector<double>>*)> callback);
      /** set a callback lambda for all steps in a sequence*/
      void setSequenceCallback(std::size_t sequence, std::function<void (std::vector<std::vector<double>>*)> callback);
      /** set a lambda to call when a particular step in a particular sequence happens */
      void setStepCallback(std::size_t sequence, std::size_t step, std::function<void (std::vector<std::vector<double>>*)> callback);
      /** update the data stored at a step in the sequencer */
      void setStepData(std::size_t sequence, std::size_t step, std::vector<std::vector<double>> data);
      /** return the sent seq, sent step, sent row, sent col's value */
      double getStepDataAt(std::size_t seq, std::size_t step, std::size_t row, std::size_t col);
      /** update a single value in the  data stored at a step in the sequencer */
      void setStepDataAt(std::size_t sequence, std::size_t step, std::size_t row, std::size_t col, double value);
      /** set all values for this seq, step, row to zero */
      void resetStepRow(std::size_t sequence, std::size_t step, std::size_t row);
      /** retrieve a copy the data for the current step */
      std::vector<std::vector<double>> getCurrentStepData(std::size_t sequence);
      /** returns a pointer to the step object stored at the sent sequence and step position */
      // Step* getStep(std::size_t seq, std::size_t step);
      /** retrieve a copy of the data for a specific step */
      std::vector<std::vector<double>> getStepData(std::size_t sequence, std::size_t step);
      /** set the sent seq, sent step, sent row, sent col's value */
      // void setStepDataAt(std::size_t seq, std::size_t step, std::size_t row, std::size_t col, double val);
      
      /** returns the number of rows of data at the sent step*/
      std::size_t howManyStepDataRows(std::size_t seq, std::size_t step);
      /** returns the number of columns of data at the sent step (data is a rectangle)*/
      std::size_t howManyStepDataCols(std::size_t seq, std::size_t step);

      /** stop playing*/
      void stop();
      /** start playing */
      void play();
      bool isPlaying();
      /** on next 0 / 4 ticks, rewind all sequences to step 0. Use in combination with enableAllTriggers to play from the top. */
      void rewindToStart();
      
      /** allows a 'keep stepping but do not trigger' when tick is called*/
      void disableAllTriggers();
      /** allows normal stepping behaviour of sending true to tick functions on sequences */
      void enableAllTriggers();
      
      /** toggle mute state of the sent sequence */
      void toggleSequenceMute(std::size_t sequence);
      /** toggle activity state of specified step in specified sequence*/
      void toggleStepActive(std::size_t sequence, std::size_t step);
      bool isStepActive(std::size_t sequence, std::size_t step) const;
      void addStepListener();
      /** wipe the data from the sent sequence*/
      void resetSequence(std::size_t sequence);

      /** get a vector of vector of strings representing the sequence. this is cached - call
       * updateSeqStringGrid after making edits to the sequence as it will not automatically update
       */
      std::vector<std::vector<std::string>>& getSequenceAsGridOfStrings();
      /** get a grid of strings representing configs for all sequences. This is generated on the fly*/
      std::vector<std::vector<std::string>> getSequenceConfigsAsGridOfStrings();

      /** vector of vector of string representation of a step. This is generated on the fly*/
      std::vector<std::vector<std::string>> getStepAsGridOfStrings(std::size_t seq, std::size_t step);

      /** regenerate the string grid representation of the sequence */
      void updateSeqStringGrid();
      
      /** returns a vector of parameter 'spec' objects for the sequencer parameters. */
      std::vector<Parameter>& getSeqConfigSpecs() ;
      /** increment the parameter at the sent index. uses the param spec to dictate the step and range*/
      void incrementSeqParam(std::size_t seq, std::size_t paramIndex);
      /** decrement the parameter at the sent index. uses param spec to dictate the step and range*/
      void decrementSeqParam(std::size_t seq, std::size_t paramIndex);
      /** increment step data for seq, step,row,col. Checks param configs for command type 
       * to decide limits and step size 
      */
      void incrementStepDataAt(std::size_t sequence, std::size_t step, std::size_t row, std::size_t col);
      /** decrement step data for seq, step,row,col. Checks param configs for command type 
       * to decide limits and step size 
      */
      void decrementStepDataAt(std::size_t sequence, std::size_t step, std::size_t row, std::size_t col);
      /** reads default value for this step data col from commands and sets it to that */
      void setStepDataToDefault(std::size_t sequence, std::size_t step, std::size_t row, std::size_t col);
       

    private:

      void setupSeqConfigSpecs();
     
      bool assertSeqAndStep(std::size_t sequence, std::size_t step) const;
        
      bool assertSequence(std::size_t sequence) const;
      /// class data members 
      /** makes reads and writes thread safe */
      std::unique_ptr<std::shared_mutex> rw_mutex;
      /** if false, ignore ticks. If true, do not ignore ticks */
      bool playing; 
      /** this value is sent to the tick call on our sequences. Allows 'step without triggering' behaviour  */
      bool triggerOnTick;

      std::vector<Sequence> sequences;
    /** representation of the sequences as a string grid, pulled from the steps' flat string representations */
      std::vector<std::vector<std::string>> seqAsStringGrid;
      std::vector<Parameter> seqConfigSpecs; 


};





