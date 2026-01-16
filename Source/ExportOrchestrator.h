#pragma once

#include <JuceHeader.h>
#include <optional>
#include "SliceStateStore.h"

class ExportOrchestrator
{
public:
    explicit ExportOrchestrator (SliceStateStore& stateStore);

    bool exportSlices (const std::optional<SliceStateStore::ExportSettings>& overrideSettings);
    bool exportFullChainWithoutVolume (const std::optional<SliceStateStore::ExportSettings>& overrideSettings);
    bool exportFullChainWithVolume (const std::optional<SliceStateStore::ExportSettings>& overrideSettings);

private:
    bool resolveSettings (const std::optional<SliceStateStore::ExportSettings>& overrideSettings,
                          SliceStateStore::ExportSettings& resolved) const;
    bool buildVolumeChain (const SliceStateStore::SliceStateSnapshot& snapshot,
                           const juce::File& chainFile);

    SliceStateStore& stateStore;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ExportOrchestrator)
};
