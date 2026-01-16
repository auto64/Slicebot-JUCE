#pragma once

#include <JuceHeader.h>
#include <atomic>
#include <array>
#include "RecordingBus.h"
#include "RecordingModule.h"

class AudioEngine final : public juce::AudioIODeviceCallback
{
public:
    enum class UiSound
    {
        Cowbell,
        Bleep
    };

    struct ActiveInputChannel
    {
        juce::String name;
        int physicalIndex; // hardware channel index
    };

    AudioEngine();
    ~AudioEngine() override;

    // lifecycle
    void start();
    void stop();

    void restoreState();
    void saveState();

    // device
    juce::AudioDeviceManager& getDeviceManager();

    // input channels (UI)
    juce::StringArray getInputChannelNames() const;
    juce::Array<ActiveInputChannel> getActiveInputChannels() const;

    // recorder control
    void armRecorder (int index);
    RecordingModule::StopResult confirmStopRecorder (int index);
    void cancelStopRecorder (int index);
    void clearRecorder (int index);
    bool startPlayback (int index);
    void stopPlayback (int index);

    void setRecorderMonitoringEnabled (int index, bool enabled);
    void setRecorderInputChannel (int index, int physicalChannel);
    void setRecorderLatchEnabled (int index, bool enabled);
    void setRecorderIncludeInGenerationEnabled (int index, bool enabled);
    void setRecorderMidiArmEnabled (int index, bool enabled);
    void setRecorderRecordArmEnabled (int index, bool enabled);
    void setRecorderLocked (int index, bool locked);
    void setRecorderInputGainDb (int index, float gainDb);

    bool hasLatchedRecorders() const;
    void armLatchedRecorders();
    RecordingModule::StopResult stopLatchedRecorders();
    bool startLatchedPlayback();
    void stopLatchedPlayback();

    int getRecorderInputChannel (int index) const;
    bool isRecorderMonitoringEnabled (int index) const;
    bool isRecorderLatchEnabled (int index) const;
    bool isRecorderIncludeInGenerationEnabled (int index) const;
    bool isRecorderMidiArmEnabled (int index) const;
    bool isRecorderRecordArmEnabled (int index) const;
    bool isRecorderLocked (int index) const;
    bool isRecorderArmed (int index) const;
    bool isRecorderPlaying (int index) const;
    float getRecorderInputGainDb (int index) const;
    float getRecorderRms (int index) const;
    float getRecorderPeak (int index) const;
    double getRecorderPlaybackProgress (int index) const;
    void seekRecorderPlayback (int index, double progress);
    double getRecorderRecordStartMs (int index) const;
    int getRecorderTotalSamples (int index) const;
    int getRecorderMaxSamples (int index) const;

    // timing
    double getRecorderCurrentPassSeconds (int index) const;

    // meters
    float getInputRMS() const;
    float getInputPeak() const;

    // ui sounds
    void playUiSound (UiSound sound);

    // JUCE callbacks
    void audioDeviceAboutToStart (juce::AudioIODevice*) override;
    void audioDeviceStopped() override;

    void audioDeviceIOCallbackWithContext (
        const float* const* input,
        int numInputChannels,
        float* const* output,
        int numOutputChannels,
        int numSamples,
        const juce::AudioIODeviceCallbackContext&) override;

private:
    juce::AudioDeviceManager deviceManager;
    RecordingBus recordingBus;

    // PHYSICAL channel per recorder (stable)
    std::array<int, RecordingBus::kNumRecorders> recorderPhysicalChannels
    {
        -1, -1, -1, -1
    };
    std::array<bool, RecordingBus::kNumRecorders> recorderMonitoringEnabled
    {
        false, false, false, false
    };
    std::array<bool, RecordingBus::kNumRecorders> recorderLatchEnabled
    {
        false, false, false, false
    };
    std::array<bool, RecordingBus::kNumRecorders> recorderIncludeInGeneration
    {
        true, true, true, true
    };
    std::array<bool, RecordingBus::kNumRecorders> recorderMidiArmEnabled
    {
        false, false, false, false
    };
    std::array<bool, RecordingBus::kNumRecorders> recorderRecordArmEnabled
    {
        true, true, true, true
    };
    std::array<bool, RecordingBus::kNumRecorders> recorderLocked
    {
        false, false, false, false
    };
    std::array<float, RecordingBus::kNumRecorders> recorderInputGainDb
    {
        0.0f, 0.0f, 0.0f, 0.0f
    };

    juce::AudioFormatManager soundFormatManager;
    juce::AudioBuffer<float> bleepBuffer;
    juce::AudioBuffer<float> cowbellBuffer;
    std::atomic<int> soundPosition { 0 };
    std::atomic<int> soundLength { 0 };
    std::atomic<UiSound> currentSound { UiSound::Cowbell };

    std::atomic<float> inputRMS  { 0.0f };
    std::atomic<float> inputPeak { 0.0f };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AudioEngine)
};
