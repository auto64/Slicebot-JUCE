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
    constexpr int kActionBarHeight = 25;
    constexpr int kStatusHeight = 24;
    constexpr int kSideColumnWidth = 24;

    juce::Colour backgroundGrey()
    {
        return juce::Colour (0xffbdbdbd);
    }

    juce::Colour panelGrey()
    {
        return juce::Colour (0xffa6a6a6);
    }

    juce::Colour buttonGrey()
    {
        return juce::Colour (0xff8f8f8f);
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
            button.setColour (juce::TextButton::buttonColourId, buttonGrey());
            button.setColour (juce::TextButton::textColourOffId, juce::Colours::white);
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
            button.setColour (juce::TextButton::buttonColourId, buttonGrey());
            button.setColour (juce::TextButton::textColourOffId, juce::Colours::white);
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
            statusLabel.setText ("Preview generated.", juce::dontSendNotification);
            statusLabel.setJustificationType (juce::Justification::centred);
            addAndMakeVisible (statusLabel);
        }

        void paint (juce::Graphics& g) override
        {
            g.setColour (juce::Colours::red);
            g.drawLine (0.0f, 0.0f, static_cast<float> (getWidth()), 0.0f, 1.0f);
        }

        void resized() override
        {
            statusLabel.setBounds (0, 4, getWidth(), getHeight() - 4);
        }

    private:
        juce::Label statusLabel;
    };
}

MainTabView::MainTabView()
{
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

    bpmValue.setEditable (true);
    bpmValue.setColour (juce::Label::backgroundColourId, juce::Colours::white);
    bpmValue.setColour (juce::Label::outlineColourId, juce::Colours::grey);
    bpmValue.setColour (juce::Label::textColourId, juce::Colours::black);
    bpmValue.setJustificationType (juce::Justification::centred);

    subdivLabel.setColour (juce::Label::textColourId, juce::Colours::black);
    bpmLabel.setColour (juce::Label::textColourId, juce::Colours::black);
    samplesLabel.setColour (juce::Label::textColourId, juce::Colours::black);
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

    updateLiveModeState();
}

void MainTabView::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff7a7a7a));
}

void MainTabView::resized()
{
    const int availableWidth = getWidth();
    const int contentWidth = juce::jmin (kContentWidth, availableWidth - 32);
    int x = (availableWidth - contentWidth) / 2;
    int y = 12;

    auto rowBounds = juce::Rectangle<int> (x, y, contentWidth, kRowHeight);
    modeMultiFile.setBounds (rowBounds.removeFromLeft (contentWidth / 4));
    modeSingleRandom.setBounds (rowBounds.removeFromLeft (contentWidth / 4));
    modeSingleManual.setBounds (rowBounds.removeFromLeft (contentWidth / 4));
    modeLive.setBounds (rowBounds);

    y += kRowHeight + kRowSpacing;
    rowBounds = juce::Rectangle<int> (x, y, contentWidth, kRowHeight);
    if (sourceButton.isVisible())
    {
        sourceButton.setBounds (rowBounds.removeFromLeft (70));
        rowBounds.removeFromLeft (8);
    }
    subdivLabel.setBounds (rowBounds.removeFromLeft (55));
    subdivHalfBar.setBounds (rowBounds.removeFromLeft (90));
    subdivQuarterBar.setBounds (rowBounds.removeFromLeft (90));
    subdivEighthNote.setBounds (rowBounds.removeFromLeft (90));
    subdivSixteenthNote.setBounds (rowBounds.removeFromLeft (90));
    subdivRandom.setBounds (rowBounds.removeFromLeft (70));

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
}

void MainTabView::updateLiveModeState()
{
    const bool isLive = modeLive.getToggleState();
    sourceButton.setVisible (! isLive);
}
