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

class TrackerMainProcessor;

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
        getState, transport, setStep, editSequence, editSong, editMachineStack,
        machineControl, loadSample, uiAction, application
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
    TrackerViewSnapshot getViewSnapshot();
    Result readResource(const juce::String& uri);
    juce::var capabilities() const;

private:
    Result executeNow(const Command& command);
    Result getStateNow(const juce::String& scope);
    Result readResourceNow(const juce::String& uri);
    Result fail(const juce::String& code, const juce::String& message) const;
    Result success(juce::var data = {}) const;
    bool revisionsMatch(const Command& command, Result& failure) const;
    void changed(bool content, bool view);
    void synchroniseAsyncCompletions();
    juce::var makeStateNow();
    juce::var makeViewNow();
    juce::var makeMachineCellsNow();
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
};
