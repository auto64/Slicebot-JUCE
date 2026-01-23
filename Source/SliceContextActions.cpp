#include "SliceContextActions.h"
#include "AudioFileIO.h"
#include "MutationOrchestrator.h"
#include "PreviewChainOrchestrator.h"
#include "SliceContextState.h"
#include "SliceStateStore.h"

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

    bool rebuildPreviewChain (SliceStateStore& stateStore)
    {
        PreviewChainOrchestrator previewChain (stateStore);
        return previewChain.rebuildPreviewChain();
    }

    bool writeSilentPreview (const SliceStateStore::SliceInfo& sliceInfo, const juce::File& previewFile)
    {
        AudioFileIO audioFileIO;
        AudioFileIO::ConvertedAudio output;
        juce::String formatDescription;
        int frameCount = sliceInfo.snippetFrameCount;

        if (previewFile.existsAsFile())
        {
            AudioFileIO::ConvertedAudio existingAudio;
            if (audioFileIO.readToMonoBuffer (previewFile, existingAudio, formatDescription))
            {
                output.sampleRate = existingAudio.sampleRate;
                frameCount = existingAudio.buffer.getNumSamples();
            }
        }

        if (frameCount <= 0)
            return false;

        if (previewFile.existsAsFile())
            previewFile.deleteFile();

        output.buffer.setSize (1, frameCount);
        output.buffer.clear();

        return audioFileIO.writeMonoWav16 (previewFile, output);
    }

    bool rebuildPreviewFromSource (const SliceStateStore::SliceInfo& sliceInfo,
                                   const juce::File& previewFile,
                                   bool shouldReverse)
    {
        AudioFileIO audioFileIO;
        AudioFileIO::ConvertedAudio sliceAudio;
        juce::String formatDescription;
        bool loaded = false;

        if (sliceInfo.snippetFrameCount > 0 && sliceInfo.fileURL.existsAsFile())
        {
            loaded = audioFileIO.readToMonoBufferSegment (sliceInfo.fileURL,
                                                         sliceInfo.startFrame,
                                                         sliceInfo.snippetFrameCount,
                                                         sliceAudio,
                                                         formatDescription);
        }

        if (! loaded && previewFile.existsAsFile())
            loaded = audioFileIO.readToMonoBuffer (previewFile, sliceAudio, formatDescription);

        if (! loaded)
            return false;

        if (shouldReverse)
        {
            auto& buffer = sliceAudio.buffer;
            const int samples = buffer.getNumSamples();
            for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
            {
                auto* data = buffer.getWritePointer (channel);
                for (int left = 0, right = samples - 1; left < right; ++left, --right)
                    std::swap (data[left], data[right]);
            }
        }

        if (previewFile.existsAsFile())
            previewFile.deleteFile();

        return audioFileIO.writeMonoWav16 (previewFile, sliceAudio);
    }

    bool swapPreviewFiles (const juce::File& leftFile, const juce::File& rightFile)
    {
        if (leftFile == rightFile)
            return true;

        if (! leftFile.existsAsFile() || ! rightFile.existsAsFile())
            return false;

        const auto tempDir = juce::File::getSpecialLocation (juce::File::tempDirectory);
        const auto tempFile = tempDir.getNonexistentChildFile ("slice_swap", ".wav", false);

        if (! leftFile.copyFileTo (tempFile))
            return false;
        if (! rightFile.copyFileTo (leftFile))
            return false;
        if (! tempFile.copyFileTo (rightFile))
            return false;

        tempFile.deleteFile();
        return true;
    }

    bool copyPreviewFile (const juce::File& sourceFile, const juce::File& targetFile)
    {
        if (! sourceFile.existsAsFile())
            return false;

        if (targetFile.existsAsFile())
            targetFile.deleteFile();

        return sourceFile.copyFileTo (targetFile);
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
            sliceInfo.isDeleted = ! sliceInfo.isDeleted;
            stateStore.setAlignedSlices (std::move (sliceInfos),
                                         std::move (previewSnippetURLs),
                                         std::move (sliceVolumeSettings));
            clearPendingAction (contextState);
            {
                const auto& previewFile = snapshot.previewSnippetURLs[static_cast<std::size_t> (index)];
                bool ok = true;
                if (sliceInfo.isDeleted)
                    ok = writeSilentPreview (sliceInfo, previewFile);
                else
                    ok = rebuildPreviewFromSource (sliceInfo, previewFile, sliceInfo.isReversed);

                if (! ok)
                    return makeResult (sliceLabel + "delete toggle failed.");

                if (! rebuildPreviewChain (stateStore))
                    return makeResult ("Preview chain rebuild failed.");
            }
            return makeResult (sliceLabel + (sliceInfo.isDeleted ? "deleted." : "restored."));
        case SliceContextAction::reverse:
            if (isLocked)
                return makeResult (sliceLabel + "is locked.");
            sliceInfo.isReversed = ! sliceInfo.isReversed;
            stateStore.setAlignedSlices (std::move (sliceInfos),
                                         std::move (previewSnippetURLs),
                                         std::move (sliceVolumeSettings));
            clearPendingAction (contextState);
            if (! sliceInfo.isDeleted)
            {
                const auto& previewFile = snapshot.previewSnippetURLs[static_cast<std::size_t> (index)];
                if (! rebuildPreviewFromSource (sliceInfo, previewFile, sliceInfo.isReversed))
                    return makeResult (sliceLabel + "reverse failed.");
                if (! rebuildPreviewChain (stateStore))
                    return makeResult ("Preview chain rebuild failed.");
            }
            return makeResult (sliceLabel + (sliceInfo.isReversed ? "reversed." : "normal."));
        case SliceContextAction::regen:
        {
            if (isLocked)
                return makeResult (sliceLabel + "is locked.");
            clearPendingAction (contextState);
            MutationOrchestrator orchestrator (stateStore, &audioEngine);
            bool ok = orchestrator.requestRegenerateSingle (index);
            if (! ok)
                return makeResult (sliceLabel + "regen failed.");
            if (sliceInfo.isDeleted)
            {
                const auto& previewFile = snapshot.previewSnippetURLs[static_cast<std::size_t> (index)];
                if (! writeSilentPreview (sliceInfo, previewFile))
                    return makeResult (sliceLabel + "regen failed.");
            }
            return makeResult (sliceLabel + "regenerated.");
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

SliceContextTargetResult handleSliceContextTargetSelection (int targetIndex,
                                                            SliceStateStore& stateStore,
                                                            SliceContextState& contextState,
                                                            AudioEngine&)
{
    SliceContextTargetResult result;

    if (contextState.pendingOperation == SliceContextState::PendingOperation::none)
        return result;

    const auto snapshot = stateStore.getSnapshot();
    if (! isValidSliceIndex (targetIndex, snapshot.sliceInfos))
    {
        result.didHandle = true;
        result.actionResult = makeResult ("Slice index out of range.");
        clearPendingAction (contextState);
        return result;
    }

    const int sourceIndex = contextState.pendingSourceSliceIndex;
    if (! isValidSliceIndex (sourceIndex, snapshot.sliceInfos))
    {
        result.didHandle = true;
        result.actionResult = makeResult ("Source slice invalid.");
        clearPendingAction (contextState);
        return result;
    }

    if (sourceIndex == targetIndex)
    {
        const bool isSwap = contextState.pendingOperation == SliceContextState::PendingOperation::swap;
        result.didHandle = true;
        result.actionResult = makeResult (isSwap ? "Swap cancelled." : "Duplicate cancelled.");
        clearPendingAction (contextState);
        return result;
    }

    auto sliceInfos = snapshot.sliceInfos;
    auto previewSnippetURLs = snapshot.previewSnippetURLs;
    auto sliceVolumeSettings = snapshot.sliceVolumeSettings;

    const auto& sourceInfo = sliceInfos[static_cast<std::size_t> (sourceIndex)];
    const auto& targetInfo = sliceInfos[static_cast<std::size_t> (targetIndex)];

    if (sourceInfo.isLocked || targetInfo.isLocked)
    {
        result.didHandle = true;
        result.actionResult = makeResult ("Target slice is locked.");
        clearPendingAction (contextState);
        return result;
    }

    const auto& sourcePreview = previewSnippetURLs[static_cast<std::size_t> (sourceIndex)];
    const auto& targetPreview = previewSnippetURLs[static_cast<std::size_t> (targetIndex)];

    if (contextState.pendingOperation == SliceContextState::PendingOperation::swap)
    {
        if (! swapPreviewFiles (sourcePreview, targetPreview))
        {
            result.didHandle = true;
            result.actionResult = makeResult ("Swap failed.");
            clearPendingAction (contextState);
            return result;
        }

        std::swap (sliceInfos[static_cast<std::size_t> (sourceIndex)],
                   sliceInfos[static_cast<std::size_t> (targetIndex)]);
        std::swap (sliceVolumeSettings[static_cast<std::size_t> (sourceIndex)],
                   sliceVolumeSettings[static_cast<std::size_t> (targetIndex)]);

        stateStore.setAlignedSlices (std::move (sliceInfos),
                                     std::move (previewSnippetURLs),
                                     std::move (sliceVolumeSettings));
        clearPendingAction (contextState);
        if (! rebuildPreviewChain (stateStore))
        {
            result.didHandle = true;
            result.actionResult = makeResult ("Preview chain rebuild failed.");
            return result;
        }

        result.didHandle = true;
        result.actionResult = makeResult ("Slices swapped.");
        return result;
    }

    if (contextState.pendingOperation == SliceContextState::PendingOperation::duplicate)
    {
        if (! copyPreviewFile (sourcePreview, targetPreview))
        {
            result.didHandle = true;
            result.actionResult = makeResult ("Duplicate failed.");
            clearPendingAction (contextState);
            return result;
        }

        sliceInfos[static_cast<std::size_t> (targetIndex)] =
            sliceInfos[static_cast<std::size_t> (sourceIndex)];
        sliceVolumeSettings[static_cast<std::size_t> (targetIndex)] =
            sliceVolumeSettings[static_cast<std::size_t> (sourceIndex)];

        stateStore.setAlignedSlices (std::move (sliceInfos),
                                     std::move (previewSnippetURLs),
                                     std::move (sliceVolumeSettings));
        clearPendingAction (contextState);
        if (! rebuildPreviewChain (stateStore))
        {
            result.didHandle = true;
            result.actionResult = makeResult ("Preview chain rebuild failed.");
            return result;
        }

        result.didHandle = true;
        result.actionResult = makeResult ("Slice duplicated.");
        return result;
    }

    return result;
}
