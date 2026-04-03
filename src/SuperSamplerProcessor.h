#pragma once
#include <JuceHeader.h>
#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "MachineInterface.h"

class SuperSamplePlayer;


//==============================================================================
// Audio processor that hosts multiple sample players and exposes sampler controls.
class SuperSamplerProcessor final : public juce::AudioProcessor, public MachineInterface
{
public:
    /** Creates the sampler processor and its player collection. */
    SuperSamplerProcessor();
    /** Destroys the sampler processor. */
    ~SuperSamplerProcessor() override;

    /** Prepares the sampler and all players for realtime playback. */
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    /** Releases any realtime sampler resources. */
    void releaseResources() override;

    /** Reports whether the sampler supports the requested bus layout. */
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;

    /** Processes one audio/MIDI block for the sampler. */
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    /** Sampler machines are editor-less in plugin-host terms. */
    juce::AudioProcessorEditor* createEditor() override;
    /** Returns false because this processor uses the tracker editor. */
    bool hasEditor() const override;

    /** Returns the JUCE processor name. */
    const juce::String getName() const override;

    /** Returns true because the sampler accepts MIDI input. */
    bool acceptsMidi() const override;
    /** Returns false because the sampler does not emit host MIDI. */
    bool producesMidi() const override;
    /** Returns false because this is not a MIDI-effect-only processor. */
    bool isMidiEffect() const override;
    /** Returns the effect tail length in seconds. */
    double getTailLengthSeconds() const override;

    /** Returns the number of exposed JUCE programs. */
    int getNumPrograms() override;
    /** Returns the active JUCE program index. */
    int getCurrentProgram() override;
    /** Selects the active JUCE program index. */
    void setCurrentProgram (int index) override;
    /** Returns the JUCE program name. */
    const juce::String getProgramName (int index) override;
    /** Renames a JUCE program. */
    void changeProgramName (int index, const juce::String& newName) override;

    /** Serialises the sampler state. */
    void getStateInformation (juce::MemoryBlock& destData) override;
    /** Restores the sampler state. */
    void setStateInformation (const void* data, int sizeInBytes) override;

    /** Handles a message from the web/API bridge. */
    void messageReceivedFromWebAPI(std::string msg);
    /** Adds a new sample player via the web/API bridge. */
    void addSamplePlayerFromWeb();
    /** Removes a sample player by id. */
    void removeSamplePlayer (int playerId);
    /** Pushes the current sampler state to the UI bridge. */
    void sendSamplerStateToUI();
    /** Requests an asynchronous sample load for the given player. */
    void requestSampleLoadFromWeb (int playerId);
    /** Returns the sampler state as a serialisable JUCE var. */
    juce::var getSamplerState() const;
    /** Returns the waveform SVG for one player. */
    juce::String getWaveformSVGForPlayer (int playerId) const;
    /** Returns waveform points for one player. */
    std::vector<float> getWaveformPointsForPlayer (int playerId) const;
    /** Returns the VU state JSON for UI consumption. */
    std::string getVuStateJson() const;
    /** Sets the MIDI note range for a player. */
    void setSampleRangeFromWeb (int playerId, int low, int high);
    /** Triggers a player immediately by id. */
    void triggerFromWeb (int playerId);
    /** Sets the static gain of a player. */
    void setGainFromUI (int playerId, float gain);
    /** Formats a note label for the sequencer view. */
    std::string describeNoteForSequencer (int midiNote) const;
    /** Returns true while the integrated file browser is open. */
    bool isBrowsingFiles() const;

    /** Builds the machine-editor UI cells for the sampler. */
    std::vector<std::vector<UIBox>> getUIBoxes(const MachineUiContext& context) override;
    /** Routes an incoming note to the matching sample player. */
    bool handleIncomingNote(unsigned short note,
                            unsigned short velocity,
                            unsigned short durationTicks,
                            MachineNoteEvent& outEvent) override;
    /** Adds a new sample player from the machine page. */
    void addEntry() override;
    /** Removes a sample player from the machine page. */
    void removeEntry(int entryIndex) override;
    /** Closes transient UI such as the integrated file browser. */
    bool dismissTransientUi() override;
    /** Notifies the sampler that the machine-page cursor moved. */
    void onCursorMoved(int row, int col) override;
    /** Lets the sampler consume left-navigation in browser mode. */
    bool navigateLeft() override;
    /** Passes printable search input to the browser. */
    bool handleTextInput(char character) override;
    /** Passes backspace search input to the browser. */
    bool handleTextBackspace() override;
    /** Returns true while the sampler browser owns keyboard input. */
    bool wantsExclusiveKeyboardInput() const override;
    /** Requests a preferred cursor row after browser navigation changes. */
    int consumePreferredCursorRow(const std::vector<std::vector<UIBox>>& cells) override;

private:
    /** Compact player state mirrored into the sampler UI. */
    struct UiPlayerState
    {
        /** Unique player id. */
        int id = 0;
        /** Lowest MIDI note accepted by the player. */
        int midiLow = 0;
        /** Highest MIDI note accepted by the player. */
        int midiHigh = 127;
        /** Player gain displayed in the UI. */
        float gain = 1.0f;
        /** True when the player is currently active. */
        bool isPlaying = false;
        /** Player VU level in decibels. */
        float vuDb = -60.0f;
        /** Short UI status string. */
        std::string status;
        /** Display name of the loaded sample file. */
        std::string fileName;
    };

