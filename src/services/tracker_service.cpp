#include "services/tracker_service.h"

#include "data/torrent_repository.h"
#include "net/swarm_scraper.h"
#include "net/tracker_site_scraper.h"

namespace rats::service {

TrackerService::TrackerService(net::SwarmScraper* swarmScraper, net::TrackerSiteScraper* siteScraper,
    data::TorrentRepository* repository, QObject* parent)
    : QObject(parent), swarmScraper_(swarmScraper), siteScraper_(siteScraper), repository_(repository)
{
    connect(swarmScraper_, &net::SwarmScraper::scraped, this, &TrackerService::onCountsScraped);
    connect(siteScraper_, &net::TrackerSiteScraper::scraped, this, &TrackerService::onInfoScraped);
    connect(siteScraper_, &net::TrackerSiteScraper::finished, this, &TrackerService::infoChecked);
}

void TrackerService::setCountScrapingEnabled(bool enabled)
{
    countEnabled_ = enabled;
}

void TrackerService::setInfoScrapingEnabled(bool enabled)
{
    infoEnabled_ = enabled;
}

void TrackerService::stop()
{
    // Stop forwarding first so a late torrentIndexed / API call issues nothing,
    // then drain the scrapers (blocking announces / in-flight HTTP).
    countEnabled_ = false;
    infoEnabled_ = false;
    swarmScraper_->stop();
    siteScraper_->stop();
}

bool TrackerService::checkCounts(const QString& hash, net::ScrapePriority priority)
{
    return countEnabled_ && swarmScraper_->requestScrape(hash, priority);
}

bool TrackerService::checkInfo(const QString& hash, net::ScrapePriority priority)
{
    return infoEnabled_ && siteScraper_->scrape(hash, priority);
}

void TrackerService::onTorrentIndexed(const domain::Torrent& torrent)
{
    checkCounts(torrent.hash, net::ScrapePriority::Background);
    checkInfo(torrent.hash, net::ScrapePriority::Background);
}

void TrackerService::onCountsScraped(const QString& hash, int seeders, int leechers, int completed)
{
    repository_->updateTrackerCounts(hash, seeders, leechers, completed);
}

void TrackerService::onInfoScraped(const QString& hash, const QJsonObject& info)
{
    repository_->mergeInfo(hash, info);
}

} // namespace rats::service
