#include "DeterministicPreviewHarness.h"

namespace
{
    constexpr double kTargetSampleRate = 44100.0;
    constexpr int kSliceCount = 4;
    constexpr double kBpm = 120.0;
    constexpr int kBeatsPerBar = 4;

    juce::File getDeterministicSourceFile()
    {
        const juce::String testPath = "/path/to/your/audio.wav"; // TODO: set a real path
        return juce::File (testPath);
    }

    juce::File getPreviewChainOutputFile (const juce::File& inputFile)
    {
        return inputFile.getSiblingFile ("preview_chain.wav");
    }

    juce::File getPreviewSnippetOutputFile (const juce::File& inputFile, int index)
    {
        return inputFile.getSiblingFile ("slice_" + juce::String (index) + ".wav");
    }

    int framesPerBar()
    {
        const double secondsPerBeat = 60.0 / kBpm;
        const double secondsPerBar = secondsPerBeat * static_cast<double> (kBeatsPerBar);
        return static_cast<int> (std::lround (secondsPerBar * kTargetSampleRate));
    }
}

DeterministicPreviewHarness::DeterministicPreviewHarness (juce::AudioDeviceManager& manager)
    : deviceManager (manager)
{
    formatManager.registerBasicFormats();
}

DeterministicPreviewHarness::~DeterministicPreviewHarness()
{
    transportSource.stop();
    transportSource.setSource (nullptr);
    sourcePlayer.setSource (nullptr);
    deviceManager.removeAudioCallback (&sourcePlayer);
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

bool DeterministicPreviewHarness::buildDeterministicSlices()
{
    sourceFile = getDeterministicSourceFile();

    if (! sourceFile.existsAsFile())
    {
        juce::Logger::writeToLog ("DeterministicPreviewHarness: source file missing at " + sourceFile.getFullPathName());
        return false;
    }

    AudioFileIO::ConvertedAudio converted;
    juce::String formatDescription;

    if (! audioFileIO.readToMonoBuffer (sourceFile, converted, formatDescription))
    {
        juce::Logger::writeToLog ("DeterministicPreviewHarness: source read failed. format=" + formatDescription);
        return false;
    }

    sourceBuffer = converted.buffer;

    sliceFrameCount = framesPerBar();
    sliceStartFrames.clear();

    const int sourceSamples = sourceBuffer.getNumSamples();
    if (sourceSamples < sliceFrameCount * kSliceCount)
    {
        juce::Logger::writeToLog ("DeterministicPreviewHarness: source file too short for deterministic slices");
        return false;
    }

    const int spacing = (sourceSamples - sliceFrameCount) / kSliceCount;

    for (int index = 0; index < kSliceCount; ++index)
        sliceStartFrames.add (index * spacing);

    std::vector<SliceStateStore::SliceInfo> sliceInfos;
    std::vector<juce::File> previewSnippetURLs;
    std::vector<float> sliceVolumeSettings;

    sliceInfos.reserve (kSliceCount);
    previewSnippetURLs.reserve (kSliceCount);
    sliceVolumeSettings.reserve (kSliceCount);

    for (int index = 0; index < kSliceCount; ++index)
    {
        const int startFrame = sliceStartFrames[index];
        const juce::File outputFile = getPreviewSnippetOutputFile (sourceFile, index);

        juce::AudioBuffer<float> sliceBuffer (1, sliceFrameCount);
        sliceBuffer.copyFrom (0, 0, sourceBuffer, 0, startFrame, sliceFrameCount);

        AudioFileIO::ConvertedAudio sliceAudio;
        sliceAudio.buffer = std::move (sliceBuffer);
        sliceAudio.sampleRate = kTargetSampleRate;

        if (! audioFileIO.writeMonoWav16 (outputFile, sliceAudio))
        {
            juce::Logger::writeToLog ("DeterministicPreviewHarness: slice write failed at " + outputFile.getFullPathName());
            return false;
        }

        SliceStateStore::SliceInfo info;
        info.fileURL = sourceFile;
        info.startFrame = startFrame;
        info.subdivisionSteps = 0;
        info.snippetFrameCount = sliceFrameCount;

        sliceInfos.push_back (info);
        previewSnippetURLs.push_back (outputFile);
        sliceVolumeSettings.push_back (1.0f);
    }

    stateStore.setAlignedSlices (std::move (sliceInfos),
                                 std::move (previewSnippetURLs),
                                 std::move (sliceVolumeSettings));

    return true;
}

bool DeterministicPreviewHarness::buildPreviewChain()
{
    const SliceStateStore::SliceStateSnapshot snapshot = stateStore.getSnapshot();

    if (snapshot.previewSnippetURLs.empty())
    {
        juce::Logger::writeToLog ("DeterministicPreviewHarness: no snippets available for chain build");
        return false;
    }

    const int totalSamples = sliceFrameCount * static_cast<int> (snapshot.previewSnippetURLs.size());
    juce::AudioBuffer<float> chainBuffer (1, totalSamples);
    chainBuffer.clear();

    for (int index = 0; index < static_cast<int> (snapshot.previewSnippetURLs.size()); ++index)
    {
        const int startFrame = sliceStartFrames[index];
        chainBuffer.copyFrom (0, index * sliceFrameCount, sourceBuffer, 0, startFrame, sliceFrameCount);
    }

    previewChainFile = getPreviewChainOutputFile (sourceFile);

    AudioFileIO::ConvertedAudio chainAudio;
    chainAudio.buffer = std::move (chainBuffer);
    chainAudio.sampleRate = kTargetSampleRate;

    if (! audioFileIO.writeMonoWav16 (previewChainFile, chainAudio))
    {
        juce::Logger::writeToLog ("DeterministicPreviewHarness: chain write failed at " + previewChainFile.getFullPathName());
        return false;
    }

    stateStore.replaceAllState (snapshot.sliceInfos,
                                snapshot.previewSnippetURLs,
                                snapshot.sliceVolumeSettings,
                                previewChainFile);

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
