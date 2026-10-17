#include "net/scrape_queue.h"

#include <QTimeZone>
#include <QtTest/QtTest>

using rats::net::ScrapePriority;
using rats::net::ScrapeQueue;
using Outcome = ScrapeQueue::Outcome;

class TestScrapeQueue : public QObject {
    Q_OBJECT

private:
    static ScrapeQueue::Limits limits()
    {
        ScrapeQueue::Limits l;
        l.maxConcurrent = 2;
        l.interactiveSlots = 1;
        l.maxQueued = 3;
        l.cooldownSecs = 3600;
        l.retrySecs = 60;
        return l;
    }

    static QString hash(int n) { return QString::number(n).rightJustified(40, '0'); }

    const QDateTime t0 = QDateTime(QDate(2026, 1, 1), QTime(12, 0), QTimeZone::UTC);

    // Fill every background slot with hashes 1..maxConcurrent.
    void fillBackground(ScrapeQueue& q)
    {
        for (int i = 1; i <= limits().maxConcurrent; ++i)
            QCOMPARE(q.submit(hash(i), ScrapePriority::Background, t0), Outcome::Started);
    }

private slots:
    void backgroundQueuesPastTheCap()
    {
        ScrapeQueue q(limits());
        fillBackground(q);
        QCOMPARE(q.submit(hash(3), ScrapePriority::Background, t0), Outcome::Queued);
        QCOMPARE(q.running(), 2);
        QCOMPARE(q.queued(), 1);

        QCOMPARE(q.finish(hash(1), true, t0), QStringList { hash(3) });
        QCOMPARE(q.running(), 2);
        QCOMPARE(q.queued(), 0);
    }

    // The click case: every regular slot is busy with crawler work, and the
    // user's request still starts immediately.
    void interactiveUsesReservedSlot()
    {
        ScrapeQueue q(limits());
        fillBackground(q);
        QCOMPARE(q.submit(hash(9), ScrapePriority::Interactive, t0), Outcome::Started);
        QCOMPARE(q.running(), 3);
        // The reserved slot is never handed to background work.
        QCOMPARE(q.submit(hash(10), ScrapePriority::Background, t0), Outcome::Queued);
        QVERIFY(q.finish(hash(9), true, t0).isEmpty());
    }

    void interactiveGoesBeforeBackground()
    {
        ScrapeQueue q(limits());
        fillBackground(q);
        QCOMPARE(q.submit(hash(3), ScrapePriority::Background, t0), Outcome::Queued);
        QCOMPARE(q.submit(hash(9), ScrapePriority::Interactive, t0), Outcome::Started);
        QCOMPARE(q.submit(hash(8), ScrapePriority::Interactive, t0), Outcome::Queued);

        QCOMPARE(q.finish(hash(1), true, t0), QStringList { hash(8) });
    }

    void interactivePromotesQueuedBackground()
    {
        ScrapeQueue q(limits());
        fillBackground(q);
        QCOMPARE(q.submit(hash(3), ScrapePriority::Background, t0), Outcome::Queued);
        QCOMPARE(q.submit(hash(3), ScrapePriority::Interactive, t0), Outcome::Started);
        QCOMPARE(q.queued(), 0); // moved out of the background queue, not duplicated
    }

    void inFlightAndQueuedRequestsAreNotDuplicated()
    {
        ScrapeQueue q(limits());
        fillBackground(q);
        QCOMPARE(q.submit(hash(1), ScrapePriority::Interactive, t0), Outcome::Running);
        QCOMPARE(q.submit(hash(3), ScrapePriority::Background, t0), Outcome::Queued);
        QCOMPARE(q.submit(hash(3), ScrapePriority::Background, t0), Outcome::Queued);
        QCOMPARE(q.queued(), 1);
    }

    void backgroundBacklogDropsOldest()
    {
        ScrapeQueue q(limits());
        fillBackground(q);
        for (int i = 3; i <= 6; ++i)
            QCOMPARE(q.submit(hash(i), ScrapePriority::Background, t0), Outcome::Queued);
        QCOMPARE(q.queued(), limits().maxQueued);

        // hash(3) fell off the front; the newest survive in order.
        QCOMPARE(q.finish(hash(1), true, t0), QStringList { hash(4) });
        QCOMPARE(q.finish(hash(2), true, t0), QStringList { hash(5) });
    }

    // A queued request is not a result: the quiet period starts at finish().
    void cooldownStartsWhenScrapeFinishes()
    {
        ScrapeQueue q(limits());
        QCOMPARE(q.submit(hash(1), ScrapePriority::Background, t0), Outcome::Started);
        q.finish(hash(1), true, t0);

        QCOMPARE(q.submit(hash(1), ScrapePriority::Interactive, t0.addSecs(3599)), Outcome::CoolingDown);
        QCOMPARE(q.submit(hash(1), ScrapePriority::Interactive, t0.addSecs(3600)), Outcome::Started);
    }

    void failedScrapeRetriesSooner()
    {
        ScrapeQueue q(limits());
        QCOMPARE(q.submit(hash(1), ScrapePriority::Interactive, t0), Outcome::Started);
        q.finish(hash(1), false, t0);

        QCOMPARE(q.submit(hash(1), ScrapePriority::Interactive, t0.addSecs(59)), Outcome::CoolingDown);
        QCOMPARE(q.submit(hash(1), ScrapePriority::Interactive, t0.addSecs(60)), Outcome::Started);
    }

    void clearDropsQueueButKeepsRunning()
    {
        ScrapeQueue q(limits());
        fillBackground(q);
        QCOMPARE(q.submit(hash(3), ScrapePriority::Background, t0), Outcome::Queued);
        q.clear();
        QCOMPARE(q.queued(), 0);
        QCOMPARE(q.running(), 2);
        QVERIFY(q.finish(hash(1), true, t0).isEmpty());
        QCOMPARE(q.running(), 1);
    }
};

QTEST_GUILESS_MAIN(TestScrapeQueue)
#include "test_scrape_queue.moc"
