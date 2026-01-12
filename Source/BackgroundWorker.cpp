#include "BackgroundWorker.h"

BackgroundWorker::BackgroundWorker() = default;

void BackgroundWorker::enqueue (std::function<void()> job)
{
    if (job)
        job();
}
