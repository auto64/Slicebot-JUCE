#pragma once

#include <JuceHeader.h>
#include <array>
#include "SliceStateStore.h"

class GlobalTabView final : public juce::Component
{
public:
    explicit GlobalTabView (SliceStateStore& stateStoreToUse);
    ~GlobalTabView() override;

    void resized() override;
    void paint (juce::Graphics& g) override;

    void applySettingsSnapshot (const SliceStateStore::SliceStateSnapshot& snapshot);

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

    void updateTransientSetting();
    void updateLayeringSetting();
    void updateMergeModeSetting (SliceStateStore::MergeMode newMode);
    void updateMergeModeButtons (bool isEnabled);
    void selectMergeModeButton (SliceStateStore::MergeMode modeToSelect);
    void configureMergeButton (juce::TextButton& button, SliceStateStore::MergeMode mode);

    StyleLookAndFeel styleLookAndFeel { kFontSize };
    SliceStateStore& stateStore;
    juce::ToggleButton transientToggle { "TRANSIENT DETECT" };
    juce::ToggleButton layeringToggle { "LAYERING" };
    juce::Label mergeLabel { "mergeLabel", "MERGE MODE:" };
    juce::TextButton mergeNone { "NONE" };
    juce::TextButton mergeFiftyFifty { "50/50" };
    juce::TextButton mergeQuarterCuts { "QUARTER" };
    juce::TextButton mergeCrossfade { "XFADE" };
    juce::TextButton mergeCrossfadeReverse { "XFADE REV" };
    juce::TextButton mergePachinko { "PACHINKO" };
    std::array<juce::TextButton*, 6> mergeButtons {};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GlobalTabView)
};
