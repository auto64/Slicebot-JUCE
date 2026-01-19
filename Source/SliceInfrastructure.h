#pragma once

#include <JuceHeader.h>
#include <optional>
#include "AudioFileIO.h"
#include "SliceStateStore.h"

struct SliceProcessingFlags
{
    bool transientDetectEnabled = false;
    bool layeringMode = false;
    bool randomSubdivisionMode = false;
    SliceStateStore::MergeMode mergeMode = SliceStateStore::MergeMode::none;
    bool pachinkoStutterEnabled = false;
    int sampleCount = 0;
};

// Pairing invariant (dormant): leftIndex = i, rightIndex = i + sampleCount.

std::optional<int> refinedStart (const juce::AudioBuffer<float>& input,
                                 juce::Random& random,
                                 int maxCandidateStart,
                                 int windowFrames,
                                 bool transientDetectEnabled);

std::optional<int> refinedStartFromWindow (const juce::AudioBuffer<float>& windowBuffer,
                                           int windowStartFrame,
                                           bool transientDetectEnabled);

juce::AudioBuffer<float> mergeSlices (const juce::AudioBuffer<float>& leftSlice,
                                      const juce::AudioBuffer<float>& rightSlice,
                                      SliceStateStore::MergeMode mergeMode);

bool regenerateSliceDormant (AudioFileIO& audioFileIO,
                             const SliceStateStore::SliceInfo& sliceInfo,
                             const juce::File& outputFile);
