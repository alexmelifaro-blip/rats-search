#ifndef RATS_NET_SWARM_SCRAPER_H
#define RATS_NET_SWARM_SCRAPER_H

#include "net/scrape_queue.h"

#include <QObject>
#include <QString>
#include <QThreadPool>

#include <atomic>

namespace rats::net {

// Announces to UDP/HTTP BitTorrent trackers to read a torrent's swarm counts
// (seeders / leechers / completed). Not to be confused with TrackerSiteScraper,
// which scrapes tracker *websites* for poster/description metadata.
//
// Pure transport-side helper: it only talks to trackers and emits results — it
// never touches the database. A higher-level service listens for scraped() and
// persists the values.
//
// The blocking announces run on a private thread pool; which hashes run, wait or
// are skipped is decided by a ScrapeQueue (concurrency cap, interactive priority,
// bounded backlog, per-hash cooldown). Lives on one thread: call it from there.
class SwarmScraper : public QObject {
    Q_OBJECT

public:
    explicit SwarmScraper(QObject* parent = nullptr);
    ~SwarmScraper() override;

    // Request a scrape of the built-in tracker list for a torrent. `infoHash`
    // must be a 40-char hex string. Non-blocking: on success scraped() is emitted
    // later on this object's thread. Returns whether a scrape is now running or
    // queued for the hash — false when it was answered recently (cooldown), is
    // invalid, or the scraper is stopping.
    bool requestScrape(const QString& infoHash, ScrapePriority priority = ScrapePriority::Background);

    // Stop scraping: reject any further requests, drop the queue, and wait for
    // in-flight announces to drain so no thread-pool task outlives this object
    // (its completion callback dereferences `this`). Idempotent; safe to call
    // from the destructor. Must be called on this object's thread.
    void stop();

    static constexpr int kTimeoutMs = 15000; // 15 s per announce
    static constexpr int kCooldownSecs = 300; // 5 min after an answered scrape
    static constexpr int kRetrySecs = 60; // after a scrape no tracker answered
    static constexpr int kMaxConcurrent = 5; // concurrent background scrapes
    static constexpr int kInteractiveSlots = 2; // extra slots for interactive ones
    static constexpr int kMaxQueued = 500; // background backlog, oldest dropped

signals:
    // Emitted once, on success, with the best swarm counts seen across the
    // torrent's trackers. Not emitted when every tracker fails.
    void scraped(const QString& infoHash, int seeders, int leechers, int completed);

private:
    // Fan out to every default tracker, keep the best successful result, then
    // report it back on this object's thread via onScrapeFinished().
    void startScrape(const QString& infoHash);

    // Free the hash's slot, emit its result and launch whatever was waiting.
    void onScrapeFinished(const QString& infoHash, bool success, int seeders, int leechers, int completed);

    static constexpr int kInfoHashHexLength = 40; // 20-byte hash as hex
    static constexpr int kMaxPoolThreads = 16; // announce worker threads

    ScrapeQueue queue_;

    // Dedicated pool for the blocking tracker announces so shutdown can drain it
    // deterministically (the global pool is shared and not ours to wait on).
    QThreadPool threadPool_;

    // Set true by stop(): gates new requests and makes in-flight announce loops
    // bail early instead of hammering every remaining tracker while shutting down.
    std::atomic<bool> stopping_ { false };
};

} // namespace rats::net

#endif // RATS_NET_SWARM_SCRAPER_H
