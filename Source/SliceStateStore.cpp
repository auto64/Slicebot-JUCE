#include "SliceStateStore.h"

SliceStateStore::SliceStateSnapshot SliceStateStore::getSnapshot() const
{
    const juce::ScopedLock lock (stateLock);
    return SliceStateSnapshot { sliceInfos, previewSnippetURLs, sliceVolumeSettings, previewChainURL, layeringMode, sampleCount };
}

void SliceStateStore::setAlignedSlices (std::vector<SliceInfo> newSliceInfos,
                                        std::vector<juce::File> newPreviewSnippetURLs,
                                        std::vector<float> newSliceVolumeSettings)
{
    enforceAlignmentOrAssert (newSliceInfos, newPreviewSnippetURLs, newSliceVolumeSettings);

    const juce::ScopedLock lock (stateLock);
    sliceInfos = std::move (newSliceInfos);
    previewSnippetURLs = std::move (newPreviewSnippetURLs);
    sliceVolumeSettings = std::move (newSliceVolumeSettings);
}

void SliceStateStore::replaceAllState (std::vector<SliceInfo> newSliceInfos,
                                       std::vector<juce::File> newPreviewSnippetURLs,
                                       std::vector<float> newSliceVolumeSettings,
                                       juce::File newPreviewChainURL)
{
    enforceAlignmentOrAssert (newSliceInfos, newPreviewSnippetURLs, newSliceVolumeSettings);

    const juce::ScopedLock lock (stateLock);
    sliceInfos = std::move (newSliceInfos);
    previewSnippetURLs = std::move (newPreviewSnippetURLs);
    sliceVolumeSettings = std::move (newSliceVolumeSettings);
    previewChainURL = std::move (newPreviewChainURL);
}

void SliceStateStore::setPreviewChainURL (juce::File newPreviewChainURL)
{
    const juce::ScopedLock lock (stateLock);
    previewChainURL = std::move (newPreviewChainURL);
}

void SliceStateStore::setLayeringState (bool newLayeringMode, int newSampleCount)
{
    const juce::ScopedLock lock (stateLock);
    layeringMode = newLayeringMode;
    sampleCount = newSampleCount;
}

void SliceStateStore::enforceAlignmentOrAssert (const std::vector<SliceInfo>& newSliceInfos,
                                                const std::vector<juce::File>& newPreviewSnippetURLs,
                                                const std::vector<float>& newSliceVolumeSettings) const
{
    jassert (newSliceInfos.size() == newPreviewSnippetURLs.size());
    jassert (newSliceInfos.size() == newSliceVolumeSettings.size());
}
