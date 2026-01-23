#include "MutationOrchestrator.h"

#include <cmath>
#include <unordered_map>

#include "AudioFileIO.h"
#include "BackgroundWorker.h"
#include "ExportOrchestrator.h"
#include "PreviewChainOrchestrator.h"
#include "SliceInfrastructure.h"
#include "AudioEngine.h"
#include "RecordingBus.h"
#include "RecordingModule.h"

namespace {
    constexpr double kTargetSampleRate = 44100.0;
    const juce::Array<int> kAllowedSubdivisionsSteps = { 8, 4, 2, 1 };
    constexpr int kPachinkoStutterCountMin = 2;
    constexpr int kPachinkoStutterCountMax = 8;
    constexpr float kPachinkoVolumeReductionMin = 0.0f;
    constexpr float kPachinkoVolumeReductionMax = 0.6f;
    constexpr float kPachinkoPitchShiftMin = -12.0f;
    constexpr float kPachinkoPitchShiftMax = 12.0f;
    constexpr int kTransientRepeatRetryCount = 4;
    constexpr int kRegenerateRetryLimit = 500;

    double resolvedBpm (double bpm)
    {
        if (bpm <= 0.0)
            return 128.0;

        return bpm;
    }

    double secondsPerBeat (double bpm)
    {
        return 60.0 / resolvedBpm (bpm);
    }

    int barWindowFrames (double bpm)
    {
        const double seconds = secondsPerBeat (bpm) * 4.0;
        return static_cast<int> (std::lround (seconds * kTargetSampleRate));
    }

    double subdivisionToQuarterNotes (int subdivisionSteps)
    {
        switch (subdivisionSteps)
        {
            case 8: return 8.0;  // half bar
            case 4: return 4.0;  // quarter bar (one beat)
            case 2: return 2.0;  // eighth note
            case 1: return 1.0;  // sixteenth note
            default: break;
        }

        return 4.0;
    }

    int resolvedSubdivision (int subdivisionSteps)
    {
        for (const int step : kAllowedSubdivisionsSteps)
        {
            if (step == subdivisionSteps)
                return step;
        }

        return 4;
    }

    int randomSubdivision (juce::Random& random)
    {
        const int index = random.nextInt (kAllowedSubdivisionsSteps.size());
        return kAllowedSubdivisionsSteps[index];
    }

    std::vector<int> buildRandomSubdivisions (int count)
    {
        std::vector<int> subdivisions;
        subdivisions.reserve (static_cast<std::size_t> (count));
        juce::Random random;
        for (int i = 0; i < count; ++i)
            subdivisions.push_back (randomSubdivision (random));
        return subdivisions;
    }

    struct SlicingSources
    {
        SliceStateStore::SourceMode sourceMode = SliceStateStore::SourceMode::multi;
        juce::Array<AudioCacheStore::CacheEntry> cacheEntries;
        std::vector<juce::File> liveFiles;
        juce::File manualFile;
        juce::String emptyReason;

        bool hasSources() const
        {
            switch (sourceMode)
            {
                case SliceStateStore::SourceMode::multi:
                case SliceStateStore::SourceMode::singleRandom:
                    return ! cacheEntries.isEmpty();
                case SliceStateStore::SourceMode::singleManual:
                    return manualFile.existsAsFile();
                case SliceStateStore::SourceMode::live:
                    return ! liveFiles.empty();
            }
            return false;
        }
    };

    SlicingSources getCurrentSlicingSources (const SliceStateStore::SliceStateSnapshot& snapshot,
                                             const AudioEngine* audioEngine)
    {
        SlicingSources sources;
        sources.sourceMode = snapshot.sourceMode;

        switch (snapshot.sourceMode)
        {
            case SliceStateStore::SourceMode::multi:
            case SliceStateStore::SourceMode::singleRandom:
            {
                for (const auto& entry : snapshot.cacheData.entries)
                {
                    if (entry.isCandidate)
                        sources.cacheEntries.add (entry);
                }
                break;
            }
            case SliceStateStore::SourceMode::singleManual:
            {
                sources.manualFile = snapshot.sourceFile;
                if (! sources.manualFile.existsAsFile())
                    sources.emptyReason = "No manual source file selected.";
                break;
            }
            case SliceStateStore::SourceMode::live:
            {
                if (audioEngine == nullptr)
                {
                    sources.emptyReason = "LIVE slicing is unavailable.";
                    break;
                }

                bool anySelected = false;
                for (int index = 0; index < RecordingBus::kNumRecorders; ++index)
                {
                    if (! audioEngine->isRecorderIncludeInGenerationEnabled (index))
                        continue;

                    anySelected = true;
                    const auto recorderFile = RecordingModule::getRecorderFile (index);
                    if (recorderFile.existsAsFile())
                        sources.liveFiles.push_back (recorderFile);
                }

                if (sources.liveFiles.empty())
                {
                    sources.emptyReason = anySelected
                                              ? "Selected LIVE recorders have no audio to slice."
                                              : "No LIVE recorders are selected for slicing.";
                }
                break;
            }
        }

        return sources;
    }

