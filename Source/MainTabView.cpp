#include "MainTabView.h"
#include "AudioCacheStore.h"
#include <cmath>

namespace
{
    constexpr int kContentWidth = 609;
    constexpr int kRowHeight = 28;
    constexpr int kRowSpacing = 10;
    constexpr int kSectionSpacing = 12;

    juce::Colour backgroundGrey()
    {
        return juce::Colour (0xff444444);
    }

    juce::Colour panelGrey()
    {
        return juce::Colour (0xff5a5a5a);
    }

    juce::Colour borderGrey()
    {
        return juce::Colour (0xff333333);
    }

    juce::Colour textGrey()
    {
        return juce::Colour (0xffcfcfcf);
    }

    juce::Colour accentBlue()
    {
        return juce::Colour (0xff4fa3f7);
    }

}

MainTabView::StyleLookAndFeel::StyleLookAndFeel (float fontSizeToUse)
    : fontSize (fontSizeToUse)
{
}

juce::Font MainTabView::StyleLookAndFeel::getTextButtonFont (juce::TextButton&, int)
{
    return juce::Font (juce::FontOptions ("Helvetica", fontSize, juce::Font::plain));
}

juce::Font MainTabView::StyleLookAndFeel::getLabelFont (juce::Label&)
{
    return juce::Font (juce::FontOptions ("Helvetica", fontSize, juce::Font::plain));
}

void MainTabView::StyleLookAndFeel::drawButtonBackground (juce::Graphics& g,
                                                          juce::Button& button,
                                                          const juce::Colour& backgroundColour,
                                                          bool,
                                                          bool)
{
    const auto bounds = button.getLocalBounds().toFloat();
    const auto baseColour = button.findColour (button.getToggleState()
                                                   ? juce::TextButton::buttonOnColourId
                                                   : juce::TextButton::buttonColourId);
    g.setColour (baseColour);
    g.fillRect (bounds);
    g.setColour (borderGrey());
    g.drawRect (bounds, 1.0f);
}

void MainTabView::StyleLookAndFeel::drawToggleButton (juce::Graphics& g,
                                                      juce::ToggleButton& button,
                                                      bool,
                                                      bool)
{
    const auto bounds = button.getLocalBounds();
    const int boxSize = juce::jmin (14, bounds.getHeight() - 6);
    const auto boxBounds = juce::Rectangle<int> (bounds.getX() + 4,
                                                 bounds.getCentreY() - boxSize / 2,
                                                 boxSize,
                                                 boxSize);

    const auto fillColour = button.getToggleState() ? accentBlue() : panelGrey();
    g.setColour (fillColour);
    g.fillRect (boxBounds);
    g.setColour (borderGrey());
    g.drawRect (boxBounds);

    g.setColour (button.findColour (juce::ToggleButton::textColourId));
    g.setFont (juce::Font (juce::FontOptions ("Helvetica", fontSize, juce::Font::plain)));
    g.drawText (button.getButtonText(),
                bounds.withTrimmedLeft (boxBounds.getRight() + 8),
                juce::Justification::centredLeft,
                false);
}

