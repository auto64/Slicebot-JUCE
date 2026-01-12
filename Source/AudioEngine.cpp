#include "AudioEngine.h"
#include "AppProperties.h"

// =====================================================
// CONSTRUCTION / DESTRUCTION
// =====================================================

AudioEngine::AudioEngine()
{
    auto& props = AppProperties::get().properties();

    if (auto* settings = props.getUserSettings())
    {
        if (auto xml = settings->getXmlValue ("audioDeviceState"))
            deviceManager.initialise (0, 0, xml.get(), true);
        else
            deviceManager.initialiseWithDefaultDevices (2, 2);
    }
    else
    {
        deviceManager.initialiseWithDefaultDevices (2, 2);
    }

    deviceManager.addAudioCallback (this);
}

AudioEngine::~AudioEngine()
{
    auto& props = AppProperties::get().properties();

    if (auto* settings = props.getUserSettings())
    {
        if (auto xml = deviceManager.createStateXml())
            settings->setValue ("audioDeviceState", xml.get());
    }

    deviceManager.removeAudioCallback (this);
}

// =====================================================
// LIFECYCLE
// =====================================================

void AudioEngine::start() {}
void AudioEngine::stop()  {}

void AudioEngine::restoreState() {}
void AudioEngine::saveState()    {}

// =====================================================
// DEVICE ACCESS
// =====================================================

juce::AudioDeviceManager& AudioEngine::getDeviceManager()
{
    return deviceManager;
}

// =====================================================
// INPUT CHANNEL INFO (UI)
// =====================================================

juce::StringArray AudioEngine::getInputChannelNames() const
{
    if (auto* device = deviceManager.getCurrentAudioDevice())
        return device->getInputChannelNames();

    return {};
}

juce::Array<AudioEngine::ActiveInputChannel>
AudioEngine::getActiveInputChannels() const
{
    juce::Array<ActiveInputChannel> result;

    auto* device = deviceManager.getCurrentAudioDevice();
    if (! device)
        return result;

    const auto names = device->getInputChannelNames();
    const auto mask  = device->getActiveInputChannels();

    for (int i = 0; i < names.size(); ++i)
    {
        if (mask[i])
            result.add ({ names[i], i });
    }

    return result;
}

// =====================================================
// RECORDER CONTROL
// =====================================================

void AudioEngine::armRecorder (int index)
{
    recordingBus.armRecorder (index);
}

RecordingModule::StopResult
AudioEngine::confirmStopRecorder (int index)
{
    return recordingBus.confirmStopRecorder (index);
}

void AudioEngine::cancelStopRecorder (int index)
{
    recordingBus.cancelStopRecorder (index);
}

void AudioEngine::clearRecorder (int index)
{
    recordingBus.clearRecorder (index);
}

void AudioEngine::setRecorderMonitoringEnabled (int index, bool enabled)
{
    recordingBus.setRecorderMonitoringEnabled (index, enabled);
}

void AudioEngine::setRecorderInputChannel (int index, int physicalChannel)
{
    if (index < 0 || index >= RecordingBus::kNumRecorders)
        return;

    recorderPhysicalChannels[index] = physicalChannel;
}

void AudioEngine::setRecorderLatchEnabled (int index, bool enabled)
{
    recordingBus.setRecorderLatchEnabled (index, enabled);
}

// =====================================================
// TIMING
// =====================================================

double AudioEngine::getRecorderCurrentPassSeconds (int index) const
{
    return recordingBus.getRecorderCurrentPassSeconds (index);
}

// =====================================================
// METERS
// =====================================================

float AudioEngine::getInputRMS() const
{
    return inputRMS.load();
}

float AudioEngine::getInputPeak() const
{
    return inputPeak.load();
}

// =====================================================
// JUCE CALLBACKS
// =====================================================

void AudioEngine::audioDeviceAboutToStart (juce::AudioIODevice* device)
{
    recordingBus.prepare (device->getCurrentSampleRate());

    // DEFAULT INPUT ASSIGNMENT (RESTORED)
    const auto activeMask = device->getActiveInputChannels();

    int firstActive = -1;
    const int highest = activeMask.getHighestBit() + 1;

    for (int i = 0; i < highest; ++i)
    {
        if (activeMask[i])
        {
            firstActive = i;
            break;
        }
    }

    if (firstActive >= 0)
    {
        for (int r = 0; r < RecordingBus::kNumRecorders; ++r)
        {
            if (recorderPhysicalChannels[r] < 0)
                recorderPhysicalChannels[r] = firstActive;
        }
    }
}

void AudioEngine::audioDeviceStopped() {}

// =====================================================
// AUDIO CALLBACK
// =====================================================

void AudioEngine::audioDeviceIOCallbackWithContext (
    const float* const* input,
    int numInputChannels,
    float* const* output,
    int numOutputChannels,
    int numSamples,
    const juce::AudioIODeviceCallbackContext&)
{
    auto* device = deviceManager.getCurrentAudioDevice();
    if (! device)
        return;

    // -------------------------------------------------
    // PHYSICAL → BUFFER MAP
    // -------------------------------------------------

    const auto activeMask = device->getActiveInputChannels();

    int physicalToBuffer[64];
    int bufferIndex = 0;

    const int highest =
        juce::jmin (64, activeMask.getHighestBit() + 1);

    for (int phys = 0; phys < highest; ++phys)
    {
        if (activeMask[phys])
            physicalToBuffer[phys] = bufferIndex++;
        else
            physicalToBuffer[phys] = -1;
    }

    for (int r = 0; r < RecordingBus::kNumRecorders; ++r)
    {
        const int phys = recorderPhysicalChannels[r];
        int buf = -1;

        if (phys >= 0 && phys < highest)
            buf = physicalToBuffer[phys];

        recordingBus.setRecorderInputBufferIndex (r, buf);
    }

    // -------------------------------------------------
    // PROCESS
    // -------------------------------------------------

    recordingBus.processAudioBlock (
        input,
        numInputChannels,
        output,
        numOutputChannels,
        numSamples);

    // -------------------------------------------------
    // METERS
    // -------------------------------------------------

    float rms  = 0.0f;
    float peak = 0.0f;

    for (int ch = 0; ch < numInputChannels; ++ch)
    {
        const float* src = input[ch];
        for (int i = 0; i < numSamples; ++i)
        {
            const float v = std::abs (src[i]);
            peak = juce::jmax (peak, v);
            rms += v * v;
        }
    }

    if (numInputChannels > 0 && numSamples > 0)
        rms = std::sqrt (rms / (numSamples * numInputChannels));

    inputRMS.store (rms);
    inputPeak.store (peak);
}
