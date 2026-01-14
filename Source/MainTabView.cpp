#include "MainTabView.h"

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

            buttons.add (&sliceAllButton);
            buttons.add (&modAllButton);
            buttons.add (&jumbleAllButton);
            buttons.add (&resliceAllButton);
            buttons.add (&exportButton);
            buttons.add (&lockButton);
            buttons.add (&loopButton);

            for (auto* button : buttons)
                addAndMakeVisible (button);
        }

        void resized() override
        {
            auto bounds = getLocalBounds();
            const int spacing = 6;

            for (auto* button : buttons)
            {
                const int width = button->getBestWidthForHeight (bounds.getHeight());
                button->setBounds (bounds.removeFromLeft (width));
                bounds.removeFromLeft (spacing);
            }
        }

    private:
        void configureButton (juce::TextButton& button, const juce::String& text)
        {
            button.setButtonText (text);
            button.setColour (juce::TextButton::buttonColourId, panelGrey());
            button.setColour (juce::TextButton::buttonOnColourId, accentBlue());
            button.setColour (juce::TextButton::textColourOffId, textGrey());
            button.setColour (juce::TextButton::textColourOnId, juce::Colours::white);
        }

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
    return juce::Font ("Helvetica", fontSize, juce::Font::plain);
}

juce::Font MainTabView::StyleLookAndFeel::getLabelFont (juce::Label&)
{
    return juce::Font ("Helvetica", fontSize, juce::Font::plain);
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
    g.setFont (juce::Font ("Helvetica", fontSize, juce::Font::plain));
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
        button->onClick = [this]() { updateLiveModeState(); };

    sourceButton.onClick = [this]()
    {
        sourceDirectoryChooser = std::make_unique<juce::FileChooser> (
            "Select Source Folder",
            juce::File(),
            "*");
        constexpr int flags = juce::FileBrowserComponent::openMode
                              | juce::FileBrowserComponent::canSelectDirectories;
        sourceDirectoryChooser->launchAsync (flags, [this] (const juce::FileChooser& chooser)
        {
            const auto selectedDirectory = chooser.getResult();
            if (selectedDirectory.exists())
            {
                stateStore.setSourceDirectory (selectedDirectory);
                updateSourcePathLabel (selectedDirectory);
            }
        });
    };

    bpmValue.setEditable (true);
    bpmValue.setColour (juce::Label::backgroundColourId, backgroundGrey());
    bpmValue.setColour (juce::Label::outlineColourId, borderGrey());
    bpmValue.setColour (juce::Label::textColourId, juce::Colours::white);
    bpmValue.setJustificationType (juce::Justification::centred);

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
    addAndMakeVisible (sourcePathLabel);
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

    sourcePathLabel.setColour (juce::Label::textColourId, textGrey());
    sourcePathLabel.setJustificationType (juce::Justification::centredLeft);

    focusView = std::make_unique<FocusWaveformView>();
    previewGrid = std::make_unique<PreviewGrid>();
    actionBar = std::make_unique<ActionBar>();
    statusArea = std::make_unique<StatusArea>();

    addAndMakeVisible (*focusView);
    addAndMakeVisible (*previewGrid);
    addAndMakeVisible (*actionBar);
    addAndMakeVisible (*statusArea);

    updateSourcePathLabel (stateStore.getSnapshot().sourceDirectory);
    updateLiveModeState();
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
    const int modeSegmentWidth = rowBounds.getWidth() / 4;
    modeMultiFile.setBounds (rowBounds.removeFromLeft (modeSegmentWidth));
    modeSingleRandom.setBounds (rowBounds.removeFromLeft (modeSegmentWidth));
    modeSingleManual.setBounds (rowBounds.removeFromLeft (modeSegmentWidth));
    modeLive.setBounds (rowBounds);

    y += kRowHeight + kRowSpacing;
    if (sourceButton.isVisible())
    {
        rowBounds = juce::Rectangle<int> (x, y, contentWidth, kRowHeight);
        sourceButton.setBounds (rowBounds.removeFromLeft (80));
        rowBounds.removeFromLeft (8);
        sourcePathLabel.setBounds (rowBounds);
        y += kRowHeight + kRowSpacing;
    }

    rowBounds = juce::Rectangle<int> (x, y, contentWidth, kRowHeight);
    subdivLabel.setBounds (rowBounds.removeFromLeft (55));
    subdivHalfBar.setBounds (rowBounds.removeFromLeft (90));
    subdivQuarterBar.setBounds (rowBounds.removeFromLeft (90));
    subdivEighthNote.setBounds (rowBounds.removeFromLeft (90));
    subdivSixteenthNote.setBounds (rowBounds.removeFromLeft (90));
    subdivRandom.setBounds (rowBounds.removeFromLeft (80));

    y += kRowHeight + kRowSpacing;
    rowBounds = juce::Rectangle<int> (x, y, contentWidth, kRowHeight);
    bpmLabel.setBounds (rowBounds.removeFromLeft (45));
    bpmValue.setBounds (rowBounds.removeFromLeft (70));
    rowBounds.removeFromLeft (20);
    samplesLabel.setBounds (rowBounds.removeFromLeft (70));
    samplesFour.setBounds (rowBounds.removeFromLeft (120));
    samplesEight.setBounds (rowBounds.removeFromLeft (120));
    samplesSixteen.setBounds (rowBounds.removeFromLeft (120));

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

void MainTabView::updateSourcePathLabel (const juce::File& directory)
{
    const auto pathText = directory.exists() ? directory.getFullPathName() : "No source selected";
    sourcePathLabel.setText (pathText, juce::dontSendNotification);
}

void MainTabView::updateLiveModeState()
{
    const bool isLive = modeLive.getToggleState();
    sourceButton.setVisible (! isLive);
    sourcePathLabel.setVisible (! isLive);
}

void MainTabView::setProgress (float progress)
{
    if (auto* status = dynamic_cast<StatusArea*> (statusArea.get()))
        status->setProgress (progress);
}
