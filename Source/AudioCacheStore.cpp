#include "AudioCacheStore.h"
#include "AppProperties.h"
#include <map>
#include <cmath>
#include <cstdint>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
#include <unordered_map>

namespace
{
    const juce::StringArray kSupportedExtensions { "mp3", "wav", "m4a", "aiff", "aif", "flac" };

    bool isSupportedExtension (const juce::String& extension)
    {
        return kSupportedExtensions.contains (extension, true);
    }

    AudioCacheStore::CacheEntry makeEntry (const juce::File& file,
                                           const juce::AudioFormatReader& reader,
                                           double minDurationSeconds)
    {
        AudioCacheStore::CacheEntry entry;
        entry.path = file.getFullPathName();

        if (reader.sampleRate > 0.0)
            entry.durationSeconds = static_cast<double> (reader.lengthInSamples) / reader.sampleRate;

        entry.fileSizeBytes = file.getSize();
        entry.lastModifiedMs = file.getLastModificationTime().toMilliseconds();
        entry.isCandidate = entry.durationSeconds >= minDurationSeconds;
        return entry;
    }

    AudioCacheStore::CacheEntry makeEntryFromMetadata (const juce::File& file,
                                                       double durationSeconds,
                                                       double minDurationSeconds)
    {
        AudioCacheStore::CacheEntry entry;
        entry.path = file.getFullPathName();
        entry.durationSeconds = durationSeconds;
        entry.fileSizeBytes = file.getSize();
        entry.lastModifiedMs = file.getLastModificationTime().toMilliseconds();
        entry.isCandidate = durationSeconds >= minDurationSeconds;
        return entry;
    }

    bool readUInt32LittleEndian (juce::InputStream& stream, uint32_t& value)
    {
        unsigned char bytes[4] = { 0 };
        if (stream.read (bytes, 4) != 4)
            return false;

        value = static_cast<uint32_t> (bytes[0])
                | (static_cast<uint32_t> (bytes[1]) << 8)
                | (static_cast<uint32_t> (bytes[2]) << 16)
                | (static_cast<uint32_t> (bytes[3]) << 24);
        return true;
    }

    bool readUInt16LittleEndian (juce::InputStream& stream, uint16_t& value)
    {
        unsigned char bytes[2] = { 0 };
        if (stream.read (bytes, 2) != 2)
            return false;

        value = static_cast<uint16_t> (bytes[0])
                | static_cast<uint16_t> (bytes[1] << 8);
        return true;
    }

    bool readFourCC (juce::InputStream& stream, juce::String& value)
    {
        char id[4] = { 0 };
        if (stream.read (id, 4) != 4)
            return false;

        value = juce::String::fromUTF8 (id, 4);
        return true;
    }

    bool readUInt32BigEndian (juce::InputStream& stream, uint32_t& value)
    {
        unsigned char bytes[4] = { 0 };
        if (stream.read (bytes, 4) != 4)
            return false;

        value = (static_cast<uint32_t> (bytes[0]) << 24)
                | (static_cast<uint32_t> (bytes[1]) << 16)
                | (static_cast<uint32_t> (bytes[2]) << 8)
                | static_cast<uint32_t> (bytes[3]);
        return true;
    }

    bool readUInt64BigEndian (juce::InputStream& stream, uint64_t& value)
    {
        unsigned char bytes[8] = { 0 };
        if (stream.read (bytes, 8) != 8)
            return false;

        value = (static_cast<uint64_t> (bytes[0]) << 56)
                | (static_cast<uint64_t> (bytes[1]) << 48)
                | (static_cast<uint64_t> (bytes[2]) << 40)
                | (static_cast<uint64_t> (bytes[3]) << 32)
                | (static_cast<uint64_t> (bytes[4]) << 24)
                | (static_cast<uint64_t> (bytes[5]) << 16)
                | (static_cast<uint64_t> (bytes[6]) << 8)
                | static_cast<uint64_t> (bytes[7]);
        return true;
    }

    bool readUInt16BigEndian (juce::InputStream& stream, uint16_t& value)
    {
        unsigned char bytes[2] = { 0 };
        if (stream.read (bytes, 2) != 2)
            return false;

        value = static_cast<uint16_t> ((bytes[0] << 8) | bytes[1]);
        return true;
    }

    bool tryReadMetadata (const juce::File& file,
                          double minDurationSeconds,
                          AudioCacheStore::CacheEntry& entry);

