#pragma once 

// Abstract clock interface used by the sequencer for timing.
class ClockAbs{
    public:
    /** Creates a clock with its tick counter reset to zero. */
    ClockAbs() : currentTick{0} {}
    /** Sets the clock tempo in beats per minute. */
    virtual void setBPM(double bpm) = 0;
    /** Returns the current tempo in beats per minute. */
    virtual double getBPM() = 0;
    
    /** Returns the absolute tick count. */
    virtual long getCurrentTick() const {return currentTick;}
    /** Resets the absolute tick counter back to zero. */
    void resetTicks(){currentTick = 0;}
    /** Advances the absolute tick counter by one. */
    void tick(){currentTick ++; }
    private:
    /** Absolute tick counter used for transport timing. */
    long currentTick; 

};
