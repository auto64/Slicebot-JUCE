#pragma once

#include <JuceHeader.h>
#include <array>
#include "SliceStateStore.h"

class GlobalTabView final : public juce::Component
{
public:
    explicit GlobalTabView (SliceStateStore& stateStoreToUse);
    ~GlobalTabView() override = default;

    void resized() override;
    void paint (juce::Graphics& g) override;

    void applySettingsSnapshot (const SliceStateStore::SliceStateSnapshot& snapshot);

private:
    void updateTransientSetting();
    void updateLayeringSetting();
    void updateMergeModeSetting (SliceStateStore::MergeMode newMode);
    void updateMergeModeButtons (bool isEnabled);
    void selectMergeModeButton (SliceStateStore::MergeMode modeToSelect);
    void configureMergeButton (juce::TextButton& button, SliceStateStore::MergeMode mode);

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