    struct CacheBuildSharedState
    {
        CacheBuildSharedState (AudioCacheStore::CacheData& targetDataIn,
                               std::atomic<bool>* shouldCancelIn,
                               std::function<void (int current, int total)> progressCallbackIn,
                               double minDurationSecondsIn,
                               std::unordered_map<std::string, AudioCacheStore::CacheEntry> cachedEntriesIn)
            : targetData (targetDataIn),
              shouldCancel (shouldCancelIn),
              progressCallback (std::move (progressCallbackIn)),
              minDurationSeconds (minDurationSecondsIn),
              cachedEntries (std::move (cachedEntriesIn))
        {
        }

        AudioCacheStore::CacheData& targetData;
        std::atomic<bool>* shouldCancel = nullptr;
        std::function<void (int current, int total)> progressCallback;
        double minDurationSeconds = 0.0;
        std::atomic<int> totalFiles { 0 };
        std::atomic<int> processed { 0 };
        std::atomic<int> lastReported { 0 };
        std::atomic<int> lastTotalReported { 0 };
        std::atomic<int> supportedFiles { 0 };
        std::map<juce::String, int> extensionCounts;
        std::unordered_map<std::string, AudioCacheStore::CacheEntry> cachedEntries;
        std::deque<juce::File> pendingFiles;
        std::mutex queueMutex;
        std::condition_variable queueCondition;
        std::atomic<bool> producerDone { false };
        std::mutex entriesMutex;
        std::mutex extensionsMutex;
    };

    void reportProgress (CacheBuildSharedState& state)
    {
        if (! state.progressCallback)
            return;

        const int current = state.processed.fetch_add (1) + 1;
        const int total = state.totalFiles.load();
        if (current == total)
        {
            state.lastReported.store (current);
            state.progressCallback (current, total);
            return;
        }

        int last = state.lastReported.load();
        if ((current - last) < 100)
            return;

        if (state.lastReported.compare_exchange_weak (last, current))
            state.progressCallback (current, total);
    }

    void handleFile (const juce::File& file,
                     juce::AudioFormatManager& formatManager,
                     CacheBuildSharedState& state)
    {
        if (state.shouldCancel != nullptr && state.shouldCancel->load())
            return;

        auto extension = file.getFileExtension().toLowerCase();
        if (extension.startsWithChar ('.'))
            extension = extension.substring (1);
        if (extension.isEmpty())
            extension = "unknown";

        if (! isSupportedExtension (extension))
        {
            reportProgress (state);
            return;
        }

        {
            const std::lock_guard<std::mutex> lock (state.extensionsMutex);
            ++state.extensionCounts[extension];
        }

        AudioCacheStore::CacheEntry entry;
        const auto cachedIt = state.cachedEntries.find (file.getFullPathName().toStdString());
        if (cachedIt != state.cachedEntries.end())
        {
            const auto currentSize = file.getSize();
            const auto currentModified = file.getLastModificationTime().toMilliseconds();
            const auto& cachedEntry = cachedIt->second;
            if (cachedEntry.fileSizeBytes == currentSize
                && cachedEntry.lastModifiedMs == currentModified
                && cachedEntry.durationSeconds > 0.0)
            {
                entry = makeEntryFromMetadata (file, cachedEntry.durationSeconds, state.minDurationSeconds);
                state.supportedFiles.fetch_add (1);
                {
                    const std::lock_guard<std::mutex> lock (state.entriesMutex);
                    state.targetData.entries.add (entry);
                }
                reportProgress (state);
                return;
            }
        }

        if (tryReadMetadata (file, state.minDurationSeconds, entry))
        {
            state.supportedFiles.fetch_add (1);
            {
                const std::lock_guard<std::mutex> lock (state.entriesMutex);
                state.targetData.entries.add (entry);
            }
            reportProgress (state);
            return;
        }

        std::unique_ptr<juce::AudioFormatReader> reader (formatManager.createReaderFor (file));
        if (reader == nullptr)
        {
            reportProgress (state);
            return;
        }

        state.supportedFiles.fetch_add (1);
        {
            const std::lock_guard<std::mutex> lock (state.entriesMutex);
            state.targetData.entries.add (makeEntry (file, *reader, state.minDurationSeconds));
        }
        reportProgress (state);
    }

    class CacheWorkerJob : public juce::ThreadPoolJob
    {
    public:
        CacheWorkerJob (CacheBuildSharedState& stateIn)
            : juce::ThreadPoolJob ("CacheWorkerJob"),
              state (stateIn)
        {
            formatManager.registerBasicFormats();
        }

