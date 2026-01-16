#include "ExportOrchestrator.h"

#include "AudioFileIO.h"
#include <cmath>

namespace {
    constexpr int kDefaultExportRetries = 3;
    constexpr float kDefaultVolume = 0.75f;

    float sliderValueToDb (float value)
    {
        if (value <= 0.75f)
            return (40.0f / 0.75f) * value - 40.0f;

        return 32.0f * value - 24.0f;
    }

    float dbToLinear (float db)
    {
        return std::pow (10.0f, db / 20.0f);
    }

    float volumeSettingToGain (const SliceStateStore::SliceVolumeSetting& setting)
    {
        if (setting.isMuted)
            return 0.0f;

        return dbToLinear (sliderValueToDb (setting.volume));
    }

    int nextAvailableExportNumber (const juce::String& prefix, const juce::File& directory)
    {
        if (! directory.isDirectory())
            return 1;

        int maxNumber = 0;
        juce::Array<juce::File> files;
        directory.findChildFiles (files, juce::File::findFiles, false);

        for (const auto& file : files)
        {
            const auto name = file.getFileName();
            if (! name.startsWith (prefix + "_"))
                continue;

            auto numberPart = name.fromFirstOccurrenceOf (prefix + "_", false, false);
            if (numberPart.contains ("_chain"))
                numberPart = numberPart.upToFirstOccurrenceOf ("_chain", false, false);
            if (numberPart.containsChar ('.'))
                numberPart = numberPart.upToFirstOccurrenceOf (".", false, false);

            const int number = numberPart.getIntValue();
            if (number > maxNumber)
                maxNumber = number;
        }

        return maxNumber + 1;
    }

    bool exportSnippetWithVolume (const juce::File& sourceFile,
                                  float gain,
                                  const juce::File& destinationFile,
                                  AudioFileIO& audioFileIO)
    {
        if (! sourceFile.existsAsFile())
            return false;

        AudioFileIO::ConvertedAudio converted;
        juce::String formatDescription;
        if (! audioFileIO.readToMonoBuffer (sourceFile, converted, formatDescription))
            return false;

        converted.buffer.applyGain (gain);

        return audioFileIO.writeMonoWav16 (destinationFile, converted);
    }
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

    const juce::File destinationDirectory = settings.exportDirectory;
    if (destinationDirectory == juce::File())
        return false;

    if (! destinationDirectory.exists())
        destinationDirectory.createDirectory();

    const int retryCount = settings.sliceExportRetryCount > 0 ? settings.sliceExportRetryCount : kDefaultExportRetries;
    bool exportedAny = false;
    AudioFileIO audioFileIO;
    const auto& sliceVolumeSettings = snapshot.sliceVolumeSettings;
    const int startingNumber = nextAvailableExportNumber (settings.exportPrefix, destinationDirectory);
    int exportNumber = startingNumber;

    for (std::size_t index = 0; index < previewSnippetURLs.size(); ++index)
    {
        const juce::File sourceFile = previewSnippetURLs[index];
        if (! sourceFile.existsAsFile())
            continue;

        const juce::File destinationFile =
            destinationDirectory.getChildFile (settings.exportPrefix
                                               + "_" + juce::String (exportNumber) + ".wav");

        bool success = false;
        for (int attempt = 0; attempt < retryCount; ++attempt)
        {
            const auto setting = index < sliceVolumeSettings.size()
                                     ? sliceVolumeSettings[index]
                                     : SliceStateStore::SliceVolumeSetting { kDefaultVolume, false };
            if (exportSnippetWithVolume (sourceFile, volumeSettingToGain (setting), destinationFile, audioFileIO))
            {
                success = true;
                break;
            }
        }

        if (success)
            exportedAny = true;

        exportNumber += 1;
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

    const juce::File destinationDirectory = settings.exportDirectory;
    if (destinationDirectory == juce::File())
        return false;

    if (! destinationDirectory.exists())
        destinationDirectory.createDirectory();

    const int exportNumber = nextAvailableExportNumber (settings.exportPrefix, destinationDirectory);
    const juce::File destinationFile =
        destinationDirectory.getChildFile (settings.exportPrefix
                                           + "_" + juce::String (exportNumber) + "_chain.wav");

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

    const juce::File destinationDirectory = settings.exportDirectory;
    if (destinationDirectory == juce::File())
        return false;

    if (! destinationDirectory.exists())
        destinationDirectory.createDirectory();

    const int exportNumber = nextAvailableExportNumber (settings.exportPrefix, destinationDirectory);
    const juce::File destinationFile =
        destinationDirectory.getChildFile (settings.exportPrefix
                                           + "_" + juce::String (exportNumber) + "_chain.wav");

    return buildVolumeChain (snapshot, destinationFile);
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

    if (previewSnippetURLs.empty())
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

        const auto setting = index < sliceVolumeSettings.size()
                                 ? sliceVolumeSettings[index]
                                 : SliceStateStore::SliceVolumeSetting { kDefaultVolume, false };
        converted.buffer.applyGain (volumeSettingToGain (setting));
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
