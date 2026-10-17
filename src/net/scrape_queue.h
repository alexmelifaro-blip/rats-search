#ifndef RATS_NET_SCRAPE_QUEUE_H
#define RATS_NET_SCRAPE_QUEUE_H

#include <QDateTime>
#include <QHash>
#include <QList>
#include <QSet>
#include <QString>
#include <QStringList>

namespace rats::net {

// Who is waiting for a scrape. Background work (every freshly indexed torrent)
// is best-effort; an interactive request (the user opened the torrent, or asked
// through the API) has somebody looking at a loading indicator.
enum class ScrapePriority { Background, Interactive };

// Scheduling policy shared by SwarmScraper and TrackerSiteScraper: which
// requests run now, which wait, and which are not worth doing at all. Pure
// bookkeeping over info-hashes — it launches nothing; the owner launches what
// submit() and finish() tell it to. Not thread-safe: use it from the owner's
// thread only.
//
//   - Slots: background scrapes share `maxConcurrent` slots. Interactive ones may
//     use those plus `interactiveSlots` more that background work never takes,
//     so a click starts at once even while the crawler keeps every regular slot
//     busy.
//   - Order: queued interactive requests always go before queued background
//     ones, and an interactive request for a hash already waiting in the
//     background queue promotes it instead of queueing it twice.
//   - Backlog: the background queue holds at most `maxQueued` hashes and drops
//     the oldest past that. The crawler indexes faster than tracker websites can
//     be asked, so an unbounded FIFO only ever grows — and the newest torrents
//     are the ones worth enriching.
//   - Quiet period: a hash is not scraped again for `cooldownSecs` after a scrape
//     that got an answer, or `retrySecs` after one that did not. It starts when
//     a scrape *finishes* — a request still sitting in the queue is not a result,
//     and must not swallow the user's own request for the same hash.
class ScrapeQueue {
public:
    struct Limits {
        int maxConcurrent = 3;
        int interactiveSlots = 2;
        int maxQueued = 200;
        int cooldownSecs = 3600;
        int retrySecs = 60;
    };

    enum class Outcome {
        Started, // a slot was claimed: the caller launches the scrape now
        Queued, // waiting for a slot; finish() hands it out later
        Running, // already in flight; that scrape answers this request too
        CoolingDown, // scraped recently; nothing will happen
    };

    explicit ScrapeQueue(const Limits& limits);

    Outcome submit(
        const QString& hash, ScrapePriority priority, const QDateTime& now = QDateTime::currentDateTimeUtc());

    // Release `hash`'s slot and start its quiet period (`succeeded` picks which
    // one). Returns the queued hashes that now hold a slot — the caller launches
    // every one of them.
    QStringList finish(const QString& hash, bool succeeded, const QDateTime& now = QDateTime::currentDateTimeUtc());

    // Forget everything still queued (shutdown). Running scrapes keep their slots
    // until they finish().
    void clear();

    int running() const { return static_cast<int>(running_.size()); }
    int queued() const { return static_cast<int>(interactive_.size() + background_.size()); }

private:
    bool hasFreeSlot(ScrapePriority priority) const;
    bool isQuiet(const QString& hash, const QDateTime& now) const;
    void pruneQuietPeriods(const QDateTime& now);

    static constexpr int kPruneIntervalSecs = 60;

    Limits limits_;
    QSet<QString> running_;
    QList<QString> interactive_;
    QList<QString> background_;
    QHash<QString, QDateTime> quietUntil_; // hash -> earliest next scrape
    QDateTime nextPrune_;
};

} // namespace rats::net

#endif // RATS_NET_SCRAPE_QUEUE_H
