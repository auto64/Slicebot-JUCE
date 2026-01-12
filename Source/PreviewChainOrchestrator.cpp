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
    const SliceStateStore::MergeMode mergeMode = snapshot.mergeMode;

    if (layeringMode)
    {
        if (sampleCount <= 0 || static_cast<int> (previewSnippetURLs.size()) != sampleCount * 2)
            return false;

        juce::Random random;
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

            SliceStateStore::MergeMode selectedMode = mergeMode;
            if (mergeMode == SliceStateStore::MergeMode::pachinko)
            {
                const int modeIndex = random.nextInt (5);
                switch (modeIndex)
                {
                    case 0: selectedMode = SliceStateStore::MergeMode::none; break;
                    case 1: selectedMode = SliceStateStore::MergeMode::fiftyFifty; break;
                    case 2: selectedMode = SliceStateStore::MergeMode::quarterCuts; break;
                    case 3: selectedMode = SliceStateStore::MergeMode::crossfade; break;
                    case 4: selectedMode = SliceStateStore::MergeMode::crossfadeReverse; break;
                    default: selectedMode = SliceStateStore::MergeMode::none; break;
                }
            }

            juce::AudioBuffer<float> mergedBuffer (1, mergedSamples);
            const float* leftData = leftAudio.buffer.getReadPointer (0);
            const float* rightData = rightAudio.buffer.getReadPointer (0);
            float* mergedData = mergedBuffer.getWritePointer (0);

            if (selectedMode == SliceStateStore::MergeMode::none)
            {
                for (int s = 0; s < mergedSamples; ++s)
                    mergedData[s] = leftData[s];
            }
            else if (selectedMode == SliceStateStore::MergeMode::fiftyFifty)
            {
                for (int s = 0; s < mergedSamples; ++s)
                    mergedData[s] = 0.5f * (leftData[s] + rightData[s]);
            }
            else if (selectedMode == SliceStateStore::MergeMode::quarterCuts)
            {
                const int quarter = juce::jmax (1, mergedSamples / 4);
                for (int s = 0; s < mergedSamples; ++s)
                {
                    const int segment = (s / quarter) % 2;
                    mergedData[s] = segment == 0 ? leftData[s] : rightData[s];
                }
            }
            else
            {
                juce::AudioBuffer<float> rightWorking (1, mergedSamples);
                rightWorking.copyFrom (0, 0, rightAudio.buffer, 0, 0, mergedSamples);
                if (selectedMode == SliceStateStore::MergeMode::crossfadeReverse)
                {
                    float* rightDataMutable = rightWorking.getWritePointer (0);
                    for (int s = 0; s < mergedSamples / 2; ++s)
                        std::swap (rightDataMutable[s], rightDataMutable[mergedSamples - 1 - s]);
                }

                const float* rightBlend = rightWorking.getReadPointer (0);
                if (mergedSamples == 1)
                {
                    mergedData[0] = 0.5f * (leftData[0] + rightBlend[0]);
                }
                else
                {
                    for (int s = 0; s < mergedSamples; ++s)
                    {
                        const float t = static_cast<float> (s) / static_cast<float> (mergedSamples - 1);
                        mergedData[s] = (1.0f - t) * leftData[s] + t * rightBlend[s];
                    }
                }
            }

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
