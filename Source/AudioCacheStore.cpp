#include "AudioCacheStore.h"
#include "AppProperties.h"

namespace
{
    AudioCacheStore::CacheEntry makeEntry (const juce::File& file,
                                           const juce::AudioFormatReader& reader,
                                           double minDurationSeconds)
    {
        AudioCacheStore::CacheEntry entry;
        entry.path = file.getFullPathName();
        entry.sampleRate = reader.sampleRate;
        entry.numChannels = static_cast<int> (reader.numChannels);

        if (reader.sampleRate > 0.0)
            entry.durationSeconds = static_cast<double> (reader.lengthInSamples) / reader.sampleRate;

        entry.isCandidate = entry.durationSeconds >= minDurationSeconds;
        return entry;
    }

    juce::File getAppSupportDirectory()
    {
        auto* propertiesFile = AppProperties::get().properties().getUserSettings();
        if (propertiesFile == nullptr)
            return juce::File();

        return propertiesFile->getFile().getParentDirectory();
    }

    juce::File getAppSupportFolder()
    {
        auto baseDir = getAppSupportDirectory();
        if (baseDir == juce::File())
            return juce::File();

        return baseDir.getChildFile ("SliceBotJUCE");
    }

    juce::var entryToVar (const AudioCacheStore::CacheEntry& entry)
    {
        auto entryObject = std::make_unique<juce::DynamicObject>();
        entryObject->setProperty ("path", entry.path);
        entryObject->setProperty ("durationSeconds", entry.durationSeconds);
        entryObject->setProperty ("sampleRate", entry.sampleRate);
        entryObject->setProperty ("numChannels", entry.numChannels);
        entryObject->setProperty ("isCandidate", entry.isCandidate);
        return entryObject.release();
    }

    bool fillEntryFromVar (const juce::var& value, AudioCacheStore::CacheEntry& entry)
    {
        if (auto* object = value.getDynamicObject())
        {
            if (object->hasProperty ("path"))
                entry.path = object->getProperty ("path").toString();

            entry.durationSeconds = static_cast<double> (object->getProperty ("durationSeconds"));
            entry.sampleRate = static_cast<double> (object->getProperty ("sampleRate"));
            entry.numChannels = static_cast<int> (object->getProperty ("numChannels"));
            if (object->hasProperty ("isCandidate"))
                entry.isCandidate = static_cast<bool> (object->getProperty ("isCandidate"));
            return ! entry.path.isEmpty();
        }

        return false;
    }
}

juce::File AudioCacheStore::getCacheFile()
{
    const auto appSupportDir = getAppSupportFolder();
    if (appSupportDir == juce::File())
        return juce::File();

    return appSupportDir.getChildFile ("AudioCache.json");
}

AudioCacheStore::CacheData AudioCacheStore::buildFromSource (const juce::File& source,
                                                             bool isDirectory,
                                                             double bpm,
                                                             std::atomic<bool>* shouldCancel,
                                                             std::function<void (int current, int total)> progressCallback,
                                                             bool* wasCancelled)
{
    CacheData data;
    data.sourcePath = source.getFullPathName();
    data.isDirectorySource = isDirectory;

    if (wasCancelled != nullptr)
        *wasCancelled = false;

    juce::AudioFormatManager formatManager;
    formatManager.registerBasicFormats();

    const double resolvedBpm = bpm > 0.0 ? bpm : 128.0;
    const double minDurationSeconds = (60.0 / resolvedBpm) * 32.0;

    if (shouldCancel != nullptr && shouldCancel->load())
    {
        if (wasCancelled != nullptr)
            *wasCancelled = true;
        return data;
    }

    if (isDirectory && source.isDirectory())
    {
        juce::Array<juce::File> files;
        source.findChildFiles (files, juce::File::findFiles, true);

        int current = 0;
        const int total = files.size();
        for (const auto& file : files)
        {
            if (shouldCancel != nullptr && shouldCancel->load())
            {
                if (wasCancelled != nullptr)
                    *wasCancelled = true;
                break;
            }

            ++current;
            std::unique_ptr<juce::AudioFormatReader> reader (formatManager.createReaderFor (file));
            if (reader == nullptr)
            {
                if (progressCallback)
                    progressCallback (current, total);
                continue;
            }

            data.entries.add (makeEntry (file, *reader, minDurationSeconds));

            if (progressCallback)
                progressCallback (current, total);
        }
    }
    else if (source.existsAsFile())
    {
        if (progressCallback)
            progressCallback (0, 1);

        std::unique_ptr<juce::AudioFormatReader> reader (formatManager.createReaderFor (source));
        if (reader != nullptr)
            data.entries.add (makeEntry (source, *reader, minDurationSeconds));

        if (progressCallback)
            progressCallback (1, 1);
    }

    return data;
}

AudioCacheStore::CacheData AudioCacheStore::load()
{
    CacheData data;
    const auto cacheFile = getCacheFile();
    if (! cacheFile.existsAsFile())
        return data;

    const auto jsonText = cacheFile.loadFileAsString();
    const auto parsed = juce::JSON::parse (jsonText);
    if (auto* object = parsed.getDynamicObject())
    {
        data.sourcePath = object->getProperty ("sourcePath").toString();
        data.isDirectorySource = static_cast<bool> (object->getProperty ("isDirectorySource"));

        if (auto* entries = object->getProperty ("entries").getArray())
        {
            for (const auto& entryValue : *entries)
            {
                CacheEntry entry;
                if (fillEntryFromVar (entryValue, entry))
                    data.entries.add (entry);
            }
        }
    }

    return data;
}

bool AudioCacheStore::save (const CacheData& data)
{
    const auto cacheFile = getCacheFile();
    if (cacheFile == juce::File())
        return false;

    juce::StringArray entryJson;
    for (const auto& entry : data.entries)
        entryJson.add (juce::JSON::toString (entryToVar (entry), true));

    const auto sourcePathJson = juce::JSON::toString (juce::var (data.sourcePath));
    const auto entriesJson = entryJson.joinIntoString (",\n");
    const auto isDirectoryText = data.isDirectorySource ? "true" : "false";

    const auto jsonText = juce::String (
        "{\n"
        "  \"sourcePath\": " + sourcePathJson + ",\n"
        "  \"isDirectorySource\": " + isDirectoryText + ",\n"
        "  \"entries\": [\n"
        + entriesJson +
        "\n  ]\n"
        "}");
    const auto parentDir = cacheFile.getParentDirectory();
    if (! parentDir.exists())
        parentDir.createDirectory();

    return cacheFile.replaceWithText (jsonText);
}
