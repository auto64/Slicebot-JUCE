#pragma once

#include <JuceHeader.h>
#include <vector>

class SliceStateStore
{
public:
    struct SliceInfo
    {
        juce::File fileURL;
        int startFrame = 0;
        int subdivisionSteps = 0;
        int snippetFrameCount = 0;
    };

    struct SliceStateSnapshot
    {
        std::vector<SliceInfo> sliceInfos;
        std::vector<juce::File> previewSnippetURLs;
        std::vector<float> sliceVolumeSettings;
        juce::File previewChainURL;
    };

    SliceStateStore() = default;

    SliceStateSnapshot getSnapshot() const;

    void setAlignedSlices (std::vector<SliceInfo> newSliceInfos,
                           std::vector<juce::File> newPreviewSnippetURLs,
                           std::vector<float> newSliceVolumeSettings);

    void replaceAllState (std::vector<SliceInfo> newSliceInfos,
                          std::vector<juce::File> newPreviewSnippetURLs,
                          std::vector<float> newSliceVolumeSettings,
                          juce::File newPreviewChainURL);

    void setPreviewChainURL (juce::File newPreviewChainURL);

private:
    void enforceAlignmentOrAssert (const std::vector<SliceInfo>& newSliceInfos,
                                   const std::vector<juce::File>& newPreviewSnippetURLs,
                                   const std::vector<float>& newSliceVolumeSettings) const;

    mutable juce::CriticalSection stateLock;
    std::vector<SliceInfo> sliceInfos;
    std::vector<juce::File> previewSnippetURLs;
    std::vector<float> sliceVolumeSettings;
    juce::File previewChainURL;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SliceStateStore)
};