MainTabView::MainTabView (SliceStateStore& stateStoreToUse)
    : styleLookAndFeel (kFontSize),
      stateStore (stateStoreToUse)
{
    setLookAndFeel (&styleLookAndFeel);
    configureSegmentButton (modeMultiFile, 100);
    configureSegmentButton (modeSingleRandom, 100);
    configureSegmentButton (modeSingleManual, 100);
    configureSegmentButton (modeLive, 100);

    configureSegmentButton (subdivHalfBar, 200);
    configureSegmentButton (subdivQuarterBar, 200);
    configureSegmentButton (subdivEighthNote, 200);
    configureSegmentButton (subdivSixteenthNote, 200);

    configureSegmentButton (samplesFour, 300);
    configureSegmentButton (samplesEight, 300);
    configureSegmentButton (samplesSixteen, 300);

    modeMultiFile.setToggleState (true, juce::dontSendNotification);
    subdivHalfBar.setToggleState (true, juce::dontSendNotification);
    samplesSixteen.setToggleState (true, juce::dontSendNotification);

    for (auto* button : { &modeMultiFile, &modeSingleRandom, &modeSingleManual, &modeLive })
        button->onClick = [this]() { updateSourceModeState(); };

    for (auto* button : { &subdivHalfBar, &subdivQuarterBar, &subdivEighthNote, &subdivSixteenthNote })
        button->onClick = [this]() { updateSliceSettingsFromUi(); };

    for (auto* button : { &samplesFour, &samplesEight, &samplesSixteen })
        button->onClick = [this]() { updateSliceSettingsFromUi(); };

    subdivRandom.onClick = [this]()
    {
        stateStore.setRandomSubdivisionEnabled (subdivRandom.getToggleState());
    };

    sourceButton.onClick = [this]()
    {
        if (isCaching.load())
        {
            cancelCache.store (true);
            updateStatusText ("Stopping cache...");
            return;
        }

        const bool isManualSingle = modeSingleManual.getToggleState();
        const auto chooserTitle = isManualSingle ? "Select Source File" : "Select Source Folder";
        sourceChooser = std::make_unique<juce::FileChooser> (
            chooserTitle,
            juce::File(),
            "*");
        const int flags = juce::FileBrowserComponent::openMode
                          | (isManualSingle ? juce::FileBrowserComponent::canSelectFiles
                                            : juce::FileBrowserComponent::canSelectDirectories);
        sourceChooser->launchAsync (flags, [this, isManualSingle] (const juce::FileChooser& chooser)
        {
            const auto selectedItem = chooser.getResult();
            if (! selectedItem.exists())
                return;

            if (isManualSingle)
                stateStore.setSourceFile (selectedItem);
            else
                stateStore.setSourceDirectory (selectedItem);

            cancelCache.store (false);
            setCachingState (true);
            updateStatusText ("Recaching input directory...");
            updateProgress (0.0f);

            cacheWorker.enqueue ([this, selectedItem, isManualSingle]()
            {
                bool wasCancelled = false;
                const double bpm = stateStore.getSnapshot().bpm;
                const auto cacheData = AudioCacheStore::buildFromSource (
                    selectedItem,
                    ! isManualSingle,
                    bpm,
                    &cancelCache,
                    [this] (int current, int total)
                    {
                        const bool hasTotal = total > 0;
                        const float progress = hasTotal
                                                   ? static_cast<float> (current) / static_cast<float> (total)
                                                   : 0.0f;
                        juce::MessageManager::callAsync ([this, current, total, progress, hasTotal]()
                        {
                            if (hasTotal)
                                updateStatusText ("Recaching: " + juce::String (current) + " of " + juce::String (total) + " files processed.");
                            else
                                updateStatusText ("Recaching: " + juce::String (current) + " files processed.");
                            updateProgress (progress);
                        });
                    },
                    &wasCancelled);

                juce::MessageManager::callAsync ([this, cacheData, wasCancelled]()
                {
                    stateStore.setCacheData (cacheData);
                    if (wasCancelled)
                    {
                        updateStatusText ("Recache cancelled. Cached " + juce::String (cacheData.entries.size()) + " files so far.");
                    }
                    else
                    {
                        AudioCacheStore::save (cacheData);
                        updateStatusText ("Recached " + juce::String (cacheData.entries.size()) + " audio files.");
                    }
                    updateProgress (1.0f);
                    setCachingState (false);
                });
            });
        });
    };

    bpmValue.setEditable (true);
    bpmValue.setColour (juce::Label::backgroundColourId, backgroundGrey());
    bpmValue.setColour (juce::Label::outlineColourId, borderGrey());
    bpmValue.setColour (juce::Label::textColourId, juce::Colours::white);
    bpmValue.setJustificationType (juce::Justification::centred);
    bpmValue.onTextChange = [this]()
    {
        updateSliceSettingsFromUi();
    };

    addAndMakeVisible (modeMultiFile);
    addAndMakeVisible (modeSingleRandom);
    addAndMakeVisible (modeSingleManual);
    addAndMakeVisible (modeLive);

    addAndMakeVisible (subdivLabel);
    addAndMakeVisible (subdivHalfBar);
    addAndMakeVisible (subdivQuarterBar);
    addAndMakeVisible (subdivEighthNote);
    addAndMakeVisible (subdivSixteenthNote);
    addAndMakeVisible (subdivRandom);

    addAndMakeVisible (sourceButton);
    addAndMakeVisible (bpmLabel);
    addAndMakeVisible (bpmValue);
    addAndMakeVisible (samplesLabel);
    addAndMakeVisible (samplesFour);
    addAndMakeVisible (samplesEight);
    addAndMakeVisible (samplesSixteen);

    applySettingsSnapshot (stateStore.getSnapshot());
    setCachingState (stateStore.getSnapshot().isCaching);
    updateSourceModeState();
}

MainTabView::~MainTabView()
{
    setLookAndFeel (nullptr);
}

void MainTabView::paint (juce::Graphics& g)
{
    g.fillAll (backgroundGrey());
}

