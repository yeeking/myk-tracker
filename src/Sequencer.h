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
    const static int cmdInd{0}; // maps to CommandRegistry::getCommand
    const static int chanInd{1};
    const static int noteInd{2};
    const static int velInd{3};
    const static int lengthInd{4};
    const static int probInd{5};
    /** when populating an empty step, use this */
    const static int maxInd{5};
    
    
    Step();

  // deleting the copy constructors as the unique_ptr 
  // in the Step class is not copyable. This means
  // you need to use std::move to 'copy' instances of step around. 
    Step(const Step& other) = delete;
    Step& operator=(const Step& other) = delete;
    Step(Step&& other) noexcept = default;
    Step& operator=(Step&& other) noexcept = default;


    /** returns a copy of the data stored in this step*/
    std::vector<std::vector<double>> getData();
    /** get the memory address of the data in this step for direct access*/
    // std::vector<std::vector<double>>* getDataDirect();
    /** returns a one line string representation of the step's data */
    std::string toStringFlat() ;
    /** returns a grid representation of the step's data*/
    std::vector<std::vector<std::string>> toStringGrid() ;
    
    /** sets the data stored in this step to a copy of the sent data and updates stored string representations */
    void setData(const std::vector<std::vector<double>>& data);
    /** get the value of the step data at the sent row and column, where each row is a distinct event and the col is the data vector for that event  */
    double getDataAt(int row, int col);
    /** how many rows of data for this step? */
    int howManyDataRows();
    /** how many cols of data for this step? */
    int howManyDataCols();

    /** update one value in the data vector for this step and updates stored string representations*/
    void setDataAt(unsigned int row, unsigned int col, double value);
    /** set the callback function called when this step is triggered*/
    void setCallback(std::function<void(std::vector<std::vector<double>>*)> callback);
    /** return the callback for this step*/
    std::function<void(std::vector<std::vector<double>>*)> getCallback();
    /** trigger this step, causing it to pass its data to its callback. if row > -1, only trigger that row */
    void trigger(int row = -1);
    void resetRow(int row);
    /** toggle the activity status of this step*/
    void toggleActive();
    void activate();
    void deactivate();
    /** returns the activity status of this step */
    bool isActive() const;
    /** convert double to string with sent no. decimal places*/
      static std::string dblToString(double val, int dps);
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
    const static int chanConfig{0}; 
    const static int tpsConfig{1};
    const static int probConfig{2};
     
    


    Sequence(Sequencer* sequencer, unsigned int seqLength = 16, unsigned short midiChannel = 1);

    // stop copying so mutex is ok 
    Sequence(const Sequence& other) = delete;
    Sequence& operator=(const Sequence& other) = delete;
    Sequence(Sequence&& other) noexcept = default;
    Sequence& operator=(Sequence&& other) noexcept = default;


    /** go to the next step. If trigger is false, just move along without triggering. */
    void tick(bool trigger = true);
    /** trigger a step's callback right now */
    void triggerStep(int step, int row);
    /** which step are you on? */
    unsigned int getCurrentStep() const;
    /** is this step number valid? */
    bool assertStep(unsigned int step) const;
    /** retrieve a copy of the step data for the sent step */
    std::vector<std::vector<double>> getStepData(int step);
    /** returns the value of the sent step's data at the sent index*/
    double getStepDataAt(int step, int row, int col);
    std::string stepToStringFlat(int step);
    int howManyStepDataRows(int step);
    /** returns the number of columns of data at the sent step (data is a rectangle)*/
    int howManyStepDataCols(int step);
    // /** get the memory address of the step data for the requested step*/
    // std::vector<std::vector<double>>* getStepDataDirect(int step);
    // Step* getStep(int step);
    /** set the data for the sent step */
    void setStepData(unsigned int step, std::vector<std::vector<double>> data);
    /** retrieve a copy of the step data for the current step */
    std::vector<std::vector<double>> getCurrentStepData();
    /** what is the length of the sequence? Length is a temporary property used
     * to define the playback length. There might be more steps than this
    */
    unsigned int getLength() const;
    /** set the length of the sequence 
     * if there are not enough steps currently for this length, 
     * an assert will crash the program. So you should also call
     * ensureEnoughStepsForLength
    */
    void setLength(int length);
    /** call this before calling setLength to make sure that many steps are 
     * available
    */
    void ensureEnoughStepsForLength(int length);
  
    /**
     * Set the permanent tick per step. To apply a temporary
     * change, call setTicksPerStepAdjustment
     */
    void setTicksPerStep(int ticksPerStep);
    void onZeroSetTicksPerStep(int nextTicksPerStep);
    /** set a new ticks per step until the sequence hits step 0*/
    void setTicksPerStepAdjustment(int ticksPerStep);
    /** return my permanent ticks per step (not the adjusted one)*/
    int getTicksPerStep() const;
    /** apply a transpose to the sequence, which is reset when the sequence
     * hits step 0 again
     */
    void setTranspose(double transpose);
    /** apply a length adjustment to the sequence. This immediately changes the length.
     * It is reset when the sequence
     * hits step 0 again
     */
    void setLengthAdjustment(signed int lengthAdjust);

    /** how many steps does this sequence have it total. This is independent of the length. Length can be lower than how many steps*/
    unsigned int howManySteps() const ;
    
    /** update a single data value in a given step*/
    void setStepDataAt(unsigned int step, unsigned int row, unsigned int col, double value);
    /** set the callback for the sent step */
    void setStepCallback(unsigned int step, 
                  std::function<void (std::vector<std::vector<double>>*)> callback);
    std::string stepToString(int step);
    /** activate/ deactivate the sent step */
    void toggleActive(unsigned int step);
    /** check if the sent step is active */
    bool isStepActive(unsigned int step) const;
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
    void resetStepRow(int step, int row);

    std::vector<std::vector<std::string>> stepAsGridOfStrings(int step);
    /** returns the mute state of this sequence*/
    bool isMuted();
    /** change mote state to its opposite */
    void toggleMuteState();
  private:

    /** function called when the sequence ticks and it is SequenceType::midiNote
     * 
    */
    void triggerMidiNoteType(); 
    /** function called when the sequence ticks and it is SequenceType::midiDrum*/
    void triggerMidiDrumType();
    /** function called when the sequence ticks and it is SequenceType::midiChord*/
    void triggerMidiChordType();
     
    /** 
     * Called when the sequence ticks and it is a transpose type SequenceType::transposer
    */
    void triggerTransposeType();
    /**
     * Called when the sequence ticks and it is SequenceType::lengthChanger
     */
    void triggerLengthType();
    /**
     * Called when the sequence ticks and it is SequenceType::tickChanger
     */
    void triggerTickType();
    /** provides access to the sequencer so this sequence can change things*/
    Sequencer* sequencer;
    /** current length. This is a signed int as we apply length adjustments to it that might be negative*/
    unsigned int currentLength;
    /** Current seq length. This is signed in case we are computing current step using currentLength*/
    unsigned int currentStep;
    unsigned short midiChannel;
    std::vector<Step> steps;
    SequenceType type;
    // temporary sequencer adjustment parameters that get reset at step 0
    double transpose; 
    signed int lengthAdjustment;
    int ticksPerStep;
    /** stores the current default for this sequence, whereas ticksperstep 
     * is the temporarily adjusted one 
     */
    int originalTicksPerStep;
    /** used to store a ticks per step update that will be applied next time tickoffour == 0*/
    int nextTicksPerStep; 
    int ticksElapsed;
    /** used to keep in sync with the '1'*/
    int tickOfFour;
    bool muted; 
    /** maps from linear midi scale to general midi drum notes*/
    std::map<int,int> midiScaleToDrum;

    std::unique_ptr<std::shared_mutex> rw_mutex;

};