        JobStatus runJob() override
        {
            while (true)
            {
                if (state.shouldCancel != nullptr && state.shouldCancel->load())
                    return jobHasFinished;

                juce::File fileToHandle;
                {
                    std::unique_lock<std::mutex> lock (state.queueMutex);
                    state.queueCondition.wait (lock, [this]()
                    {
                        return state.producerDone.load() || ! state.pendingFiles.empty();
                    });

                    if (state.pendingFiles.empty())
                    {
                        if (state.producerDone.load())
                            break;
                        continue;
                    }

                    fileToHandle = state.pendingFiles.front();
                    state.pendingFiles.pop_front();
                }

                handleFile (fileToHandle, formatManager, state);
            }

            return jobHasFinished;
        }

    private:
        CacheBuildSharedState& state;
        juce::AudioFormatManager formatManager;
    };

    double readExtended80 (juce::InputStream& stream, bool& ok)
    {
        ok = false;
        unsigned char bytes[10] = { 0 };
        if (stream.read (bytes, 10) != 10)
            return 0.0;

        const int exponent = ((bytes[0] & 0x7F) << 8) | bytes[1];
        const uint64_t hiMantissa = (static_cast<uint64_t> (bytes[2]) << 24)
                                    | (static_cast<uint64_t> (bytes[3]) << 16)
                                    | (static_cast<uint64_t> (bytes[4]) << 8)
                                    | static_cast<uint64_t> (bytes[5]);
        const uint64_t loMantissa = (static_cast<uint64_t> (bytes[6]) << 24)
                                    | (static_cast<uint64_t> (bytes[7]) << 16)
                                    | (static_cast<uint64_t> (bytes[8]) << 8)
                                    | static_cast<uint64_t> (bytes[9]);

        if (exponent == 0 && hiMantissa == 0 && loMantissa == 0)
        {
            ok = true;
            return 0.0;
        }

        double mantissa = static_cast<double> (hiMantissa) * std::pow (2.0, -31.0)
                          + static_cast<double> (loMantissa) * std::pow (2.0, -63.0);
        mantissa += 1.0;
        const int adjustedExponent = exponent - 16383;
        ok = true;
        const double result = std::ldexp (mantissa, adjustedExponent);
        return (bytes[0] & 0x80) != 0 ? -result : result;
    }

    bool tryReadWavMetadata (const juce::File& file,
                             double minDurationSeconds,
                             AudioCacheStore::CacheEntry& entry)
    {
        const auto extension = file.getFileExtension().toLowerCase();
        if (extension != ".wav" && extension != ".wave")
            return false;

        juce::FileInputStream stream (file);
        if (! stream.openedOk())
            return false;

        juce::String riffId;
        if (! readFourCC (stream, riffId) || riffId != "RIFF")
            return false;

        uint32_t riffSize = 0;
        if (! readUInt32LittleEndian (stream, riffSize))
            return false;
        juce::ignoreUnused (riffSize);

        juce::String waveId;
        if (! readFourCC (stream, waveId) || waveId != "WAVE")
            return false;

        bool hasFmt = false;
        bool hasData = false;
        uint16_t audioFormat = 0;
        uint16_t numChannels = 0;
        uint32_t sampleRate = 0;
        uint16_t bitsPerSample = 0;
        uint32_t dataSize = 0;

        while (! stream.isExhausted())
        {
            juce::String chunkId;
            if (! readFourCC (stream, chunkId))
                break;

            uint32_t chunkSize = 0;
            if (! readUInt32LittleEndian (stream, chunkSize))
                break;

            if (chunkId == "fmt ")
            {
                if (! readUInt16LittleEndian (stream, audioFormat))
                    return false;
                if (! readUInt16LittleEndian (stream, numChannels))
                    return false;
                if (! readUInt32LittleEndian (stream, sampleRate))
                    return false;

                uint32_t byteRate = 0;
                uint16_t blockAlign = 0;
                if (! readUInt32LittleEndian (stream, byteRate))
                    return false;
                if (! readUInt16LittleEndian (stream, blockAlign))
                    return false;
                if (! readUInt16LittleEndian (stream, bitsPerSample))
                    return false;

                const int remaining = static_cast<int> (chunkSize) - 16;
                if (remaining > 0)
                    stream.skipNextBytes (remaining);

                hasFmt = true;
            }
            else if (chunkId == "data")
            {
                dataSize = chunkSize;
                stream.skipNextBytes (static_cast<int> (chunkSize));
                hasData = true;
            }
            else
            {
                stream.skipNextBytes (static_cast<int> (chunkSize));
            }

            if ((chunkSize % 2) != 0)
                stream.skipNextBytes (1);

            if (hasFmt && hasData)
                break;
        }

        if (! hasFmt || ! hasData)
            return false;

        if (numChannels == 0 || sampleRate == 0 || bitsPerSample == 0)
            return false;

        if (audioFormat != 1 && audioFormat != 3)
            return false;

        const double bytesPerFrame = static_cast<double> (numChannels) * (static_cast<double> (bitsPerSample) / 8.0);
        if (bytesPerFrame <= 0.0)
            return false;

        const double durationSeconds = static_cast<double> (dataSize) / bytesPerFrame / static_cast<double> (sampleRate);
        entry = makeEntryFromMetadata (file, durationSeconds, minDurationSeconds);
        return true;
    }

