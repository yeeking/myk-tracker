/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin processor.

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include <array>
#include <atomic>
#include <memory>
#include <mutex>
#include <utility>
#include <deque>
#include <optional>
#include <thread>
#include <type_traits>
#include <vector>

#include "MachineUtilsAbs.h"
#include "ClockAbs.h"
#include "Sequencer.h"
#include "SequencerEditor.h"
#include "TrackerController.h"
#include "SuperSamplerProcessor.h"
#include "machines/WavetableSynthMachine.h"
#include "machines/WaveshaperDistortionMachine.h"
#include "machines/DelayFxMachine.h"
#include "machines/ChannelStripMachine.h"
#include "machines/AuxReverbMachine.h"
#include "machines/FilterFxMachine.h"
#include "machines/ModulationMatrixMachine.h"
#include "PerformanceMetrics.h"

class TrackerControlService;

//==============================================================================
/**
*/
// Main plugin processor that owns the sequencer, samplers, and timing.
class TrackerMainProcessor  :    public MachineUtilsAbs, 
                            public ClockAbs, 
                            public juce::AudioProcessor, 
                            public juce::ChangeBroadcaster,
                            public MachineHost,
                            public SongHost,
                            private juce::OSCReceiver::Listener<juce::OSCReceiver::MessageLoopCallback>

                            #if JucePlugin_Enable_ARA
                             , public juce::AudioProcessorARAExtension
                            #endif
{
public:
    //==============================================================================
    TrackerMainProcessor();
    ~TrackerMainProcessor() override;

    // the MachineUtils interface 
    void allNotesOff() override;
    void sendMessageToMachine(CommandType machineType, unsigned short machineId, unsigned short note, unsigned short velocity, unsigned short durInTicks) override; 
    void beginHeldNote(CommandType machineType, unsigned short machineId, unsigned short note, unsigned short velocity) override;
    void endHeldNote(CommandType machineType, unsigned short machineId, unsigned short note) override;
    std::string describeStepNote(CommandType machineType, unsigned short machineId, unsigned short note) const override;
    void sendQueuedMessages(long tick) override; 
    void toggleAuxSendForStack(unsigned short machineId, bool isAux1) override;
    void setFilterCutoffHzForStack(unsigned short machineId, double cutoffHz) override;
    // the ClockAbs interface
    void setBPM(double bpm) override; 
    double getBPM() override; 
    void setInternalClockEnabled(bool enabled);
    bool isInternalClockEnabled() const;
    bool isHostClockActive() const;
    

    //==============================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    PerformanceMetrics& getPerformanceMetrics() noexcept { return performanceMetrics; }
    const PerformanceMetrics& getPerformanceMetrics() const noexcept { return performanceMetrics; }

   #ifndef JucePlugin_PreferredChannelConfigurations
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
   #endif

    using juce::AudioProcessor::processBlock;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

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
    /** wipes midiToSend  */
    void clearPendingEvents();

    Sequencer* getSequencer();
    SequencerEditor* getSequenceEditor();
    TrackerController* getTrackerController();
    /** Returns the shared control boundary used by the GUI and local MCP server. */
    TrackerControlService& getControlService();
    /** Copies one step's event rows without constructing the full saved document. */
    bool getStepValuesForSequenceSet(std::size_t setIndex, std::size_t sequenceIndex,
                                     std::size_t stepIndex, std::vector<std::vector<double>>& values) const;
    /** Returns a track's step count, or zero if the set/track is invalid. */
    std::size_t getStepCountForSequenceSet(std::size_t setIndex, std::size_t sequenceIndex) const;
    std::size_t getMachineCount(CommandType type) const override;
    MachineInterface* getMachine(CommandType type, std::size_t index) override;
    const MachineInterface* getMachine(CommandType type, std::size_t index) const override;
    std::size_t getMachineStackCount() const override;
    std::vector<CommandType> getMachineStackTypes(std::size_t stackIndex) const override;
    void addMachineToStack(std::size_t stackIndex) override;
    void removeMachineFromStack(std::size_t stackIndex, std::size_t slotIndex) override;
    void cycleMachineTypeInStack(std::size_t stackIndex, std::size_t slotIndex, int direction) override;
    void moveMachineInStack(std::size_t stackIndex, std::size_t slotIndex, int direction) override;
    bool isMachineEnabledInStack(std::size_t stackIndex, std::size_t slotIndex) const override;
    void toggleMachineEnabledInStack(std::size_t stackIndex, std::size_t slotIndex) override;
    bool isModulationEnabledInStack(std::size_t stackIndex) const override;
    void toggleModulationEnabledInStack(std::size_t stackIndex) override;
    float getMachineSendLevelDbInStack(std::size_t stackIndex, std::size_t slotIndex) const override;
    void adjustMachineSendLevelDbInStack(std::size_t stackIndex, std::size_t slotIndex, int direction) override;
    bool machineHasReturnLevelInStack(std::size_t stackIndex, std::size_t slotIndex) const override;
    float getMachineReturnLevelDbInStack(std::size_t stackIndex, std::size_t slotIndex) const override;
    void adjustMachineReturnLevelDbInStack(std::size_t stackIndex, std::size_t slotIndex, int direction) override;
    float getStackMeterLevel(std::size_t stackIndex) const override;
    float getStackGainDb(std::size_t stackIndex) const override;
    void setStackGainDb(std::size_t stackIndex, float gainDb) override;
    float getStackPan(std::size_t stackIndex) const override;
    void setStackPan(std::size_t stackIndex, float pan) override;
    float getMasterMeterLevel(std::size_t channel) const override;
    float getMasterGainDb() const override;
    void setMasterGainDb(float gainDb) override;
    bool isMasterMuted() const override;
    void setMasterMuted(bool muted) override;
    bool isMasterRecording() const override;
    bool toggleMasterRecording() override;
    float getAuxReturnMeterLevel(std::size_t auxIndex, std::size_t channel) const override;
    float getAuxReturnGainDb(std::size_t auxIndex) const override;
    void setAuxReturnGainDb(std::size_t auxIndex, float gainDb) override;
    float getAuxReturnPan(std::size_t auxIndex) const override;
    void setAuxReturnPan(std::size_t auxIndex, float pan) override;
    bool isAuxReturnMuted(std::size_t auxIndex) const override;
    void setAuxReturnMuted(std::size_t auxIndex, bool muted) override;
    bool isStackMuted(std::size_t stackIndex) const override;
    bool isStackSolo(std::size_t stackIndex) const override;
    void setStackMuted(std::size_t stackIndex, bool muted) override;
    void setStackSolo(std::size_t stackIndex, bool solo) override;
    void silenceStack(std::size_t stackIndex) override;
    int getStackMidiOutputChannel(std::size_t stackIndex) const override;
    void adjustStackMidiOutputChannel(std::size_t stackIndex, int direction) override;
    std::size_t getSequenceSetCount() const override;
    std::size_t getViewedSequenceSetIndex() const override;
    void setViewedSequenceSetIndex(std::size_t index) override;
    std::size_t getSongRowCount() const override;
    std::size_t getSelectedSongRow() const override;
    std::size_t getCurrentPlaybackSongRow() const override;
    void setSelectedSongRow(std::size_t row) override;
    std::size_t getSongRowSequenceSetId(std::size_t row) const override;
    int getSongRowBeatCount(std::size_t row) const override;
    int getSongRowBpm(std::size_t row) const override;
    SongPlayMode getSongPlayMode() const override;
    void setSongPlayMode(SongPlayMode mode) override;
    std::size_t addSongRowByCloningViewedSet() override;
    void removeSongRow(std::size_t row) override;
    void adjustSongRowSequenceSetId(std::size_t row, int direction) override;
    void adjustSongRowBeatCount(std::size_t row, int direction) override;
    void adjustSongRowBpm(std::size_t row, int direction) override;
    /** Set the stored tempo for a sequence set, applying it immediately when active. */
    void setSequenceSetBpm(std::size_t setIndex, double bpm);
    void toggleSongPlayback() override;
    void rewindSongTransport() override;
    void sendCurrentCellValueOverOscIfChanged(bool refreshMachineCells = true);
    void recreateSequencersAndMachines();
    struct PendingZoomCommand
    {
        float delta = 0.0f;
        float normalizedX = 0.5f;
        float normalizedY = 0.5f;
    };
    std::vector<PendingZoomCommand> consumePendingZoomCommands();
    struct UiScreenshotRequest
    {
        std::shared_ptr<juce::WaitableEvent> completed;
        std::shared_ptr<juce::Image> image;
        std::shared_ptr<juce::String> error;
    };
    /** Must be called on the JUCE message thread. */
    juce::String requestUiScreenshot(std::shared_ptr<UiScreenshotRequest> request);

    /** Copies the most recent mono scope samples of the given machine stack into
        out, oldest to newest. Blocks until the audio thread is idle. */
    void copyStackScope(std::size_t stackIndex, std::vector<float>& out);
    /** Copies the most recent mono scope samples for several distinct machine
        stacks under one audio-exclusive section. `out` is sized before the
        section is entered so the audio thread is never delayed by UI allocation. */
    void copyStackScopes(const std::vector<std::size_t>& stackIndices,
                         std::vector<std::vector<float>>& out);

    template <typename Fn>
    auto withAudioThreadExclusive(Fn&& fn) -> decltype(fn())
    {
        using ReturnType = decltype(fn());
        for (;;)
        {
            while (processing.load(std::memory_order_acquire))
                std::this_thread::yield();
            std::unique_lock<std::mutex> lock(audioMutex);
            if (!processing.load(std::memory_order_acquire))
            {
                if constexpr (std::is_void_v<ReturnType>)
                {
                    fn();
                    return;
                }
                else
                {
                    return fn();
                }
            }
        }
    }
    
private:
    static constexpr std::size_t kScopeRingSampleCount = 1024;
    friend class TrackerControlService;
    struct SongRow
    {
        std::size_t sequenceSetId = 0;
        int beatCount = 16;
    };
    /** Machine instances and their parameters are shared by all sequence sets.
        This stores only the slot bypass state each set recalls.  Slot IDs keep
        the state stable when slots are reordered. */
    struct SequenceSetMachineEnableState
    {
        struct Slot
        {
            std::string id;
            CommandType type = CommandType::MidiNote;
            bool enabled = true;
        };

        std::vector<std::vector<Slot>> stacks;
        std::vector<bool> modulationEnabled;
    };
    std::vector<std::unique_ptr<Sequencer>> sequenceSets;
    /** Persistent tempo per sequence set; Song rows reference these values by set ID. */
    std::vector<double> sequenceSetBpms;
    std::vector<SequenceSetMachineEnableState> sequenceSetMachineEnableStates;
    std::vector<SongRow> songRows;
    SongPlayMode songPlayMode = SongPlayMode::sequence;
    std::size_t viewedSequenceSetIndex = 0;
    std::size_t activePlaybackSequenceSetIndex = 0;
    std::optional<std::size_t> pendingPlaybackSequenceSetIndex;
    std::size_t selectedSongRow = 0;
    std::size_t currentSongRow = 0;
    int currentSongRowBeatCounter = 0;
    bool pendingTransportQuarterBeatReset = false;
    bool pendingTransportStartOnQuarterBeat = false;
    /** keep the seq editor in the processor as the plugineditor
     * can be deleted but the processor persists and we want to retain state on the seqeditor
    */
    SequencerEditor seqEditor; 
    /** as for the seqeditor, this is here for easy statefulness*/
    TrackerController trackerController;
    std::unique_ptr<TrackerControlService> controlService;
    /** temporary place where we store midi as we generate it. it is later filtered to 'send in this block' and 'send in the future' events */
    juce::MidiBuffer midiToSend; 
    /** persistent scratch buffer for 'send in the future' events, reused every block so the
     * audio path does not allocate; audio-thread only, always empty between processBlock calls */
    juce::MidiBuffer futureMidi;
    struct ScheduledSamplerEvent
    {
        std::size_t stackIndex = 0;
        juce::MidiMessage message;
        int samplePosition = 0;
    };
    /** A sequencer note scheduled for an internal-synth (wavetable/filter) stack. */
    struct ScheduledNoteEvent
    {
        std::size_t stackIndex = 0;
        unsigned short note = 0;
        unsigned short velocity = 0;
        unsigned short durationTicks = 0;
        bool isNoteOff = false;
        int samplePosition = 0;
    };
    /** Pending + scratch buffers for one note-event pipeline (message-thread enqueue, audio-thread drain). */
    struct NoteEventQueue
    {
        std::vector<ScheduledNoteEvent> pending;
        std::vector<ScheduledNoteEvent> scratch;
        void clear()
        {
            pending.clear();
            scratch.clear();
        }
    };
    struct MachineStack
    {
        struct GainCache
        {
            float db = 0.0f;
            float linear = 1.0f;
            float get(float nextDb)
            {
                if (std::abs(nextDb - db) > std::numeric_limits<float>::epsilon())
                {
                    db = nextDb;
                    linear = std::pow(10.0f, db / 20.0f);
                }
                return linear;
            }
        };
        struct SlotState
        {
            /** Persisted identity; grid coordinates are deliberately not an API. */
            std::string id;
            CommandType type = CommandType::MidiNote;
            bool enabled = true;
            float sendLevelDb = 0.0f;
            float returnLevelDb = 0.0f;
            GainCache sendGain;
            GainCache returnGain;
        };

        std::unique_ptr<SuperSamplerProcessor> sampler;
        std::unique_ptr<WavetableSynthMachine> wavetableSynth;
        std::unique_ptr<WaveshaperDistortionMachine> distortionFx;
        std::unique_ptr<DelayFxMachine> delayFx;
        std::unique_ptr<ChannelStripMachine> channelStripFx;
        std::unique_ptr<FilterFxMachine> filterFx;
        std::unique_ptr<ModulationMatrixMachine> modulationMatrix;
        bool modulationEnabled = false;
        std::vector<SlotState> slots;
        juce::AudioBuffer<float> renderBuffer;
        juce::AudioBuffer<float> delayTailBuffer;
        juce::MidiBuffer samplerMidiBuffer;
        /** Wavetable notes distributed into the current block, sorted by sample offset. */
        std::vector<MachineScheduledNote> wavetableBlockNotes;
        /** Filter-sweep notes distributed into the current block, sorted by sample offset. */
        std::vector<MachineScheduledNote> filterBlockNotes;
        std::vector<MachineScheduledNote> modulationBlockNotes;
        bool audioProcessingActive = false;
        bool samplerProcessingActive = false;
        bool wavetableProcessingActive = false;
        bool filterProcessingActive = false;
        bool muted = false;
        bool solo = false;
        int midiOutputChannel = 1;
        float gainDb = 0.0f;
        GainCache gain;
        float pan = 0.0f;
        float panCurrent = 0.0f;
        float panTarget = 0.0f;
        /** Smoothed final pan after audio-rate modulation. */
        float panSmoothed = 0.0f;
        int panRampSamples = 0;
        float meterLevel = 0.0f;
        /** Audio-thread ring buffer of the stack's final mono output (L+R)/2 for the
            UI oscilloscope. Allocated in prepareToPlay; the UI reads it via
            copyStackScope under withAudioThreadExclusive. */
        std::vector<float> scopeRing;
        std::size_t scopeHead = 0;

        /** Invokes `f` for each owned machine in fixed stack order. The machines
            are always constructed by initialiseMachines before any use. */
        template <typename F>
        void forEachMachine(F&& f)
        {
            f(*sampler);
            f(*wavetableSynth);
            f(*distortionFx);
            f(*delayFx);
            f(*channelStripFx);
            f(*filterFx);
            f(*modulationMatrix);
        }
    };
    struct SharedAuxBus
    {
        std::unique_ptr<AuxReverbMachine> machine;
        juce::AudioBuffer<float> inputBuffer;
        std::array<float, 2> meterLevels { 0.0f, 0.0f };
        float gainDb = 0.0f;
        float gainLinear = 1.0f;
        float pan = 0.0f;
        float panCurrent = 0.0f;
        float panTarget = 0.0f;
        int panRampSamples = 0;
        bool muted = false;
        int id = 0;
    };
    std::vector<ScheduledSamplerEvent> samplerEventsToSend;
    std::vector<ScheduledSamplerEvent> scratchFutureSamplerEvents;
    NoteEventQueue wavetableEvents;
    NoteEventQueue filterEvents;
    NoteEventQueue modulationEvents;
    static constexpr std::size_t kMaxModulationClockTicks = 256;
    std::array<ModulationClockTick, kMaxModulationClockTicks> modulationClockTicks {};
    std::size_t modulationClockTickCount = 0;
    int currentTickSampleOffset = 0;
    std::vector<MachineStack> machineStacks;
    SharedAuxBus auxBus1;
    SharedAuxBus auxBus2;
    float masterGainDb = 0.0f;
    float masterGainLinear = 1.0f;
    bool masterMuted = false;
    double preparedSampleRate = 44100.0;
    float panModulationSmoothing = 0.0f;
    std::array<float, 2> masterMeterLevels { 0.0f, 0.0f };
    struct RecordingSession
    {
        ~RecordingSession()
        {
            writer.reset();
            thread.stopThread(2000);
        }

        juce::TimeSliceThread thread { "MYK Tracker WAV Writer" };
        std::unique_ptr<juce::AudioFormatWriter::ThreadedWriter> writer;
    };
    std::shared_ptr<RecordingSession> recordingSession;
    std::atomic<juce::AudioFormatWriter::ThreadedWriter*> activeRecordingWriter { nullptr };
    std::mutex audioMutex;
    std::atomic<bool> processing { false };
    juce::MidiBuffer emptyMidiBuffer;


    // unsigned long elapsedSamples;
    /** stores current no. elapsed samples since program launch position between 0 and maxHorizon */
    int elapsedSamples;
    int lastQdOnAt{0}; // temporary test to measure intervals between note ons 
    int lastSendOnAt{0}; // temp to test when we actually sent it 
    int maxHorizon;   
    unsigned int samplesPerTick; 
    int quarterBeatTicksAccumulator {0};
    double lastHostPpqPosition {0.0};
    bool hostPpqValid {false};
    bool hostWasPlaying {false};
    bool sequencerWasPlaying {false};
    std::atomic<bool> internalClockEnabled { true };
    std::atomic<bool> hostClockActive { false };
    std::atomic<double> bpm; 
    int outstandingNoteOffs;
    /** configure plugin params */
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    /** stores the plugin state */
    juce::AudioProcessorValueTreeState apvts;
  
    juce::var stringGridToVar(const std::vector<std::vector<std::string>>& grid);
    juce::var numberGridToVar(const std::vector<std::vector<double>>& grid);
    void fillCommonUiState(juce::DynamicObject::Ptr state);

    /** convert ui state into a var  */
    juce::var getUiState();
    /** Converts only the grid needed by the active editor page into a var. */
    juce::var getUiStateForCurrentPage();
    /** Returns the editor mode string used by UI and persisted state. */
    juce::String getUiModeString() const;
    /** convert state into 'storable' var */
    juce::var serializeSequencerState();
    /** retrieve state from var  */
    void restoreSequencerState(const juce::var& stateVar);
    static std::unique_ptr<Sequencer> createDefaultSequenceSet(std::size_t sequenceCount = 16, std::size_t sequenceLength = 8);
    void resetSongState();
    Sequencer* getViewedSequencerInternal();
    const Sequencer* getViewedSequencerInternal() const;
    Sequencer* getPlaybackSequencerInternal();
    const Sequencer* getPlaybackSequencerInternal() const;
    void bindViewedSequenceSetToEditor();
    void schedulePlaybackSequenceSetSwitch(std::size_t index);
    void switchPlaybackSequenceSetImmediately(std::size_t index, bool rewindNow);
    void applyPendingSequenceSetSwitchForCurrentQuarterBeat();
    bool isPlaybackSequencerAtBoundary() const;
    void handleSongBeatBoundary();
    void primeSequenceSetForTransportStart(std::size_t index);
    juce::var serializeSingleSequencer(const Sequencer& sequencerToSave) const;
    void restoreSingleSequencer(Sequencer& target, const juce::var& seqVar);
    SequenceSetMachineEnableState captureMachineEnableState() const;
    void captureMachineEnableStateForSequenceSet(std::size_t setIndex);
    void normaliseMachineEnableState(SequenceSetMachineEnableState& state) const;
    void normaliseAllMachineEnableStates();
    void applyMachineEnableStateForSequenceSet(std::size_t setIndex);
    std::size_t getMachineEnableStateEditingSetIndex() const;
    bool isSongPlaybackRunning() const;
    static constexpr std::size_t kMachineStackCount = 8;
    MachineStack* getMachineStack(std::size_t stackIndex);
    const MachineStack* getMachineStack(std::size_t stackIndex) const;
    MachineStack::SlotState* getMachineSlot(std::size_t stackIndex, std::size_t slotIndex);
    const MachineStack::SlotState* getMachineSlot(std::size_t stackIndex, std::size_t slotIndex) const;
    MachineInterface* getMachineForStackType(MachineStack& stack, CommandType type);
    const MachineInterface* getMachineForStackType(const MachineStack& stack, CommandType type) const;
    SharedAuxBus* getAuxBusForType(CommandType type);
    const SharedAuxBus* getAuxBusForType(CommandType type) const;
    SharedAuxBus* getAuxBus(std::size_t auxIndex);
    const SharedAuxBus* getAuxBus(std::size_t auxIndex) const;
    static MachineStack::SlotState makeDefaultSlotState(CommandType type);
    /** (property key, machine) pairs for state save/restore. The persisted names
        are part of the plugin state format; do not rename. */
    static std::array<std::pair<const char*, MachineInterface*>, 7> stackMachineStateEntries(MachineStack& stack);
    static bool isAuxSendType(CommandType type);
    static bool slotSupportsReturnLevel(CommandType type);
    static bool slotAllowsDuplicate(CommandType type);
    void refreshStackProcessingState(MachineStack& stack);
    bool isStackAudible(std::size_t stackIndex) const;
    void refreshAllStackProcessingStates();
    AudioEffectMachine* getAudioEffectForStackType(MachineStack& stack, CommandType type);
    const AudioEffectMachine* getAudioEffectForStackType(const MachineStack& stack, CommandType type) const;
    void oscMessageReceived(const juce::OSCMessage& message) override;
    void oscBundleReceived(const juce::OSCBundle& bundle) override;
    juce::String getCurrentCellOscPayload(bool refreshMachineCells = true);
    static juce::String formatOscMessage(const juce::OSCMessage& message);
    void handleIncomingOscControlMessage(const juce::OSCMessage& message);
    void queueZoomCommand(float delta, float normalizedX, float normalizedY);
    void initialiseOsc();
    void initialiseMachines();
    bool startMasterRecording();
    void stopMasterRecording(bool deferFlush = false);
    void configureClockListeners();
    void removeClockListeners();
    void updateClockedMachineActivity();
    bool emitQuarterBeatTickIfNeeded();
    void emitClockedMachineEvent(std::size_t stackIndex, CommandType machineType, const MachineNoteEvent& event);
    void processPlaybackTickBoundary();
    void enqueueMachineMidi(juce::MidiBuffer& targetBuffer,
                            unsigned short channel,
                            unsigned short outNote,
                            unsigned short outVelocity,
                            unsigned short outDurTicks);
    void enqueueStackSamplerMidi(std::size_t stackIndex,
                                 unsigned short outNote,
                                 unsigned short outVelocity,
                                 unsigned short outDurTicks);
    void enqueueStackNote(NoteEventQueue& queue,
                          std::size_t stackIndex,
                          unsigned short note,
                          unsigned short velocity,
                          unsigned short durInTicks,
                          bool isNoteOff = false);
    /** Delivers queued note events that fall inside the current block and keeps the
        per-stack delivery vector sorted for the machine's cursor walk. */
    void distributeNoteEvents(NoteEventQueue& queue,
                              int blockStartSample,
                              int blockEndSample,
                              std::vector<MachineScheduledNote> MachineStack::* blockNotesMember);
    bool isStackAssigned(std::size_t stackIndex);
    bool stackContainsType(std::size_t stackIndex, CommandType machineType) const;
    std::optional<std::size_t> findMachineInStack(std::size_t stackIndex, CommandType type) const;
    void dispatchNoteThroughStack(std::size_t stackIndex,
                                  unsigned short note,
                                  unsigned short velocity,
                                  unsigned short durInTicks,
                                  std::size_t startSlotIndex = 0,
                                  bool held = false);
    void allNotesOffForStack(std::size_t stackIndex);
    //==============================================================================
    juce::OSCReceiver oscReceiver;
    juce::OSCSender oscSender;
    juce::String lastSentCellOscPayload;
    bool oscReceiverReady = false;
    bool oscSenderReady = false;
    std::mutex pendingZoomMutex;
    std::deque<PendingZoomCommand> pendingZoomCommands;
    PerformanceMetrics performanceMetrics;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TrackerMainProcessor)
};
