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

    juce::AudioBuffer<float> mixToMono (const juce::AudioBuffer<float>& input)
    {
        const int numSamples = input.getNumSamples();
        const int numChannels = input.getNumChannels();
        juce::AudioBuffer<float> monoBuffer (1, numSamples);
        monoBuffer.clear();

        for (int channel = 0; channel < numChannels; ++channel)
            monoBuffer.addFrom (0, 0, input, channel, 0, numSamples, 1.0f / static_cast<float> (numChannels));

        return monoBuffer;
    }

    juce::AudioBuffer<float> resampleToTarget (const juce::AudioBuffer<float>& input, double sourceRate)
    {
        if (juce::approximatelyEqual (sourceRate, kTargetSampleRate))
            return input;

        const int inputSamples = input.getNumSamples();
        const double ratio = kTargetSampleRate / sourceRate;
        const int outputSamples = static_cast<int> (std::ceil (inputSamples * ratio));

        juce::AudioBuffer<float> resampled (1, outputSamples);
        resampled.clear();

        juce::LagrangeInterpolator interpolator;
        interpolator.reset();
        interpolator.process (ratio, input.getReadPointer (0), resampled.getWritePointer (0), outputSamples);

        return resampled;
    }

    juce::AudioBuffer<float> trimOrPadToTarget (const juce::AudioBuffer<float>& input, int targetSamples)
    {
        if (targetSamples <= 0)
            return input;

        const int availableSamples = input.getNumSamples();
        if (availableSamples == targetSamples)
            return input;

        juce::AudioBuffer<float> output (1, targetSamples);
        output.clear();

        const int copySamples = juce::jmin (availableSamples, targetSamples);
        output.copyFrom (0, 0, input, 0, 0, copySamples);

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

    const bool needsDownmix = tempBuffer.getNumChannels() != kTargetChannels;
    const bool needsResample = ! juce::approximatelyEqual (reader->sampleRate, kTargetSampleRate);
    if (needsDownmix || needsResample)
        formatDescription = formatDescription + " -> converted to 44.1k/mono";

    juce::AudioBuffer<float> monoBuffer = needsDownmix
        ? mixToMono (tempBuffer)
        : tempBuffer;

    juce::AudioBuffer<float> resampled = resampleToTarget (monoBuffer, reader->sampleRate);

    output.buffer = std::move (resampled);
    output.sampleRate = kTargetSampleRate;

    return true;
}

bool AudioFileIO::readToMonoBufferSegment (const juce::File& inputFile,
                                           int startFrame,
                                           int frameCount,
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

    if (frameCount <= 0)
        return false;

    const double sourceRate = reader->sampleRate;
    const double ratio = sourceRate / kTargetSampleRate;
    const auto startSample = static_cast<juce::int64> (std::floor (static_cast<double> (startFrame) * ratio));
    const auto requestedSamples = static_cast<juce::int64> (std::ceil (static_cast<double> (frameCount) * ratio));
    const juce::int64 totalSamples = reader->lengthInSamples;

    if (startSample >= totalSamples)
        return false;

    const juce::int64 availableSamples = totalSamples - startSample;
    const int samplesToRead = static_cast<int> (juce::jmin (availableSamples, requestedSamples));
    if (samplesToRead <= 0)
        return false;

    juce::AudioBuffer<float> tempBuffer (static_cast<int> (reader->numChannels), samplesToRead);
    if (! reader->read (&tempBuffer, 0, samplesToRead, startSample, true, true))
        return false;

    const bool needsDownmix = tempBuffer.getNumChannels() != kTargetChannels;
    const bool needsResample = ! juce::approximatelyEqual (reader->sampleRate, kTargetSampleRate);
    if (needsDownmix || needsResample)
        formatDescription = formatDescription + " -> converted to 44.1k/mono";

    juce::AudioBuffer<float> monoBuffer = needsDownmix
        ? mixToMono (tempBuffer)
        : tempBuffer;

    juce::AudioBuffer<float> resampled = resampleToTarget (monoBuffer, reader->sampleRate);
    juce::AudioBuffer<float> trimmed = trimOrPadToTarget (resampled, frameCount);

    output.buffer = std::move (trimmed);
    output.sampleRate = kTargetSampleRate;

    return true;
}

bool AudioFileIO::getFileDurationFrames (const juce::File& inputFile,
                                         int& durationFrames,
                                         juce::String& formatDescription) const
{
    std::unique_ptr<juce::AudioFormatReader> reader (formatManager.createReaderFor (inputFile));
    if (reader == nullptr)
    {
        formatDescription = "unrecognized format";
        return false;
    }

    formatDescription = describeFormat (*reader);

    const double ratio = kTargetSampleRate / reader->sampleRate;
    durationFrames = static_cast<int> (std::ceil (static_cast<double> (reader->lengthInSamples) * ratio));
    return durationFrames > 0;
}

bool AudioFileIO::writeMonoWav16 (const juce::File& outputFile,
                                  const ConvertedAudio& input) const
{
    if (input.sampleRate != kTargetSampleRate)
        return false;

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