    bool tryReadAiffMetadata (const juce::File& file,
                              double minDurationSeconds,
                              AudioCacheStore::CacheEntry& entry)
    {
        const auto extension = file.getFileExtension().toLowerCase();
        if (extension != ".aiff" && extension != ".aif")
            return false;

        juce::FileInputStream stream (file);
        if (! stream.openedOk())
            return false;

        juce::String formId;
        if (! readFourCC (stream, formId) || formId != "FORM")
            return false;

        uint32_t formSize = 0;
        if (! readUInt32BigEndian (stream, formSize))
            return false;
        juce::ignoreUnused (formSize);

        juce::String formType;
        if (! readFourCC (stream, formType))
            return false;

        if (formType != "AIFF" && formType != "AIFC")
            return false;

        bool hasComm = false;
        uint32_t numFrames = 0;
        double sampleRate = 0.0;

        while (! stream.isExhausted())
        {
            juce::String chunkId;
            if (! readFourCC (stream, chunkId))
                break;

            uint32_t chunkSize = 0;
            if (! readUInt32BigEndian (stream, chunkSize))
                break;

            if (chunkId == "COMM")
            {
                uint16_t numChannels = 0;
                if (! readUInt16BigEndian (stream, numChannels))
                    return false;
                juce::ignoreUnused (numChannels);

                if (! readUInt32BigEndian (stream, numFrames))
                    return false;

                uint16_t sampleSize = 0;
                if (! readUInt16BigEndian (stream, sampleSize))
                    return false;
                juce::ignoreUnused (sampleSize);

                bool ok = false;
                sampleRate = readExtended80 (stream, ok);
                if (! ok)
                    return false;

                const int remaining = static_cast<int> (chunkSize) - 18;
                if (remaining > 0)
                    stream.skipNextBytes (remaining);

                hasComm = true;
            }
            else
            {
                stream.skipNextBytes (static_cast<int> (chunkSize));
            }

            if ((chunkSize % 2) != 0)
                stream.skipNextBytes (1);

            if (hasComm)
                break;
        }

        if (! hasComm || sampleRate <= 0.0 || numFrames == 0)
            return false;

        const double durationSeconds = static_cast<double> (numFrames) / sampleRate;
        entry = makeEntryFromMetadata (file, durationSeconds, minDurationSeconds);
        return true;
    }

    bool tryReadFlacMetadata (const juce::File& file,
                              double minDurationSeconds,
                              AudioCacheStore::CacheEntry& entry)
    {
        const auto extension = file.getFileExtension().toLowerCase();
        if (extension != ".flac")
            return false;

        juce::FileInputStream stream (file);
        if (! stream.openedOk())
            return false;

        juce::String flacId;
        if (! readFourCC (stream, flacId) || flacId != "fLaC")
            return false;

        bool isLast = false;
        while (! stream.isExhausted() && ! isLast)
        {
            uint8_t header = 0;
            if (stream.read (&header, 1) != 1)
                return false;

            isLast = (header & 0x80) != 0;
            const uint8_t blockType = header & 0x7F;

            uint32_t blockLength = 0;
            unsigned char lengthBytes[3] = { 0 };
            if (stream.read (lengthBytes, 3) != 3)
                return false;
            blockLength = (static_cast<uint32_t> (lengthBytes[0]) << 16)
                          | (static_cast<uint32_t> (lengthBytes[1]) << 8)
                          | static_cast<uint32_t> (lengthBytes[2]);

            if (blockType == 0)
            {
                if (blockLength < 34)
                    return false;

                unsigned char info[34] = { 0 };
                if (stream.read (info, 34) != 34)
                    return false;

                const uint64_t sampleRate = (static_cast<uint64_t> (info[10]) << 12)
                                            | (static_cast<uint64_t> (info[11]) << 4)
                                            | (static_cast<uint64_t> (info[12]) >> 4);
                const uint64_t totalSamples = ((static_cast<uint64_t> (info[13] & 0x0F) << 32)
                                               | (static_cast<uint64_t> (info[14]) << 24)
                                               | (static_cast<uint64_t> (info[15]) << 16)
                                               | (static_cast<uint64_t> (info[16]) << 8)
                                               | static_cast<uint64_t> (info[17]));

                if (sampleRate == 0 || totalSamples == 0)
                    return false;

                const double durationSeconds = static_cast<double> (totalSamples) / static_cast<double> (sampleRate);
                entry = makeEntryFromMetadata (file, durationSeconds, minDurationSeconds);
                return true;
            }
            else
            {
                stream.skipNextBytes (static_cast<int> (blockLength));
            }
        }

        return false;
    }

