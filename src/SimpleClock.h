#pragma once

#include <thread>
#include <chrono>
#include <functional>
#include <iostream>
#include "ClockAbs.h"

class SimpleClock  : public ClockAbs
{
  public:
    SimpleClock(int _sleepTimeMs = 5, 
                std::function<void()>_callback = [](){
                    std::cout << "SimpleClock::default tick callback" << std::endl;
                }) : sleepTimeMs{_sleepTimeMs}, running{false}, callback{_callback}, currentTick{0}, inTick{false}
     {
       // constructor body
     }

    ~SimpleClock()
    {
      stop();
      if (tickThread != nullptr) delete tickThread;
    }
    void setBPM(unsigned int bpm) override 
    {
      // TODO
      
    }
    /** start with the sent interval between calls the to callback*/
    void start(int _intervalMs)
    {
      stop();
      inTick = false;
      running = true;
      tickThread = new std::thread(SimpleClock::ticker, this, _intervalMs, sleepTimeMs);
    }

    void stop()
    {
      if (running)
      {
       //t << "SimpleClock::stop shutting down " << std::endl;
        running = false; 
        tickThread->join();
      }
    }
    /** set the function to be called when the click ticks */
    void setCallback(std::function<void()> c){

      while(inTick) ;
       //	std::cout << "setcallback in tick" << std::endl;// wait until not in tick
      inTick = true;
      callback = c;
      inTick = false;
    }
    void tick()
    {
      //while(inTick) ;
	//	std::cout << "tick in tick" << std::endl;// wait until not in ti
      inTick = true;// mutex stuff
      currentTick ++;
      // call the callback
      //std::cout << "SimpleClock::tick" << std::endl; 
      callback();
      inTick = false; 
    }
    long getCurrentTick() const override 
    {
      return currentTick;
    }

 static void ticker(SimpleClock* clock, long intervalMs, long sleepTimeMs)
    {
      long nowMs = 0;
      long elapsedMs = 0;
      long startTimeMs = SimpleClock::getNow();
      long remainingMs;
      while(clock->running)
      {
        nowMs = SimpleClock::getNow();
        elapsedMs = nowMs - startTimeMs;
        if (elapsedMs >= intervalMs) // time to tick
        { 
          // move the start time along
          startTimeMs = nowMs;
          //std::cout << "SimpleClock::ticker elapsed time  " << elapsedMs << " of " << intervalMs << std::endl;
          clock->tick();
        }
        else {
          // sleep - but how much?
          remainingMs = intervalMs - elapsedMs;
          if (remainingMs > sleepTimeMs) // sleep as much as possible
            std::this_thread::sleep_for(std::chrono::milliseconds(remainingMs - sleepTimeMs));
          else
            std::this_thread::sleep_for(std::chrono::milliseconds(sleepTimeMs));
        }
      }
    }
    
  private:
    static long getNow()
    {
      return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    }
    long intervalMs;
    long sleepTimeMs; // lower means more precision in the timing
    bool running;     
    std::thread* tickThread {nullptr};
    std::function<void()> callback;
    long currentTick;
    bool inTick;
};

