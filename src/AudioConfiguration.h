#pragma once

#include <string>
#include <vector>

/** Standalone-owned audio device state consumed by the tracker grid. */
struct AudioConfigurationSnapshot
{
    bool standalone = false;
    bool applying = false;
    bool canApply = false;
    std::vector<std::string> inputDevices;
    std::vector<std::string> outputDevices;
    std::string activeInput;
    std::string activeOutput;
    std::string pendingInput;
    std::string pendingOutput;
    double activeSampleRate = 0.0;
    double pendingSampleRate = 0.0;
    int activeBufferSize = 0;
    int pendingBufferSize = 0;
    int inputLatencySamples = 0;
    int outputLatencySamples = 0;
    int bitDepth = 0;
    double cpuUsage = -1.0;
    double peakCpuUsage = -1.0;
    int xruns = -1;
    std::string deviceType;
    std::string status;
};

class AudioConfigurationHost
{
public:
    virtual ~AudioConfigurationHost() = default;
    virtual AudioConfigurationSnapshot getAudioConfigurationSnapshot() = 0;
    virtual void refreshAudioDevices() = 0;
    virtual void adjustAudioOutput(int direction) = 0;
    virtual void adjustAudioInput(int direction) = 0;
    virtual void adjustAudioSampleRate(int direction) = 0;
    virtual void adjustAudioBufferSize(int direction) = 0;
    virtual void applyAudioConfigurationAsync() = 0;
    virtual void revertAudioConfiguration() = 0;
    virtual void resetAudioPerformanceStats() = 0;
};
