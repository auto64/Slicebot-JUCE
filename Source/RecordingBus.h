#pragma once

#include <juce_core/juce_core.h>
#include <array>

#include "RecordingModule.h"

class RecordingBus
{
public:
    static constexpr int kNumRecorders = 4;

    RecordingBus();

    // =====================================================
    // DEVICE LIFECYCLE
    // =====================================================
    void prepare (double sampleRate);

    // =====================================================
    // RECORD CONTROL
    // =====================================================
    void armRecorder (int index);
    RecordingModule::StopResult confirmStopRecorder (int index);
    void cancelStopRecorder (int index);
    void clearRecorder (int index);

    bool isRecorderArmed (int index) const;

    // =====================================================
    // ROUTING
    // =====================================================
    void setRecorderInputBufferIndex (int index, int bufferIndex);
    void setRecorderMonitoringEnabled (int index, bool enabled);

    // =====================================================
    // TIMING
    // =====================================================
    double getRecorderCurrentPassSeconds (int index) const;

    // =====================================================
    // AUDIO
    // =====================================================
    void processAudioBlock (const float* const* input,
                            int numInputChannels,
                            float* const* output,
                            int numOutputChannels,
                            int numSamples);

private:
    struct RecorderSlot
    {
        RecordingModule recorder;
        int  bufferIndex       = -1;
        bool armed             = false;
        bool monitoringEnabled = false;
    };

    std::array<RecorderSlot, kNumRecorders> recorders;
    double sampleRate = 0.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RecordingBus)
};
