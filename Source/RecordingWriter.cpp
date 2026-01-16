#include "RecordingWriter.h"
#include <cstring>

RecordingWriter::RecordingWriter (int maxSamplesIn,
                                  int numChannels,
                                  double initialSampleRate,
                                  const juce::File& targetFile)
    : buffer (numChannels, maxSamplesIn),
      file (targetFile),
      sampleRate (initialSampleRate),
      maxSamples (maxSamplesIn)
{
    buffer.clear();
}

void RecordingWriter::setSampleRate (double newSampleRate)
{
    sampleRate = newSampleRate;
}

void RecordingWriter::clear()
{
    buffer.clear();
    writeHead = 0;
    passStart = 0;
}

void RecordingWriter::beginPass()
{
    passStart = writeHead;
}

void RecordingWriter::rollbackPass()
{
    writeHead = passStart;
}

void RecordingWriter::commitPass()
{
    passStart = writeHead;
}

bool RecordingWriter::isFull() const
{
    return writeHead >= maxSamples;
}

int RecordingWriter::getTotalSamples() const
{
    return writeHead;
}

int RecordingWriter::getPassSamples() const
{
    return writeHead - passStart;
}

int RecordingWriter::getMaxSamples() const
{
    return maxSamples;
}

void RecordingWriter::write (const float* const* input,
                             int numChannels,
                             int numSamples)
{
    if (isFull())
        return;

    const int remaining = maxSamples - writeHead;
    const int toWrite   = juce::jmin (remaining, numSamples);

    for (int ch = 0; ch < numChannels; ++ch)
        buffer.copyFrom (ch,
                         writeHead,
                         input[ch],
                         toWrite);

    writeHead += toWrite;
}

bool RecordingWriter::writeToDisk()
{
    if (writeHead <= 0)
        return false;

    file.deleteFile();
    file.create();

    juce::WavAudioFormat format;
    auto stream = std::unique_ptr<juce::FileOutputStream> (
        file.createOutputStream());

    if (! stream)
        return false;

    auto writer = std::unique_ptr<juce::AudioFormatWriter> (
        format.createWriterFor (stream.release(),
                                sampleRate,
                                buffer.getNumChannels(),
                                24,
                                {},
                                0));

    if (! writer)
        return false;

    return writer->writeFromAudioSampleBuffer (buffer,
                                               0,
                                               writeHead);
}

bool RecordingWriter::loadFromDisk()
{
    if (! file.existsAsFile())
        return false;

    juce::AudioFormatManager formatManager;
    formatManager.registerBasicFormats();

    auto reader = std::unique_ptr<juce::AudioFormatReader> (
        formatManager.createReaderFor (file));

    if (! reader)
        return false;

    const juce::int64 totalSamples = juce::jmin<juce::int64> (reader->lengthInSamples,
                                                              static_cast<juce::int64> (maxSamples));

    buffer.clear();
    reader->read (&buffer, 0, static_cast<int> (totalSamples), 0, true, true);

    writeHead = static_cast<int> (totalSamples);
    passStart = writeHead;
    return true;
}

int RecordingWriter::readSamples (float* dest,
                                  int startSample,
                                  int numSamples) const
{
    if (startSample >= writeHead || numSamples <= 0)
        return 0;

    const int available = writeHead - startSample;
    const int toRead = juce::jmin (available, numSamples);

    if (toRead <= 0)
        return 0;

    auto* src = buffer.getReadPointer (0, startSample);
    std::memcpy (dest, src, static_cast<size_t> (toRead) * sizeof (float));
    return toRead;
}
