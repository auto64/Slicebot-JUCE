#pragma once

#include <JuceHeader.h>
#include "SliceStateStore.h"

class MainTabView final : public juce::Component
{
public:
    explicit MainTabView (SliceStateStore& stateStoreToUse);
    ~MainTabView() override;

    void resized() override;
    void paint (juce::Graphics& g) override;

    void setProgress (float progress);

private:
    static constexpr float kFontSize = 13.0f;

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
    void updateSourcePathLabel (const SliceStateStore::SliceStateSnapshot& snapshot);
    void updateStatusText (const juce::String& text);
    void updateProgress (float progress);
    void updateLiveModeState();

    StyleLookAndFeel styleLookAndFeel;
    SliceStateStore& stateStore;

    juce::TextButton modeMultiFile { "MULTI-FILE" };
    juce::TextButton modeSingleRandom { "SINGLE FILE (RANDOM)" };
    juce::TextButton modeSingleManual { "SINGLE FILE (MANUAL)" };
    juce::TextButton modeLive { "LIVE" };

    juce::TextButton sourceButton { "SOURCE" };
    juce::Label sourcePathLabel { "sourcePathLabel", "No source selected" };
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

    std::unique_ptr<juce::Component> focusView;
    std::unique_ptr<juce::Component> previewGrid;
    std::unique_ptr<juce::Component> actionBar;
    std::unique_ptr<juce::Component> statusArea;
    std::unique_ptr<juce::FileChooser> sourceChooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainTabView)
};
