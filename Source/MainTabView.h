#pragma once

#include <JuceHeader.h>
#include <atomic>
#include "SliceStateStore.h"
#include "BackgroundWorker.h"

class MainTabView final : public juce::Component
{
public:
    explicit MainTabView (SliceStateStore& stateStoreToUse);
    ~MainTabView() override;

    void resized() override;
    void paint (juce::Graphics& g) override;

    void setStatusTextCallback (std::function<void(const juce::String&)> callback);
    void setProgressCallback (std::function<void(float)> callback);
    void setBpmChangedCallback (std::function<void(double)> callback);
    void setProgress (float progress);
    void setLiveModeSelected (bool isLive);

private:
    static constexpr float kFontSize = 11.0f;

    class StyleLookAndFeel final : public juce::LookAndFeel_V4
    {
    public:
        explicit StyleLookAndFeel (float fontSizeToUse);
        juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override;
        juce::Font getLabelFont (juce::Label& label) override;
        void drawButtonBackground (juce::Graphics& g,
                                   juce::Button& button,
                                   const juce::Colour& backgroundColour,
                                   bool isMouseOverButton,
                                   bool isButtonDown) override;
        void drawToggleButton (juce::Graphics& g,
                               juce::ToggleButton& button,
                               bool isMouseOverButton,
                               bool isButtonDown) override;

    private:
        float fontSize;
    };

    void configureSegmentButton (juce::TextButton& button, int groupId);
    void applySettingsSnapshot (const SliceStateStore::SliceStateSnapshot& snapshot);
    void setSubdivisionToggleState (int subdivisionSteps);
    void setSubdivisionFromUi (int subdivisionSteps);
    void updateSliceSettingsFromUi();
    void updateStatusText (const juce::String& text);
    void updateProgress (float progress);
    void updateLiveModeState();
    void updateSourceModeState();
    void setCachingState (bool isCaching);

    StyleLookAndFeel styleLookAndFeel;
    SliceStateStore& stateStore;

    juce::TextButton modeMultiFile { "MULTI-FILE" };
    juce::TextButton modeSingleRandom { "SINGLE FILE (RANDOM)" };
    juce::TextButton modeSingleManual { "SINGLE FILE (MANUAL)" };
    juce::TextButton modeLive { "LIVE" };

    juce::TextButton sourceButton { "SOURCE" };
    juce::Label subdivLabel { "subdivLabel", "SUBDIV" };
    juce::TextButton subdivHalfBar { "1/2 BAR" };
    juce::TextButton subdivQuarterBar { "1/4 BAR" };
    juce::TextButton subdivEighthNote { "8TH NOTE" };
    juce::TextButton subdivSixteenthNote { "16TH NOTE" };
    juce::ToggleButton subdivRandom { "RANDOM" };

    juce::Label bpmLabel { "bpmLabel", "BPM:" };
    juce::Label bpmValue { "bpmValue", "128.0" };
    juce::Label samplesLabel { "samplesLabel", "SAMPLES:" };
    juce::TextButton samplesFour { "4" };
    juce::TextButton samplesEight { "8" };
    juce::TextButton samplesSixteen { "16" };

    std::unique_ptr<juce::FileChooser> sourceChooser;
    std::function<void(const juce::String&)> statusTextCallback;
    std::function<void(float)> progressCallback;
    std::function<void(double)> bpmChangedCallback;
    BackgroundWorker cacheWorker;
    std::atomic<bool> isCaching { false };
    std::atomic<bool> cancelCache { false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainTabView)
};
