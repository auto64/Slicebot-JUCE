#include "MainComponent.h"

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

    class GridPlaceholder final : public juce::Component
    {
    public:
        void paint (juce::Graphics& g) override
        {
            g.fillAll (juce::Colours::darkgrey);

            const int cellW = 150;
            const int cellH = 64;
            const int spacing = 3;

            g.setColour (juce::Colours::grey);

            for (int row = 0; row < 4; ++row)
            {
                for (int col = 0; col < 4; ++col)
                {
                    const int x = col * (cellW + spacing);
                    const int y = row * (cellH + spacing);
                    g.fillRect (x, y, cellW, cellH);
                }
            }
        }
    };

    class PersistentFrame final : public juce::Component,
                                  private juce::ChangeListener
    {
    public:
        explicit PersistentFrame (juce::TabbedComponent& tabsToTrack)
            : tabs (tabsToTrack)
        {
            addAndMakeVisible (focusPlaceholder);
            addAndMakeVisible (gridPlaceholder);
            addAndMakeVisible (bottomPlaceholder);
            tabs.getTabbedButtonBar().addChangeListener (this);
            setVisible (tabs.getCurrentTabName() != "SETTINGS");
        }

        ~PersistentFrame() override
        {
            tabs.getTabbedButtonBar().removeChangeListener (this);
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
            gridPlaceholder.setBounds (0, y, gridW, gridH);
            y += gridH + spacing;
            bottomPlaceholder.setBounds (0, y, gridW, bottomH);
        }

    private:
        void changeListenerCallback (juce::ChangeBroadcaster*) override
        {
            setVisible (tabs.getCurrentTabName() != "SETTINGS");
        }

        juce::TabbedComponent& tabs;
        GreyPlaceholder focusPlaceholder;
        GridPlaceholder gridPlaceholder;
        GreyPlaceholder bottomPlaceholder;
    };

    class TabHeaderContainer final : public juce::Component,
                                     private juce::ChangeListener
    {
    public:
        explicit TabHeaderContainer (juce::TabbedComponent& tabsToTrack)
            : tabs (tabsToTrack)
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

            const bool showMain = currentTab == "MAIN";
            const bool showGlobal = currentTab == "GLOBAL";
            const bool showLocal = currentTab == "LOCAL";
            const bool showLive = currentTab == "LIVE";

            setVisible (currentTab != "SETTINGS");
            mainHeader.setVisible (showMain);
            globalHeader.setVisible (showGlobal);
            localHeader.setVisible (showLocal);
            liveHeader.setVisible (showLive);
        }

        juce::TabbedComponent& tabs;
        GreyPlaceholder mainHeader;
        GreyPlaceholder globalHeader;
        GreyPlaceholder localHeader;
        juce::Component liveHeader;
    };
}

// =======================
// SETTINGS VIEW
// =======================

SettingsView::SettingsView (AudioEngine& engine)
    : audioEngine (engine)
{
    deviceSelector = std::unique_ptr<juce::AudioDeviceSelectorComponent>(
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

    auto* persistentFrame = new PersistentFrame (tabs);
    persistentFrame->setComponentID ("persistentFrame");
    addAndMakeVisible (persistentFrame);

    auto* headerContainer = new TabHeaderContainer (tabs);
    headerContainer->setComponentID ("tabHeaders");
    headerContainer->setLiveContent (recorderModule.get());
    addAndMakeVisible (headerContainer);

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
                 &settingsView,
                 false);

    addAndMakeVisible (tabs);
    
}

void MainComponent::resized()
{
    tabs.setBounds (getLocalBounds());

    const int headerH = 25;

    if (auto* frame = findChildWithID ("persistentFrame"))
    {
        const int focusH = 96;
        const int gridH = 4 * 64 + 3 * 3;
        const int spacing = 6;
        const int bottomH = 25;
        const int frameH = focusH + spacing + gridH + spacing + bottomH;

        frame->setBounds (0, headerH, 609, frameH);
    }

    if (auto* headers = findChildWithID ("tabHeaders"))
    {
        headers->setBounds (0, 0, 609, headerH);
    }
}