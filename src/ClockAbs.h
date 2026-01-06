#pragma once 

// Abstract clock interface used by the sequencer for timing.
class ClockAbs{
    public:
    ClockAbs() : currentTick{0} {}
    /** implement bpm changing here */
    virtual void setBPM(double bpm) = 0;
    virtual double getBPM() = 0;
    
    /** get the clock's current tick */
    virtual long getCurrentTick() const {return currentTick;}
    /** reset the clock's tick counter */
    void resetTicks(){currentTick = 0;}
    /** tell the clock to tick. Normally called by a thread or somesuch. It might even tick itself */
    void tick(){currentTick ++; }
    private:
    long currentTick; 

};
