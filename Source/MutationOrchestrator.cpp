#include "MutationOrchestrator.h"
#include "AudioFileIO.h"
#include "BackgroundWorker.h"
#include "PreviewChainOrchestrator.h"
#include "SliceInfrastructure.h"

namespace
{
    constexpr double kTargetSampleRate = 44100.0;
    constexpr double kDefaultBpm = 120.0;
    constexpr bool kRandomSubdivisionModeEnabled = true;
    constexpr int kSelectedSubdivision = 4;
    constexpr bool kTransientDetectEnabled = true;
    const juce::Array<int> kAllowedSubdivisionsSteps = { 8, 4, 2, 1 };

    double sanitizedBpm()
    {
        if (kDefaultBpm <= 0.0)
            return 128.0;

        return kDefaultBpm;
    }

    double secondsPerBeat()
    {
        return 60.0 / sanitizedBpm();
    }

    int windowFramesPerBar()
    {
        const double seconds = secondsPerBeat() * 4.0;
        return static_cast<int> (std::lround (seconds * kTargetSampleRate));
    }

    int subdivisionToBeats (int subdivisionSteps)
    {
        switch (subdivisionSteps)
        {
            case 8: return 8;
            case 4: return 4;
            case 2: return 2;
            case 1: return 1;
            default: break;
        }

        return 4;
    }

    int resolvedSelectedSubdivision()
    {
        for (const int step : kAllowedSubdivisionsSteps)
        {
            if (step == kSelectedSubdivision)
                return step;
        }

        return 4;
    }

    int subdivisionToFrameCount (int subdivisionSteps)
    {
        const int beats = subdivisionToBeats (subdivisionSteps);
        const double durationSeconds = secondsPerBeat() * static_cast<double> (beats);
        return static_cast<int> (std::lround (durationSeconds * kTargetSampleRate));
    }

    int computedNoGoZoneFrames()
    {
        const double seconds = std::ceil (secondsPerBeat() * 8.0);
        return static_cast<int> (std::lround (seconds * kTargetSampleRate));
    }
}

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
    bool rebuildOk = false;
    worker.enqueue ([&]
    {
        const auto snapshot = stateStore.getSnapshot();
        if (index < 0 || index >= static_cast<int> (snapshot.sliceInfos.size()))
            return;

        auto sliceInfos = snapshot.sliceInfos;
        auto previewSnippetURLs = snapshot.previewSnippetURLs;
        auto sliceVolumeSettings = snapshot.sliceVolumeSettings;

        SliceProcessingFlags flags;
        flags.layeringMode = false;
        flags.sampleCount = static_cast<int> (sliceInfos.size());

        if (flags.layeringMode)
        {
            if (flags.sampleCount <= 0 || static_cast<int> (sliceInfos.size()) != flags.sampleCount * 2)
                return;
        }

        const int logicalIndex = flags.layeringMode ? (index % flags.sampleCount) : index;
        const int leftIndex = logicalIndex;
        const int rightIndex = flags.layeringMode ? logicalIndex + flags.sampleCount : -1;

        auto resliceAtIndex = [&] (int targetIndex)
        {
            const auto& sliceInfo = sliceInfos[static_cast<std::size_t> (targetIndex)];
            const juce::File sourceFile = sliceInfo.fileURL;

            AudioFileIO audioFileIO;
            AudioFileIO::ConvertedAudio converted;
            juce::String formatDescription;

            if (! audioFileIO.readToMonoBuffer (sourceFile, converted, formatDescription))
                return false;

            const int noGoZoneFrames = computedNoGoZoneFrames();
            const int fileDurationFrames = converted.buffer.getNumSamples();
            const int maxCandidateStart = juce::jmax (0, fileDurationFrames - noGoZoneFrames);

            juce::Random random;
            int startFrame = random.nextInt (maxCandidateStart + 1);

            const int snippetFrameCount = sliceInfo.snippetFrameCount;
            const int windowFrames = windowFramesPerBar();
            const auto refined = refinedStart (converted.buffer, startFrame, windowFrames, kTransientDetectEnabled);
            if (refined.has_value())
                startFrame = refined.value();

            if (startFrame + snippetFrameCount > fileDurationFrames)
                return false;

            const juce::File outputFile = previewSnippetURLs[static_cast<std::size_t> (targetIndex)];

            juce::AudioBuffer<float> sliceBuffer (1, snippetFrameCount);
            sliceBuffer.copyFrom (0, 0, converted.buffer, 0, startFrame, snippetFrameCount);

            AudioFileIO::ConvertedAudio sliceAudio;
            sliceAudio.buffer = std::move (sliceBuffer);
            sliceAudio.sampleRate = kTargetSampleRate;

            if (! audioFileIO.writeMonoWav16 (outputFile, sliceAudio))
                return false;

            SliceStateStore::SliceInfo updatedInfo = sliceInfo;
            updatedInfo.startFrame = startFrame;
            sliceInfos[static_cast<std::size_t> (targetIndex)] = updatedInfo;
            return true;
        };

        if (! resliceAtIndex (leftIndex))
            return;

        if (flags.layeringMode && rightIndex >= 0)
        {
            if (! resliceAtIndex (rightIndex))
                return;
        }

        stateStore.setAlignedSlices (std::move (sliceInfos),
                                     std::move (previewSnippetURLs),
                                     std::move (sliceVolumeSettings));

        PreviewChainOrchestrator previewChain (stateStore);
        rebuildOk = previewChain.rebuildPreviewChain();
        if (rebuildOk)
            clearStutterUndoBackup();
    });

    return rebuildOk;
}

