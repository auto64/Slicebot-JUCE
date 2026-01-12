#include "MutationOrchestrator.h"
#include "BackgroundWorker.h"
#include "PreviewChainOrchestrator.h"

MutationOrchestrator::MutationOrchestrator (SliceStateStore& store)
    : stateStore (store)
{
}

void MutationOrchestrator::setCaching (bool cachingState)
{
    caching.store (cachingState);
}

bool MutationOrchestrator::isCaching() const
{
    return caching.load();
}

bool MutationOrchestrator::requestResliceSingle (int index)
{
    if (! guardMutation())
        return false;

    if (! validateIndex (index))
        return false;

    if (! validateAlignment())
        return false;

    BackgroundWorker worker;
    worker.enqueue ([] {});

    PreviewChainOrchestrator previewChain (stateStore);
    previewChain.rebuildPreviewChain();

    return true;
}

bool MutationOrchestrator::requestResliceAll()
{
    if (! guardMutation())
        return false;

    if (! validateAlignment())
        return false;

    BackgroundWorker worker;
    worker.enqueue ([] {});

    PreviewChainOrchestrator previewChain (stateStore);
    previewChain.rebuildPreviewChain();

    return true;
}

bool MutationOrchestrator::requestRegenerateSingle (int index)
{
    if (! guardMutation())
        return false;

    if (! validateIndex (index))
        return false;

    if (! validateAlignment())
        return false;

    BackgroundWorker worker;
    worker.enqueue ([] {});

    PreviewChainOrchestrator previewChain (stateStore);
    previewChain.rebuildPreviewChain();

    return true;
}

bool MutationOrchestrator::requestRegenerateAll()
{
    if (! guardMutation())
        return false;

    if (! validateAlignment())
        return false;

    BackgroundWorker worker;
    worker.enqueue ([] {});

    PreviewChainOrchestrator previewChain (stateStore);
    previewChain.rebuildPreviewChain();

    return true;
}

void MutationOrchestrator::clearStutterUndoBackup()
{
    stutterUndoBackup = juce::File();
}

bool MutationOrchestrator::hasStutterUndoBackup() const
{
    return stutterUndoBackup != juce::File();
}

bool MutationOrchestrator::guardMutation() const
{
    return ! caching.load();
}

bool MutationOrchestrator::validateIndex (int index) const
{
    if (index < 0)
        return false;

    const auto snapshot = stateStore.getSnapshot();
    return index < static_cast<int> (snapshot.sliceInfos.size());
}

bool MutationOrchestrator::validateAlignment() const
{
    const auto snapshot = stateStore.getSnapshot();
    const std::size_t size = snapshot.sliceInfos.size();
    return snapshot.previewSnippetURLs.size() == size
        && snapshot.sliceVolumeSettings.size() == size;
}