    bool tryReadMp3Metadata (const juce::File& file,
                             double minDurationSeconds,
                             AudioCacheStore::CacheEntry& entry)
    {
        const auto extension = file.getFileExtension().toLowerCase();
        if (extension != ".mp3")
            return false;

        juce::FileInputStream stream (file);
        if (! stream.openedOk())
            return false;

        int64_t offset = 0;
        unsigned char header[10] = { 0 };
        if (stream.read (header, 10) != 10)
            return false;

        if (header[0] == 'I' && header[1] == 'D' && header[2] == '3')
        {
            const uint32_t size = ((header[6] & 0x7F) << 21)
                                  | ((header[7] & 0x7F) << 14)
                                  | ((header[8] & 0x7F) << 7)
                                  | (header[9] & 0x7F);
            offset = 10 + static_cast<int64_t> (size);
            stream.setPosition (offset);
        }
        else
        {
            stream.setPosition (0);
        }

        unsigned char frameHeader[4] = { 0 };
        if (stream.read (frameHeader, 4) != 4)
            return false;

        const uint32_t frame = (static_cast<uint32_t> (frameHeader[0]) << 24)
                               | (static_cast<uint32_t> (frameHeader[1]) << 16)
                               | (static_cast<uint32_t> (frameHeader[2]) << 8)
                               | static_cast<uint32_t> (frameHeader[3]);

        if ((frame & 0xFFE00000) != 0xFFE00000)
            return false;

        const int versionBits = (frame >> 19) & 0x3;
        const int layerBits = (frame >> 17) & 0x3;
        const int bitrateIndex = (frame >> 12) & 0xF;
        const int sampleRateIndex = (frame >> 10) & 0x3;

        if (versionBits == 1 || layerBits != 1 || bitrateIndex == 0 || bitrateIndex == 15 || sampleRateIndex == 3)
            return false;

        const bool isMpeg1 = versionBits == 3;

        static const int bitrateTableMpeg1[16] = { 0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320, 0 };
        static const int bitrateTableMpeg2[16] = { 0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160, 0 };
        static const int sampleRateTable[3] = { 44100, 48000, 32000 };

        int sampleRate = sampleRateTable[sampleRateIndex];
        if (versionBits == 2)
            sampleRate /= 2;
        else if (versionBits == 0)
            sampleRate /= 4;

        const int bitrate = (isMpeg1 ? bitrateTableMpeg1[bitrateIndex] : bitrateTableMpeg2[bitrateIndex]);

        if (sampleRate <= 0 || bitrate <= 0)
            return false;

        const int64_t fileSize = file.getSize();
        const int64_t audioBytes = juce::jmax<int64_t> (0, fileSize - offset);
        if (audioBytes == 0)
            return false;

        const double durationSeconds = (static_cast<double> (audioBytes) * 8.0) / (static_cast<double> (bitrate) * 1000.0);
        entry = makeEntryFromMetadata (file, durationSeconds, minDurationSeconds);
        return true;
    }

