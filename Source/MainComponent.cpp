#include "MainComponent.h"
#include "MainTabView.h"
#include "GlobalTabView.h"
#include "AudioCacheStore.h"
#include <cmath>

namespace
{
    class GreyPlaceholder final : public juce::Component
    {
    public:
        void paint (juce::Graphics& g) override
        {
            g.fillAll (juce::Colours::darkgrey);
        }
    };

    class GridCell final : public juce::Component
    {
    public:
        explicit GridCell (int indexToDraw)
            : index (indexToDraw)
        {
        }

        void paint (juce::Graphics& g) override
        {
            g.fillAll (juce::Colours::darkgrey);
            g.setColour (juce::Colours::grey);
            g.drawRect (getLocalBounds(), 1);
        }

    private:
        int index = 0;
    };

    class PreviewGrid final : public juce::Component
    {
    public:
        PreviewGrid()
        {
            for (int index = 0; index < totalCells; ++index)
            {
                auto cell = std::make_unique<GridCell> (index);
                addAndMakeVisible (*cell);
                cells.add (std::move (cell));
            }
        }

        void resized() override
        {
            for (int row = 0; row < rows; ++row)
            {
                for (int col = 0; col < columns; ++col)
                {
                    const int index = row * columns + col;
                    const int x = col * (cellW + spacing);
                    const int y = row * (cellH + spacing);
                    cells[index]->setBounds (x, y, cellW, cellH);
                }
            }
        }

    private:
        static constexpr int columns = 4;
        static constexpr int rows = 4;
        static constexpr int totalCells = columns * rows;
        static constexpr int cellW = 150;
        static constexpr int cellH = 64;
        static constexpr int spacing = 3;

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
            configureButton (recacheButton, "RECACHE");
            configureButton (lockButton, "🔒");
            configureButton (loopButton, "LOOP");

            buttons.add (&sliceAllButton);
            buttons.add (&modAllButton);
            buttons.add (&jumbleAllButton);
            buttons.add (&resliceAllButton);
            buttons.add (&exportButton);
            buttons.add (&recacheButton);
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

