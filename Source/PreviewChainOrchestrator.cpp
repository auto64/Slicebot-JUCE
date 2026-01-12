#include "PreviewChainOrchestrator.h"
#include "AudioFileIO.h"

PreviewChainOrchestrator::PreviewChainOrchestrator (SliceStateStore& store)
    : stateStore (store)
{
}

bool PreviewChainOrchestrator::rebuildPreviewChain()
{
    const auto snapshot = stateStore.getSnapshot();
    if (snapshot.previewSnippetURLs.empty())
        return false;

    AudioFileIO audioFileIO;
    auto previewSnippetURLs = snapshot.previewSnippetURLs;
    auto sliceVolumeSettings = snapshot.sliceVolumeSettings;

    const bool layeringMode = snapshot.layeringMode;
    const int sampleCount = snapshot.sampleCount;

    if (layeringMode)
    {
        if (sampleCount <= 0 || static_cast<int> (previewSnippetURLs.size()) != sampleCount * 2)
            return false;

        for (int i = 0; i < sampleCount; ++i)
        {
            const auto& leftFile = previewSnippetURLs[static_cast<std::size_t> (i)];
            const auto& rightFile = previewSnippetURLs[static_cast<std::size_t> (i + sampleCount)];

            AudioFileIO::ConvertedAudio leftAudio;
            AudioFileIO::ConvertedAudio rightAudio;
            juce::String formatDescription;

            if (! audioFileIO.readToMonoBuffer (leftFile, leftAudio, formatDescription))
                return false;

            if (! audioFileIO.readToMonoBuffer (rightFile, rightAudio, formatDescription))
                return false;

            const int leftSamples = leftAudio.buffer.getNumSamples();
            const int rightSamples = rightAudio.buffer.getNumSamples();
            const int mergedSamples = juce::jmin (leftSamples, rightSamples);

            if (mergedSamples <= 0)
                return false;

            juce::AudioBuffer<float> mergedBuffer (1, mergedSamples);
            const float* leftData = leftAudio.buffer.getReadPointer (0);
            const float* rightData = rightAudio.buffer.getReadPointer (0);
            float* mergedData = mergedBuffer.getWritePointer (0);

            for (int s = 0; s < mergedSamples; ++s)
                mergedData[s] = 0.5f * (leftData[s] + rightData[s]);

            const juce::File mergedFile = leftFile.getSiblingFile ("merged_" + juce::String (i) + ".wav");

            AudioFileIO::ConvertedAudio mergedAudio;
            mergedAudio.buffer = std::move (mergedBuffer);
            mergedAudio.sampleRate = 44100.0;

            if (! audioFileIO.writeMonoWav16 (mergedFile, mergedAudio))
                return false;

            previewSnippetURLs[static_cast<std::size_t> (i)] = mergedFile;
        }

        stateStore.setAlignedSlices (snapshot.sliceInfos,
                                     previewSnippetURLs,
                                     sliceVolumeSettings);
    }

    const int chainCount = layeringMode ? sampleCount : static_cast<int> (previewSnippetURLs.size());

    std::vector<juce::AudioBuffer<float>> snippetBuffers;
    snippetBuffers.reserve (static_cast<std::size_t> (chainCount));

    int totalSamples = 0;

    for (int i = 0; i < chainCount; ++i)
    {
        const auto& snippetFile = previewSnippetURLs[static_cast<std::size_t> (i)];
        AudioFileIO::ConvertedAudio snippetAudio;
        juce::String formatDescription;

        if (! audioFileIO.readToMonoBuffer (snippetFile, snippetAudio, formatDescription))
            return false;

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

    const juce::File previewChainFile =
        previewSnippetURLs.front().getSiblingFile ("preview_chain.wav");

    AudioFileIO::ConvertedAudio chainAudio;
    chainAudio.buffer = std::move (chainBuffer);
    chainAudio.sampleRate = 44100.0;

    if (! audioFileIO.writeMonoWav16 (previewChainFile, chainAudio))
        return false;

    stateStore.setPreviewChainURL (previewChainFile);

    return true;
}