void MainTabView::resized()
{
    const int availableWidth = getWidth();
    const int contentWidth = juce::jmin (kContentWidth, availableWidth - 32);
    int x = (availableWidth - contentWidth) / 2;
    int y = 12;

    auto rowBounds = juce::Rectangle<int> (x, y, contentWidth, kRowHeight);
    const int modeEdgeWidth = 120;
    const int modeMiddleWidth = (rowBounds.getWidth() - modeEdgeWidth * 2) / 2;
    modeMultiFile.setBounds (rowBounds.removeFromLeft (modeEdgeWidth));
    modeSingleRandom.setBounds (rowBounds.removeFromLeft (modeMiddleWidth));
    modeSingleManual.setBounds (rowBounds.removeFromLeft (modeMiddleWidth));
    modeLive.setBounds (rowBounds);

    y += kRowHeight + kRowSpacing;
    rowBounds = juce::Rectangle<int> (x, y, contentWidth, kRowHeight);
    subdivLabel.setBounds (rowBounds.removeFromLeft (65));
    const int randomWidth = 70;
    const int subdivSegmentWidth = (rowBounds.getWidth() - randomWidth) / 4;
    subdivHalfBar.setBounds (rowBounds.removeFromLeft (subdivSegmentWidth));
    subdivQuarterBar.setBounds (rowBounds.removeFromLeft (subdivSegmentWidth));
    subdivEighthNote.setBounds (rowBounds.removeFromLeft (subdivSegmentWidth));
    subdivSixteenthNote.setBounds (rowBounds.removeFromLeft (subdivSegmentWidth));
    subdivRandom.setBounds (rowBounds.removeFromLeft (randomWidth));

    y += kRowHeight + kRowSpacing;
    rowBounds = juce::Rectangle<int> (x, y, contentWidth, kRowHeight);
    const int spacing = 10;
    const int sourceWidth = 80;
    const int bpmLabelWidth = 40;
    const int bpmValueWidth = 50;
    const int samplesLabelWidth = 80;
    const int sampleSegmentWidth =
        (rowBounds.getWidth()
         - sourceWidth
         - bpmLabelWidth
         - bpmValueWidth
         - samplesLabelWidth
         - spacing * 4)
        / 3;
    sourceButton.setBounds (rowBounds.removeFromLeft (sourceWidth));
    rowBounds.removeFromLeft (spacing);
    bpmLabel.setBounds (rowBounds.removeFromLeft (bpmLabelWidth));
    bpmValue.setBounds (rowBounds.removeFromLeft (bpmValueWidth));
    rowBounds.removeFromLeft (spacing);
    samplesLabel.setBounds (rowBounds.removeFromLeft (samplesLabelWidth));
    rowBounds.removeFromLeft (spacing);
    samplesFour.setBounds (rowBounds.removeFromLeft (sampleSegmentWidth));
    samplesEight.setBounds (rowBounds.removeFromLeft (sampleSegmentWidth));
    samplesSixteen.setBounds (rowBounds);

    y += kRowHeight + kSectionSpacing;
}

void MainTabView::configureSegmentButton (juce::TextButton& button, int groupId)
{
    button.setClickingTogglesState (true);
    button.setRadioGroupId (groupId);
    button.setColour (juce::TextButton::buttonColourId, panelGrey());
    button.setColour (juce::TextButton::buttonOnColourId, accentBlue());
    button.setColour (juce::TextButton::textColourOffId, textGrey());
    button.setColour (juce::TextButton::textColourOnId, juce::Colours::white);
}

void MainTabView::applySettingsSnapshot (const SliceStateStore::SliceStateSnapshot& snapshot)
{
    bpmValue.setText (juce::String (snapshot.bpm, 1), juce::dontSendNotification);

    subdivHalfBar.setToggleState (snapshot.subdivisionSteps == 8, juce::dontSendNotification);
    subdivQuarterBar.setToggleState (snapshot.subdivisionSteps == 4, juce::dontSendNotification);
    subdivEighthNote.setToggleState (snapshot.subdivisionSteps == 2, juce::dontSendNotification);
    subdivSixteenthNote.setToggleState (snapshot.subdivisionSteps == 1, juce::dontSendNotification);

    subdivRandom.setToggleState (snapshot.randomSubdivisionEnabled, juce::dontSendNotification);

    samplesFour.setToggleState (snapshot.sampleCountSetting == 4, juce::dontSendNotification);
    samplesEight.setToggleState (snapshot.sampleCountSetting == 8, juce::dontSendNotification);
    samplesSixteen.setToggleState (snapshot.sampleCountSetting == 16, juce::dontSendNotification);

    switch (snapshot.sourceMode)
    {
        case SliceStateStore::SourceMode::multi:
            modeMultiFile.setToggleState (true, juce::dontSendNotification);
            break;
        case SliceStateStore::SourceMode::singleRandom:
            modeSingleRandom.setToggleState (true, juce::dontSendNotification);
            break;
        case SliceStateStore::SourceMode::singleManual:
            modeSingleManual.setToggleState (true, juce::dontSendNotification);
            break;
        case SliceStateStore::SourceMode::live:
            modeLive.setToggleState (true, juce::dontSendNotification);
            break;
    }
}

