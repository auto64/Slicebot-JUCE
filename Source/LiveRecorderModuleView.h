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

    // called when audio device / active inputs change
    void refreshInputChannels();

    void setDeleteModuleHandler (std::function<void()> handler);

private:
    class DeleteButton final : public juce::TextButton
    {
    public:
        using juce::TextButton::TextButton;

        void setSingleClickHandler (std::function<void()> handler);
        void setDoubleClickHandler (std::function<void()> handler);

    protected:
        void mouseUp (const juce::MouseEvent& event) override;

    private:
        std::function<void()> singleClickHandler;
        std::function<void()> doubleClickHandler;
    };

    // callbacks
    void buttonClicked (juce::Button*) override;
    void comboBoxChanged (juce::ComboBox*) override;
    void timerCallback() override;

    void showUnderMinWarning();
    void showClearWarning();
    void applyPersistedControlState();
    void handleDeleteModule();

    // state
    AudioEngine& audioEngine;
    const int recorderIndex;

    bool   isRecording = false;
    bool   stopDialogOpen = false;
    double lastRecordedSeconds = 0.0;

    // 🔴 REQUIRED for flashing (was accidentally dropped)
    double flashPhase = 0.0;

    // Look & Feel (tiles)
    FlatTileLookAndFeel flatTiles;

    // UI
    juce::ToggleButton midiArmButton { "MIDI ARM" };
    juce::ComboBox     channelBox;

    juce::ToggleButton monitorButton { "I" };
    juce::ToggleButton linkButton    { "L" };
    juce::ToggleButton sliceButton   { "" }; // ✓ drawn by LookAndFeel
    DeleteButton       clearButton   { "X" };

    // RECORD BUTTON (counter)
    juce::TextButton   timeCounter;

    float rms  = 0.0f;
    float peak = 0.0f;

    std::function<void()> deleteModuleHandler;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LiveRecorderModuleView)
};
