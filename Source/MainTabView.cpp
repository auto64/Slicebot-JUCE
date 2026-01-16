#include "MainTabView.h"
#include "AudioCacheStore.h"
#include <cmath>

namespace
{
    constexpr int kContentWidth = 609;
    constexpr int kRowHeight = 28;
    constexpr int kRowSpacing = 10;
    constexpr int kSectionSpacing = 12;
    constexpr int kFocusHeight = 96;
    constexpr int kGridCellWidth = 150;
    constexpr int kGridCellHeight = 64;
    constexpr int kGridSpacing = 3;
    constexpr int kGridColumns = 4;
    constexpr int kGridRows = 4;
    constexpr int kActionBarHeight = 28;
    constexpr int kStatusHeight = 24;
    constexpr int kSideColumnWidth = 24;

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

    class WaveformArea final : public juce::Component
    {
    public:
        void paint (juce::Graphics& g) override
        {
            g.fillAll (panelGrey());
        }
    };

    class FocusWaveformView final : public juce::Component
    {
    public:
        FocusWaveformView()
        {
            const juce::String labels[] = { "S", "W", "R", "C", "P" };
            for (const auto& label : labels)
            {
                auto* button = leftButtons.add (new juce::TextButton (label));
                configureSmallButton (*button);
                addAndMakeVisible (*button);
            }

            configureSmallButton (muteButton);
            muteButton.setButtonText ("M");

            addAndMakeVisible (muteButton);
            addAndMakeVisible (waveformArea);
        }

        void paint (juce::Graphics& g) override
        {
            g.fillAll (backgroundGrey());
        }

        void resized() override
        {
            auto bounds = getLocalBounds();
            auto leftColumn = bounds.removeFromLeft (kSideColumnWidth);
            auto rightColumn = bounds.removeFromRight (kSideColumnWidth);

            const int buttonHeight = leftColumn.getHeight() / leftButtons.size();
            for (int i = 0; i < leftButtons.size(); ++i)
            {
                leftButtons[i]->setBounds (leftColumn.removeFromTop (buttonHeight));
            }

            const int muteSize = kSideColumnWidth;
            muteButton.setBounds (
                rightColumn.getX(),
                rightColumn.getBottom() - muteSize,
                muteSize,
                muteSize
            );

            waveformArea.setBounds (bounds);
        }

    private:
        void configureSmallButton (juce::TextButton& button)
        {
            button.setColour (juce::TextButton::buttonColourId, panelGrey());
            button.setColour (juce::TextButton::buttonOnColourId, accentBlue());
            button.setColour (juce::TextButton::textColourOffId, textGrey());
            button.setColour (juce::TextButton::textColourOnId, juce::Colours::white);
        }

        juce::OwnedArray<juce::TextButton> leftButtons;
        juce::TextButton muteButton;
        WaveformArea waveformArea;
    };

    class GridCell final : public juce::Component
    {
    public:
        void paint (juce::Graphics& g) override
        {
            g.fillAll (backgroundGrey());
        }
    };

    class PreviewGrid final : public juce::Component
    {
    public:
        PreviewGrid()
        {
            for (int index = 0; index < kGridColumns * kGridRows; ++index)
            {
                auto cell = std::make_unique<GridCell>();
                addAndMakeVisible (*cell);
                cells.add (std::move (cell));
            }
        }

        void paint (juce::Graphics& g) override
        {
            g.fillAll (panelGrey());
        }

        void resized() override
        {
            for (int row = 0; row < kGridRows; ++row)
            {
                for (int col = 0; col < kGridColumns; ++col)
                {
                    const int index = row * kGridColumns + col;
                    const int x = col * (kGridCellWidth + kGridSpacing);
                    const int y = row * (kGridCellHeight + kGridSpacing);
                    cells[index]->setBounds (x, y, kGridCellWidth, kGridCellHeight);
                }
            }
        }

    private:
        juce::OwnedArray<GridCell> cells;
    };

