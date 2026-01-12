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
    void runTemporaryResliceAllDebug();

private:
    bool buildDeterministicSlices();
    bool buildPreviewChain();
    bool startPlayback();
    void stopPlayback();

    void clearPendingState();

    juce::AudioDeviceManager& deviceManager;
    AudioFileIO audioFileIO;
    SliceStateStore stateStore;

    juce::AudioFormatManager formatManager;
    juce::AudioTransportSource transportSource;
    juce::AudioSourcePlayer sourcePlayer;
    std::unique_ptr<juce::AudioFormatReaderSource> readerSource;

    std::vector<SliceStateStore::SliceInfo> pendingSliceInfos;
    std::vector<juce::File> pendingPreviewSnippetURLs;
    std::vector<float> pendingSliceVolumeSettings;

    juce::File previewChainFile;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DeterministicPreviewHarness)
};
