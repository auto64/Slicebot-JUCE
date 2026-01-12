#include "RecordingModule.h"

RecordingModule::RecordingModule() {}

juce::File RecordingModule::getRecorderFile (int recorderIndex)
{
    auto dir = juce::File::getSpecialLocation (
                   juce::File::userDocumentsDirectory)
                   .getChildFile ("SliceBot");

    dir.createDirectory();

    return dir.getChildFile (
        "Recorder" + juce::String (recorderIndex + 1) + ".wav");
}

void RecordingModule::prepareDevice (double sr,
                                     int recorderIndex)
{
    sampleRate = sr;

    const int maxSamples =
        static_cast<int> (kMaxSeconds * sampleRate);

    if (! writer)
    {
        auto file = getRecorderFile (recorderIndex);

        writer = std::make_unique<RecordingWriter> (
            maxSamples,
            1,
            sampleRate,
            file);
    }
    else
    {
        // 🔒 PRESERVE BUFFER — only update device-dependent info
        writer->setSampleRate (sampleRate);
    }
}

void RecordingModule::arm()
{
    if (! writer || writer->isFull())
        return;

    armed = true;
    writer->beginPass();
}

RecordingModule::StopResult RecordingModule::confirmStop()
{
    if (! armed)
        return StopResult::Kept;

    armed = false;

    const int passSamples  = writer->getPassSamples();
    const double passSecs  = passSamples / sampleRate;

    if (writer->isFull())
    {
        writer->commitPass();
        writer->writeToDisk();
        return StopResult::Kept;
    }

    if (passSecs < kMinSeconds)
    {
        writer->rollbackPass();
        return StopResult::DeletedTooShort;
    }

    writer->commitPass();
    writer->writeToDisk();
    return StopResult::Kept;
}

void RecordingModule::cancelStopRequest() {}

bool RecordingModule::isArmed() const
{
    return armed;
}

void RecordingModule::setMonitoringEnabled (bool enabled)
{
    monitoringEnabled = enabled;
}

bool RecordingModule::isMonitoringEnabled() const
{
    return monitoringEnabled;
}

void RecordingModule::process (const float* input,
                               int numSamples)
{
    if (! armed || ! writer)
        return;

    const float* inputs[1] = { input };
    writer->write (inputs, 1, numSamples);
}

double RecordingModule::getCurrentPassSeconds() const
{
    if (! writer || sampleRate <= 0.0)
        return 0.0;

    return static_cast<double> (writer->getPassSamples()) / sampleRate;
}

void RecordingModule::clear()
{
    if (writer)
        writer->clear();

    armed = false;
}
