#pragma once

#include <JuceHeader.h>

class AudioCacheStore
{
public:
    struct CacheEntry
    {
        juce::String path;
        double durationSeconds = 0.0;
        double sampleRate = 0.0;
        int numChannels = 0;
    };

    struct CacheData
    {
        juce::String sourcePath;
        bool isDirectorySource = false;
        juce::Array<CacheEntry> entries;
    };

    static juce::File getCacheFile();
    static CacheData load();
    static bool save (const CacheData& data);
};
