#include "ExportOrchestrator.h"

#include "AudioFileIO.h"

namespace {
    constexpr int kDefaultExportRetries = 3;
}

ExportOrchestrator::ExportOrchestrator (SliceStateStore& store)
    : stateStore (store)
{
}

bool ExportOrchestrator::exportSlices (const std::optional<SliceStateStore::ExportSettings>& overrideSettings)
{
    SliceStateStore::ExportSettings settings;
    if (! resolveSettings (overrideSettings, settings))
        return false;

    const auto snapshot = stateStore.getSnapshot();
    const auto& previewSnippetURLs = snapshot.previewSnippetURLs;
    if (previewSnippetURLs.empty())
        return false;

    const juce::File destinationDirectory = settings.sliceExportDirectory;
    if (destinationDirectory == juce::File())
        return false;

    if (! destinationDirectory.exists())
        destinationDirectory.createDirectory();

    const int retryCount = settings.sliceExportRetryCount > 0 ? settings.sliceExportRetryCount : kDefaultExportRetries;
    bool exportedAny = false;

    for (std::size_t index = 0; index < previewSnippetURLs.size(); ++index)
    {
        const juce::File sourceFile = previewSnippetURLs[index];
        if (! sourceFile.existsAsFile())
            continue;

        const juce::File destinationFile =
            destinationDirectory.getChildFile ("slice_" + juce::String (static_cast<int> (index)) + ".wav");

        bool success = false;
        for (int attempt = 0; attempt < retryCount; ++attempt)
        {
            if (sourceFile.copyFileTo (destinationFile))
            {
                success = true;
                break;
            }
        }

        if (success)
            exportedAny = true;
    }

    return exportedAny;
}

bool ExportOrchestrator::exportFullChainWithoutVolume (const std::optional<SliceStateStore::ExportSettings>& overrideSettings)
{
    SliceStateStore::ExportSettings settings;
    if (! resolveSettings (overrideSettings, settings))
        return false;

    const auto snapshot = stateStore.getSnapshot();
    const juce::File previewChainURL = snapshot.previewChainURL;
    if (! previewChainURL.existsAsFile())
        return false;

    const juce::File destinationFile = settings.chainExportFile;
    if (destinationFile == juce::File())
        return false;

    return previewChainURL.copyFileTo (destinationFile);
}

bool ExportOrchestrator::exportFullChainWithVolume (const std::optional<SliceStateStore::ExportSettings>& overrideSettings)
{
    SliceStateStore::ExportSettings settings;
    if (! resolveSettings (overrideSettings, settings))
        return false;

    const auto snapshot = stateStore.getSnapshot();
    if (snapshot.previewSnippetURLs.empty())
        return false;

    juce::File loopChainFile = snapshot.previewChainURL;
    if (loopChainFile == juce::File())
        loopChainFile = snapshot.previewSnippetURLs.front().getSiblingFile ("loop_chain.wav");
    else
        loopChainFile = loopChainFile.getSiblingFile ("loop_chain.wav");

    if (! buildVolumeChain (snapshot, loopChainFile))
        return false;

    stateStore.setPreviewChainURL (loopChainFile);

    const juce::File destinationFile = settings.chainExportFile;
    if (destinationFile == juce::File())
        return false;

    return loopChainFile.copyFileTo (destinationFile);
}

bool ExportOrchestrator::resolveSettings (const std::optional<SliceStateStore::ExportSettings>& overrideSettings,
                                          SliceStateStore::ExportSettings& resolved) const
{
    const auto snapshot = stateStore.getSnapshot();
    if (snapshot.exportSettingsLocked)
    {
        resolved = snapshot.exportSettings;
        return true;
    }

    if (! overrideSettings.has_value())
        return false;

    resolved = overrideSettings.value();
    return true;
}

bool ExportOrchestrator::buildVolumeChain (const SliceStateStore::SliceStateSnapshot& snapshot,
                                           const juce::File& chainFile)
{
    const auto& previewSnippetURLs = snapshot.previewSnippetURLs;
    const auto& sliceVolumeSettings = snapshot.sliceVolumeSettings;

    if (previewSnippetURLs.empty() || previewSnippetURLs.size() != sliceVolumeSettings.size())
        return false;

    AudioFileIO audioFileIO;
    std::vector<juce::AudioBuffer<float>> snippetBuffers;
    snippetBuffers.reserve (previewSnippetURLs.size());

    int totalSamples = 0;

    for (std::size_t index = 0; index < previewSnippetURLs.size(); ++index)
    {
        const juce::File snippetFile = previewSnippetURLs[index];
        if (! snippetFile.existsAsFile())
            continue;

        AudioFileIO::ConvertedAudio converted;
        juce::String formatDescription;
        if (! audioFileIO.readToMonoBuffer (snippetFile, converted, formatDescription))
            continue;

        converted.buffer.applyGain (sliceVolumeSettings[index]);
        totalSamples += converted.buffer.getNumSamples();
        snippetBuffers.push_back (std::move (converted.buffer));
    }

    if (snippetBuffers.empty() || totalSamples <= 0)
        return false;

    juce::AudioBuffer<float> chainBuffer (1, totalSamples);
    chainBuffer.clear();

    int writePosition = 0;
    for (const auto& buffer : snippetBuffers)
    {
        const int samples = buffer.getNumSamples();
        chainBuffer.copyFrom (0, writePosition, buffer, 0, 0, samples);
        writePosition += samples;
    }

    AudioFileIO::ConvertedAudio chainAudio;
    chainAudio.buffer = std::move (chainBuffer);
    chainAudio.sampleRate = 44100.0;

    return audioFileIO.writeMonoWav16 (chainFile, chainAudio);
}
