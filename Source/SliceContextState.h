#pragma once

struct SliceContextState
{
    enum class PendingOperation
    {
        none,
        swap,
        duplicate
    };

    PendingOperation pendingOperation = PendingOperation::none;
    int pendingSourceSliceIndex = -1;
};