        void setRecacheHandler (std::function<void()> handler)
        {
            recacheButton.onClick = std::move (handler);
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
                return juce::Font ("Helvetica", juce::jmin (11.0f, buttonHeight * 0.5f), juce::Font::plain);
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
                g.setColour (juce::Colour (0xff333333));
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
            button.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff5a5a5a));
            button.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xff4fa3f7));
            button.setColour (juce::TextButton::textColourOffId, juce::Colour (0xffcfcfcf));
            button.setColour (juce::TextButton::textColourOnId, juce::Colours::white);
        }

        CompactLookAndFeel compactLookAndFeel;
        juce::TextButton sliceAllButton;
        juce::TextButton modAllButton;
        juce::TextButton jumbleAllButton;
        juce::TextButton resliceAllButton;
        juce::TextButton exportButton;
        juce::TextButton recacheButton;
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
            statusLabel.setColour (juce::Label::textColourId, juce::Colour (0xffcfcfcf));
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
            g.fillAll (juce::Colour (0xff444444));
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

    class PersistentFrame final : public juce::Component,
                                  private juce::ChangeListener
    {
    public:
        PersistentFrame (juce::TabbedComponent& tabsToTrack, SliceStateStore& stateStoreToUse)
            : tabs (tabsToTrack),
              stateStore (stateStoreToUse)
        {
            addAndMakeVisible (focusPlaceholder);
            addAndMakeVisible (grid);
            actionBar = std::make_unique<ActionBar>();
            statusArea = std::make_unique<StatusArea>();
            addAndMakeVisible (*actionBar);
            addAndMakeVisible (*statusArea);
            tabs.getTabbedButtonBar().addChangeListener (this);

            // Settings bypass path: hide frame while Settings tab is active.
            setVisible (tabs.getCurrentTabName() != "SETTINGS");

            if (auto* bar = dynamic_cast<ActionBar*> (actionBar.get()))
            {
                bar->setRecacheHandler ([this]()
                {
                    const auto snapshot = stateStore.getSnapshot();
                    const bool hasFile = snapshot.sourceFile.existsAsFile();
                    const bool hasDir = snapshot.sourceDirectory.isDirectory();
                    if (! hasFile && ! hasDir)
                    {
                        setStatusText ("No source selected.");
                        return;
                    }

                    const auto source = hasFile ? snapshot.sourceFile : snapshot.sourceDirectory;
                    setStatusText ("Recaching...");
                    setProgress (0.0f);

                    const auto cacheData = AudioCacheStore::buildFromSource (
                        source,
                        ! hasFile,
                        [this] (int current, int total)
                        {
                            if (total > 0)
                                setProgress (static_cast<float> (current) / static_cast<float> (total));
                        });
                    AudioCacheStore::save (cacheData);
                    stateStore.setCacheData (cacheData);
                    setStatusText ("Recache complete.");
                    setProgress (1.0f);
                });
            }
        }

        ~PersistentFrame() override
        {
            tabs.getTabbedButtonBar().removeChangeListener (this);
        }

        void paint (juce::Graphics& g) override
        {
            g.fillAll (juce::Colours::grey);
        }

        void resized() override
        {
            const int focusW = 609;
            const int focusH = 96;
            const int gridW = 609;
            const int gridH = 4 * 64 + 3 * 3;
            const int spacing = 6;
            const int actionBarH = 28;
            const int statusH = 24;

            int y = 0;
            focusPlaceholder.setBounds (0, y, focusW, focusH);
            y += focusH + spacing;
            grid.setBounds (0, y, gridW, gridH);
            y += gridH + spacing;
            if (actionBar != nullptr)
                actionBar->setBounds (0, y, gridW, actionBarH);
            y += actionBarH + 8;
            if (statusArea != nullptr)
                statusArea->setBounds (0, y, gridW, statusH);
        }

        void setStatusText (const juce::String& text)
        {
            if (auto* status = dynamic_cast<StatusArea*> (statusArea.get()))
                status->setStatusText (text);
        }

        void setProgress (float progress)
        {
            if (auto* status = dynamic_cast<StatusArea*> (statusArea.get()))
                status->setProgress (progress);
        }

    private:
        void changeListenerCallback (juce::ChangeBroadcaster*) override
        {
            // Settings bypass path: hide frame while Settings tab is active.
            setVisible (tabs.getCurrentTabName() != "SETTINGS");
        }

        juce::TabbedComponent& tabs;
        SliceStateStore& stateStore;
        GreyPlaceholder focusPlaceholder;
        PreviewGrid grid;
        std::unique_ptr<juce::Component> actionBar;
        std::unique_ptr<juce::Component> statusArea;
    };

    class TabHeaderContainer final : public juce::Component,
                                     private juce::ChangeListener
    {
    public:
        TabHeaderContainer (juce::TabbedComponent& tabsToTrack,
                            SliceStateStore& stateStoreToUse,
                            MainTabView& mainTabViewToUse)
            : tabs (tabsToTrack),
              mainHeader (mainTabViewToUse),
              globalHeader (stateStoreToUse),
              stateStore (stateStoreToUse)
        {
            addAndMakeVisible (mainHeader);
            addAndMakeVisible (globalHeader);
            addAndMakeVisible (localHeader);
            addAndMakeVisible (liveHeader);

            tabs.getTabbedButtonBar().addChangeListener (this);
            updateVisibleHeader();
        }

        ~TabHeaderContainer() override
        {
            tabs.getTabbedButtonBar().removeChangeListener (this);
        }

        void setLiveContent (juce::Component* content)
        {
            if (content != nullptr)
                liveHeader.addAndMakeVisible (content);
        }

        void paint (juce::Graphics& g) override
        {
            g.fillAll (juce::Colours::grey);
        }

        void resized() override
        {
            const auto bounds = getLocalBounds();
            mainHeader.setBounds (bounds);
            globalHeader.setBounds (bounds);
            localHeader.setBounds (bounds);
            liveHeader.setBounds (bounds);
        }

    private:
        void changeListenerCallback (juce::ChangeBroadcaster*) override
        {
            updateVisibleHeader();
        }

        void updateVisibleHeader()
        {
            const auto currentTab = tabs.getCurrentTabName();

            // Header region ownership (per-tab visibility).
            const bool showMain = currentTab == "MAIN";
            const bool showGlobal = currentTab == "GLOBAL";
            const bool showLocal = currentTab == "LOCAL";
            const bool showLive = currentTab == "LIVE";

            // Settings bypass path: hide header while Settings tab is active.
            setVisible (currentTab != "SETTINGS");

            // Centralized header visibility toggles.
            mainHeader.setVisible (showMain);
            globalHeader.setVisible (showGlobal);
            localHeader.setVisible (showLocal);
            liveHeader.setVisible (showLive);

            if (showGlobal)
                globalHeader.applySettingsSnapshot (stateStore.getSnapshot());
        }

        juce::TabbedComponent& tabs;
        MainTabView& mainHeader;
        SliceStateStore& stateStore;
        GlobalTabView globalHeader;
        GreyPlaceholder localHeader;
        juce::Component liveHeader;
    };

    class ContentArea final : public juce::Component,
                              private juce::ChangeListener
    {
    public:
        ContentArea (juce::TabbedComponent& tabsToTrack,
                     SettingsView& settingsToUse,
                     SliceStateStore& stateStoreToUse,
                     juce::Component* liveContent)
            : tabs (tabsToTrack),
              settingsView (settingsToUse),
              persistentFrame (tabsToTrack, stateStoreToUse),
              mainTabView (stateStoreToUse),
              headerContainer (tabsToTrack, stateStoreToUse, mainTabView)
        {
            headerContainer.setLiveContent (liveContent);
            addAndMakeVisible (headerContainer);
            addAndMakeVisible (persistentFrame);
            addAndMakeVisible (settingsView);

            mainTabView.setStatusTextCallback ([this] (const juce::String& text)
            {
                persistentFrame.setStatusText (text);
            });
            mainTabView.setProgressCallback ([this] (float progress)
            {
                persistentFrame.setProgress (progress);
            });

            tabs.getTabbedButtonBar().addChangeListener (this);
            updateVisibleContent();
        }

        ~ContentArea() override
        {
            tabs.getTabbedButtonBar().removeChangeListener (this);
        }

        void paint (juce::Graphics& g) override
        {
            g.fillAll (juce::Colour (0xff7a7a7a));
        }

        void resized() override
        {
            const int headerH = 150;

            const int focusH = 96;
            const int gridH = 4 * 64 + 3 * 3;
            const int spacing = 6;
            const int actionBarH = 28;
            const int statusH = 24;
            const int frameH = focusH + spacing + gridH + spacing + actionBarH + 8 + statusH;

            const int headerTopPadding = 16;
            const int headerBottomPadding = -10;

            headerContainer.setBounds (
                5,
                headerTopPadding,
                609 - 5 - 5,
                headerH - headerTopPadding + headerBottomPadding
            );

            persistentFrame.setBounds (
                0,
                headerH,
                609,
                frameH
            );

            settingsView.setBounds (getLocalBounds());
        }

    private:
        void changeListenerCallback (juce::ChangeBroadcaster*) override
        {
            updateVisibleContent();
        }

        void updateVisibleContent()
        {
            const auto currentTab = tabs.getCurrentTabName();
            const bool isSettings = currentTab == "SETTINGS";
            settingsView.setVisible (isSettings);
            headerContainer.setVisible (! isSettings);
            persistentFrame.setVisible (! isSettings);
        }

        juce::TabbedComponent& tabs;
        SettingsView& settingsView;
        PersistentFrame persistentFrame;
        MainTabView mainTabView;
        TabHeaderContainer headerContainer;
    };
}

