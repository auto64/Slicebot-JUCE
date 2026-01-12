#include "MutationOrchestrator.h"

#include <cmath>

#include "AudioFileIO.h"
#include "BackgroundWorker.h"
#include "ExportOrchestrator.h"
#include "PreviewChainOrchestrator.h"
#include "SliceInfrastructure.h"

namespace {
    constexpr double kTargetSampleRate = 44100.0;
    constexpr double kDefaultBpm = 120.0;
    constexpr bool kRandomSubdivisionModeEnabled = true;
    constexpr int kSelectedSubdivision = 4;
    constexpr bool kTransientDetectEnabled = true;
    const juce::Array<int> kAllowedSubdivisionsSteps = { 8, 4, 2, 1 };
    constexpr int kPachinkoStutterCountMin = 2;
    constexpr int kPachinkoStutterCountMax = 8;
    constexpr float kPachinkoVolumeReductionMin = 0.0f;
    constexpr float kPachinkoVolumeReductionMax = 0.6f;
    constexpr float kPachinkoPitchShiftMin = -12.0f;
    constexpr float kPachinkoPitchShiftMax = 12.0f;

    double resolvedBpm()
    {
        if (kDefaultBpm <= 0.0)
            return 128.0;

        return kDefaultBpm;
    }

    double secondsPerBeat()
    {
        return 60.0 / resolvedBpm();
    }

    int barWindowFrames()
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

    int resolvedSubdivision()
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

    int noGoZoneFrames()
    {
        const double seconds = std::ceil (secondsPerBeat() * 8.0);
        return static_cast<int> (std::lround (seconds * kTargetSampleRate));
    }

    int startFrameFromFraction (float fraction, int totalFrames)
    {
        if (totalFrames <= 0)
            return 0;

        const float clampedFraction = juce::jlimit (0.0f, 1.0f, fraction);
        int startFrame = static_cast<int> (std::floor (clampedFraction * static_cast<float> (totalFrames)));
        if (startFrame >= totalFrames)
            startFrame = totalFrames - 1;

        return juce::jmax (0, startFrame);
    }

    float semitonesToRatio (float semitones)
    {
        return static_cast<float> (std::pow (2.0f, semitones / 12.0f));
    }

    juce::AudioBuffer<float> buildStutteredBuffer (const juce::AudioBuffer<float>& input,
                                                   int stutterCount,
                                                   float volumeReductionStep,
                                                   float pitchShiftSemitones,
                                                   bool truncateEnabled,
                                                   float startFraction)
    {
        const int totalFrames = input.getNumSamples();
        if (totalFrames <= 0)
            return input;

        if (stutterCount <= 0)
            return input;

        const int startFrame = startFrameFromFraction (startFraction, totalFrames);
        const int remainingFrames = totalFrames - startFrame;
        if (remainingFrames <= 0)
            return input;

        const int segmentLength = juce::jmax (1, (remainingFrames + stutterCount - 1) / stutterCount);
        const int targetFrames = truncateEnabled ? (segmentLength * stutterCount) : totalFrames;

        juce::AudioBuffer<float> output (1, targetFrames);
        output.clear();

        const float* inputData = input.getReadPointer (0);
        float* outputData = output.getWritePointer (0);

        int writePosition = 0;
        if (! truncateEnabled && startFrame > 0)
        {
            output.copyFrom (0, 0, input, 0, 0, startFrame);
            writePosition = startFrame;
        }

        const float pitchRatio = semitonesToRatio (pitchShiftSemitones);
        const float safePitchRatio = pitchRatio > 0.0f ? pitchRatio : 1.0f;

        for (int repeatIndex = 0; repeatIndex < stutterCount && writePosition < targetFrames; ++repeatIndex)
        {
            const float gain = juce::jmax (0.0f, 1.0f - volumeReductionStep * static_cast<float> (repeatIndex));
            double position = 0.0;

            for (int s = 0; s < segmentLength && writePosition < targetFrames; ++s)
            {
                const int baseIndex = static_cast<int> (position) % segmentLength;
                const int nextIndex = (baseIndex + 1) % segmentLength;
                const float frac = static_cast<float> (position - std::floor (position));

                const int sourceIndex = startFrame + baseIndex;
                const int sourceNextIndex = startFrame + nextIndex;
                const float sampleA = inputData[juce::jmin (sourceIndex, totalFrames - 1)];
                const float sampleB = inputData[juce::jmin (sourceNextIndex, totalFrames - 1)];

                const float sample = sampleA + (sampleB - sampleA) * frac;
                outputData[writePosition] = sample * gain;
                ++writePosition;

                position += safePitchRatio;
                while (position >= static_cast<double> (segmentLength))
                    position -= static_cast<double> (segmentLength);
            }
        }

        return output;
    }

