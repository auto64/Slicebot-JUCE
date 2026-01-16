#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_formats/juce_audio_formats.h>

class RecordingWriter
{
public:
    RecordingWriter (int maxSamples,
                     int numChannels,
                     double initialSampleRate,
                     const juce::File& targetFile);

    // device-dependent (may change)
    void setSampleRate (double newSampleRate);

    // recording control
    void beginPass();
    void rollbackPass();
    void commitPass();

    bool isFull() const;
    int  getTotalSamples() const;
    int  getPassSamples() const;
    int  getMaxSamples() const;

    void write (const float* const* input,
                int numChannels,
                int numSamples);

    bool writeToDisk();
    bool loadFromDisk();

    int readSamples (float* dest,
                     int startSample,
                     int numSamples) const;

    void clear();

private:
    juce::AudioBuffer<float> buffer;
    juce::File file;

    double sampleRate = 0.0;

    int writeHead = 0;
    int passStart = 0;
    int maxSamples = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RecordingWriter)
};
