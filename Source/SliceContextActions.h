#pragma once

#include <JuceHeader.h>

class AudioEngine;
class SliceStateStore;
struct SliceContextState;

enum class SliceContextAction
{
    lock,
    remove,
    regen,
    swap,
    duplicate,
    reverse
};

struct SliceContextActionResult
{
    juce::String statusText;
    bool shouldDismissOverlay = true;
};

struct SliceContextTargetResult
{
    bool didHandle = false;
    SliceContextActionResult actionResult;
};

SliceContextActionResult handleSliceContextAction (SliceContextAction action,
                                                   int index,
                                                   SliceStateStore& stateStore,
                                                   SliceContextState& contextState,
                                                   AudioEngine& audioEngine);

SliceContextTargetResult handleSliceContextTargetSelection (int targetIndex,
                                                            SliceStateStore& stateStore,
                                                            SliceContextState& contextState,
                                                            AudioEngine& audioEngine);