    bool tryReadM4aMetadata (const juce::File& file,
                             double minDurationSeconds,
                             AudioCacheStore::CacheEntry& entry)
    {
        const auto extension = file.getFileExtension().toLowerCase();
        if (extension != ".m4a")
            return false;

        juce::FileInputStream stream (file);
        if (! stream.openedOk())
            return false;

        const int64_t fileSize = file.getSize();
        int64_t position = 0;

        auto readAtomHeader = [&] (int64_t basePosition, uint64_t& atomSize, juce::String& atomType) -> bool
        {
            uint32_t size32 = 0;
            if (! readUInt32BigEndian (stream, size32))
                return false;

            if (! readFourCC (stream, atomType))
                return false;

            atomSize = size32;
            if (atomSize == 1)
            {
                if (! readUInt64BigEndian (stream, atomSize))
                    return false;
            }
            else if (atomSize == 0)
            {
                atomSize = static_cast<uint64_t> (fileSize - basePosition);
            }

            return atomSize >= 8;
        };

        auto parseMoovForDuration = [&] (int64_t moovPos) -> bool
        {
            stream.setPosition (moovPos);
            uint64_t headerSize = 0;
            juce::String atomType;
            if (! readAtomHeader (moovPos, headerSize, atomType))
                return false;

            if (atomType != "moov" || headerSize < 8)
                return false;

            const int64_t moovEnd = moovPos + static_cast<int64_t> (headerSize);
            int64_t childPos = stream.getPosition();
            while (childPos + 8 <= moovEnd)
            {
                stream.setPosition (childPos);
                uint64_t childSize = 0;
                juce::String childType;
                if (! readAtomHeader (childPos, childSize, childType))
                    return false;

                const int64_t childDataStart = stream.getPosition();
                if (childType == "mvhd")
                {
                    uint8_t version = 0;
                    if (stream.read (&version, 1) != 1)
                        return false;

                    stream.skipNextBytes (3);

                    if (version == 0)
                    {
                        stream.skipNextBytes (8);
                        uint32_t timescale = 0;
                        uint32_t duration = 0;
                        if (! readUInt32BigEndian (stream, timescale))
                            return false;
                        if (! readUInt32BigEndian (stream, duration))
                            return false;

                        if (timescale == 0 || duration == 0)
                            return false;

                        const double durationSeconds = static_cast<double> (duration) / static_cast<double> (timescale);
                        entry = makeEntryFromMetadata (file, durationSeconds, minDurationSeconds);
                        return true;
                    }
                    else if (version == 1)
                    {
                        stream.skipNextBytes (16);
                        uint32_t timescale = 0;
                        uint64_t duration = 0;
                        if (! readUInt32BigEndian (stream, timescale))
                            return false;
                        if (! readUInt64BigEndian (stream, duration))
                            return false;

                        if (timescale == 0 || duration == 0)
                            return false;

                        const double durationSeconds = static_cast<double> (duration) / static_cast<double> (timescale);
                        entry = makeEntryFromMetadata (file, durationSeconds, minDurationSeconds);
                        return true;
                    }

                    return false;
                }

                if (childSize == 0)
                    break;

                childPos = childDataStart + static_cast<int64_t> (childSize - 8);
            }

            return false;
        };

        const int64_t tailSize = juce::jmin<int64_t> (fileSize, 1024 * 1024);
        if (tailSize >= 12)
        {
            juce::HeapBlock<char> tailBuffer (static_cast<size_t> (tailSize));
            stream.setPosition (fileSize - tailSize);
            if (stream.read (tailBuffer.get(), static_cast<int> (tailSize)) == static_cast<int> (tailSize))
            {
                for (int64_t i = tailSize - 4; i >= 4; --i)
                {
                    if (tailBuffer[i] == 'm'
                        && tailBuffer[i + 1] == 'o'
                        && tailBuffer[i + 2] == 'o'
                        && tailBuffer[i + 3] == 'v')
                    {
                        const unsigned char b0 = static_cast<unsigned char> (tailBuffer[i - 4]);
                        const unsigned char b1 = static_cast<unsigned char> (tailBuffer[i - 3]);
                        const unsigned char b2 = static_cast<unsigned char> (tailBuffer[i - 2]);
                        const unsigned char b3 = static_cast<unsigned char> (tailBuffer[i - 1]);
                        const uint32_t atomSize = (static_cast<uint32_t> (b0) << 24)
                                                  | (static_cast<uint32_t> (b1) << 16)
                                                  | (static_cast<uint32_t> (b2) << 8)
                                                  | static_cast<uint32_t> (b3);
                        if (atomSize >= 8 && atomSize != 1)
                        {
                            const int64_t moovPos = (fileSize - tailSize) + (i - 4);
                            if (moovPos >= 0 && (moovPos + static_cast<int64_t> (atomSize)) <= fileSize)
                            {
                                if (parseMoovForDuration (moovPos))
                                    return true;
                            }
                        }
                    }
                }
            }
        }

        while (position + 8 <= fileSize)
        {
            stream.setPosition (position);
            uint64_t atomSize = 0;
            juce::String atomType;
            if (! readAtomHeader (position, atomSize, atomType))
                return false;

            const int64_t atomDataStart = stream.getPosition();
            if (atomType == "moov")
            {
                int64_t moovPos = atomDataStart;
                const int64_t moovEnd = position + static_cast<int64_t> (atomSize);
                while (moovPos + 8 <= moovEnd)
                {
                    stream.setPosition (moovPos);
                    uint64_t childSize = 0;
                    juce::String childType;
                    if (! readAtomHeader (moovPos, childSize, childType))
                        return false;

                    const int64_t childDataStart = stream.getPosition();
                    if (childType == "mvhd")
                    {
                        uint8_t version = 0;
                        if (stream.read (&version, 1) != 1)
                            return false;

                        stream.skipNextBytes (3);

                        if (version == 0)
                        {
                            stream.skipNextBytes (8);
                            uint32_t timescale = 0;
                            uint32_t duration = 0;
                            if (! readUInt32BigEndian (stream, timescale))
                                return false;
                            if (! readUInt32BigEndian (stream, duration))
                                return false;

                            if (timescale == 0 || duration == 0)
                                return false;

                            const double durationSeconds = static_cast<double> (duration) / static_cast<double> (timescale);
                            entry = makeEntryFromMetadata (file, durationSeconds, minDurationSeconds);
                            return true;
                        }
                        else if (version == 1)
                        {
                            stream.skipNextBytes (16);
                            uint32_t timescale = 0;
                            uint64_t duration = 0;
                            if (! readUInt32BigEndian (stream, timescale))
                                return false;
                            if (! readUInt64BigEndian (stream, duration))
                                return false;

                            if (timescale == 0 || duration == 0)
                                return false;

                            const double durationSeconds = static_cast<double> (duration) / static_cast<double> (timescale);
                            entry = makeEntryFromMetadata (file, durationSeconds, minDurationSeconds);
                            return true;
                        }
                    }

                    if (childSize == 0)
                        break;

                    moovPos = childDataStart + static_cast<int64_t> (childSize - 8);
                }

                return false;
            }

            if (atomSize == 0)
                break;

            position += static_cast<int64_t> (atomSize);
        }

        return false;
    }

