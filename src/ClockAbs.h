#pragma once 

class ClockAbs{
    public:
    ClockAbs() : currentTick{0} {}
    /** implement bpm changing here */
    virtual void setBPM(unsigned int bpm) = 0;
    /** get the clock's current tick */
    virtual long getCurrentTick() const {return currentTick;}
    /** tell the clock to tick. Normally called by a thread or somesuch. It might even tick itself */
    void tick(){currentTick ++; }
    private:
    long currentTick; 

};