/** represents a sequencer which is used to store a grid of data and to step through it */
class Sequencer  {
    public:
    /** create a sequencer: channels,stepsPerChannel*/
      Sequencer(unsigned int seqCount = 4, unsigned int seqLength = 16);
      ~Sequencer();

      /** set seq channels and seq types of this sequence to the same as the sent sequence*/
      void copyChannelAndTypeSettings(Sequencer* otherSeq);
      unsigned int howManySequences() const ;
      unsigned int howManySteps(unsigned int sequence) const ;
      unsigned int getCurrentStep(unsigned int sequence) const;
      SequenceType getSequenceType(unsigned int sequence) const;
      unsigned int getSequenceTicksPerStep(unsigned int sequence) const;

      /** go to the next step. If trigger is false, just move along without triggering. */
      void tick(bool trigger = true);
      /** trigger a step's callback right now */
      void triggerStep(int seq, int step, int row);
      /** return a pointer to the sequence with sent id*/
      Sequence* getSequence(unsigned int sequence);
      /** the the type of sequence to type*/
      void setSequenceType(unsigned int sequence, SequenceType type);
       /** set the length of the sequence 
       * If it is higher than the current max length, new steps will be created
       * using callbacks that are copies of the one at the last, previously existant
       * step
       */
      void setSequenceLength(unsigned int sequence, unsigned int length);
      /** reduce the playback (as opposed to total possible) length of the sequence by 1 */
      void shrinkSequence(unsigned int sequence);
      /** increase the length of the sequence by 1, adding new steps in memory if needed, as per setSequenceLength*/
      void extendSequence(unsigned int sequence);
      /** set all callbacks on all sequences to the sent lambda*/
      void setAllCallbacks(std::function<void (std::vector<std::vector<double>>*)> callback);
      /** set a callback lambda for all steps in a sequence*/
      void setSequenceCallback(unsigned int sequence, std::function<void (std::vector<std::vector<double>>*)> callback);
      /** set a lambda to call when a particular step in a particular sequence happens */
      void setStepCallback(unsigned int sequence, unsigned int step, std::function<void (std::vector<std::vector<double>>*)> callback);
      /** update the data stored at a step in the sequencer */
      void setStepData(unsigned int sequence, unsigned int step, std::vector<std::vector<double>> data);
      /** return the sent seq, sent step, sent row, sent col's value */
      double getStepDataAt(int seq, int step, int row, int col);
      /** update a single value in the  data stored at a step in the sequencer */
      void setStepDataAt(unsigned int sequence, unsigned int step, unsigned int row, unsigned int col, double value);
      /** set all values for this seq, step, row to zero */
      void resetStepRow(int sequence, int step, int row);
      /** toggle mute state of the sent sequence */
      void toggleSequenceMute(int sequence);
      /** retrieve a copy the data for the current step */
      std::vector<std::vector<double>> getCurrentStepData(int sequence);
      /** returns a pointer to the step object stored at the sent sequence and step position */
      // Step* getStep(int seq, int step);
      /** retrieve a copy of the data for a specific step */
      std::vector<std::vector<double>> getStepData(int sequence, int step);
      /** set the sent seq, sent step, sent row, sent col's value */
      // void setStepDataAt(int seq, int step, int row, int col, double val);
      
