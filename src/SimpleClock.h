#pragma once

#include <thread>
#include <chrono>
#include <functional>
#include <iostream>
#include <shared_mutex>

#include "ClockAbs.h"

class SimpleClock  : public ClockAbs
{
  public:
    SimpleClock(int _sleepTimeMs = 5, 
                std::function<void()>_callback = [](){
                    std::cout << "SimpleClock::default tick callback" << std::endl;
                }) : 
                rw_mutex{std::make_unique<std::shared_mutex>()}, 
                sleepTimeMs{_sleepTimeMs}, running{false}, callback{_callback}, currentTick{0}, bpm{120}
     {
       // constructor body
     }

    ~SimpleClock()
    {
      stop();
      if (tickThread != nullptr) delete tickThread;
    }
    void setBPM(double _bpm) override 
    {
      unsigned long intMS = static_cast<unsigned long> (60.0/bpm*1000.0/8.0);
      // std::cout << "clock: bpm is " << bpm << " ms is " << intMS << std::endl;
      bpm = _bpm;
      start(intMS);
    }
    double getBPM() override
    {
      return bpm; 
      // return 1000.0 / (intervalMs * 4);// how many (4 ticks) in 1 second 
    }

    /** start with the sent interval between calls the to callback*/
    void start(int _intervalMs)
    {
      if (running){stop();}
      std::unique_lock<std::shared_mutex> lock(*rw_mutex);
      running = true;
      tickThread = new std::thread(SimpleClock::ticker, this, _intervalMs, sleepTimeMs);
    }

    void stop()
    {
      std::unique_lock<std::shared_mutex> lock(*rw_mutex);
      if (running)
      {
       //t << "SimpleClock::stop shutting down " << std::endl;
        running = false; 
        tickThread->join();
      }
    }
    /** set the function to be called when the click ticks */
    void setCallback(std::function<void()> c){
      std::unique_lock<std::shared_mutex> lock(*rw_mutex);
      callback = c;
    }
    void tick()
    {
      std::unique_lock<std::shared_mutex> lock(*rw_mutex);
      currentTick ++;
      callback();
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
    std::unique_ptr<std::shared_mutex> rw_mutex;

    // long intervalMs;
    long sleepTimeMs; // lower means more precision in the timing
    bool running;     
    std::function<void()> callback;
    long currentTick;
    double bpm;

    std::thread* tickThread {nullptr};

};