// =======================
// SETTINGS VIEW
// =======================

SettingsView::SettingsView (AudioEngine& engine)
    : audioEngine (engine)
{
    deviceSelector = std::unique_ptr<juce::AudioDeviceSelectorComponent> (
        new juce::AudioDeviceSelectorComponent (
            audioEngine.getDeviceManager(),
            0, 256,   // allow inputs
            0, 256,   // allow outputs
            false,
            false,
            false,
            false));

    addAndMakeVisible (*deviceSelector);
}

void SettingsView::resized()
{
    deviceSelector->setBounds (getLocalBounds().reduced (20));
}

// =======================
// MAIN COMPONENT
// =======================

MainComponent::MainComponent (AudioEngine& engine)
    : settingsView (engine)
{
    recorderModule =
        std::make_unique<LiveRecorderModuleView> (engine, 0);

    tabs.addTab ("MAIN",
                 juce::Colours::darkgrey,
                 new juce::Component(),
                 true);

    tabs.addTab ("GLOBAL",
                 juce::Colours::darkgrey,
                 new juce::Component(),
                 true);

    tabs.addTab ("LOCAL",
                 juce::Colours::darkgrey,
                 new juce::Component(),
                 true);

    tabs.addTab ("LIVE",
                 juce::Colours::darkgrey,
                 new juce::Component(),
                 true);

    tabs.addTab ("SETTINGS",
                 juce::Colours::darkgrey,
                 new juce::Component(),
                 true);

    addAndMakeVisible (tabs);

    auto* contentArea = new ContentArea (tabs, settingsView, stateStore, recorderModule.get());
    contentArea->setComponentID ("contentArea");
    addAndMakeVisible (contentArea);
}

void MainComponent::visibilityChanged()
{
    if (! isVisible())
        return;

    stateStore.setCacheData (AudioCacheStore::load());
}

void MainComponent::resized()
{
    const int tabStripH = 25;

    tabs.setTabBarDepth (tabStripH);
    tabs.setBounds (0, 0, getWidth(), tabStripH);

    if (auto* contentArea = findChildWithID ("contentArea"))
    {
        contentArea->setBounds (0, tabStripH, getWidth(), getHeight() - tabStripH);
    }
}
