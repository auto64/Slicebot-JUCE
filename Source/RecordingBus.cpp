#include "RecordingBus.h"

// =====================================================
// CONSTRUCTION
// =====================================================

RecordingBus::RecordingBus() {}

// =====================================================
// LIFECYCLE
// =====================================================

void RecordingBus::prepare (double sampleRate)
{
    this->sampleRate = sampleRate;

    // CRITICAL: ensure writers exist
    for (int i = 0; i < kNumRecorders; ++i)
        recorders[i].recorder.prepareDevice (sampleRate, i);
}

// =====================================================
// RECORD CONTROL
// =====================================================

void RecordingBus::armRecorder (int index)
{
    if (index < 0 || index >= kNumRecorders)
        return;

    if (hasLatchedRecorders())
    {
        armLatchedRecorders();
    }
    else
    {
        recorders[index].armed = true;
        recorders[index].recorder.arm();
    }
}

RecordingModule::StopResult
RecordingBus::confirmStopRecorder (int index)
{
    if (index < 0 || index >= kNumRecorders)
        return RecordingModule::StopResult::Kept;

    auto& slot = recorders[index];

    if (! slot.armed)
        return RecordingModule::StopResult::Kept;

    if (hasLatchedRecorders())
    {
        return stopLatchedRecorders();
    }

    slot.armed = false;
    return slot.recorder.confirmStop();
}

void RecordingBus::cancelStopRecorder (int)
{
}

void RecordingBus::clearRecorder (int index)
{
    if (index < 0 || index >= kNumRecorders)
        return;

    recorders[index].recorder.clear();
    recorders[index].armed = false;
}

// =====================================================
// STATE
// =====================================================

bool RecordingBus::hasLatchedRecorders() const
{
    for (const auto& slot : recorders)
    {
        if (slot.latchEnabled)
            return true;
    }

    return false;
}

void RecordingBus::armLatchedRecorders()
{
    for (auto& slot : recorders)
    {
        if (! slot.latchEnabled)
            continue;

        slot.armed = true;
        slot.recorder.arm();
    }
}

RecordingModule::StopResult RecordingBus::stopLatchedRecorders()
{
    RecordingModule::StopResult result = RecordingModule::StopResult::Kept;
    for (auto& slot : recorders)
    {
        if (! slot.latchEnabled)
            continue;

        slot.armed = false;
        const auto stopResult = slot.recorder.confirmStop();
        if (stopResult == RecordingModule::StopResult::DeletedTooShort)
            result = stopResult;
    }
    return result;
}

bool RecordingBus::isRecorderArmed (int index) const
{
    if (index < 0 || index >= kNumRecorders)
        return false;

    return recorders[index].armed;
}

void RecordingBus::setRecorderLatchEnabled (int index, bool enabled)
{
    if (index < 0 || index >= kNumRecorders)
        return;

    recorders[index].latchEnabled = enabled;
}

bool RecordingBus::isRecorderLatchEnabled (int index) const
{
    if (index < 0 || index >= kNumRecorders)
        return false;

    return recorders[index].latchEnabled;
}

// =====================================================
// ROUTING
// =====================================================

void RecordingBus::setRecorderInputBufferIndex (int index, int bufferIndex)
{
    if (index < 0 || index >= kNumRecorders)
        return;

    recorders[index].bufferIndex = bufferIndex;
}

void RecordingBus::setRecorderMonitoringEnabled (int index, bool enabled)
{
    if (index < 0 || index >= kNumRecorders)
        return;

    recorders[index].monitoringEnabled = enabled;
    recorders[index].recorder.setMonitoringEnabled (enabled);
}

// =====================================================
// TIMING
// =====================================================

double RecordingBus::getRecorderCurrentPassSeconds (int index) const
{
    if (index < 0 || index >= kNumRecorders)
        return 0.0;

    return recorders[index].recorder.getCurrentPassSeconds();
}

// =====================================================
// AUDIO
// =====================================================

void RecordingBus::processAudioBlock (const float* const* input,
                                      int numInputChannels,
                                      float* const* output,
                                      int numOutputChannels,
                                      int numSamples)
{
    for (int ch = 0; ch < numOutputChannels; ++ch)
        juce::FloatVectorOperations::clear (output[ch], numSamples);

    for (auto& slot : recorders)
    {
        const int buf = slot.bufferIndex;
        if (buf < 0 || buf >= numInputChannels)
            continue;

        const float* src = input[buf];

        if (slot.armed)
            slot.recorder.process (src, numSamples);

        if (slot.monitoringEnabled)
        {
            for (int out = 0; out < numOutputChannels; ++out)
                juce::FloatVectorOperations::add (
                    output[out], src, numSamples);
        }
    }
}
