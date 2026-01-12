#pragma once

#include <JuceHeader.h>
#include <optional>
#include "AudioFileIO.h"
#include "SliceStateStore.h"

enum class MergeMode
{
    leftOnly,
    pachinko
};

struct SliceProcessingFlags
{
    bool transientDetectEnabled = false;
    bool layeringMode = false;
    bool randomSubdivisionMode = false;
    MergeMode mergeMode = MergeMode::leftOnly;
    bool pachinkoStutterEnabled = false;
};

std::optional<int> refinedStart (const juce::AudioBuffer<float>& input,
                                 int startFrame,
                                 int windowFrames,
                                 bool transientDetectEnabled);

juce::AudioBuffer<float> mergeSlices (const juce::AudioBuffer<float>& leftSlice,
                                      const juce::AudioBuffer<float>& rightSlice,
                                      MergeMode mergeMode);

bool regenerateSliceDormant (AudioFileIO& audioFileIO,
                             const SliceStateStore::SliceInfo& sliceInfo,
                             const juce::File& outputFile);