    bool warnIfMissingLiveSources (const SlicingSources& sources)
    {
        if (sources.sourceMode != SliceStateStore::SourceMode::live || sources.hasSources())
            return false;

        const juce::String reason = sources.emptyReason.isNotEmpty()
                                        ? sources.emptyReason
                                        : "No LIVE recorders are available for slicing.";
        juce::AlertWindow::showMessageBoxAsync (juce::AlertWindow::WarningIcon,
                                               "No LIVE sources",
                                               reason);
        return true;
    }

    int subdivisionToFrameCount (double bpm, int subdivisionSteps)
    {
        const double quarterNotes = subdivisionToQuarterNotes (subdivisionSteps);
        const double durationSeconds = secondsPerBeat (bpm) * (quarterNotes / 4.0);
        return static_cast<int> (std::lround (durationSeconds * kTargetSampleRate));
    }

    int noGoZoneFrames (double bpm)
    {
        const double seconds = std::ceil (secondsPerBeat (bpm) * 8.0);
        return static_cast<int> (std::lround (seconds * kTargetSampleRate));
    }

    juce::File getPreviewTempFolder()
    {
        auto tempDir = juce::File::getSpecialLocation (juce::File::tempDirectory);
        if (tempDir == juce::File())
            return {};

        return tempDir.getChildFile ("AudioSnippetPreview");
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

MutationOrchestrator::MutationOrchestrator (SliceStateStore& store, AudioEngine* engine)
    : stateStore (store),
      audioEngine (engine)
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

    const auto snapshot = stateStore.getSnapshot();
    const auto sources = getCurrentSlicingSources (snapshot, audioEngine);
    if (warnIfMissingLiveSources (sources))
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
        const double bpm = snapshot.bpm;
        const int subdivisionSteps = resolvedSubdivision (snapshot.subdivisionSteps);
        const bool transientDetectEnabled = snapshot.transientDetectionEnabled;

        const bool layeringMode = snapshot.layeringMode;
        const int sampleCount = snapshot.sampleCountSetting;
        if (layeringMode)
        {
            if (sampleCount <= 0 || static_cast<int> (sliceInfos.size()) != sampleCount * 2)
                return;
        }

        const int logicalIndex = layeringMode ? (index % sampleCount) : index;
        const int leftIndex = logicalIndex;
        const int rightIndex = layeringMode ? logicalIndex + sampleCount : -1;

        juce::Random& random = juce::Random::getSystemRandom();

        auto resliceIndex = [&] (int targetIndex)
        {
            const auto& sliceInfo = sliceInfos[static_cast<std::size_t> (targetIndex)];
            const juce::File sourceFile = sliceInfo.fileURL;

            AudioFileIO audioFileIO;
            juce::String formatDescription;

            int fileDurationFrames = 0;
            if (! audioFileIO.getFileDurationFrames (sourceFile, fileDurationFrames, formatDescription))
                return false;

            const int snippetFrameCount = subdivisionToFrameCount (bpm, subdivisionSteps);
            const juce::File outputFile = previewSnippetURLs[static_cast<std::size_t> (targetIndex)];
            const int maxCandidateStart = juce::jmax (0, fileDurationFrames - noGoZoneFrames (bpm));
            int startFrame = 0;

            if (transientDetectEnabled)
            {
                const int windowFrames = barWindowFrames (bpm);
                if (windowFrames <= 0 || windowFrames > fileDurationFrames)
                    return false;

                const int maxWindowStart = fileDurationFrames - windowFrames;
                const int cappedCandidateStart = juce::jlimit (0, maxWindowStart, maxCandidateStart);
                const int windowStart = random.nextInt (cappedCandidateStart + 1);

                AudioFileIO::ConvertedAudio detectionAudio;
                if (! audioFileIO.readToMonoBufferSegment (sourceFile,
                                                           windowStart,
                                                           windowFrames,
                                                           detectionAudio,
                                                           formatDescription))
                    return false;

                const auto refined = refinedStartFromWindow (detectionAudio.buffer,
                                                             windowStart,
                                                             transientDetectEnabled);
                if (! refined.has_value())
                    return false;
                startFrame = refined.value();

                if (startFrame + snippetFrameCount > fileDurationFrames)
                    return false;

                AudioFileIO::ConvertedAudio sliceAudio;
                if (! audioFileIO.readToMonoBufferSegment (sourceFile,
                                                           startFrame,
                                                           snippetFrameCount,
                                                           sliceAudio,
                                                           formatDescription))
                    return false;

                if (! audioFileIO.writeMonoWav16 (outputFile, sliceAudio))
                    return false;
            }
            else
            {
                startFrame = random.nextInt (maxCandidateStart + 1);
                if (startFrame + snippetFrameCount > fileDurationFrames)
                    return false;

                AudioFileIO::ConvertedAudio sliceAudio;
                if (! audioFileIO.readToMonoBufferSegment (sourceFile, startFrame, snippetFrameCount, sliceAudio, formatDescription))
                    return false;

                if (! audioFileIO.writeMonoWav16 (outputFile, sliceAudio))
                    return false;
            }

            SliceStateStore::SliceInfo updatedInfo = sliceInfo;
            updatedInfo.startFrame = startFrame;
            updatedInfo.snippetFrameCount = snippetFrameCount;
            updatedInfo.sourceMode = snapshot.sourceMode;
            updatedInfo.bpm = snapshot.bpm;
            updatedInfo.transientDetectionEnabled = snapshot.transientDetectionEnabled;
            updatedInfo.sourcePath = snapshot.cacheData.sourcePath;
            updatedInfo.sourceIsDirectory = snapshot.cacheData.isDirectorySource;
            updatedInfo.candidatePaths.clear();
            if (snapshot.sourceMode == SliceStateStore::SourceMode::multi
                || snapshot.sourceMode == SliceStateStore::SourceMode::singleRandom)
            {
                updatedInfo.candidatePaths.reserve (static_cast<std::size_t> (snapshot.cacheData.entries.size()));
                for (const auto& entry : snapshot.cacheData.entries)
                {
                    if (entry.isCandidate)
                        updatedInfo.candidatePaths.push_back (entry.path);
                }
            }
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
        clearStutterUndoBackup();
        rebuildOk = true;
    });