bool MutationOrchestrator::requestResliceAll()
{
    if (! guardMutation())
        return false;

    if (! validateAlignment())
        return false;

    BackgroundWorker worker;
    bool rebuildOk = false;
    worker.enqueue ([&]
    {
        const auto snapshot = stateStore.getSnapshot();
        auto sliceInfos = snapshot.sliceInfos;
        auto previewSnippetURLs = snapshot.previewSnippetURLs;
        auto sliceVolumeSettings = snapshot.sliceVolumeSettings;

        if (sliceInfos.empty())
            return;

        SliceProcessingFlags flags;
        flags.layeringMode = false;
        flags.sampleCount = static_cast<int> (sliceInfos.size());

        if (flags.layeringMode)
        {
            if (flags.sampleCount <= 0 || static_cast<int> (sliceInfos.size()) != flags.sampleCount * 2)
                return;
        }

        AudioFileIO audioFileIO;
        const int noGoZoneFrames = computedNoGoZoneFrames();
        const int windowFrames = windowFramesPerBar();
        juce::Random random;

        const int loopCount = flags.layeringMode ? flags.sampleCount : static_cast<int> (sliceInfos.size());

        for (int logicalIndex = 0; logicalIndex < loopCount; ++logicalIndex)
        {
            const int leftIndex = logicalIndex;
            const int rightIndex = flags.layeringMode ? logicalIndex + flags.sampleCount : -1;

            auto resliceAtIndex = [&] (int targetIndex)
            {
                const auto& sliceInfo = sliceInfos[static_cast<std::size_t> (targetIndex)];
                const juce::File sourceFile = sliceInfo.fileURL;

                AudioFileIO::ConvertedAudio converted;
                juce::String formatDescription;

                if (! audioFileIO.readToMonoBuffer (sourceFile, converted, formatDescription))
                    return false;

                const int fileDurationFrames = converted.buffer.getNumSamples();
                const int maxCandidateStart = juce::jmax (0, fileDurationFrames - noGoZoneFrames);
                int startFrame = random.nextInt (maxCandidateStart + 1);

                const int snippetFrameCount = sliceInfo.snippetFrameCount;
                const auto refined = refinedStart (converted.buffer, startFrame, windowFrames, kTransientDetectEnabled);
                if (refined.has_value())
                    startFrame = refined.value();

                if (startFrame + snippetFrameCount > fileDurationFrames)
                    return false;

                const juce::File outputFile = previewSnippetURLs[static_cast<std::size_t> (targetIndex)];

                juce::AudioBuffer<float> sliceBuffer (1, snippetFrameCount);
                sliceBuffer.copyFrom (0, 0, converted.buffer, 0, startFrame, snippetFrameCount);

                AudioFileIO::ConvertedAudio sliceAudio;
                sliceAudio.buffer = std::move (sliceBuffer);
                sliceAudio.sampleRate = kTargetSampleRate;

                if (! audioFileIO.writeMonoWav16 (outputFile, sliceAudio))
                    return false;

                SliceStateStore::SliceInfo updatedInfo = sliceInfo;
                updatedInfo.startFrame = startFrame;
                sliceInfos[static_cast<std::size_t> (targetIndex)] = updatedInfo;
                return true;
            };

            if (! resliceAtIndex (leftIndex))
                continue;

            if (flags.layeringMode && rightIndex >= 0)
            {
                if (! resliceAtIndex (rightIndex))
                    continue;
            }
        }

        stateStore.setAlignedSlices (std::move (sliceInfos),
                                     std::move (previewSnippetURLs),
                                     std::move (sliceVolumeSettings));

        PreviewChainOrchestrator previewChain (stateStore);
        rebuildOk = previewChain.rebuildPreviewChain();
        if (rebuildOk)
            clearStutterUndoBackup();
    });

    return rebuildOk;
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
    bool rebuildOk = false;
    worker.enqueue ([&]
    {
        const auto snapshot = stateStore.getSnapshot();
        if (index < 0 || index >= static_cast<int> (snapshot.sliceInfos.size()))
            return;

        auto sliceInfos = snapshot.sliceInfos;
        auto previewSnippetURLs = snapshot.previewSnippetURLs;
        auto sliceVolumeSettings = snapshot.sliceVolumeSettings;

        SliceProcessingFlags flags;
        flags.layeringMode = false;
        flags.sampleCount = static_cast<int> (sliceInfos.size());

        if (flags.layeringMode)
        {
            if (flags.sampleCount <= 0 || static_cast<int> (sliceInfos.size()) != flags.sampleCount * 2)
                return;
        }

        const int logicalIndex = flags.layeringMode ? (index % flags.sampleCount) : index;
        const int leftIndex = logicalIndex;
        const int rightIndex = flags.layeringMode ? logicalIndex + flags.sampleCount : -1;

        auto regenerateAtIndex = [&] (int targetIndex)
        {
            const auto& sliceInfo = sliceInfos[static_cast<std::size_t> (targetIndex)];
            const juce::File sourceFile = sliceInfo.fileURL;
            const int startFrame = sliceInfo.startFrame;
            const int snippetFrameCount = sliceInfo.snippetFrameCount;

            AudioFileIO audioFileIO;
            AudioFileIO::ConvertedAudio converted;
            juce::String formatDescription;

            if (! audioFileIO.readToMonoBuffer (sourceFile, converted, formatDescription))
                return false;

            const int fileDurationFrames = converted.buffer.getNumSamples();
            if (startFrame + snippetFrameCount > fileDurationFrames)
                return false;

            const juce::File outputFile = previewSnippetURLs[static_cast<std::size_t> (targetIndex)];

            juce::AudioBuffer<float> sliceBuffer (1, snippetFrameCount);
            sliceBuffer.copyFrom (0, 0, converted.buffer, 0, startFrame, snippetFrameCount);

            AudioFileIO::ConvertedAudio sliceAudio;
            sliceAudio.buffer = std::move (sliceBuffer);
            sliceAudio.sampleRate = kTargetSampleRate;

            if (! audioFileIO.writeMonoWav16 (outputFile, sliceAudio))
                return false;

            return true;
        };

        if (! regenerateAtIndex (leftIndex))
            return;

        if (flags.layeringMode && rightIndex >= 0)
        {
            if (! regenerateAtIndex (rightIndex))
                return;
        }

        stateStore.setAlignedSlices (std::move (sliceInfos),
                                     std::move (previewSnippetURLs),
                                     std::move (sliceVolumeSettings));

        PreviewChainOrchestrator previewChain (stateStore);
        rebuildOk = previewChain.rebuildPreviewChain();
        if (rebuildOk)
            clearStutterUndoBackup();
    });

    return rebuildOk;
}

