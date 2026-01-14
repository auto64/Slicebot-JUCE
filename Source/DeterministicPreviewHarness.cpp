#include "DeterministicPreviewHarness.h"
#include "MutationOrchestrator.h"
#include "SliceInfrastructure.h"

namespace
{
    constexpr double kTargetSampleRate = 44100.0;
    constexpr int kSliceCount = 4;
    constexpr double kBpm = 120.0;

    constexpr bool kRandomSourceSelectionEnabled = true;
    constexpr bool kRandomSubdivisionModeEnabled = true;
    constexpr int kSelectedSubdivision = 4;
    constexpr bool kTransientDetectEnabled = true;

    const juce::StringArray kCandidateSourcePaths = {
        "/path/to/your/audio.wav", // TODO: set a real path
        "/path/to/your/alternate.wav" // TODO: set a real path
    };

    const juce::Array<int> kAllowedSubdivisionsSteps = { 8, 4, 2, 1 };

    juce::File getPreviewChainOutputFile (const juce::File& inputFile)
    {
        return inputFile.getSiblingFile ("preview_chain.wav");
    }

    juce::File getPreviewSnippetOutputFile (const juce::File& inputFile, int index)
    {
        return inputFile.getSiblingFile ("slice_" + juce::String (index) + ".wav");
    }

    double sanitizedBpm()
    {
        if (kBpm <= 0.0)
            return 128.0;

        return kBpm;
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
        const double quarterNotes = subdivisionToQuarterNotes (subdivisionSteps);
        const double durationSeconds = secondsPerBeat() * (quarterNotes / 4.0);
        return static_cast<int> (std::lround (durationSeconds * kTargetSampleRate));
    }

    int computedNoGoZoneFrames()
    {
        const double seconds = std::ceil (secondsPerBeat() * 8.0);
        return static_cast<int> (std::lround (seconds * kTargetSampleRate));
    }
}

DeterministicPreviewHarness::DeterministicPreviewHarness (juce::AudioDeviceManager& manager)
    : deviceManager (manager)
{
    formatManager.registerBasicFormats();
}

DeterministicPreviewHarness::~DeterministicPreviewHarness()
{
    stopPlayback();
}

void DeterministicPreviewHarness::run()
{
    if (! buildDeterministicSlices())
        return;

    if (! buildPreviewChain())
        return;

    if (! startPlayback())
        return;

    juce::Logger::writeToLog ("DeterministicPreviewHarness: preview chain playback started");
}

void DeterministicPreviewHarness::runTemporaryResliceAllDebug()
{
    MutationOrchestrator orchestrator (stateStore);
    if (! orchestrator.requestResliceAll())
        return;

    const auto snapshot = stateStore.getSnapshot();
    if (snapshot.previewChainURL == juce::File())
        return;

    previewChainFile = snapshot.previewChainURL;
    stopPlayback();
    startPlayback();
}

void DeterministicPreviewHarness::clearPendingState()
{
    pendingSliceInfos.clear();
    pendingPreviewSnippetURLs.clear();
    pendingSliceVolumeSettings.clear();
    previewChainFile = juce::File();
}

bool DeterministicPreviewHarness::buildDeterministicSlices()
{
    clearPendingState();

    juce::Random random;
    SliceProcessingFlags flags;
    flags.layeringMode = false;
    flags.sampleCount = kSliceCount;

    const int targetSlices = flags.layeringMode ? flags.sampleCount * 2 : flags.sampleCount;

    pendingSliceInfos.reserve (targetSlices);
    pendingPreviewSnippetURLs.reserve (targetSlices);
    pendingSliceVolumeSettings.reserve (targetSlices);

    const int noGoZoneFrames = computedNoGoZoneFrames();
    const int windowFrames = windowFramesPerBar();

    for (int index = 0; index < targetSlices; ++index)
    {
        if (kCandidateSourcePaths.isEmpty())
            break;

        const int sourceIndex = kRandomSourceSelectionEnabled
            ? random.nextInt (kCandidateSourcePaths.size())
            : 0;

        const juce::File candidateFile (kCandidateSourcePaths[sourceIndex]);
        if (! candidateFile.existsAsFile())
            continue;

        AudioFileIO::ConvertedAudio converted;
        juce::String formatDescription;

        if (! audioFileIO.readToMonoBuffer (candidateFile, converted, formatDescription))
            continue;

        const int fileDurationFrames = converted.buffer.getNumSamples();
        int startFrame = 0;
        if (kTransientDetectEnabled)
        {
            const auto refined = refinedStart (converted.buffer, 0, windowFrames, kTransientDetectEnabled);
            if (! refined.has_value())
                continue;
            startFrame = refined.value();
        }
        else
        {
            const int maxCandidateStart = juce::jmax (0, fileDurationFrames - noGoZoneFrames);
            startFrame = random.nextInt (maxCandidateStart + 1);
        }

        const int subdivisionSteps = kRandomSubdivisionModeEnabled
            ? kAllowedSubdivisionsSteps[random.nextInt (kAllowedSubdivisionsSteps.size())]
            : resolvedSelectedSubdivision();

        const int sliceFrameCount = subdivisionToFrameCount (subdivisionSteps);

        if (startFrame + sliceFrameCount > fileDurationFrames)
            continue;

        const juce::File outputFile = getPreviewSnippetOutputFile (candidateFile, index);

        juce::AudioBuffer<float> sliceBuffer (1, sliceFrameCount);
        sliceBuffer.copyFrom (0, 0, converted.buffer, 0, startFrame, sliceFrameCount);

        AudioFileIO::ConvertedAudio sliceAudio;
        sliceAudio.buffer = std::move (sliceBuffer);
        sliceAudio.sampleRate = kTargetSampleRate;

        if (! audioFileIO.writeMonoWav16 (outputFile, sliceAudio))
            continue;

        SliceStateStore::SliceInfo info;
        info.fileURL = candidateFile;
        info.startFrame = startFrame;
        info.subdivisionSteps = subdivisionSteps;
        info.snippetFrameCount = sliceFrameCount;

        pendingSliceInfos.push_back (info);
        pendingPreviewSnippetURLs.push_back (outputFile);
        pendingSliceVolumeSettings.push_back (1.0f);
    }

    if (flags.layeringMode && static_cast<int> (pendingSliceInfos.size()) != targetSlices)
        return false;

    return ! pendingSliceInfos.empty();
}