    class ActionBar final : public juce::Component
    {
    public:
        ActionBar()
        {
            configureButton (sliceAllButton, "SLICE ALL");
            configureButton (modAllButton, "MOD ALL");
            configureButton (jumbleAllButton, "JUMBLE ALL");
            configureButton (resliceAllButton, "RESLICE ALL");
            configureButton (exportButton, "EXPORT");
            configureButton (lockButton, "🔒");
            configureButton (loopButton, "LOOP");

            loopButton.setClickingTogglesState (true);

            buttons.add (&sliceAllButton);
            buttons.add (&modAllButton);
            buttons.add (&jumbleAllButton);
            buttons.add (&resliceAllButton);
            buttons.add (&exportButton);
            buttons.add (&lockButton);
            buttons.add (&loopButton);

            for (auto* button : buttons)
            {
                button->setLookAndFeel (&compactLookAndFeel);
                addAndMakeVisible (button);
            }
        }

        ~ActionBar() override
        {
            for (auto* button : buttons)
                button->setLookAndFeel (nullptr);
        }

        void setLoopHandler (std::function<void(bool)> handler)
        {
            loopButton.onClick = [this, handler = std::move (handler)]()
            {
                handler (loopButton.getToggleState());
            };
        }

        void setLoopState (bool isEnabled)
        {
            loopButton.setToggleState (isEnabled, juce::dontSendNotification);
        }

        void resized() override
        {
            auto bounds = getLocalBounds();
            const int spacing = 5;
            layoutButtons (bounds, spacing);
        }

    private:
        class CompactLookAndFeel final : public juce::LookAndFeel_V4
        {
        public:
            juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override
            {
                return juce::Font (juce::FontOptions ("Helvetica",
                                                      juce::jmin (11.0f, buttonHeight * 0.5f),
                                                      juce::Font::plain));
            }

            void drawButtonBackground (juce::Graphics& g,
                                       juce::Button& button,
                                       const juce::Colour&,
                                       bool,
                                       bool) override
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

            void drawButtonText (juce::Graphics& g,
                                 juce::TextButton& button,
                                 bool,
                                 bool) override
            {
                g.setFont (getTextButtonFont (button, button.getHeight()));
                g.setColour (button.findColour (button.getToggleState()
                                                    ? juce::TextButton::textColourOnId
                                                    : juce::TextButton::textColourOffId));
                const auto textBounds = button.getLocalBounds().reduced (2, 1);
                g.drawFittedText (button.getButtonText(),
                                  textBounds,
                                  juce::Justification::centred,
                                  1);
            }
        };

        void layoutButtons (juce::Rectangle<int> bounds, int spacing)
        {
            const int buttonCount = buttons.size();
            if (buttonCount == 0)
                return;

            const int availableWidth = bounds.getWidth() - spacing * (buttonCount - 1);
            int totalBestWidth = 0;
            for (auto* button : buttons)
                totalBestWidth += button->getBestWidthForHeight (bounds.getHeight());

            const float scale = totalBestWidth > 0
                                    ? juce::jmin (1.0f, static_cast<float> (availableWidth) / static_cast<float> (totalBestWidth))
                                    : 1.0f;

            for (int index = 0; index < buttonCount; ++index)
            {
                auto* button = buttons[index];
                const int bestWidth = button->getBestWidthForHeight (bounds.getHeight());
                int width = static_cast<int> (std::floor (bestWidth * scale));
                if (index == buttonCount - 1)
                    width = bounds.getWidth();
                button->setBounds (bounds.removeFromLeft (width));
                if (index < buttonCount - 1)
                    bounds.removeFromLeft (spacing);
            }
        }

        void configureButton (juce::TextButton& button, const juce::String& text)
        {
            button.setButtonText (text);
            button.setColour (juce::TextButton::buttonColourId, panelGrey());
            button.setColour (juce::TextButton::buttonOnColourId, accentBlue());
            button.setColour (juce::TextButton::textColourOffId, textGrey());
            button.setColour (juce::TextButton::textColourOnId, juce::Colours::white);
        }

        CompactLookAndFeel compactLookAndFeel;
        juce::TextButton sliceAllButton;
        juce::TextButton modAllButton;
        juce::TextButton jumbleAllButton;
        juce::TextButton resliceAllButton;
        juce::TextButton exportButton;
        juce::TextButton lockButton;
        juce::TextButton loopButton;
        juce::Array<juce::TextButton*> buttons;
    };