bool MutationOrchestrator::requestRegenerateAll()
{
    if (! guardMutation())
        return false;

    if (! validateAlignment())
        return false;

    BackgroundWorker worker;
    bool rebuildOk = false;
    worker.enqueue ([&]
    {
        const auto snapshot = stateStore.getSnapshot();
        auto sliceInfos = snapshot.sliceInfos;
        auto previewSnippetURLs = snapshot.previewSnippetURLs;
        auto sliceVolumeSettings = snapshot.sliceVolumeSettings;

        if (sliceInfos.empty())
            return;

        SliceProcessingFlags flags;
        flags.layeringMode = false;
        flags.sampleCount = static_cast<int> (sliceInfos.size());

        if (flags.layeringMode)
        {
            if (flags.sampleCount <= 0 || static_cast<int> (sliceInfos.size()) != flags.sampleCount * 2)
                return;
        }

        AudioFileIO audioFileIO;
        juce::Random random;

        const int loopCount = flags.layeringMode ? flags.sampleCount : static_cast<int> (sliceInfos.size());

        for (int logicalIndex = 0; logicalIndex < loopCount; ++logicalIndex)
        {
            const int leftIndex = logicalIndex;
            const int rightIndex = flags.layeringMode ? logicalIndex + flags.sampleCount : -1;

            auto regenerateAtIndex = [&] (int targetIndex)
            {
                const auto& sliceInfo = sliceInfos[static_cast<std::size_t> (targetIndex)];
                const juce::File sourceFile = sliceInfo.fileURL;
                const int startFrame = sliceInfo.startFrame;
                const int originalFrameCount = sliceInfo.snippetFrameCount;

                AudioFileIO::ConvertedAudio converted;
                juce::String formatDescription;

                if (! audioFileIO.readToMonoBuffer (sourceFile, converted, formatDescription))
                    return false;

                const int fileDurationFrames = converted.buffer.getNumSamples();
                int snippetFrameCount = originalFrameCount;

                if (kRandomSubdivisionModeEnabled)
                {
                    const int subdivisionSteps =
                        kAllowedSubdivisionsSteps[random.nextInt (kAllowedSubdivisionsSteps.size())];
                    snippetFrameCount = subdivisionToFrameCount (subdivisionSteps);
                }

                if (startFrame + snippetFrameCount > fileDurationFrames)
                    return false;

                const juce::File outputFile = previewSnippetURLs[static_cast<std::size_t> (targetIndex)];

                juce::AudioBuffer<float> sliceBuffer (1, snippetFrameCount);
                sliceBuffer.copyFrom (0, 0, converted.buffer, 0, startFrame, snippetFrameCount);

                AudioFileIO::ConvertedAudio sliceAudio;
                sliceAudio.buffer = std::move (sliceBuffer);
                sliceAudio.sampleRate = kTargetSampleRate;

                if (! audioFileIO.writeMonoWav16 (outputFile, sliceAudio))
                    return false;

                if (! kRandomSubdivisionModeEnabled)
                {
                    SliceStateStore::SliceInfo updatedInfo = sliceInfo;
                    updatedInfo.snippetFrameCount = snippetFrameCount;
                    sliceInfos[static_cast<std::size_t> (targetIndex)] = updatedInfo;
                }

                return true;
            };

            if (! regenerateAtIndex (leftIndex))
                continue;

            if (flags.layeringMode && rightIndex >= 0)
            {
                if (! regenerateAtIndex (rightIndex))
                    continue;
            }
        }

        stateStore.setAlignedSlices (std::move (sliceInfos),
                                     std::move (previewSnippetURLs),
                                     std::move (sliceVolumeSettings));

        PreviewChainOrchestrator previewChain (stateStore);
        rebuildOk = previewChain.rebuildPreviewChain();
        if (rebuildOk)
            clearStutterUndoBackup();
    });

    return rebuildOk;
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