void MainTabView::updateSliceSettingsFromUi()
{
    const auto snapshot = stateStore.getSnapshot();
    const double newBpm = bpmValue.getText().getDoubleValue();

    int subdivision = snapshot.subdivisionSteps;
    if (subdivHalfBar.getToggleState())
        subdivision = 8;
    else if (subdivQuarterBar.getToggleState())
        subdivision = 4;
    else if (subdivEighthNote.getToggleState())
        subdivision = 2;
    else if (subdivSixteenthNote.getToggleState())
        subdivision = 1;

    int samples = snapshot.sampleCountSetting;
    if (samplesFour.getToggleState())
        samples = 4;
    else if (samplesEight.getToggleState())
        samples = 8;
    else if (samplesSixteen.getToggleState())
        samples = 16;

    const double safeBpm = newBpm > 0.0 ? newBpm : snapshot.bpm;

    stateStore.setSliceSettings (safeBpm,
                                 subdivision,
                                 samples,
                                 snapshot.transientDetectionEnabled);
    bpmValue.setText (juce::String (safeBpm, 1), juce::dontSendNotification);

    if (bpmChangedCallback)
        bpmChangedCallback (safeBpm);
}

void MainTabView::updateStatusText (const juce::String& text)
{
    if (statusTextCallback)
        statusTextCallback (text);
}

void MainTabView::updateProgress (float progress)
{
    if (progressCallback)
        progressCallback (progress);
}

void MainTabView::updateLiveModeState()
{
    const bool isLive = modeLive.getToggleState();
    sourceButton.setVisible (! isLive);
}

void MainTabView::updateSourceModeState()
{
    if (modeMultiFile.getToggleState())
    {
        stateStore.setSourceMode (SliceStateStore::SourceMode::multi);
    }
    else if (modeSingleRandom.getToggleState())
    {
        stateStore.setSourceMode (SliceStateStore::SourceMode::singleRandom);
    }
    else if (modeSingleManual.getToggleState())
    {
        stateStore.setSourceMode (SliceStateStore::SourceMode::singleManual);
    }
    else if (modeLive.getToggleState())
    {
        stateStore.setSourceMode (SliceStateStore::SourceMode::live);
    }

    updateLiveModeState();
}

void MainTabView::setCachingState (bool cachingState)
{
    isCaching.store (cachingState);
    stateStore.setCaching (cachingState);

    const bool enabled = ! cachingState;
    modeMultiFile.setEnabled (enabled);
    modeSingleRandom.setEnabled (enabled);
    modeSingleManual.setEnabled (enabled);
    modeLive.setEnabled (enabled);
    subdivHalfBar.setEnabled (enabled);
    subdivQuarterBar.setEnabled (enabled);
    subdivEighthNote.setEnabled (enabled);
    subdivSixteenthNote.setEnabled (enabled);
    subdivRandom.setEnabled (enabled);
    bpmValue.setEnabled (enabled);
    samplesFour.setEnabled (enabled);
    samplesEight.setEnabled (enabled);
    samplesSixteen.setEnabled (enabled);

    sourceButton.setButtonText (cachingState ? "Stop Cache" : "Source");
}

void MainTabView::setLiveModeSelected (bool isLive)
{
    modeLive.setToggleState (isLive, juce::sendNotification);
    updateSourceModeState();
}

void MainTabView::setProgress (float progress)
{
    if (progressCallback)
        progressCallback (progress);
}

void MainTabView::setStatusTextCallback (std::function<void(const juce::String&)> callback)
{
    statusTextCallback = std::move (callback);
}

void MainTabView::setProgressCallback (std::function<void(float)> callback)
{
    progressCallback = std::move (callback);
}

void MainTabView::setBpmChangedCallback (std::function<void(double)> callback)
{
    bpmChangedCallback = std::move (callback);
}
