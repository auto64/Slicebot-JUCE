#pragma once

#include <JuceHeader.h>
#include <atomic>
#include <array>
#include "RecordingBus.h"
#include "RecordingModule.h"

class AudioEngine final : public juce::AudioIODeviceCallback
{
public:
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

    void setRecorderMonitoringEnabled (int index, bool enabled);
    void setRecorderInputChannel (int index, int physicalChannel);
    void setRecorderLatchEnabled (int index, bool enabled);

    // timing
    double getRecorderCurrentPassSeconds (int index) const;

    // meters
    float getInputRMS() const;
    float getInputPeak() const;

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

    std::atomic<float> inputRMS  { 0.0f };
    std::atomic<float> inputPeak { 0.0f };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AudioEngine)
};
