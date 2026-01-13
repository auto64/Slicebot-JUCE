#include "MainComponent.h"

// =======================
// SETTINGS VIEW
// =======================

SettingsView::SettingsView (AudioEngine& engine)
: audioEngine (engine)
{
deviceSelector = std::make_unique<juce::AudioDeviceSelectorComponent>(
    audioEngine.getDeviceManager(),
    0, 256,
    0, 256,
    false,
    false,
    false,
    false);


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
             recorderModule.get(),
             false);

tabs.addTab ("SETTINGS",
             juce::Colours::darkgrey,
             &settingsView,
             false);

addAndMakeVisible (tabs);
setSize (1000, 700);
}

void MainComponent::resized()
{
tabs.setBounds (getLocalBounds());
}