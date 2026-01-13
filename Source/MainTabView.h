#pragma once

#include <JuceHeader.h>

class MainTabView final : public juce::Component
{
public:
    MainTabView();
    ~MainTabView() override = default;

    void resized() override;
    void paint (juce::Graphics& g) override;

private:
    void configureSegmentButton (juce::TextButton& button, int groupId);
    void updateLiveModeState();

    juce::TextButton modeMultiFile { "Multi-file" };
    juce::TextButton modeSingleRandom { "Single file (Random)" };
    juce::TextButton modeSingleManual { "Single file (Manual)" };
    juce::TextButton modeLive { "LIVE" };

    juce::TextButton sourceButton { "Source" };
    juce::Label subdivLabel { "subdivLabel", "Subdiv" };
    juce::TextButton subdivHalfBar { "1/2 bar" };
    juce::TextButton subdivQuarterBar { "1/4 bar" };
    juce::TextButton subdivEighthNote { "8th note" };
    juce::TextButton subdivSixteenthNote { "16th note" };
    juce::ToggleButton subdivRandom { "random" };

    juce::Label bpmLabel { "bpmLabel", "BPM:" };
    juce::Label bpmValue { "bpmValue", "128.0" };
    juce::Label samplesLabel { "samplesLabel", "Samples:" };
    juce::TextButton samplesFour { "4" };
    juce::TextButton samplesEight { "8" };
    juce::TextButton samplesSixteen { "16" };

    std::unique_ptr<juce::Component> focusView;
    std::unique_ptr<juce::Component> previewGrid;
    std::unique_ptr<juce::Component> actionBar;
    std::unique_ptr<juce::Component> statusArea;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainTabView)
};
