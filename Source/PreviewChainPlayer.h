#pragma once

#include <JuceHeader.h>

class PreviewChainPlayer final
{
public:
    explicit PreviewChainPlayer (juce::AudioDeviceManager& deviceManagerToUse);
    ~PreviewChainPlayer();

    bool startPlayback (const juce::File& previewChainFile);
    void stopPlayback();
    void setLooping (bool shouldLoop);

    bool isLooping() const;
    bool isPlaying() const;

private:
    juce::AudioDeviceManager& deviceManager;
    juce::AudioFormatManager formatManager;
    juce::AudioTransportSource transportSource;
    juce::AudioSourcePlayer sourcePlayer;
    std::unique_ptr<juce::AudioFormatReaderSource> readerSource;
    bool isLoopEnabled = false;
    bool isPlayingFlag = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PreviewChainPlayer)
};
