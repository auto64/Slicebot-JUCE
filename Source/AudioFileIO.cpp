#include "AudioFileIO.h"

namespace
{
    constexpr double kTargetSampleRate = 44100.0;
    constexpr int kTargetBitsPerSample = 16;
    constexpr int kTargetChannels = 1;

    juce::String describeFormat (const juce::AudioFormatReader& reader)
    {
        return juce::String ("sr=") + juce::String (reader.sampleRate, 2)
            + ", bits=" + juce::String (reader.bitsPerSample)
            + ", ch=" + juce::String (reader.numChannels);
    }

    juce::AudioBuffer<float> downmixToMono (const juce::AudioBuffer<float>& input)
    {
        const int numSamples = input.getNumSamples();
        juce::AudioBuffer<float> mono (1, numSamples);
        mono.clear();

        const int numChannels = input.getNumChannels();
        for (int channel = 0; channel < numChannels; ++channel)
        {
            mono.addFrom (0, 0, input, channel, 0, numSamples, 1.0f / static_cast<float> (numChannels));
        }

        return mono;
    }

    juce::AudioBuffer<float> resampleToTarget (const juce::AudioBuffer<float>& input,
                                               double sourceRate)
    {
        if (juce::approximatelyEqual (sourceRate, kTargetSampleRate))
            return input;

        const double ratio = sourceRate / kTargetSampleRate;
        const int inputSamples = input.getNumSamples();
        const int outputSamples = static_cast<int> (std::ceil (inputSamples / ratio));

        juce::AudioBuffer<float> output (1, outputSamples);
        output.clear();

        juce::LagrangeInterpolator resampler;
        resampler.reset();

        const float* inputData = input.getReadPointer (0);
        float* outputData = output.getWritePointer (0);
        resampler.process (ratio, inputData, outputData, outputSamples);

        return output;
    }
}

AudioFileIO::AudioFileIO()
{
    formatManager.registerBasicFormats();
}

bool AudioFileIO::readToMonoBuffer (const juce::File& inputFile,
                                    ConvertedAudio& output,
                                    juce::String& formatDescription) const
{
    std::unique_ptr<juce::AudioFormatReader> reader (formatManager.createReaderFor (inputFile));
    if (reader == nullptr)
    {
        formatDescription = "unrecognized format";
        return false;
    }

    formatDescription = describeFormat (*reader);

    juce::AudioBuffer<float> tempBuffer (static_cast<int> (reader->numChannels),
                                         static_cast<int> (reader->lengthInSamples));

    if (! reader->read (&tempBuffer, 0, static_cast<int> (reader->lengthInSamples), 0, true, true))
        return false;

    const juce::AudioBuffer<float> mono = downmixToMono (tempBuffer);
    output.buffer = resampleToTarget (mono, reader->sampleRate);
    output.sampleRate = kTargetSampleRate;

    return true;
}

bool AudioFileIO::writeMonoWav16 (const juce::File& outputFile,
                                  const ConvertedAudio& input) const
{
    if (input.buffer.getNumChannels() != kTargetChannels)
        return false;

    juce::WavAudioFormat wavFormat;
    std::unique_ptr<juce::FileOutputStream> outputStream (outputFile.createOutputStream());
    if (outputStream == nullptr)
        return false;

    std::unique_ptr<juce::AudioFormatWriter> writer (wavFormat.createWriterFor (outputStream.get(),
                                                                               kTargetSampleRate,
                                                                               kTargetChannels,
                                                                               kTargetBitsPerSample,
                                                                               {},
                                                                               0));
    if (writer == nullptr)
        return false;

    outputStream.release();

    const int numSamples = input.buffer.getNumSamples();
    return writer->writeFromAudioSampleBuffer (input.buffer, 0, numSamples);
}

void AudioFileIO::runSmokeTestAtStartup()
{
    const juce::String testPath = "/path/to/your/audio.wav"; // TODO: set a real path
    const juce::File inputFile (testPath);

    AudioFileIO audioFileIO;
    ConvertedAudio converted;
    juce::String inputFormat;

    if (! inputFile.existsAsFile())
    {
        juce::Logger::writeToLog ("AudioFileIO smoke test: input file missing at " + inputFile.getFullPathName());
        return;
    }

    const bool readOk = audioFileIO.readToMonoBuffer (inputFile, converted, inputFormat);
    if (! readOk)
    {
        juce::Logger::writeToLog ("AudioFileIO smoke test: read failed. format=" + inputFormat);
        return;
    }

    const juce::File outputFile = inputFile.getSiblingFile (inputFile.getFileNameWithoutExtension() + "_converted.wav");
    const bool writeOk = audioFileIO.writeMonoWav16 (outputFile, converted);

    juce::Logger::writeToLog ("AudioFileIO smoke test: input format=" + inputFormat);
    juce::Logger::writeToLog ("AudioFileIO smoke test: output format=sr=44100.0, bits=16, ch=1");
    juce::Logger::writeToLog ("AudioFileIO smoke test: output path=" + outputFile.getFullPathName());
    juce::Logger::writeToLog (juce::String ("AudioFileIO smoke test: success=") + (writeOk ? "true" : "false"));
}
