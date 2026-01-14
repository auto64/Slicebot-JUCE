#include "GlobalTabView.h"

namespace
{
    juce::Colour backgroundGrey()
    {
        return juce::Colour (0xff444444);
    }

    juce::Colour borderGrey()
    {
        return juce::Colour (0xff333333);
    }

    juce::Colour panelGrey()
    {
        return juce::Colour (0xff5a5a5a);
    }

    juce::Colour textGrey()
    {
        return juce::Colour (0xffcfcfcf);
    }

    juce::Colour accentBlue()
    {
        return juce::Colour (0xff4fa3f7);
    }

    constexpr int kRowHeight = 28;
    constexpr int kRowSpacing = 8;
    constexpr int kMergeModeGroup = 7101;
}

GlobalTabView::GlobalTabView (SliceStateStore& stateStoreToUse)
    : stateStore (stateStoreToUse)
{
    transientToggle.setColour (juce::ToggleButton::textColourId, textGrey());
    transientToggle.onClick = [this]() { updateTransientSetting(); };
    addAndMakeVisible (transientToggle);

    layeringToggle.setColour (juce::ToggleButton::textColourId, textGrey());
    layeringToggle.onClick = [this]() { updateLayeringSetting(); };
    addAndMakeVisible (layeringToggle);

    mergeLabel.setColour (juce::Label::textColourId, textGrey());
    addAndMakeVisible (mergeLabel);

    mergeButtons = { &mergeNone,
                     &mergeFiftyFifty,
                     &mergeQuarterCuts,
                     &mergeCrossfade,
                     &mergeCrossfadeReverse,
                     &mergePachinko };

    configureMergeButton (mergeNone, SliceStateStore::MergeMode::none);
    configureMergeButton (mergeFiftyFifty, SliceStateStore::MergeMode::fiftyFifty);
    configureMergeButton (mergeQuarterCuts, SliceStateStore::MergeMode::quarterCuts);
    configureMergeButton (mergeCrossfade, SliceStateStore::MergeMode::crossfade);
    configureMergeButton (mergeCrossfadeReverse, SliceStateStore::MergeMode::crossfadeReverse);
    configureMergeButton (mergePachinko, SliceStateStore::MergeMode::pachinko);

    applySettingsSnapshot (stateStore.getSnapshot());
}

void GlobalTabView::paint (juce::Graphics& g)
{
    g.fillAll (backgroundGrey());
    g.setColour (borderGrey());
    g.drawRect (getLocalBounds(), 1);
}

void GlobalTabView::resized()
{
    auto bounds = getLocalBounds().reduced (12);
    auto topRow = bounds.removeFromTop (kRowHeight);
    const int transientWidth = 160;
    transientToggle.setBounds (topRow.removeFromLeft (transientWidth));
    topRow.removeFromLeft (kRowSpacing);
    const int layeringWidth = 110;
    layeringToggle.setBounds (topRow.removeFromLeft (layeringWidth));

    bounds.removeFromTop (kRowSpacing);
    auto mergeRow = bounds.removeFromTop (kRowHeight);
    mergeLabel.setBounds (mergeRow.removeFromLeft (110));
    mergeRow.removeFromLeft (kRowSpacing);

    for (auto* button : mergeButtons)
    {
        const int buttonWidth = juce::jmax (70, button->getBestWidthForHeight (kRowHeight));
        button->setBounds (mergeRow.removeFromLeft (buttonWidth));
        mergeRow.removeFromLeft (4);
    }
}

void GlobalTabView::applySettingsSnapshot (const SliceStateStore::SliceStateSnapshot& snapshot)
{
    transientToggle.setToggleState (snapshot.transientDetectionEnabled, juce::dontSendNotification);
    layeringToggle.setToggleState (snapshot.layeringMode, juce::dontSendNotification);
    selectMergeModeButton (snapshot.mergeMode);
    updateMergeModeButtons (snapshot.layeringMode);
}

void GlobalTabView::updateTransientSetting()
{
    const auto snapshot = stateStore.getSnapshot();
    stateStore.setSliceSettings (snapshot.bpm,
                                 snapshot.subdivisionSteps,
                                 snapshot.sampleCountSetting,
                                 transientToggle.getToggleState());
}

void GlobalTabView::updateLayeringSetting()
{
    const auto snapshot = stateStore.getSnapshot();
    const bool isLayering = layeringToggle.getToggleState();
    stateStore.setLayeringState (isLayering, snapshot.sampleCountSetting);
    updateMergeModeButtons (isLayering);
}

void GlobalTabView::updateMergeModeSetting (SliceStateStore::MergeMode newMode)
{
    stateStore.setMergeMode (newMode);
}

void GlobalTabView::updateMergeModeButtons (bool isEnabled)
{
    for (auto* button : mergeButtons)
        button->setEnabled (isEnabled);
}

void GlobalTabView::selectMergeModeButton (SliceStateStore::MergeMode modeToSelect)
{
    for (auto* button : mergeButtons)
        button->setToggleState (false, juce::dontSendNotification);

    switch (modeToSelect)
    {
        case SliceStateStore::MergeMode::none:
            mergeNone.setToggleState (true, juce::dontSendNotification);
            break;
        case SliceStateStore::MergeMode::fiftyFifty:
            mergeFiftyFifty.setToggleState (true, juce::dontSendNotification);
            break;
        case SliceStateStore::MergeMode::quarterCuts:
            mergeQuarterCuts.setToggleState (true, juce::dontSendNotification);
            break;
        case SliceStateStore::MergeMode::crossfade:
            mergeCrossfade.setToggleState (true, juce::dontSendNotification);
            break;
        case SliceStateStore::MergeMode::crossfadeReverse:
            mergeCrossfadeReverse.setToggleState (true, juce::dontSendNotification);
            break;
        case SliceStateStore::MergeMode::pachinko:
            mergePachinko.setToggleState (true, juce::dontSendNotification);
            break;
        default:
            break;
    }
}

void GlobalTabView::configureMergeButton (juce::TextButton& button, SliceStateStore::MergeMode mode)
{
    button.setClickingTogglesState (true);
    button.setRadioGroupId (kMergeModeGroup);
    button.setColour (juce::TextButton::buttonColourId, panelGrey());
    button.setColour (juce::TextButton::buttonOnColourId, accentBlue());
    button.setColour (juce::TextButton::textColourOffId, textGrey());
    button.setColour (juce::TextButton::textColourOnId, juce::Colours::white);
    button.onClick = [this, mode, &button]()
    {
        if (button.getToggleState())
            updateMergeModeSetting (mode);
    };
    addAndMakeVisible (button);
}