bool DeterministicPreviewHarness::buildPreviewChain()
{
    if (pendingPreviewSnippetURLs.empty())
    {
        juce::Logger::writeToLog ("DeterministicPreviewHarness: no snippets available for chain build");
        return false;
    }

    std::vector<juce::AudioBuffer<float>> snippetBuffers;
    snippetBuffers.reserve (pendingPreviewSnippetURLs.size());

    int totalSamples = 0;

    for (const auto& snippetFile : pendingPreviewSnippetURLs)
    {
        AudioFileIO::ConvertedAudio snippetAudio;
        juce::String formatDescription;

        if (! audioFileIO.readToMonoBuffer (snippetFile, snippetAudio, formatDescription))
        {
            juce::Logger::writeToLog ("DeterministicPreviewHarness: snippet read failed for chain build");
            return false;
        }

        totalSamples += snippetAudio.buffer.getNumSamples();
        snippetBuffers.push_back (std::move (snippetAudio.buffer));
    }

    juce::AudioBuffer<float> chainBuffer (1, totalSamples);
    chainBuffer.clear();

    int writePosition = 0;
    for (const auto& snippetBuffer : snippetBuffers)
    {
        const int samplesToCopy = snippetBuffer.getNumSamples();
        chainBuffer.copyFrom (0, writePosition, snippetBuffer, 0, 0, samplesToCopy);
        writePosition += samplesToCopy;
    }

    previewChainFile = getPreviewChainOutputFile (pendingPreviewSnippetURLs.front());

    AudioFileIO::ConvertedAudio chainAudio;
    chainAudio.buffer = std::move (chainBuffer);
    chainAudio.sampleRate = kTargetSampleRate;

    if (! audioFileIO.writeMonoWav16 (previewChainFile, chainAudio))
    {
        juce::Logger::writeToLog ("DeterministicPreviewHarness: chain write failed at " + previewChainFile.getFullPathName());
        return false;
    }

    stateStore.setLayeringState (false, kSliceCount);
    stateStore.setMergeMode (SliceStateStore::MergeMode::none);
    stateStore.replaceAllState (std::move (pendingSliceInfos),
                                std::move (pendingPreviewSnippetURLs),
                                std::move (pendingSliceVolumeSettings),
                                previewChainFile);

    clearPendingState();

    return true;
}

bool DeterministicPreviewHarness::startPlayback()
{
    std::unique_ptr<juce::AudioFormatReader> reader (formatManager.createReaderFor (previewChainFile));
    if (reader == nullptr)
    {
        juce::Logger::writeToLog ("DeterministicPreviewHarness: preview chain reader creation failed");
        return false;
    }

    readerSource = std::make_unique<juce::AudioFormatReaderSource> (reader.release(), true);
    transportSource.setSource (readerSource.get(), 0, nullptr, kTargetSampleRate);

    sourcePlayer.setSource (&transportSource);
    deviceManager.addAudioCallback (&sourcePlayer);
    transportSource.start();

    return true;
}

void DeterministicPreviewHarness::stopPlayback()
{
    transportSource.stop();
    transportSource.setSource (nullptr);
    sourcePlayer.setSource (nullptr);
    deviceManager.removeAudioCallback (&sourcePlayer);
    readerSource.reset();
}
