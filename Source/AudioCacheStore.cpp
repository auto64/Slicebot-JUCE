#include "AudioCacheStore.h"
#include "AppProperties.h"

namespace
{
    juce::File getAppSupportDirectory()
    {
        auto* propertiesFile = AppProperties::get().properties().getUserSettings();
        if (propertiesFile == nullptr)
            return juce::File();

        return propertiesFile->getFile().getParentDirectory();
    }

    juce::var entryToVar (const AudioCacheStore::CacheEntry& entry)
    {
        auto entryObject = std::make_unique<juce::DynamicObject>();
        entryObject->setProperty ("path", entry.path);
        entryObject->setProperty ("durationSeconds", entry.durationSeconds);
        entryObject->setProperty ("sampleRate", entry.sampleRate);
        entryObject->setProperty ("numChannels", entry.numChannels);
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
            return ! entry.path.isEmpty();
        }

        return false;
    }
}

juce::File AudioCacheStore::getCacheFile()
{
    const auto appSupportDir = getAppSupportDirectory();
    if (appSupportDir == juce::File())
        return juce::File();

    return appSupportDir.getChildFile ("AudioCache.json");
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
