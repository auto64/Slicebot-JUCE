#include "PreviewChainOrchestrator.h"

PreviewChainOrchestrator::PreviewChainOrchestrator (SliceStateStore& store)
    : stateStore (store)
{
}

bool PreviewChainOrchestrator::rebuildPreviewChain()
{
    return false;
}