    return rebuildOk;
}

bool MutationOrchestrator::requestResliceAll()
{
    if (! guardMutation())
        return false;

    if (! validateAlignment())
        return false;

    const auto snapshot = stateStore.getSnapshot();
    const auto sources = getCurrentSlicingSources (snapshot, audioEngine);
    if (warnIfMissingLiveSources (sources))
        return false;

    BackgroundWorker worker;
    bool rebuildOk = false;

    worker.enqueue ([&, snapshot]
    {
        auto sliceInfos = snapshot.sliceInfos;
        auto previewSnippetURLs = snapshot.previewSnippetURLs;
        auto sliceVolumeSettings = snapshot.sliceVolumeSettings;
        const double bpm = snapshot.bpm;
        const int subdivisionSteps = resolvedSubdivision (snapshot.subdivisionSteps);
        const bool transientDetectEnabled = snapshot.transientDetectionEnabled;

        if (sliceInfos.empty())
            return;

        const bool layeringMode = snapshot.layeringMode;
        const int sampleCount = snapshot.sampleCountSetting;
        if (layeringMode)
        {
            if (sampleCount <= 0 || static_cast<int> (sliceInfos.size()) != sampleCount * 2)
                return;
        }

        AudioFileIO audioFileIO;
        juce::Random& random = juce::Random::getSystemRandom();

        const int loopCount = layeringMode ? sampleCount : static_cast<int> (sliceInfos.size());
        for (int logicalIndex = 0; logicalIndex < loopCount; ++logicalIndex)
        {
            const int leftIndex = logicalIndex;
            const int rightIndex = layeringMode ? logicalIndex + sampleCount : -1;

            auto resliceIndex = [&] (int targetIndex)
            {
                const auto& sliceInfo = sliceInfos[static_cast<std::size_t> (targetIndex)];
                const juce::File sourceFile = sliceInfo.fileURL;

                juce::String formatDescription;

                int fileDurationFrames = 0;
                if (! audioFileIO.getFileDurationFrames (sourceFile, fileDurationFrames, formatDescription))
                    return false;

                const int snippetFrameCount = subdivisionToFrameCount (bpm, subdivisionSteps);
                const juce::File outputFile = previewSnippetURLs[static_cast<std::size_t> (targetIndex)];
                const int maxCandidateStart = juce::jmax (0, fileDurationFrames - noGoZoneFrames (bpm));
                int startFrame = 0;

                if (transientDetectEnabled)
                {
                    const int windowFrames = barWindowFrames (bpm);
                    if (windowFrames <= 0 || windowFrames > fileDurationFrames)
                        return false;

                    const int maxWindowStart = fileDurationFrames - windowFrames;
                    const int cappedCandidateStart = juce::jlimit (0, maxWindowStart, maxCandidateStart);
                    const int windowStart = random.nextInt (cappedCandidateStart + 1);

                    AudioFileIO::ConvertedAudio detectionAudio;
                    if (! audioFileIO.readToMonoBufferSegment (sourceFile,
                                                               windowStart,
                                                               windowFrames,
                                                               detectionAudio,
                                                               formatDescription))
                        return false;

                    const auto refined = refinedStartFromWindow (detectionAudio.buffer,
                                                                 windowStart,
                                                                 transientDetectEnabled);
                    if (! refined.has_value())
                        return false;
                    startFrame = refined.value();

                    if (startFrame + snippetFrameCount > fileDurationFrames)
                        return false;

                    AudioFileIO::ConvertedAudio sliceAudio;
                    if (! audioFileIO.readToMonoBufferSegment (sourceFile,
                                                               startFrame,
                                                               snippetFrameCount,
                                                               sliceAudio,
                                                               formatDescription))
                        return false;

                    if (! audioFileIO.writeMonoWav16 (outputFile, sliceAudio))
                        return false;
                }
                else
                {
                    startFrame = random.nextInt (maxCandidateStart + 1);
                    if (startFrame + snippetFrameCount > fileDurationFrames)
                        return false;

                    AudioFileIO::ConvertedAudio sliceAudio;
                    if (! audioFileIO.readToMonoBufferSegment (sourceFile, startFrame, snippetFrameCount, sliceAudio, formatDescription))
                        return false;

                    if (! audioFileIO.writeMonoWav16 (outputFile, sliceAudio))
                        return false;
                }

                SliceStateStore::SliceInfo updatedInfo = sliceInfo;
                updatedInfo.startFrame = startFrame;
                updatedInfo.snippetFrameCount = snippetFrameCount;
                updatedInfo.sourceMode = snapshot.sourceMode;
                updatedInfo.bpm = snapshot.bpm;
                updatedInfo.transientDetectionEnabled = snapshot.transientDetectionEnabled;
                updatedInfo.sourcePath = snapshot.cacheData.sourcePath;
                updatedInfo.sourceIsDirectory = snapshot.cacheData.isDirectorySource;
                updatedInfo.candidatePaths.clear();
                if (snapshot.sourceMode == SliceStateStore::SourceMode::multi
                    || snapshot.sourceMode == SliceStateStore::SourceMode::singleRandom)
                {
                    updatedInfo.candidatePaths.reserve (static_cast<std::size_t> (snapshot.cacheData.entries.size()));
                    for (const auto& entry : snapshot.cacheData.entries)
                    {
                        if (entry.isCandidate)
                            updatedInfo.candidatePaths.push_back (entry.path);
                    }
                }
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
        clearStutterUndoBackup();
        rebuildOk = true;
    });

    return rebuildOk;
}

bool MutationOrchestrator::requestSliceAll()
{
    if (! guardMutation())
        return false;

    const auto snapshot = stateStore.getSnapshot();
    const auto sources = getCurrentSlicingSources (snapshot, audioEngine);
    if (warnIfMissingLiveSources (sources))
        return false;

    BackgroundWorker worker;
    bool rebuildOk = false;

    worker.enqueue ([&, snapshot, sources]
    {

        const bool layeringMode = snapshot.layeringMode;
        const int sampleCount = snapshot.sampleCountSetting;
        const int targetCount = layeringMode ? sampleCount * 2 : sampleCount;
        if (targetCount <= 0)
            return;

        std::vector<SliceStateStore::SliceInfo> sliceInfos;
        std::vector<juce::File> previewSnippetURLs;
        std::vector<SliceStateStore::SliceVolumeSetting> sliceVolumeSettings;
        sliceInfos.reserve (static_cast<std::size_t> (targetCount));
        previewSnippetURLs.reserve (static_cast<std::size_t> (targetCount));
        sliceVolumeSettings.reserve (static_cast<std::size_t> (targetCount));

        const double bpm = snapshot.bpm;
        const int defaultSubdivision = resolvedSubdivision (snapshot.subdivisionSteps);

        std::vector<int> subdivisions;
        if (snapshot.randomSubdivisionEnabled)
        {
            if (layeringMode)
            {
                auto baseSubdivisions = buildRandomSubdivisions (sampleCount);
                subdivisions.reserve (baseSubdivisions.size() * 2);
                subdivisions.insert (subdivisions.end(), baseSubdivisions.begin(), baseSubdivisions.end());
                subdivisions.insert (subdivisions.end(), baseSubdivisions.begin(), baseSubdivisions.end());
            }
            else
            {
                subdivisions = buildRandomSubdivisions (targetCount);
            }
        }

        auto subdivisionForIndex = [&] (int index)
        {
            if (! subdivisions.empty())
                return subdivisions[static_cast<std::size_t> (index)];
            return defaultSubdivision;
        };

        juce::Random& random = juce::Random::getSystemRandom();
        juce::Array<AudioCacheStore::CacheEntry> availableEntries;
        std::vector<juce::File> liveFiles;

        const auto previewTempFolder = getPreviewTempFolder();
        if (previewTempFolder == juce::File())
            return;

        previewTempFolder.deleteRecursively();
        previewTempFolder.createDirectory();

        switch (snapshot.sourceMode)
        {
            case SliceStateStore::SourceMode::multi:
            {
                availableEntries = sources.cacheEntries;
                break;
            }
            case SliceStateStore::SourceMode::singleRandom:
            {
                availableEntries = sources.cacheEntries;
                if (! availableEntries.isEmpty())
                {
                    const int selectedIndex = random.nextInt (availableEntries.size());
                    const auto selected = availableEntries[selectedIndex];
                    availableEntries.clear();
                    availableEntries.add (selected);
                }
                break;
            }
            case SliceStateStore::SourceMode::singleManual:
            {
                if (! snapshot.sourceFile.existsAsFile())
                    return;
                break;
            }
            case SliceStateStore::SourceMode::live:
            {
                liveFiles = sources.liveFiles;
                break;
            }
        }

        if (snapshot.sourceMode == SliceStateStore::SourceMode::live)
        {
            if (liveFiles.empty())
                return;
        }
        else if (snapshot.sourceMode == SliceStateStore::SourceMode::singleManual)
        {
            if (! snapshot.sourceFile.existsAsFile())
                return;
        }
        else
        {
            if (availableEntries.isEmpty())
                return;
        }

        AudioFileIO audioFileIO;
        struct CachedAudio
        {
            AudioFileIO::ConvertedAudio converted;
            int durationFrames = 0;
        };
        std::unordered_map<std::string, CachedAudio> fullFileCache;
        const bool enableFullFileCache =
            snapshot.sourceMode == SliceStateStore::SourceMode::singleManual
            || snapshot.sourceMode == SliceStateStore::SourceMode::singleRandom;
        const int entryCount = availableEntries.size();
        int lastStartFrame = -1;

        for (int index = 0; index < targetCount; ++index)
        {
            bool added = false;
            for (int attempt = 0; attempt < 5 && ! added; ++attempt)
            {
                juce::File sourceFile;
                if (snapshot.sourceMode == SliceStateStore::SourceMode::singleManual)
                {
                    sourceFile = snapshot.sourceFile;
                }
                else if (snapshot.sourceMode == SliceStateStore::SourceMode::live)
                {
                    if (liveFiles.empty())
                        return;
                    sourceFile = liveFiles[static_cast<std::size_t> (random.nextInt (static_cast<int> (liveFiles.size())))];
                }
                else
                {
                    const int entryIndex = random.nextInt (entryCount);
                    const auto& entry = availableEntries.getReference (entryIndex);
                    sourceFile = juce::File (entry.path);
                }

                if (! sourceFile.existsAsFile())
                    continue;

                juce::String formatDescription;

                int fileDurationFrames = 0;
                const std::string cacheKey = sourceFile.getFullPathName().toStdString();
                CachedAudio* cachedAudio = nullptr;

                if (enableFullFileCache)
                {
                    auto [it, inserted] = fullFileCache.try_emplace (cacheKey);
                    if (inserted)
                    {
                        if (! audioFileIO.readToMonoBuffer (sourceFile, it->second.converted, formatDescription))
                        {
                            fullFileCache.erase (it);
                        }
                        else
                        {
                            it->second.durationFrames = it->second.converted.buffer.getNumSamples();
                        }
                    }

                    auto found = fullFileCache.find (cacheKey);
                    if (found != fullFileCache.end())
                    {
                        cachedAudio = &found->second;
                        fileDurationFrames = cachedAudio->durationFrames;
                    }
                }

                if (cachedAudio == nullptr)
                {
                    if (! audioFileIO.getFileDurationFrames (sourceFile, fileDurationFrames, formatDescription))
                        continue;
                }

                if (fileDurationFrames <= 0)
                    continue;

                const int subdivisionSteps = subdivisionForIndex (index);
                const int snippetFrameCount = subdivisionToFrameCount (bpm, subdivisionSteps);
                if (snippetFrameCount <= 0)
                    continue;

                const juce::File outputFile = previewTempFolder.getChildFile ("slice_" + juce::String (index) + ".wav");

                const int maxCandidateStart = juce::jmax (0, fileDurationFrames - noGoZoneFrames (bpm));
                int startFrame = 0;

                if (snapshot.transientDetectionEnabled)
                {
                    bool foundStart = false;
                    for (int retry = 0; retry <= kTransientRepeatRetryCount; ++retry)
                    {
                        const int windowFrames = barWindowFrames (bpm);
                        if (windowFrames <= 0 || windowFrames > fileDurationFrames)
                            break;

                        const auto refined = [&]() -> std::optional<int>
                        {
                            if (cachedAudio != nullptr)
                            {
                                return refinedStart (cachedAudio->converted.buffer,
                                                     random,
                                                     maxCandidateStart,
                                                     windowFrames,
                                                     snapshot.transientDetectionEnabled);
                            }

                            const int maxWindowStart = fileDurationFrames - windowFrames;
                            const int cappedCandidateStart = juce::jlimit (0, maxWindowStart, maxCandidateStart);
                            const int windowStart = random.nextInt (cappedCandidateStart + 1);

                            AudioFileIO::ConvertedAudio detectionAudio;
                            if (! audioFileIO.readToMonoBufferSegment (sourceFile,
                                                                       windowStart,
                                                                       windowFrames,
                                                                       detectionAudio,
                                                                       formatDescription))
                                return std::nullopt;

                            return refinedStartFromWindow (detectionAudio.buffer,
                                                           windowStart,
                                                           snapshot.transientDetectionEnabled);
                        }();

                        if (! refined.has_value())
                            continue;
                        const int candidateStart = refined.value();
                        if (candidateStart == lastStartFrame)
                            continue;
                        startFrame = candidateStart;
                        foundStart = true;
                        break;
                    }
                    if (! foundStart)
                        continue;

                    if (startFrame + snippetFrameCount > fileDurationFrames)
                        continue;

                    AudioFileIO::ConvertedAudio sliceAudio;
                    if (cachedAudio != nullptr)
                    {
                        if (startFrame + snippetFrameCount > cachedAudio->converted.buffer.getNumSamples())
                            continue;
                        sliceAudio.sampleRate = cachedAudio->converted.sampleRate;
                        sliceAudio.buffer = juce::AudioBuffer<float> (1, snippetFrameCount);
                        sliceAudio.buffer.copyFrom (0, 0, cachedAudio->converted.buffer, 0, startFrame, snippetFrameCount);
                    }
                    else if (! audioFileIO.readToMonoBufferSegment (sourceFile,
                                                                   startFrame,
                                                                   snippetFrameCount,
                                                                   sliceAudio,
                                                                   formatDescription))
                    {
                        continue;
                    }

                    if (! audioFileIO.writeMonoWav16 (outputFile, sliceAudio))
                        continue;
                }
                else
                {
                    startFrame = random.nextInt (maxCandidateStart + 1);
                    if (startFrame + snippetFrameCount > fileDurationFrames)
                        continue;

                    AudioFileIO::ConvertedAudio sliceAudio;
                    if (cachedAudio != nullptr)
                    {
                        if (startFrame + snippetFrameCount > cachedAudio->converted.buffer.getNumSamples())
                            continue;
                        sliceAudio.sampleRate = cachedAudio->converted.sampleRate;
                        sliceAudio.buffer = juce::AudioBuffer<float> (1, snippetFrameCount);
                        sliceAudio.buffer.copyFrom (0, 0, cachedAudio->converted.buffer, 0, startFrame, snippetFrameCount);
                    }
                    else if (! audioFileIO.readToMonoBufferSegment (sourceFile,
                                                                   startFrame,
                                                                   snippetFrameCount,
                                                                   sliceAudio,
                                                                   formatDescription))
                    {
                        continue;
                    }

                    if (! audioFileIO.writeMonoWav16 (outputFile, sliceAudio))
                        continue;
                }

                SliceStateStore::SliceInfo info;
                info.fileURL = sourceFile;
                info.startFrame = startFrame;
                info.subdivisionSteps = subdivisionSteps;
                info.snippetFrameCount = snippetFrameCount;
                info.sourceMode = snapshot.sourceMode;
                info.bpm = snapshot.bpm;
                info.transientDetectionEnabled = snapshot.transientDetectionEnabled;
                info.sourcePath = snapshot.cacheData.sourcePath;
                info.sourceIsDirectory = snapshot.cacheData.isDirectorySource;
                info.candidatePaths.clear();
                if (snapshot.sourceMode == SliceStateStore::SourceMode::multi
                    || snapshot.sourceMode == SliceStateStore::SourceMode::singleRandom)
                {
                    info.candidatePaths.reserve (static_cast<std::size_t> (sources.cacheEntries.size()));
                    for (const auto& entry : sources.cacheEntries)
                        info.candidatePaths.push_back (entry.path);
                }

                sliceInfos.push_back (info);
                previewSnippetURLs.push_back (outputFile);
                sliceVolumeSettings.push_back ({ 0.75f, false });
                lastStartFrame = startFrame;
                added = true;
            }

            if (! added)
                return;
        }

        stateStore.setAlignedSlices (std::move (sliceInfos),
                                     std::move (previewSnippetURLs),
                                     std::move (sliceVolumeSettings));
        stateStore.setLayeringState (layeringMode, sampleCount);

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

    const auto snapshot = stateStore.getSnapshot();
    const auto sources = getCurrentSlicingSources (snapshot, audioEngine);
    if (warnIfMissingLiveSources (sources))
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
        const double bpm = snapshot.bpm;
        const int subdivisionSteps = resolvedSubdivision (snapshot.subdivisionSteps);

        const bool layeringMode = snapshot.layeringMode;
        const int sampleCount = snapshot.sampleCountSetting;
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
            const auto sourceModeToUse = sliceInfo.sourceMode;
            const double bpmToUse = sliceInfo.bpm > 0.0 ? sliceInfo.bpm : bpm;
            const bool transientDetectEnabled = sliceInfo.transientDetectionEnabled;
            const int subdivisionToUse = sliceInfo.subdivisionSteps > 0 ? sliceInfo.subdivisionSteps : subdivisionSteps;
            const int snippetFrameCount = subdivisionToFrameCount (bpmToUse, subdivisionToUse);
            if (snippetFrameCount <= 0)
                return false;

            std::vector<juce::String> candidatePaths = sliceInfo.candidatePaths;
            if (candidatePaths.empty()
                && (sourceModeToUse == SliceStateStore::SourceMode::multi
                    || sourceModeToUse == SliceStateStore::SourceMode::singleRandom))
            {
                candidatePaths.reserve (static_cast<std::size_t> (snapshot.cacheData.entries.size()));
                for (const auto& entry : snapshot.cacheData.entries)
                {
                    if (entry.isCandidate)
                        candidatePaths.push_back (entry.path);
                }
            }

            if (sourceModeToUse == SliceStateStore::SourceMode::live && sources.liveFiles.empty())
                return false;
            if (sourceModeToUse == SliceStateStore::SourceMode::singleManual && ! sliceInfo.fileURL.existsAsFile())
                return false;
            if (sourceModeToUse != SliceStateStore::SourceMode::live
                && sourceModeToUse != SliceStateStore::SourceMode::singleManual
                && candidatePaths.empty())
            {
                return false;
            }

            juce::Random& random = juce::Random::getSystemRandom();
            const juce::File outputFile = previewSnippetURLs[static_cast<std::size_t> (targetIndex)];

            for (int attempt = 0; attempt < kRegenerateRetryLimit; ++attempt)
            {
                juce::File sourceFile;
                if (sourceModeToUse == SliceStateStore::SourceMode::live)
                {
                    sourceFile = sources.liveFiles[static_cast<std::size_t> (random.nextInt (static_cast<int> (sources.liveFiles.size())))];
                }
                else if (sourceModeToUse == SliceStateStore::SourceMode::singleManual)
                {
                    sourceFile = sliceInfo.fileURL;
                }
                else if (! candidatePaths.empty())
                {
                    const int entryIndex = random.nextInt (static_cast<int> (candidatePaths.size()));
                    sourceFile = juce::File (candidatePaths[static_cast<std::size_t> (entryIndex)]);
                }

                if (! sourceFile.existsAsFile())
                    continue;

                juce::String formatDescription;

                AudioFileIO audioFileIO;
                int fileDurationFrames = 0;
                if (! audioFileIO.getFileDurationFrames (sourceFile, fileDurationFrames, formatDescription))
                    continue;

                if (fileDurationFrames <= 0)
                    continue;

                const int maxCandidateStart = juce::jmax (0, fileDurationFrames - noGoZoneFrames (bpmToUse));
                int startFrame = 0;

                if (transientDetectEnabled)
                {
                    bool foundStart = false;
                    for (int retry = 0; retry <= kTransientRepeatRetryCount; ++retry)
                    {
                        const int windowFrames = barWindowFrames (bpmToUse);
                        if (windowFrames <= 0 || windowFrames > fileDurationFrames)
                            break;

                        const int maxWindowStart = fileDurationFrames - windowFrames;
                        const int cappedCandidateStart = juce::jlimit (0, maxWindowStart, maxCandidateStart);
                        const int windowStart = random.nextInt (cappedCandidateStart + 1);

                        AudioFileIO::ConvertedAudio detectionAudio;
                        if (! audioFileIO.readToMonoBufferSegment (sourceFile,
                                                                   windowStart,
                                                                   windowFrames,
                                                                   detectionAudio,
                                                                   formatDescription))
                            continue;

                        const auto refined = refinedStartFromWindow (detectionAudio.buffer,
                                                                     windowStart,
                                                                     transientDetectEnabled);
                        if (! refined.has_value())
                            continue;

                        startFrame = refined.value();
                        foundStart = true;
                        break;
                    }

                    if (! foundStart)
                        continue;
                }
                else
                {
                    startFrame = random.nextInt (maxCandidateStart + 1);
                }

                if (startFrame + snippetFrameCount > fileDurationFrames)
                    continue;
                if (startFrame == sliceInfo.startFrame && fileDurationFrames > snippetFrameCount)
                    continue;

                AudioFileIO::ConvertedAudio sliceAudio;
                if (! audioFileIO.readToMonoBufferSegment (sourceFile,
                                                           startFrame,
                                                           snippetFrameCount,
                                                           sliceAudio,
                                                           formatDescription))
                    continue;
                if (sliceInfo.isReversed)
                    reverseMonoBuffer (sliceAudio.buffer);

                if (! audioFileIO.writeMonoWav16 (outputFile, sliceAudio))
                    continue;

                SliceStateStore::SliceInfo updatedInfo = sliceInfo;
                updatedInfo.fileURL = sourceFile;
                updatedInfo.startFrame = startFrame;
                updatedInfo.snippetFrameCount = snippetFrameCount;
                updatedInfo.subdivisionSteps = subdivisionToUse;
                sliceInfos[static_cast<std::size_t> (targetIndex)] = updatedInfo;
                return true;
            }

            return false;
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
        clearStutterUndoBackup();
        rebuildOk = true;
    });

    return rebuildOk;
}

bool MutationOrchestrator::requestRegenerateAll()
{
    if (! guardMutation())
        return false;

    if (! validateAlignment())
        return false;

    const auto snapshot = stateStore.getSnapshot();
    const auto sources = getCurrentSlicingSources (snapshot, audioEngine);
    if (warnIfMissingLiveSources (sources))
        return false;

    BackgroundWorker worker;
    bool rebuildOk = false;

    worker.enqueue ([&]
    {
        const auto snapshot = stateStore.getSnapshot();
        auto sliceInfos = snapshot.sliceInfos;
        auto previewSnippetURLs = snapshot.previewSnippetURLs;
        auto sliceVolumeSettings = snapshot.sliceVolumeSettings;
        const double bpm = snapshot.bpm;
        const int defaultSubdivision = resolvedSubdivision (snapshot.subdivisionSteps);

        if (sliceInfos.empty())
            return;

        const bool layeringMode = snapshot.layeringMode;
        const int sampleCount = snapshot.sampleCountSetting;
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
                const int subdivisionSteps =
                    snapshot.randomSubdivisionEnabled ? randomSubdivision (random) : defaultSubdivision;
                const int snippetFrameCount = subdivisionToFrameCount (bpm, subdivisionSteps);

                juce::String formatDescription;

                const juce::File outputFile = previewSnippetURLs[static_cast<std::size_t> (targetIndex)];

                int fileDurationFrames = 0;
                if (! audioFileIO.getFileDurationFrames (sourceFile, fileDurationFrames, formatDescription))
                    return false;

                AudioFileIO::ConvertedAudio sliceAudio;
                if (startFrame + snippetFrameCount > fileDurationFrames)
                    return false;

                if (! audioFileIO.readToMonoBufferSegment (sourceFile, startFrame, snippetFrameCount, sliceAudio, formatDescription))
                    return false;

                if (! audioFileIO.writeMonoWav16 (outputFile, sliceAudio))
                    return false;

                SliceStateStore::SliceInfo updatedInfo = sliceInfo;
                updatedInfo.snippetFrameCount = snippetFrameCount;
                updatedInfo.subdivisionSteps = subdivisionSteps;
                sliceInfos[static_cast<std::size_t> (targetIndex)] = updatedInfo;

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
    if (caching.load())
        return false;

    return ! stateStore.isCaching();
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
