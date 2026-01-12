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
    std::vector<juce::AudioBuffer<float>> snippetBuffers;
    snippetBuffers.reserve (snapshot.previewSnippetURLs.size());

    int totalSamples = 0;

    for (const auto& snippetFile : snapshot.previewSnippetURLs)
    {
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
        snapshot.previewSnippetURLs.front().getSiblingFile ("preview_chain.wav");

    AudioFileIO::ConvertedAudio chainAudio;
    chainAudio.buffer = std::move (chainBuffer);
    chainAudio.sampleRate = 44100.0;

    if (! audioFileIO.writeMonoWav16 (previewChainFile, chainAudio))
        return false;

    stateStore.setPreviewChainURL (previewChainFile);

    return true;
}
