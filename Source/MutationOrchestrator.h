#pragma once

#include <JuceHeader.h>
#include <optional>
#include "SliceStateStore.h"

class AudioEngine;

class MutationOrchestrator
{
public:
    explicit MutationOrchestrator (SliceStateStore& stateStore, AudioEngine* engine = nullptr);

    void setCaching (bool caching);
    bool isCaching() const;

    bool requestResliceSingle (int index);
    bool requestResliceAll();
    bool requestSliceAll();
    bool requestRegenerateSingle (int index);
    bool requestRegenerateAll();
    bool requestStutterSingle (int index);
    bool requestStutterUndo (int index);
    bool requestPachinkoStutterAll();
    bool requestPachinkoReverseAll();
    bool requestExportSlices (const std::optional<SliceStateStore::ExportSettings>& overrideSettings);
    bool requestExportFullChainWithoutVolume (const std::optional<SliceStateStore::ExportSettings>& overrideSettings);
    bool requestExportFullChainWithVolume (const std::optional<SliceStateStore::ExportSettings>& overrideSettings);

    void clearStutterUndoBackup();
    bool hasStutterUndoBackup() const;

private:
    bool guardMutation() const;
    bool validateIndex (int index) const;
    bool validateAlignment() const;

    SliceStateStore& stateStore;
    AudioEngine* audioEngine = nullptr;
    std::atomic<bool> caching { false };
    juce::File stutterUndoBackup;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MutationOrchestrator)
};