    bool tryReadMetadata (const juce::File& file,
                          double minDurationSeconds,
                          AudioCacheStore::CacheEntry& entry)
    {
        return tryReadWavMetadata (file, minDurationSeconds, entry)
               || tryReadAiffMetadata (file, minDurationSeconds, entry)
               || tryReadFlacMetadata (file, minDurationSeconds, entry)
               || tryReadMp3Metadata (file, minDurationSeconds, entry)
               || tryReadM4aMetadata (file, minDurationSeconds, entry);
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
        entryObject->setProperty ("duration", entry.durationSeconds);
        entryObject->setProperty ("fileSizeBytes", static_cast<double> (entry.fileSizeBytes));
        entryObject->setProperty ("lastModifiedMs", static_cast<double> (entry.lastModifiedMs));
        return entryObject.release();
    }

    bool fillEntryFromVar (const juce::var& value, AudioCacheStore::CacheEntry& entry)
    {
        if (auto* object = value.getDynamicObject())
        {
            if (object->hasProperty ("path"))
                entry.path = object->getProperty ("path").toString();

            if (object->hasProperty ("duration"))
                entry.durationSeconds = static_cast<double> (object->getProperty ("duration"));
            else if (object->hasProperty ("durationSeconds"))
                entry.durationSeconds = static_cast<double> (object->getProperty ("durationSeconds"));

            if (object->hasProperty ("fileSizeBytes"))
                entry.fileSizeBytes = static_cast<int64_t> (object->getProperty ("fileSizeBytes"));

            if (object->hasProperty ("lastModifiedMs"))
                entry.lastModifiedMs = static_cast<int64_t> (object->getProperty ("lastModifiedMs"));

            entry.isCandidate = true;
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
    const double buildStartMs = juce::Time::getMillisecondCounterHiRes();
    CacheData data;
    data.sourcePath = source.getFullPathName();
    data.isDirectorySource = isDirectory;

    if (wasCancelled != nullptr)
        *wasCancelled = false;

    const double resolvedBpm = bpm > 0.0 ? bpm : 128.0;
    const double minDurationSeconds = (60.0 / resolvedBpm) * 32.0;

    std::unordered_map<std::string, AudioCacheStore::CacheEntry> cachedEntries;
    if (const auto existingCache = load();
        existingCache.sourcePath == data.sourcePath
        && existingCache.isDirectorySource == data.isDirectorySource)
    {
        for (const auto& entry : existingCache.entries)
            cachedEntries.emplace (entry.path.toStdString(), entry);
    }

    if (shouldCancel != nullptr && shouldCancel->load())
    {
        if (wasCancelled != nullptr)
            *wasCancelled = true;
        return data;
    }

    CacheBuildSharedState sharedState (data,
                                       shouldCancel,
                                       progressCallback,
                                       minDurationSeconds,
                                       std::move (cachedEntries));

    if (progressCallback)
        progressCallback (0, 0);

    auto enqueueFile = [&sharedState] (const juce::File& file)
    {
        auto extension = file.getFileExtension().toLowerCase();
        if (extension.startsWithChar ('.'))
            extension = extension.substring (1);
        if (! isSupportedExtension (extension))
            return;

        int total = 0;
        {
            const std::lock_guard<std::mutex> lock (sharedState.queueMutex);
            sharedState.pendingFiles.push_back (file);
            total = sharedState.totalFiles.fetch_add (1) + 1;
        }
        sharedState.queueCondition.notify_one();

        if (sharedState.progressCallback)
        {
            int lastTotal = sharedState.lastTotalReported.load();
            if (total == 1 || (total - lastTotal) >= 100)
            {
                if (sharedState.lastTotalReported.compare_exchange_weak (lastTotal, total))
                {
                    const int current = sharedState.processed.load();
                    sharedState.progressCallback (current, total);
                }
            }
        }
    };

    std::thread producer ([&]()
    {
        if (isDirectory && source.isDirectory())
        {
            for (const auto& entry : juce::RangedDirectoryIterator (source, true, "*", juce::File::findFiles))
            {
                if (shouldCancel != nullptr && shouldCancel->load())
                {
                    if (wasCancelled != nullptr)
                        *wasCancelled = true;
                    break;
                }

                enqueueFile (entry.getFile());
            }
        }
        else if (source.existsAsFile())
        {
            enqueueFile (source);
        }

        sharedState.producerDone.store (true);
        sharedState.queueCondition.notify_all();
        if (sharedState.progressCallback)
            sharedState.progressCallback (sharedState.processed.load(), sharedState.totalFiles.load());
    });

    const int cpuCount = juce::jmax (1, juce::SystemStats::getNumCpus());
    const int workerCount = juce::jmax (1, juce::jmin (cpuCount, 8));
    juce::ThreadPool pool (workerCount);
    juce::OwnedArray<CacheWorkerJob> jobs;
    for (int i = 0; i < workerCount; ++i)
    {
        auto* job = jobs.add (new CacheWorkerJob (sharedState));
        pool.addJob (job, false);
    }

    pool.removeAllJobs (true, -1);
    if (producer.joinable())
        producer.join();

    if (shouldCancel != nullptr && shouldCancel->load())
    {
        if (wasCancelled != nullptr)
            *wasCancelled = true;
    }

    juce::StringArray extensionSummary;
    for (const auto& entry : sharedState.extensionCounts)
        extensionSummary.add (entry.first + "=" + juce::String (entry.second));

    const int totalFilesScanned = sharedState.totalFiles.load();
    const double buildElapsedMs = juce::Time::getMillisecondCounterHiRes() - buildStartMs;
    juce::Logger::writeToLog ("Cache build finished in "
                              + juce::String (buildElapsedMs, 2)
                              + " ms. scanned="
                              + juce::String (totalFilesScanned)
                              + ", supported="
                              + juce::String (sharedState.supportedFiles.load())
                              + ", per-extension=["
                              + extensionSummary.joinIntoString (", ")
                              + "]");

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
        if (object->hasProperty ("sourceDirectory"))
            data.sourcePath = object->getProperty ("sourceDirectory").toString();
        else
            data.sourcePath = object->getProperty ("sourcePath").toString();

        if (data.sourcePath.isNotEmpty())
        {
            const juce::File sourceFile (data.sourcePath);
            data.isDirectorySource = sourceFile.exists() && sourceFile.isDirectory();
        }

        if (auto* entries = object->getProperty ("files").getArray())
        {
            for (const auto& entryValue : *entries)
            {
                CacheEntry entry;
                if (fillEntryFromVar (entryValue, entry))
                    data.entries.add (entry);
            }
        }
        else if (auto* entries = object->getProperty ("entries").getArray())
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

    const auto jsonText = juce::String (
        "{\n"
        "  \"sourceDirectory\": " + sourcePathJson + ",\n"
        "  \"files\": [\n"
        + entriesJson +
        "\n  ]\n"
        "}");
    const auto parentDir = cacheFile.getParentDirectory();
    if (! parentDir.exists())
        parentDir.createDirectory();

    return cacheFile.replaceWithText (jsonText);
}
