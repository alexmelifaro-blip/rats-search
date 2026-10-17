#ifndef RATS_NET_TRACKER_SITE_SCRAPER_H
#define RATS_NET_TRACKER_SITE_SCRAPER_H

#include "net/scrape_queue.h"

#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QVector>

class QNetworkAccessManager;

namespace rats::net {

// Result of scraping a single tracker website for one info-hash. Internal DTO:
// the parse* helpers fill it in and mergeResults() folds the per-tracker
// DTOs into the single JSON object carried by scraped().
struct TrackerSiteInfo {
    QString trackerName; // "rutracker" | "nyaa"
    QString name; // torrent title as shown on the tracker
    QString poster; // poster image URL
    QString description; // plain-text description
    QString contentCategory; // category / breadcrumb path
    int threadId = 0; // topic / view id on the tracker
    bool success = false;
};

// Scrapes tracker websites (RuTracker, Nyaa) for a torrent's poster image,
// description and category, using hand-rolled QRegularExpression HTML parsing
// (faithfully ported from the legacy Electron "strategies"). Not to be confused
// with SwarmScraper, which announces to trackers for seeder/leecher counts.
//
// Pure network-side helper: it NEVER touches the database. All strategies for a
// hash run in parallel; once they finish the merged metadata is delivered via
// the scraped() signal as a JSON object whose keys are those of the torrent's
// `info` field (poster, description, contentCategory, trackers[],
// rutrackerThreadId, nyaaThreadId, trackerName). A higher-level
// service listens for scraped() and persists those fields onto the torrent.
//
// Which hashes run, wait or are skipped is decided by a ScrapeQueue. Every
// in-flight QNetworkReply holds a fully decompressed tracker page in memory, so
// background scrapes are capped hard; an interactive request still starts at
// once. Lives on one thread (QNetworkAccessManager requires it): call it there.
class TrackerSiteScraper : public QObject {
    Q_OBJECT

public:
    explicit TrackerSiteScraper(QObject* parent = nullptr);
    ~TrackerSiteScraper() override;

    // Scrape every supported tracker for `infoHash` (40-char hex). Non-blocking.
    // Returns whether a scrape is now running or queued for the hash — and so
    // whether finished() will follow (a queued *background* request can still be
    // dropped from a full backlog). False when the hash was scraped recently, is
    // invalid, or the scraper is stopping. Whether scraping happens at all is
    // decided by the caller (TrackerService).
    bool scrape(const QString& infoHash, ScrapePriority priority = ScrapePriority::Background);

    // Stop scraping: reject further scrape() calls and abort every in-flight
    // network request so shutdown is not held up waiting on tracker websites.
    // Idempotent.
    void stop();

    static constexpr int kTimeoutMs = 20000; // 20 s per request
    static constexpr int kCooldownSecs = 3600; // 1 h after an answered scrape
    static constexpr int kRetrySecs = 60; // after a scrape a tracker failed to answer
    static constexpr int kMaxConcurrent = 3; // concurrent background scrapes
    static constexpr int kInteractiveSlots = 2; // extra slots for interactive ones
    static constexpr int kMaxQueued = 200; // background backlog, oldest dropped

signals:
    // Emitted once all strategies for `infoHash` have finished AND at least one
    // tracker yielded data. `info` carries only the freshly scraped keys; the
    // listener is responsible for merging them into the stored torrent.
    void scraped(const QString& infoHash, const QJsonObject& info);

    // Emitted after every scrape, found or not (after scraped() when found), so
    // whoever is waiting on the hash can stop waiting.
    void finished(const QString& infoHash);

private:
    void startScrape(const QString& infoHash);

    // Strategy launchers — one network round-trip family each.
    void scrapeRutracker(const QString& hash);
    void scrapeNyaa(const QString& hash);
    void scrapeNyaaViewPage(const QString& hash, const QString& viewUrl);

    // HTML parsers (faithful ports of the legacy regex parsing).
    TrackerSiteInfo parseRutrackerHtml(const QByteArray& rawData);
    TrackerSiteInfo parseNyaaSearchHtml(const QByteArray& rawData);
    TrackerSiteInfo parseNyaaViewHtml(const QByteArray& rawData);

    // Called by each strategy when it finishes; once all have reported, merges
    // the results, emits and hands the slot to the next queued hash.
    // onStrategyFailed() is the same for a strategy that got no answer from its
    // tracker, which shortens the hash's quiet period to a retry delay.
    void onStrategyComplete(const QString& hash, const TrackerSiteInfo& info);
    void onStrategyFailed(const QString& hash);

    // Fold per-tracker results into the JSON object carried by scraped(). Empty
    // when no tracker found the torrent.
    static QJsonObject mergeResults(const QVector<TrackerSiteInfo>& results);

    // Strip HTML tags / decode entities into plain text.
    static QString stripHtml(const QString& html);

    // Decode Windows-1251 bytes to a QString. Kept verbatim: Qt6 without ICU has
    // no codec for this legacy encoding, which RuTracker still serves.
    static QString decodeWindows1251(const QByteArray& data);

    // Clamp a description to kMaxDescriptionLength, appending an ellipsis when
    // truncated. Factored out of the three legacy copy-paste sites.
    static QString truncateDescription(const QString& text);

    // Member-function pointer type for a parallel strategy. The strategy list
    // (kStrategies) is the single source of truth for how many results a scrape
    // waits on — the count is derived from its size, never hardcoded.
    using Strategy = void (TrackerSiteScraper::*)(const QString&);
    static const QVector<Strategy> kStrategies;

    // Named constants (no magic numbers in the logic below).
    static constexpr int kInfoHashHexLength = 40; // 20-byte hash as hex
    static constexpr int kEncodingSniffLength = 2000; // bytes scanned for charset
    static constexpr int kMaxDescriptionLength = 5000; // description clamp

    QNetworkAccessManager* networkManager_;

    // Set by stop(): rejects new scrapes while the app is shutting down.
    bool stopping_ = false;

    ScrapeQueue queue_;

    // In-flight scrapes: accumulate per-strategy results until all report.
    struct PendingScrape {
        int pendingCount = 0;
        bool failed = false; // some tracker did not answer
        QVector<TrackerSiteInfo> results;
    };
    QHash<QString, PendingScrape> pendingScrapes_;
};

} // namespace rats::net

#endif // RATS_NET_TRACKER_SITE_SCRAPER_H