      /** returns the number of rows of data at the sent step*/
      int howManyStepDataRows(int seq, int step);
      /** returns the number of columns of data at the sent step (data is a rectangle)*/
      int howManyStepDataCols(int seq, int step);
      /** get the memory address of the data for this step for direct viewing/ editing*/
      //std::vector<std::vector<double>>* getStepDataDirect(int sequence, int step);
      void toggleActive(int sequence, int step);
      bool isStepActive(int sequence, int step) const;
      void addStepListener();
      /** wipe the data from the sent sequence*/
      void resetSequence(int sequence);
  /** print out a tracker style view of the sequence */
      std::string toString();
  /** get a vector of vector of strings representing the sequence. this is cached - call
   * updateSeqStringGrid after making edits to the sequence as it will not automatically update
   */
     std::vector<std::vector<std::string>>& getSequenceAsGridOfStrings();
     /** get a grid of strings representing configs for all sequences. This is generated on the fly*/
     std::vector<std::vector<std::string>> getSequenceConfigsAsGridOfStrings();
     
     /** vector of vector of string representation of a step. This is generated on the fly*/
     std::vector<std::vector<std::string>> getStepAsGridOfStrings(int seq, int step);
     
      /** regenerate the string grid representation of the sequence */
      void updateSeqStringGrid();
      
      /** returns a vector of parameter 'spec' objects for the sequencer parameters. */
      std::vector<Parameter>& getSeqConfigSpecs();
      /** increment the parameter at the sent index. uses the param spec to dictate the step and range*/
      void incrementSeqParam(int seq, int paramIndex);
      /** decrement the parameter at the sent index. uses param spec to dictate the step and range*/
      void decrementSeqParam(int seq, int paramIndex);
      /** increment step data for seq, step,row,col. Checks param configs for command type 
       * to decide limits and step size 
      */
      void incrementStepDataAt(unsigned int sequence, unsigned int step, unsigned int row, unsigned int col);
      /** decrement step data for seq, step,row,col. Checks param configs for command type 
       * to decide limits and step size 
      */
      void decrementStepDataAt(unsigned int sequence, unsigned int step, unsigned int row, unsigned int col);
      /** reads default value for this step data col from commands and sets it to that */
      void setStepDataAtDefault(unsigned int sequence, unsigned int step, unsigned int row, unsigned int col);
       

    private:

      void setupSeqConfigSpecs();
     
      bool assertSeqAndStep(unsigned int sequence, unsigned int step) const;
        
      bool assertSequence(unsigned int sequence) const;
      /// class data members  
      std::vector<Sequence> sequences;
    /** representation of the sequences as a string grid, pulled from the steps' flat string representations */
      std::vector<std::vector<std::string>> seqAsStringGrid;
      std::vector<Parameter> seqConfigSpecs; 


};





