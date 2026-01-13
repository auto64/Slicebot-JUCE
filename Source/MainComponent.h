#pragma once

#include <JuceHeader.h>
#include "AudioEngine.h"
#include "LiveRecorderModuleView.h"

// =======================
// SETTINGS VIEW
// =======================

class SettingsView final : public juce::Component
{
public:
    explicit SettingsView (AudioEngine& engine);
    ~SettingsView() override = default;

    void resized() override;

private:
    AudioEngine& audioEngine;

    std::unique_ptr<juce::AudioDeviceSelectorComponent> deviceSelector;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SettingsView)
};

// =======================
// MAIN COMPONENT
// =======================

class MainComponent final : public juce::Component
{
public:
   enum class Tab
    {
        Main,
        Global,
        Local,
        Live,
        Settings
    };

    explicit MainComponent (AudioEngine& engine);
    ~MainComponent() override = default;

    void resized() override;

private:
    juce::TabbedComponent tabs { juce::TabbedButtonBar::TabsAtTop };

    SettingsView settingsView;
    std::unique_ptr<LiveRecorderModuleView> recorderModule;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};
