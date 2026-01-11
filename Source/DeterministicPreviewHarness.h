#pragma once

#include <JuceHeader.h>
#include "AudioFileIO.h"
#include "SliceStateStore.h"

class DeterministicPreviewHarness
{
public:
    explicit DeterministicPreviewHarness (juce::AudioDeviceManager& deviceManager);
    ~DeterministicPreviewHarness();

    void run();

private:
    bool buildDeterministicSlices();
    bool buildPreviewChain();
    bool startPlayback();

    juce::AudioDeviceManager& deviceManager;
    AudioFileIO audioFileIO;
    SliceStateStore stateStore;

    juce::AudioFormatManager formatManager;
    juce::AudioTransportSource transportSource;
    juce::AudioSourcePlayer sourcePlayer;
    std::unique_ptr<juce::AudioFormatReaderSource> readerSource;

    juce::File sourceFile;
    juce::File previewChainFile;
    juce::AudioBuffer<float> sourceBuffer;
    int sliceFrameCount = 0;
    juce::Array<int> sliceStartFrames;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DeterministicPreviewHarness)
};