    void reverseMonoBuffer (juce::AudioBuffer<float>& buffer)
    {
        const int totalFrames = buffer.getNumSamples();
        if (totalFrames <= 1)
            return;

        float* data = buffer.getWritePointer (0);
        for (int left = 0, right = totalFrames - 1; left < right; ++left, --right)
            std::swap (data[left], data[right]);
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

        const bool layeringMode = snapshot.layeringMode;
        const int sampleCount = snapshot.sampleCount;
        if (layeringMode)
        {
            if (sampleCount <= 0 || static_cast<int> (sliceInfos.size()) != sampleCount * 2)
                return;
        }

        const int logicalIndex = layeringMode ? (index % sampleCount) : index;
        const int leftIndex = logicalIndex;
        const int rightIndex = layeringMode ? logicalIndex + sampleCount : -1;

        auto resliceIndex = [&] (int targetIndex)
        {
            const auto& sliceInfo = sliceInfos[static_cast<std::size_t> (targetIndex)];
            const juce::File sourceFile = sliceInfo.fileURL;

            AudioFileIO audioFileIO;
            AudioFileIO::ConvertedAudio converted;
            juce::String formatDescription;

            if (! audioFileIO.readToMonoBuffer (sourceFile, converted, formatDescription))
                return false;

            const int fileDurationFrames = converted.buffer.getNumSamples();
            const int maxCandidateStart = juce::jmax (0, fileDurationFrames - noGoZoneFrames());

            juce::Random random;
            int startFrame = random.nextInt (maxCandidateStart + 1);

            const int snippetFrameCount = sliceInfo.snippetFrameCount;
            const auto refined = refinedStart (converted.buffer,
                                               startFrame,
                                               barWindowFrames(),
                                               kTransientDetectEnabled);
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

        if (! resliceIndex (leftIndex))
            return;

        if (layeringMode && rightIndex >= 0)
        {
            if (! resliceIndex (rightIndex))
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

        const bool layeringMode = snapshot.layeringMode;
        const int sampleCount = snapshot.sampleCount;
        if (layeringMode)
        {
            if (sampleCount <= 0 || static_cast<int> (sliceInfos.size()) != sampleCount * 2)
                return;
        }

        AudioFileIO audioFileIO;
        juce::Random random;

        const int loopCount = layeringMode ? sampleCount : static_cast<int> (sliceInfos.size());
        for (int logicalIndex = 0; logicalIndex < loopCount; ++logicalIndex)
        {
            const int leftIndex = logicalIndex;
            const int rightIndex = layeringMode ? logicalIndex + sampleCount : -1;

            auto resliceIndex = [&] (int targetIndex)
            {
                const auto& sliceInfo = sliceInfos[static_cast<std::size_t> (targetIndex)];
                const juce::File sourceFile = sliceInfo.fileURL;

                AudioFileIO::ConvertedAudio converted;
                juce::String formatDescription;

                if (! audioFileIO.readToMonoBuffer (sourceFile, converted, formatDescription))
                    return false;

                const int fileDurationFrames = converted.buffer.getNumSamples();
                const int maxCandidateStart = juce::jmax (0, fileDurationFrames - noGoZoneFrames());
                int startFrame = random.nextInt (maxCandidateStart + 1);

                const int snippetFrameCount = sliceInfo.snippetFrameCount;
                const auto refined = refinedStart (converted.buffer,
                                                   startFrame,
                                                   barWindowFrames(),
                                                   kTransientDetectEnabled);
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

            if (! resliceIndex (leftIndex))
                continue;

            if (layeringMode && rightIndex >= 0)
            {
                if (! resliceIndex (rightIndex))
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

        const bool layeringMode = snapshot.layeringMode;
        const int sampleCount = snapshot.sampleCount;
        if (layeringMode)
        {
            if (sampleCount <= 0 || static_cast<int> (sliceInfos.size()) != sampleCount * 2)
                return;
        }

        const int logicalIndex = layeringMode ? (index % sampleCount) : index;
        const int leftIndex = logicalIndex;
        const int rightIndex = layeringMode ? logicalIndex + sampleCount : -1;

        auto regenerateIndex = [&] (int targetIndex)
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

        if (! regenerateIndex (leftIndex))
            return;

        if (layeringMode && rightIndex >= 0)
        {
            if (! regenerateIndex (rightIndex))
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

        const bool layeringMode = snapshot.layeringMode;
        const int sampleCount = snapshot.sampleCount;
        if (layeringMode)
        {
            if (sampleCount <= 0 || static_cast<int> (sliceInfos.size()) != sampleCount * 2)
                return;
        }

        AudioFileIO audioFileIO;
        juce::Random random;

        const int loopCount = layeringMode ? sampleCount : static_cast<int> (sliceInfos.size());
        for (int logicalIndex = 0; logicalIndex < loopCount; ++logicalIndex)
        {
            const int leftIndex = logicalIndex;
            const int rightIndex = layeringMode ? logicalIndex + sampleCount : -1;

            auto regenerateIndex = [&] (int targetIndex)
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

            if (! regenerateIndex (leftIndex))
                continue;

            if (layeringMode && rightIndex >= 0)
            {
                if (! regenerateIndex (rightIndex))
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

bool MutationOrchestrator::requestStutterSingle (int index)
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
        if (index < 0 || index >= static_cast<int> (snapshot.previewSnippetURLs.size()))
            return;

        const juce::File targetFile = snapshot.previewSnippetURLs[static_cast<std::size_t> (index)];
        if (! targetFile.existsAsFile())
            return;

        const juce::File backupFile = targetFile.getSiblingFile ("stutter_undo_" + juce::String (index) + ".wav");
        if (! targetFile.copyFileTo (backupFile))
            return;

        stateStore.setStutterUndoBackupEntry (index, backupFile);
        stutterUndoBackup = backupFile;

        AudioFileIO audioFileIO;
        AudioFileIO::ConvertedAudio converted;
        juce::String formatDescription;

        if (! audioFileIO.readToMonoBuffer (targetFile, converted, formatDescription))
            return;

        const auto stuttered = buildStutteredBuffer (converted.buffer,
                                                     snapshot.stutterCount,
                                                     snapshot.stutterVolumeReductionStep,
                                                     snapshot.stutterPitchShiftSemitones,
                                                     snapshot.stutterTruncateEnabled,
                                                     snapshot.stutterStartFraction);

        AudioFileIO::ConvertedAudio outputAudio;
        outputAudio.buffer = stuttered;
        outputAudio.sampleRate = converted.sampleRate;

        if (! audioFileIO.writeMonoWav16 (targetFile, outputAudio))
            return;

        PreviewChainOrchestrator previewChain (stateStore);
        rebuildOk = previewChain.rebuildPreviewChain();
    });

    return rebuildOk;
}

bool MutationOrchestrator::requestStutterUndo (int index)
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
        if (index < 0 || index >= static_cast<int> (snapshot.previewSnippetURLs.size()))
            return;

        const auto backupIt = snapshot.stutterUndoBackup.find (index);
        if (backupIt == snapshot.stutterUndoBackup.end())
            return;

        const juce::File backupFile = backupIt->second;
        if (! backupFile.existsAsFile())
            return;

        const juce::File targetFile = snapshot.previewSnippetURLs[static_cast<std::size_t> (index)];
        if (! backupFile.copyFileTo (targetFile))
            return;

        PreviewChainOrchestrator previewChain (stateStore);
        rebuildOk = previewChain.rebuildPreviewChain();
    });

    return rebuildOk;
}

bool MutationOrchestrator::requestPachinkoStutterAll()
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
        auto previewSnippetURLs = snapshot.previewSnippetURLs;
        if (previewSnippetURLs.empty())
            return;

        AudioFileIO audioFileIO;
        juce::Random random;

        auto randomFloatInRange = [&] (float minValue, float maxValue)
        {
            return minValue + random.nextFloat() * (maxValue - minValue);
        };

        for (std::size_t index = 0; index < previewSnippetURLs.size(); ++index)
        {
            if (! random.nextBool())
                continue;

            const juce::File targetFile = previewSnippetURLs[index];
            if (! targetFile.existsAsFile())
                continue;

            AudioFileIO::ConvertedAudio converted;
            juce::String formatDescription;

            if (! audioFileIO.readToMonoBuffer (targetFile, converted, formatDescription))
                continue;

            const int stutterCountRange = kPachinkoStutterCountMax - kPachinkoStutterCountMin + 1;
            const int stutterCount = kPachinkoStutterCountMin + random.nextInt (stutterCountRange);
            const float stutterVolumeReductionStep =
                randomFloatInRange (kPachinkoVolumeReductionMin, kPachinkoVolumeReductionMax);
            const float stutterPitchShiftSemitones =
                randomFloatInRange (kPachinkoPitchShiftMin, kPachinkoPitchShiftMax);
            const bool stutterTruncateEnabled = random.nextBool();
            const float stutterStartFraction = random.nextFloat();

            const auto stuttered = buildStutteredBuffer (converted.buffer,
                                                         stutterCount,
                                                         stutterVolumeReductionStep,
                                                         stutterPitchShiftSemitones,
                                                         stutterTruncateEnabled,
                                                         stutterStartFraction);

            AudioFileIO::ConvertedAudio outputAudio;
            outputAudio.buffer = stuttered;
            outputAudio.sampleRate = converted.sampleRate;

            if (! audioFileIO.writeMonoWav16 (targetFile, outputAudio))
                continue;
        }

        PreviewChainOrchestrator previewChain (stateStore);
        rebuildOk = previewChain.rebuildPreviewChain();
    });

    return rebuildOk;
}

bool MutationOrchestrator::requestPachinkoReverseAll()
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
        if (snapshot.manualReverseEnabled)
            return;

