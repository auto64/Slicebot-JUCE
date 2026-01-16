#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
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
    void prepare (double sampleRate, int bufferSize);

    // =====================================================
    // RECORD CONTROL
    // =====================================================
    void armRecorder (int index);
    RecordingModule::StopResult confirmStopRecorder (int index);
    void cancelStopRecorder (int index);
    void clearRecorder (int index);

    bool hasLatchedRecorders() const;
    void armLatchedRecorders();
    RecordingModule::StopResult stopLatchedRecorders();

    bool isRecorderArmed (int index) const;
    void setRecorderLatchEnabled (int index, bool enabled);
    bool isRecorderLatchEnabled (int index) const;
    void setRecorderRecordArmEnabled (int index, bool enabled);
    bool isRecorderRecordArmEnabled (int index) const;

    bool startPlayback (int index);
    void stopPlayback (int index);
    bool isRecorderPlaying (int index) const;
    bool startLatchedPlayback();
    void stopLatchedPlayback();

    double getRecorderPlaybackProgress (int index) const;
    void seekRecorderPlayback (int index, double progress);
    double getRecorderRecordStartMs (int index) const;
    int getRecorderTotalSamples (int index) const;
    int getRecorderMaxSamples (int index) const;

    void setRecorderInputGainDb (int index, float gainDb);
    float getRecorderInputGainDb (int index) const;
    float getRecorderRms (int index) const;
    float getRecorderPeak (int index) const;

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
        bool latchEnabled      = false;
        bool recordArmEnabled  = true;

        bool playing = false;
        juce::int64 playbackPosition = 0;
        double recordStartMs = 0.0;

        float inputGainDb = 0.0f;
        float inputGainLinear = 1.0f;
        float rms = 0.0f;
        float peak = 0.0f;

        juce::AudioBuffer<float> inputBuffer;
        juce::AudioBuffer<float> playbackBuffer;
    };

    std::array<RecorderSlot, kNumRecorders> recorders;
    double sampleRate = 0.0;
    int bufferSize = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RecordingBus)
};
