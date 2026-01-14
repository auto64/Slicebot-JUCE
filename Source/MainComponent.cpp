#include "MainComponent.h"
#include "MainTabView.h"
#include "GlobalTabView.h"
#include "AudioCacheStore.h"

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

    class PersistentFrame final : public juce::Component,
                                  private juce::ChangeListener
    {
    public:
        explicit PersistentFrame (juce::TabbedComponent& tabsToTrack)
            : tabs (tabsToTrack)
        {
            addAndMakeVisible (focusPlaceholder);
            addAndMakeVisible (grid);
            addAndMakeVisible (bottomPlaceholder);
            tabs.getTabbedButtonBar().addChangeListener (this);

            // Settings bypass path: hide frame while Settings tab is active.
            setVisible (tabs.getCurrentTabName() != "SETTINGS");
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
            const int bottomH = 25;

            int y = 0;
            focusPlaceholder.setBounds (0, y, focusW, focusH);
            y += focusH + spacing;
            grid.setBounds (0, y, gridW, gridH);
            y += gridH + spacing;
            bottomPlaceholder.setBounds (0, y, gridW, bottomH);
        }

    private:
        void changeListenerCallback (juce::ChangeBroadcaster*) override
        {
            // Settings bypass path: hide frame while Settings tab is active.
            setVisible (tabs.getCurrentTabName() != "SETTINGS");
        }

        juce::TabbedComponent& tabs;
        GreyPlaceholder focusPlaceholder;
        PreviewGrid grid;
        GreyPlaceholder bottomPlaceholder;
    };

    class TabHeaderContainer final : public juce::Component,
                                     private juce::ChangeListener
    {
    public:
        TabHeaderContainer (juce::TabbedComponent& tabsToTrack, SliceStateStore& stateStoreToUse)
            : tabs (tabsToTrack),
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
        SliceStateStore& stateStore;
        GreyPlaceholder mainHeader;
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
              persistentFrame (tabsToTrack),
              headerContainer (tabsToTrack, stateStoreToUse),
              mainTabView (stateStoreToUse)
        {
            headerContainer.setLiveContent (liveContent);
            addAndMakeVisible (headerContainer);
            addAndMakeVisible (persistentFrame);
            addAndMakeVisible (settingsView);
            addAndMakeVisible (mainTabView);

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
            const int bottomH = 25;
            const int frameH = focusH + spacing + gridH + spacing + bottomH;

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
            mainTabView.setBounds (getLocalBounds());
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
            const bool isMain = currentTab == "MAIN";

            settingsView.setVisible (isSettings);
            mainTabView.setVisible (isMain);
            headerContainer.setVisible (! isSettings && ! isMain);
            persistentFrame.setVisible (! isSettings && ! isMain);
        }

        juce::TabbedComponent& tabs;
        SettingsView& settingsView;
        PersistentFrame persistentFrame;
        TabHeaderContainer headerContainer;
        MainTabView mainTabView;
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
