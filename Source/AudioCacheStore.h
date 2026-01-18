#pragma once

#include <JuceHeader.h>
#include <atomic>
#include <functional>

class AudioCacheStore
{
public:
    struct CacheEntry
    {
        juce::String path;
        double durationSeconds = 0.0;
        int64_t fileSizeBytes = 0;
        int64_t lastModifiedMs = 0;
        bool isCandidate = true;
    };

    struct CacheData
    {
        juce::String sourcePath;
        bool isDirectorySource = false;
        juce::Array<CacheEntry> entries;
    };

    static juce::File getCacheFile();
    static CacheData buildFromSource (const juce::File& source,
                                      bool isDirectory,
                                      double bpm,
                                      std::atomic<bool>* shouldCancel,
                                      std::function<void (int current, int total)> progressCallback = {},
                                      bool* wasCancelled = nullptr);
    static CacheData load();
    static bool save (const CacheData& data);
};
