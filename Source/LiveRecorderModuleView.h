#pragma once

#include <JuceHeader.h>
#include "FlatTileLookAndFeel.h"

// Forward declaration
class AudioEngine;

class LiveRecorderModuleView final : public juce::Component,
                                     private juce::Button::Listener,
                                     private juce::ComboBox::Listener,
                                     private juce::Timer
{
public:
    LiveRecorderModuleView (AudioEngine& engine, int recorderIndex);
    ~LiveRecorderModuleView() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

    // called when audio device / active inputs change
    void refreshInputChannels();

    void setDeleteModuleHandler (std::function<void()> handler);

private:
    // callbacks
    void buttonClicked (juce::Button*) override;
    void comboBoxChanged (juce::ComboBox*) override;
    void timerCallback() override;

    void showUnderMinWarning();
    void showDeleteWarning();
    void showLockedWarning();
    void showMissingRecordingWarning();
    void showRecordingInProgressWarning();
    void applyPersistedControlState();

    // state
    AudioEngine& audioEngine;
    const int recorderIndex;

    bool   isRecording = false;
    bool   stopDialogOpen = false;
    double lastRecordedSeconds = 0.0;
    double recordingOffsetSeconds = 0.0;
    bool isPlaying = false;

    // 🔴 REQUIRED for flashing (was accidentally dropped)
    double flashPhase = 0.0;

    // Look & Feel (tiles)
    FlatTileLookAndFeel flatTiles;

    // UI
    juce::ToggleButton midiInButton { "MIDI IN" };
    juce::ToggleButton midiOutButton { "MIDI OUT" };
    juce::ComboBox     channelBox;

    juce::ToggleButton recordArmButton { "REC" };
    juce::ToggleButton monitorButton { "I" };
    juce::ToggleButton linkButton    { "L" };
    juce::ToggleButton lockButton    { "LOCK" };
    juce::ToggleButton sliceButton   { "" }; // ✓ drawn by LookAndFeel
    juce::TextButton   clearButton   { "X" };

    // RECORD BUTTON (counter)
    juce::TextButton   timeCounter;

    float rms  = 0.0f;
    float peak = 0.0f;
    float gainPosition = 0.5f;
    juce::Rectangle<int> meterBounds;
    juce::Rectangle<int> progressBounds;
    bool adjustingGain = false;

    std::function<void()> deleteModuleHandler;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LiveRecorderModuleView)
};
