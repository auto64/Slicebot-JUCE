#pragma once

#include <JuceHeader.h>
#include <vector>
#include "AudioCacheStore.h"

class SliceStateStore
{
public:
    enum class SourceMode
    {
        multi,
        singleRandom,
        singleManual,
        live
    };

    enum class MergeMode
    {
        none,
        fiftyFifty,
        quarterCuts,
        crossfade,
        crossfadeReverse,
        pachinko
    };

    struct SliceInfo
    {
        juce::File fileURL;
        int startFrame = 0;
        int subdivisionSteps = 0;
        int snippetFrameCount = 0;
        SourceMode sourceMode = SourceMode::multi;
        double bpm = 128.0;
        bool transientDetectionEnabled = true;
        juce::String sourcePath;
        bool sourceIsDirectory = false;
        std::vector<juce::String> candidatePaths;
        bool isLocked = false;
        bool isDeleted = false;
        bool isReversed = false;
    };

    struct SliceVolumeSetting
    {
        float volume = 0.75f;
        bool isMuted = false;
    };

    struct ExportSettings
    {
        juce::File exportDirectory;
        juce::String exportPrefix = "export";
        bool generateIndividual = true;
        bool generateChain = true;
        int sliceExportRetryCount = 3;
    };

    struct SliceStateSnapshot
    {
        juce::File sourceDirectory;
        juce::File sourceFile;
        AudioCacheStore::CacheData cacheData;
        SourceMode sourceMode = SourceMode::multi;
        double bpm = 128.0;
        int subdivisionSteps = 4;
        int sampleCountSetting = 16;
        bool randomSubdivisionEnabled = false;
        bool transientDetectionEnabled = true;
        bool isCaching = false;
        std::vector<SliceInfo> sliceInfos;
        std::vector<juce::File> previewSnippetURLs;
        std::vector<SliceVolumeSetting> sliceVolumeSettings;
        juce::File previewChainURL;
        bool layeringMode = false;
        int sampleCount = 0;
        MergeMode mergeMode = MergeMode::none;
        bool manualReverseEnabled = false;
        bool exportSettingsLocked = false;
        ExportSettings exportSettings;
        int stutterCount = 4;
        float stutterVolumeReductionStep = 0.2f;
        float stutterPitchShiftSemitones = 1.0f;
        bool stutterTruncateEnabled = false;
        float stutterStartFraction = 0.0f;
        std::map<int, juce::File> stutterUndoBackup;
    };

    SliceStateStore() = default;

    SliceStateSnapshot getSnapshot() const;

    void setCacheData (AudioCacheStore::CacheData newCacheData);
    void setSliceSettings (double newBpm,
                           int newSubdivisionSteps,
                           int newSampleCountSetting,
                           bool newTransientDetectionEnabled);
    void setSourceMode (SourceMode newMode);
    void setRandomSubdivisionEnabled (bool enabled);
    void setCaching (bool cachingState);
    bool isCaching() const;

    void setAlignedSlices (std::vector<SliceInfo> newSliceInfos,
                           std::vector<juce::File> newPreviewSnippetURLs,
                           std::vector<SliceVolumeSetting> newSliceVolumeSettings);

    void replaceAllState (std::vector<SliceInfo> newSliceInfos,
                          std::vector<juce::File> newPreviewSnippetURLs,
                          std::vector<SliceVolumeSetting> newSliceVolumeSettings,
                          juce::File newPreviewChainURL);

    void setPreviewChainURL (juce::File newPreviewChainURL);
    void setSourceDirectory (juce::File newSourceDirectory);
    void setSourceFile (juce::File newSourceFile);
    void setLayeringState (bool newLayeringMode, int newSampleCount);
    void setMergeMode (MergeMode newMergeMode);
    void setManualReverseEnabled (bool newManualReverseEnabled);
    void setExportSettingsLocked (bool newExportSettingsLocked);
    void setExportSettings (ExportSettings newExportSettings);
    void setStutterSettings (int newStutterCount,
                             float newStutterVolumeReductionStep,
                             float newStutterPitchShiftSemitones,
                             bool newStutterTruncateEnabled,
                             float newStutterStartFraction);
    void clearStutterUndoBackup();
    void setStutterUndoBackupEntry (int index, juce::File originalSnippet);

private:
    void enforceAlignmentOrAssert (const std::vector<SliceInfo>& newSliceInfos,
                                   const std::vector<juce::File>& newPreviewSnippetURLs,
                                   const std::vector<SliceVolumeSetting>& newSliceVolumeSettings) const;

    mutable juce::CriticalSection stateLock;
    juce::File sourceDirectory;
    juce::File sourceFile;
    AudioCacheStore::CacheData cacheData;
    SourceMode sourceMode = SourceMode::multi;
    double bpm = 128.0;
    int subdivisionSteps = 4;
    int sampleCountSetting = 16;
    bool randomSubdivisionEnabled = false;
    bool transientDetectionEnabled = true;
    bool isCachingState = false;
    std::vector<SliceInfo> sliceInfos;
    std::vector<juce::File> previewSnippetURLs;
    std::vector<SliceVolumeSetting> sliceVolumeSettings;
    juce::File previewChainURL;
    bool layeringMode = false;
    int sampleCount = 0;
    MergeMode mergeMode = MergeMode::none;
    bool manualReverseEnabled = false;
    bool exportSettingsLocked = false;
    ExportSettings exportSettings;
    int stutterCount = 4;
    float stutterVolumeReductionStep = 0.2f;
    float stutterPitchShiftSemitones = 1.0f;
    bool stutterTruncateEnabled = false;
    float stutterStartFraction = 0.0f;
    std::map<int, juce::File> stutterUndoBackup;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SliceStateStore)
};