    class StatusArea final : public juce::Component
    {
    public:
        StatusArea()
        {
            statusLabel.setText ("PREVIEW GENERATED.", juce::dontSendNotification);
            statusLabel.setJustificationType (juce::Justification::centred);
            statusLabel.setColour (juce::Label::textColourId, textGrey());
            addAndMakeVisible (statusLabel);
        }

        void setStatusText (const juce::String& text)
        {
            statusLabel.setText (text, juce::dontSendNotification);
        }

        void setProgress (float progress)
        {
            progressValue = juce::jlimit (0.0f, 1.0f, progress);
            repaint();
        }

        void paint (juce::Graphics& g) override
        {
            g.fillAll (backgroundGrey());
            g.setColour (juce::Colour (0xffd9534f));
            const float width = static_cast<float> (getWidth());
            const float progressWidth = width * progressValue;
            g.drawLine (0.0f, 0.0f, progressWidth, 0.0f, 1.0f);
        }

        void resized() override
        {
            statusLabel.setBounds (0, 4, getWidth(), getHeight() - 4);
        }

    private:
        juce::Label statusLabel;
        float progressValue = 0.0f;
    };
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
                        if (total <= 0)
                            return;
                        const float progress = static_cast<float> (current) / static_cast<float> (total);
                        juce::MessageManager::callAsync ([this, current, total, progress]()
                        {
                            updateStatusText ("Recaching: " + juce::String (current) + " of " + juce::String (total) + " files processed.");
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

    subdivLabel.setColour (juce::Label::textColourId, textGrey());
    bpmLabel.setColour (juce::Label::textColourId, textGrey());
    samplesLabel.setColour (juce::Label::textColourId, textGrey());
    subdivLabel.setJustificationType (juce::Justification::centredLeft);
    bpmLabel.setJustificationType (juce::Justification::centredLeft);
    samplesLabel.setJustificationType (juce::Justification::centredLeft);

    addAndMakeVisible (modeMultiFile);
    addAndMakeVisible (modeSingleRandom);
    addAndMakeVisible (modeSingleManual);
    addAndMakeVisible (modeLive);

    addAndMakeVisible (sourceButton);
    addAndMakeVisible (subdivLabel);
    addAndMakeVisible (subdivHalfBar);
    addAndMakeVisible (subdivQuarterBar);
    addAndMakeVisible (subdivEighthNote);
    addAndMakeVisible (subdivSixteenthNote);
    addAndMakeVisible (subdivRandom);

    subdivRandom.setColour (juce::ToggleButton::textColourId, textGrey());

    addAndMakeVisible (bpmLabel);
    addAndMakeVisible (bpmValue);
    addAndMakeVisible (samplesLabel);
    addAndMakeVisible (samplesFour);
    addAndMakeVisible (samplesEight);
    addAndMakeVisible (samplesSixteen);

    focusView = std::make_unique<FocusWaveformView>();
    previewGrid = std::make_unique<PreviewGrid>();
    actionBar = std::make_unique<ActionBar>();
    statusArea = std::make_unique<StatusArea>();

    addAndMakeVisible (*focusView);
    addAndMakeVisible (*previewGrid);
    addAndMakeVisible (*actionBar);
    addAndMakeVisible (*statusArea);

    if (auto* bar = dynamic_cast<ActionBar*> (actionBar.get()))
        juce::ignoreUnused (bar);

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

    focusView->setBounds (x, y, contentWidth, kFocusHeight);
    y += kFocusHeight + kSectionSpacing;

    const int gridHeight = kGridRows * kGridCellHeight + (kGridRows - 1) * kGridSpacing;
    previewGrid->setBounds (x, y, contentWidth, gridHeight);
    y += gridHeight + kSectionSpacing;

    actionBar->setBounds (x, y, contentWidth, kActionBarHeight);
    y += kActionBarHeight + 8;

    statusArea->setBounds (x, y, contentWidth, kStatusHeight);
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
    if (auto* status = dynamic_cast<StatusArea*> (statusArea.get()))
        status->setStatusText (text);
    if (statusTextCallback)
        statusTextCallback (text);
}

void MainTabView::updateProgress (float progress)
{
    if (auto* status = dynamic_cast<StatusArea*> (statusArea.get()))
        status->setProgress (progress);
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
    if (auto* status = dynamic_cast<StatusArea*> (statusArea.get()))
        status->setProgress (progress);
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