        auto previewSnippetURLs = snapshot.previewSnippetURLs;
        if (previewSnippetURLs.empty())
            return;

        AudioFileIO audioFileIO;
        juce::Random random;

        for (std::size_t index = 0; index < previewSnippetURLs.size(); ++index)
        {
            if (! random.nextBool())
                continue;

            const juce::File targetFile = previewSnippetURLs[index];
            if (! targetFile.existsAsFile())
                continue;

            AudioFileIO::ConvertedAudio converted;
            juce::String formatDescription;

            if (! audioFileIO.readToMonoBuffer (targetFile, converted, formatDescription))
                continue;

            reverseMonoBuffer (converted.buffer);

            AudioFileIO::ConvertedAudio outputAudio;
            outputAudio.buffer = std::move (converted.buffer);
            outputAudio.sampleRate = converted.sampleRate;

            if (! audioFileIO.writeMonoWav16 (targetFile, outputAudio))
                continue;
        }

        PreviewChainOrchestrator previewChain (stateStore);
        rebuildOk = previewChain.rebuildPreviewChain();
    });

    return rebuildOk;
}

bool MutationOrchestrator::requestExportSlices (const std::optional<SliceStateStore::ExportSettings>& overrideSettings)
{
    if (! guardMutation())
        return false;

    if (! validateAlignment())
        return false;

    BackgroundWorker worker;
    bool exportOk = false;

    worker.enqueue ([&]
    {
        ExportOrchestrator exporter (stateStore);
        exportOk = exporter.exportSlices (overrideSettings);
    });

    return exportOk;
}

