#pragma once

#include <JuceHeader.h>

class AudioFileIO
{
public:
    struct ConvertedAudio
    {
        juce::AudioBuffer<float> buffer;
        double sampleRate = 44100.0;
    };

    AudioFileIO();

    bool readToMonoBuffer (const juce::File& inputFile,
                           ConvertedAudio& output,
                           juce::String& formatDescription) const;

    bool readToMonoBufferSegment (const juce::File& inputFile,
                                  int startFrame,
                                  int frameCount,
                                  ConvertedAudio& output,
                                  juce::String& formatDescription) const;

    bool getFileDurationFrames (const juce::File& inputFile,
                                int& durationFrames,
                                juce::String& formatDescription) const;

    bool writeMonoWav16 (const juce::File& outputFile,
                         const ConvertedAudio& input) const;

    static void runSmokeTestAtStartup();

private:
    mutable juce::AudioFormatManager formatManager;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AudioFileIO)
};