    /** JUCE parameter tree used for state persistence. */
    juce::AudioProcessorValueTreeState apvts;
    /** Last browsed sample directory, persisted across sessions. */
    juce::File lastSampleDirectory;

    /** Creates the sampler parameter layout for the APVTS. */
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    /** Broadcasts a message to connected UI clients. */
    void broadcastMessage (const juce::String& msg);
    /** Processes MIDI-triggered playback for all players. */
    void processSamplerBlock (juce::AudioBuffer<float>& buffer, const juce::MidiBuffer& midi);
    /** Adds a new player and returns its id. */
    int addSamplePlayer();
    /** Removes a player internally by id. */
    bool removeSamplePlayerInternal (int playerId);
    /** Exports sampler state to a JUCE var. */
    juce::var toVar() const;
    /** Loads a sample asynchronously into a player. */
    void loadSampleAsync (int playerId, const juce::File& file, std::function<void (bool, juce::String)> onComplete);
    /** Sets the MIDI range for a player. */
    bool setMidiRange (int playerId, int low, int high);
    /** Sets the gain for a player. */
    bool setGain (int playerId, float gain);
    /** Triggers a player by id. */
    bool trigger (int playerId);
    /** Returns the waveform SVG for a player. */
    juce::String getWaveformSVG (int playerId) const;
    /** Returns waveform points for a player. */
    std::vector<float> getWaveformPoints (int playerId) const;
    /** Returns the cached VU JSON string. */
    std::shared_ptr<std::string> getVuJson() const;
    /** Exports the sampler state to a ValueTree. */
    juce::ValueTree exportToValueTree() const;
    /** Restores sampler state from a ValueTree. */
    void importFromValueTree (const juce::ValueTree& tree);
    /** Loads a sample synchronously into a player. */
    bool loadSampleInternal (int playerId, const juce::File& file, juce::String& error);
    /** Returns the player matching an id, or null. */
    SuperSamplePlayer* getPlayer (int playerId) const;
    /** Builds the integrated file-browser UI. */
    std::vector<std::vector<UIBox>> buildBrowserUi();
    /** Opens the integrated file browser for a player. */
    void openFileBrowserForPlayer(int playerId);
    /** Closes the integrated file browser. */
    void closeFileBrowser();
    /** Moves the browser up one directory. */
    void browseUp();
    /** Enters a child directory from the browser. */
    void browseInto(const juce::File& target);
    /** Loads the currently browsed file into the target player. */
    void loadBrowsedFile(const juce::File& file);
    /** Previews the currently browsed audio file. */
    void previewBrowsedFile(const juce::File& file);
    /** Stops the preview player immediately. */
    void stopPreviewPlayback();

    /** Owned sample players for normal sampler playback. */
    std::vector<std::unique_ptr<SuperSamplePlayer>> players;
    /** Hidden player used for browser preview playback. */
    std::unique_ptr<SuperSamplePlayer> previewPlayer;
    /** Protects player state shared between UI and audio threads. */
    mutable std::mutex playerMutex;
    /** Next player id to allocate. */
    int nextId { 1 };
    /** Audio format manager used for sample decoding. */
    juce::AudioFormatManager formatManager;
    /** Cached VU JSON string for UI clients. */
    std::string vuJson;
    /** Spin lock protecting the cached VU JSON string. */
    mutable juce::SpinLock vuLock;

    /** UI snapshot of each player. */
    std::vector<UiPlayerState> uiPlayers;
    /** Per-player glow levels used by the machine UI. */
    std::vector<float> uiGlowLevels;
    /** Player currently associated with the open browser. */
    int browsingPlayerId { -1 };
    /** Directory currently displayed in the browser. */
    juce::File browsingDirectory;
    /** Active browser search query. */
    std::string browserSearchQuery;
    /** Cached browser UI cells. */
    std::vector<std::vector<UIBox>> cachedBrowserUi;
    /** True when the cached browser UI must be rebuilt. */
    bool browserUiDirty { true };
    /** Preview file currently loaded in the hidden player. */
    juce::File previewLoadedFile;
    /** Folder label to refocus after navigating upward. */
    std::string pendingBrowserFocusLabel;
    /** Cancels stale async preview requests. */
    std::atomic<std::uint64_t> previewRequestGeneration { 0 };
    /** Last output sample rate seen in prepareToPlay. */
    double currentOutputSampleRate { 44100.0 };
    /** Last block size seen in prepareToPlay. */
    int currentBlockSize { 512 };

    //==============================================================================
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SuperSamplerProcessor)
};