bool MutationOrchestrator::requestExportFullChainWithoutVolume (const std::optional<SliceStateStore::ExportSettings>& overrideSettings)
{
    if (! guardMutation())
        return false;

    BackgroundWorker worker;
    bool exportOk = false;

    worker.enqueue ([&]
    {
        ExportOrchestrator exporter (stateStore);
        exportOk = exporter.exportFullChainWithoutVolume (overrideSettings);
    });

    return exportOk;
}

bool MutationOrchestrator::requestExportFullChainWithVolume (const std::optional<SliceStateStore::ExportSettings>& overrideSettings)
{
    if (! guardMutation())
        return false;

    if (! validateAlignment())
        return false;

    BackgroundWorker worker;
    bool exportOk = false;

    worker.enqueue ([&]
    {
        ExportOrchestrator exporter (stateStore);
        exportOk = exporter.exportFullChainWithVolume (overrideSettings);
    });

    return exportOk;
}

void MutationOrchestrator::clearStutterUndoBackup()
{
    stutterUndoBackup = juce::File();
    stateStore.clearStutterUndoBackup();
}

bool MutationOrchestrator::hasStutterUndoBackup() const
{
    if (stutterUndoBackup != juce::File())
        return true;

    const auto snapshot = stateStore.getSnapshot();
    return ! snapshot.stutterUndoBackup.empty();
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
