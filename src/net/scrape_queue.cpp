#include "net/scrape_queue.h"

namespace rats::net {

ScrapeQueue::ScrapeQueue(const Limits& limits) : limits_(limits) { }

ScrapeQueue::Outcome ScrapeQueue::submit(const QString& hash, ScrapePriority priority, const QDateTime& now)
{
    if (running_.contains(hash))
        return Outcome::Running;

    const bool interactive = priority == ScrapePriority::Interactive;
    if (interactive_.contains(hash) || (!interactive && background_.contains(hash)))
        return Outcome::Queued;

    if (isQuiet(hash, now))
        return Outcome::CoolingDown;

    // Promotion: the background entry, if any, is superseded by this request.
    if (interactive)
        background_.removeOne(hash);

    if (hasFreeSlot(priority)) {
        running_.insert(hash);
        return Outcome::Started;
    }

    if (interactive) {
        interactive_.append(hash);
    } else {
        background_.append(hash);
        while (background_.size() > limits_.maxQueued)
            background_.removeFirst();
    }
    return Outcome::Queued;
}

QStringList ScrapeQueue::finish(const QString& hash, bool succeeded, const QDateTime& now)
{
    if (running_.remove(hash))
        quietUntil_.insert(hash, now.addSecs(succeeded ? limits_.cooldownSecs : limits_.retrySecs));
    pruneQuietPeriods(now);

    QStringList started;
    while (!interactive_.isEmpty() && hasFreeSlot(ScrapePriority::Interactive)) {
        started.append(interactive_.takeFirst());
        running_.insert(started.last());
    }
    while (!background_.isEmpty() && hasFreeSlot(ScrapePriority::Background)) {
        started.append(background_.takeFirst());
        running_.insert(started.last());
    }
    return started;
}

void ScrapeQueue::clear()
{
    interactive_.clear();
    background_.clear();
}

bool ScrapeQueue::hasFreeSlot(ScrapePriority priority) const
{
    int capacity = limits_.maxConcurrent;
    if (priority == ScrapePriority::Interactive)
        capacity += limits_.interactiveSlots;
    return running_.size() < capacity;
}

bool ScrapeQueue::isQuiet(const QString& hash, const QDateTime& now) const
{
    const auto it = quietUntil_.constFind(hash);
    return it != quietUntil_.constEnd() && now < it.value();
}

void ScrapeQueue::pruneQuietPeriods(const QDateTime& now)
{
    if (nextPrune_.isValid() && now < nextPrune_)
        return;
    nextPrune_ = now.addSecs(kPruneIntervalSecs);

    for (auto it = quietUntil_.begin(); it != quietUntil_.end();) {
        if (now >= it.value())
            it = quietUntil_.erase(it);
        else
            ++it;
    }
}

} // namespace rats::net
