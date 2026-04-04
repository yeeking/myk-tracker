#pragma once

#include <JuceHeader.h>

#include <algorithm>
#include <vector>

class ClockListener
{
public:
    virtual ~ClockListener() = default;
    virtual void tick(int quarterBeat) = 0;
    virtual void reset() = 0;
};

// Abstract transport clock interface used by the tracker and clocked machines.
class ClockAbs
{
public:
    /** Sets the clock tempo in beats per minute. */
    virtual void setBPM(double bpm) = 0;
    /** Returns the current tempo in beats per minute. */
    virtual double getBPM() = 0;

    /** Registers a listener for quarter-beat clock updates. */
    void addListener(ClockListener& listener)
    {
        const juce::ScopedLock lock(listenerLock);
        if (std::find(listeners.begin(), listeners.end(), &listener) == listeners.end())
            listeners.push_back(&listener);
    }

    /** Unregisters a previously registered listener. */
    void removeListener(ClockListener& listener)
    {
        const juce::ScopedLock lock(listenerLock);
        listeners.erase(std::remove(listeners.begin(), listeners.end(), &listener), listeners.end());
    }

    /** Clears all registered listeners. */
    void clearListeners()
    {
        const juce::ScopedLock lock(listenerLock);
        listeners.clear();
    }

    /** Returns the absolute transport tick count. */
    long getCurrentTick() const noexcept { return currentTick; }
    /** Returns the current bar position in quarter-beats, from 1 to 16. */
    int getCurrentQuarterBeat() const noexcept { return currentQuarterBeat; }

protected:
    /** Advances the absolute transport tick by one engine tick. */
    void advanceClockTick() noexcept { ++currentTick; }
    /** Resets the absolute transport tick counter. */
    void resetClockTicks() noexcept { currentTick = 0; }
    /** Sets the current published quarter-beat. */
    void setCurrentQuarterBeat(int quarterBeat) noexcept { currentQuarterBeat = quarterBeat; }
    /** Broadcasts a quarter-beat tick to listeners. */
    void notifyClockTick(int quarterBeat)
    {
        setCurrentQuarterBeat(quarterBeat);
        const juce::ScopedLock lock(listenerLock);
        for (auto* listener : listeners)
            if (listener != nullptr)
                listener->tick(quarterBeat);
    }
    /** Broadcasts a clock reset to listeners. */
    void notifyClockReset()
    {
        const juce::ScopedLock lock(listenerLock);
        for (auto* listener : listeners)
            if (listener != nullptr)
                listener->reset();
    }

private:
    long currentTick = 0;
    int currentQuarterBeat = 0;
    juce::CriticalSection listenerLock;
    std::vector<ClockListener*> listeners;
};
