#ifndef RATS_SERVICE_TRACKER_SERVICE_H
#define RATS_SERVICE_TRACKER_SERVICE_H

#include "domain/torrent.h"
#include "net/scrape_queue.h"

#include <QJsonObject>
#include <QObject>
#include <QString>

namespace rats::net {
class SwarmScraper;
class TrackerSiteScraper;
} // namespace rats::net

namespace rats::data {
class TorrentRepository;
}

namespace rats::service {

// Orchestrates the two tracker scrapers and persists their results. It listens
// for newly indexed torrents and, when enabled, kicks off a seeders/leechers
// scrape and a website-metadata (poster/description) scrape; results flow back
// through the scrapers' signals and are written to the repository. The scrapers
// themselves never touch the database — this is the only glue that does.
//
// Scrapes of freshly indexed torrents are background work; the explicit check*
// calls default to interactive priority, which jumps every background queue.
class TrackerService : public QObject {
    Q_OBJECT

public:
    TrackerService(net::SwarmScraper* swarmScraper, net::TrackerSiteScraper* siteScraper,
        data::TorrentRepository* repository, QObject* parent = nullptr);

    void setCountScrapingEnabled(bool enabled);
    void setInfoScrapingEnabled(bool enabled);

    // Stop forwarding scrapes and tear down the underlying scrapers. Called on
    // shutdown so no fresh tracker work is issued while the app is closing.
    void stop();

    // Explicit requests (the details panel, the API "tracker.check" method).
    // Each returns whether a scrape is now running or queued for the hash —
    // false when scraping is disabled or the hash was checked recently. For
    // checkInfo(), true means infoChecked() follows.
    bool checkCounts(const QString& hash, net::ScrapePriority priority = net::ScrapePriority::Interactive);
    bool checkInfo(const QString& hash, net::ScrapePriority priority = net::ScrapePriority::Interactive);

signals:
    // A website-info scrape for `hash` ended, found or not. Anything it found is
    // already in the repository (and announced by its torrentUpdated) by then.
    void infoChecked(const QString& hash);

public slots:
    // Wire to IndexingService::torrentIndexed.
    void onTorrentIndexed(const domain::Torrent& torrent);

private slots:
    void onCountsScraped(const QString& hash, int seeders, int leechers, int completed);
    void onInfoScraped(const QString& hash, const QJsonObject& info);

private:
    net::SwarmScraper* swarmScraper_;
    net::TrackerSiteScraper* siteScraper_;
    data::TorrentRepository* repository_;
    bool countEnabled_ = false;
    bool infoEnabled_ = false;
};

} // namespace rats::service

#endif // RATS_SERVICE_TRACKER_SERVICE_H
