#pragma once

#include <juce_core/juce_core.h>
#include "RecordingWriter.h"

class RecordingModule
{
public:
    enum class StopResult
    {
        Kept,
        DeletedTooShort
    };

    RecordingModule();

    static juce::File getRecorderFile (int recorderIndex);

    // device lifecycle (SAFE to call repeatedly)
    void prepareDevice (double sampleRate,
                        int recorderIndex);

    // recording lifecycle
    void arm();
    StopResult confirmStop();
    void cancelStopRequest();
    bool isArmed() const;

    void setMonitoringEnabled (bool enabled);
    bool isMonitoringEnabled() const;

    void process (const float* input,
                  int numSamples);

    double getCurrentPassSeconds() const;
    void clear();

private:
    static constexpr double kMinSeconds = 25.0;
    static constexpr int    kMaxSeconds = 600;

    std::unique_ptr<RecordingWriter> writer;

    double sampleRate = 0.0;
    bool armed = false;
    bool monitoringEnabled = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RecordingModule)
};
