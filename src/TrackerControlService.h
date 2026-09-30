#pragma once

#include <JuceHeader.h>

#include <cstdint>
#include <atomic>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

class TrackerMainProcessor;
struct UIBox;

/** The UI- and transport-neutral command surface for a tracker instance.
    All mutations are serialised onto the JUCE message thread before they touch
    the processor.  HTTP clients therefore never access audio state directly. */
class TrackerControlService
{
public:
    /** Immutable copy returned to adapters.  The JSON representation is kept
        at the boundary for compatibility while adapters stop borrowing model
        objects or UI callbacks. */
    struct TrackerViewSnapshot
    {
        std::uint64_t contentRevision = 0;
        std::uint64_t viewRevision = 0;
        juce::var state;
    };

    enum class CommandKind
    {
        getState, getTrackNotes, getStepValues, setTrackNotes, setTrackLengths,
        transport, setStep, editSequence, editSong, editMachineStack,
        machineControl, loadSample, uiAction, application, getScreenshot
    };

    struct Command
    {
        CommandKind kind = CommandKind::getState;
        juce::var arguments;
        std::optional<std::uint64_t> expectedContentRevision;
        std::optional<std::uint64_t> expectedViewRevision;
    };

    struct Result
    {
        bool ok = true;
        juce::String code;
        juce::String message;
        std::uint64_t contentRevision = 0;
        std::uint64_t viewRevision = 0;
        juce::var data;
    };

    explicit TrackerControlService(TrackerMainProcessor& processor);

    Result execute(const Command& command);
    Result getState(const juce::String& scope = "state");
    TrackerViewSnapshot getViewSnapshot(bool includeSerializedMachineCells = true);
    /** Returns the lightweight view snapshot consumed by the JUCE editor timer. */
    TrackerViewSnapshot getViewSnapshotForPage();
    Result readResource(const juce::String& uri);
    juce::var capabilities() const;

private:
    Result executeNow(const Command& command);
    Result getScreenshot();
    Result getStateNow(const juce::String& scope, bool includeSerializedMachineCells = true, bool pageOnly = false);
    Result readResourceNow(const juce::String& uri);
    Result fail(const juce::String& code, const juce::String& message) const;
    Result success(juce::var data = {}) const;
    bool revisionsMatch(const Command& command, Result& failure) const;
    void changed(bool content, bool view);
    void synchroniseAsyncCompletions();
    juce::var makeUiNow();
    juce::var makeUiForPageNow();
    juce::var makeStateNow();
    juce::var makeViewNow(bool includeSerializedMachineCells = true, bool pageOnly = false);
    juce::var makeMachineCellsNow();
    juce::var makeMachineControlsNow();
    juce::var serialiseMachineCells(std::size_t stackIndex, const juce::String& slotId,
                                    const std::vector<std::vector<UIBox>>& cells) const;
    juce::var serialiseGrid(const std::vector<std::vector<std::string>>& grid) const;

    template <typename Fn>
    Result onMessageThread(Fn&& fn)
    {
        if (auto* manager = juce::MessageManager::getInstanceWithoutCreating();
            manager != nullptr && manager->isThisTheMessageThread())
            return fn();

        auto result = std::make_shared<Result>();
        auto completed = std::make_shared<juce::WaitableEvent>();
        const bool queued = juce::MessageManager::callAsync([result, completed, callback = std::function<Result()>(std::forward<Fn>(fn))]() mutable
        {
            *result = callback();
            completed->signal();
        });
        if (!queued)
            return fail("message_thread_unavailable", "The tracker message thread is unavailable");
        if (!completed->wait(2000))
            return fail("message_thread_timeout", "The tracker did not process the command in time");
        return *result;
    }

    TrackerMainProcessor& processor;
    std::uint64_t contentRevision = 0;
    std::uint64_t viewRevision = 0;
    struct AsyncState
    {
        struct Load
        {
            juce::String id;
            juce::String status;
            juce::String path;
            juce::String error;
            int stackId = 0;
            int playerId = 0;
            int startNote = 0;
            int endNote = 127;
        };
        std::mutex mutex;
        std::map<std::string, Load> loads;
        std::atomic<std::uint64_t> completedSinceRevision { 0 };
        std::atomic<std::uint64_t> nextId { 1 };
    };
    std::shared_ptr<AsyncState> asyncState = std::make_shared<AsyncState>();
    /** Session-local sequence clipboard.  It intentionally is not persisted. */
    struct SequenceClipboard
    {
        bool hasSteps = false;
        bool pasteAtCursor = false;
        std::size_t wholeSequenceLength = 0;
        std::vector<std::vector<std::vector<double>>> steps;
    } sequenceClipboard;
    struct SequenceSelection
    {
        bool active = false;
        std::size_t sequence = 0;
        std::size_t anchorStep = 0;
        std::size_t cursorStep = 0;
    } sequenceSelection;
};
