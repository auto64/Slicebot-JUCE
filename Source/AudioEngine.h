#pragma once

#include <JuceHeader.h>
#include <atomic>
#include <array>
#include "RecordingBus.h"
#include "RecordingModule.h"

class AudioEngine final : public juce::AudioIODeviceCallback,
                          private juce::HighResolutionTimer,
                          private juce::MidiInputCallback,
                          private juce::AsyncUpdater
{
public:
    enum class UiSound
    {
        Cowbell,
        Bleep
    };

    enum class MidiSyncMode
    {
        off = 0,
        receive = 1,
        send = 2
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

    // midi sync
    void setMidiSyncMode (MidiSyncMode mode);
    MidiSyncMode getMidiSyncMode() const;
    void setMidiSyncInputDeviceIdentifier (const juce::String& identifier);
    void setMidiSyncOutputDeviceIdentifier (const juce::String& identifier);
    juce::String getMidiSyncInputDeviceIdentifier() const;
    juce::String getMidiSyncOutputDeviceIdentifier() const;
    void setMidiVirtualPortsEnabled (bool enabled);
    bool getMidiVirtualPortsEnabled() const;
    void setMidiSyncBpm (double bpm);
    double getMidiSyncBpm() const;

    void setRecorderMidiInEnabled (int index, bool enabled);
    void setRecorderMidiOutEnabled (int index, bool enabled);
    bool isRecorderMidiInEnabled (int index) const;
    bool isRecorderMidiOutEnabled (int index) const;
    int getTransportMasterRecorderIndex() const;

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
    void hiResTimerCallback() override;
    void handleIncomingMidiMessage (juce::MidiInput* source,
                                    const juce::MidiMessage& message) override;
    void handleAsyncUpdate() override;
    void updateMidiClockState();
    void updateMidiInputState();
    void openMidiInputDevice();
    void closeMidiInputDevice();
    void openMidiOutputDevice();
    void closeMidiOutputDevice();
    juce::MidiOutput* getActiveMidiOutput() const;
    void sendMidiStart();
    void sendMidiStop();
    void applyExternalTransportStart();
    void applyExternalTransportStop();
    bool hasAnyRecorderMidiInEnabled() const;

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
    std::array<bool, RecordingBus::kNumRecorders> recorderMidiInEnabled
    {
        false, false, false, false
    };
    std::array<bool, RecordingBus::kNumRecorders> recorderMidiOutEnabled
    {
        false, false, false, false
    };
    int transportMasterRecorderIndex = -1;
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

    MidiSyncMode midiSyncMode = MidiSyncMode::off;
    juce::String midiSyncInputDeviceIdentifier;
    juce::String midiSyncOutputDeviceIdentifier;
#if JUCE_MAC
    bool midiVirtualPortsEnabled = true;
#else
    bool midiVirtualPortsEnabled = false;
#endif
    double midiSyncBpm = 120.0;
    bool midiClockRunning = false;
    std::atomic<bool> externalTransportPlaying { false };
    std::atomic<double> lastExternalClockMs { 0.0 };
    std::atomic<int> pendingExternalTransportCommand { 0 };
    juce::String activeMidiInputIdentifier;
    std::unique_ptr<juce::MidiInput> midiInput;
    juce::String activeMidiOutputIdentifier;
    std::unique_ptr<juce::MidiOutput> midiOutput;
    std::unique_ptr<juce::MidiOutput> midiVirtualOutput;

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
