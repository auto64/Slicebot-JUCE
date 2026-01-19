#include "SliceContextActions.h"
#include "SliceContextState.h"
#include "SliceStateStore.h"
#include "MutationOrchestrator.h"

namespace
{
    SliceContextActionResult makeResult (const juce::String& text, bool dismiss = true)
    {
        SliceContextActionResult result;
        result.statusText = text;
        result.shouldDismissOverlay = dismiss;
        return result;
    }

    bool isValidSliceIndex (int index, const std::vector<SliceStateStore::SliceInfo>& infos)
    {
        return index >= 0 && index < static_cast<int> (infos.size());
    }

    void clearPendingAction (SliceContextState& contextState)
    {
        contextState.pendingOperation = SliceContextState::PendingOperation::none;
        contextState.pendingSourceSliceIndex = -1;
    }
}

SliceContextActionResult handleSliceContextAction (SliceContextAction action,
                                                   int index,
                                                   SliceStateStore& stateStore,
                                                   SliceContextState& contextState,
                                                   AudioEngine& audioEngine)
{
    const auto snapshot = stateStore.getSnapshot();
    if (! isValidSliceIndex (index, snapshot.sliceInfos))
        return makeResult ("Slice index out of range.");

    auto sliceInfos = snapshot.sliceInfos;
    auto previewSnippetURLs = snapshot.previewSnippetURLs;
    auto sliceVolumeSettings = snapshot.sliceVolumeSettings;

    auto& sliceInfo = sliceInfos[static_cast<std::size_t> (index)];
    const bool isLocked = sliceInfo.isLocked;

    const auto sliceLabel = "Slice " + juce::String (index + 1) + " ";

    auto toggleFlag = [&](bool& flag, const juce::String& enabledText, const juce::String& disabledText)
    {
        flag = ! flag;
        stateStore.setAlignedSlices (std::move (sliceInfos),
                                     std::move (previewSnippetURLs),
                                     std::move (sliceVolumeSettings));
        clearPendingAction (contextState);
        return makeResult (sliceLabel + (flag ? enabledText : disabledText));
    };

    switch (action)
    {
        case SliceContextAction::lock:
            return toggleFlag (sliceInfo.isLocked, "locked.", "unlocked.");
        case SliceContextAction::remove:
            if (isLocked)
                return makeResult (sliceLabel + "is locked.");
            return toggleFlag (sliceInfo.isDeleted, "deleted.", "restored.");
        case SliceContextAction::reverse:
            if (isLocked)
                return makeResult (sliceLabel + "is locked.");
            return toggleFlag (sliceInfo.isReversed, "reversed.", "normal.");
        case SliceContextAction::regen:
        {
            if (isLocked)
                return makeResult (sliceLabel + "is locked.");
            clearPendingAction (contextState);
            MutationOrchestrator orchestrator (stateStore, &audioEngine);
            const bool ok = orchestrator.requestRegenerateSingle (index);
            return makeResult (ok ? sliceLabel + "regenerated."
                                  : sliceLabel + "regen failed.");
        }
        case SliceContextAction::swap:
        case SliceContextAction::duplicate:
        {
            if (isLocked)
                return makeResult (sliceLabel + "is locked.");
            const auto desiredOperation =
                action == SliceContextAction::swap
                    ? SliceContextState::PendingOperation::swap
                    : SliceContextState::PendingOperation::duplicate;
            if (contextState.pendingOperation == desiredOperation
                && contextState.pendingSourceSliceIndex == index)
            {
                clearPendingAction (contextState);
                return makeResult (action == SliceContextAction::swap
                                       ? "Swap cancelled."
                                       : "Duplicate cancelled.");
            }

            contextState.pendingOperation = desiredOperation;
            contextState.pendingSourceSliceIndex = index;
            return makeResult (action == SliceContextAction::swap
                                   ? "Swap armed. Select target slice."
                                   : "Duplicate armed. Select target slice.");
        }
        default:
            break;
    }

    return makeResult ("Unknown action.");
}
