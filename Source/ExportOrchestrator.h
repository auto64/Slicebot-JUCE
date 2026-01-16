#pragma once

#include <JuceHeader.h>
#include "SliceStateStore.h"

class PreviewChainOrchestrator
{
public:
    explicit PreviewChainOrchestrator (SliceStateStore& stateStore);

    bool rebuildPreviewChain();
    bool rebuildLoopChainWithVolume();

private:
    SliceStateStore& stateStore;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PreviewChainOrchestrator)
};
