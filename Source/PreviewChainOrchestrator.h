#pragma once

#ifndef SLICEBOT_PREVIEW_CHAIN_ORCHESTRATOR_H
#define SLICEBOT_PREVIEW_CHAIN_ORCHESTRATOR_H

#include <JuceHeader.h>
#include "SliceStateStore.h"

class PreviewChainOrchestrator
{
public:
    explicit PreviewChainOrchestrator (SliceStateStore& stateStore);

    bool rebuildPreviewChain() const;
    bool rebuildLoopChainWithVolume() const;

private:
    SliceStateStore& stateStore;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PreviewChainOrchestrator)
};

#endif
