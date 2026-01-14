#include "SliceStateStore.h"

SliceStateStore::SliceStateSnapshot SliceStateStore::getSnapshot() const
{
    const juce::ScopedLock lock (stateLock);
    return SliceStateSnapshot { sourceDirectory,
                                sourceFile,
                                sliceInfos,
                                previewSnippetURLs,
                                sliceVolumeSettings,
                                previewChainURL,
                                layeringMode,
                                sampleCount,
                                mergeMode,
                                manualReverseEnabled,
                                exportSettingsLocked,
                                exportSettings,
                                stutterCount,
                                stutterVolumeReductionStep,
                                stutterPitchShiftSemitones,
                                stutterTruncateEnabled,
                                stutterStartFraction,
                                stutterUndoBackup };
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

void SliceStateStore::setSourceDirectory (juce::File newSourceDirectory)
{
    const juce::ScopedLock lock (stateLock);
    sourceDirectory = std::move (newSourceDirectory);
    sourceFile = juce::File();
}

void SliceStateStore::setSourceFile (juce::File newSourceFile)
{
    const juce::ScopedLock lock (stateLock);
    sourceFile = std::move (newSourceFile);
    sourceDirectory = juce::File();
}

void SliceStateStore::setLayeringState (bool newLayeringMode, int newSampleCount)
{
    const juce::ScopedLock lock (stateLock);
    layeringMode = newLayeringMode;
    sampleCount = newSampleCount;
}

void SliceStateStore::setMergeMode (MergeMode newMergeMode)
{
    const juce::ScopedLock lock (stateLock);
    mergeMode = newMergeMode;
}

void SliceStateStore::setManualReverseEnabled (bool newManualReverseEnabled)
{
    const juce::ScopedLock lock (stateLock);
    manualReverseEnabled = newManualReverseEnabled;
}

void SliceStateStore::setExportSettingsLocked (bool newExportSettingsLocked)
{
    const juce::ScopedLock lock (stateLock);
    exportSettingsLocked = newExportSettingsLocked;
}

void SliceStateStore::setExportSettings (ExportSettings newExportSettings)
{
    const juce::ScopedLock lock (stateLock);
    exportSettings = std::move (newExportSettings);
}

void SliceStateStore::setStutterSettings (int newStutterCount,
                                          float newStutterVolumeReductionStep,
                                          float newStutterPitchShiftSemitones,
                                          bool newStutterTruncateEnabled,
                                          float newStutterStartFraction)
{
    const juce::ScopedLock lock (stateLock);
    stutterCount = newStutterCount;
    stutterVolumeReductionStep = newStutterVolumeReductionStep;
    stutterPitchShiftSemitones = newStutterPitchShiftSemitones;
    stutterTruncateEnabled = newStutterTruncateEnabled;
    stutterStartFraction = newStutterStartFraction;
}

void SliceStateStore::clearStutterUndoBackup()
{
    const juce::ScopedLock lock (stateLock);
    stutterUndoBackup.clear();
}

void SliceStateStore::setStutterUndoBackupEntry (int index, juce::File originalSnippet)
{
    const juce::ScopedLock lock (stateLock);
    stutterUndoBackup[index] = std::move (originalSnippet);
}

void SliceStateStore::enforceAlignmentOrAssert (const std::vector<SliceInfo>& newSliceInfos,
                                                const std::vector<juce::File>& newPreviewSnippetURLs,
                                                const std::vector<float>& newSliceVolumeSettings) const
{
    jassert (newSliceInfos.size() == newPreviewSnippetURLs.size());
    jassert (newSliceInfos.size() == newSliceVolumeSettings.size());
}
