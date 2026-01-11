#include <JuceHeader.h>
#include "AudioEngine.h"
#include "AudioFileIO.h"
#include "MainComponent.h"

class SliceBotJUCEApplication final : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override       { return "SliceBotJUCE"; }
    const juce::String getApplicationVersion() override    { return "0.1.0"; }

    void initialise (const juce::String&) override
    {
        AudioFileIO::runSmokeTestAtStartup();

        audioEngine.restoreState();
        audioEngine.start();

        mainWindow.reset (new MainWindow (audioEngine));
    }

    void shutdown() override
    {
        audioEngine.saveState();
        audioEngine.stop();
        mainWindow = nullptr;
    }

private:
    class MainWindow final : public juce::DocumentWindow
    {
    public:
        explicit MainWindow (AudioEngine& engine)
            : DocumentWindow ("SliceBotJUCE",
                              juce::Colours::black,
                              DocumentWindow::allButtons)
        {
            setUsingNativeTitleBar (true);
            setContentOwned (new MainComponent (engine), true);
            centreWithSize (1000, 1000);
            setVisible (true);
        }

        void closeButtonPressed() override
        {
            juce::JUCEApplication::getInstance()->systemRequestedQuit();
        }
    };

    AudioEngine audioEngine;
    std::unique_ptr<MainWindow> mainWindow;
};

START_JUCE_APPLICATION (SliceBotJUCEApplication)
