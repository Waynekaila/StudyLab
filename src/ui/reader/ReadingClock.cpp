#include "ui/reader/ReadingClock.h"

#include "services/activity/ActivityService.h"
#include "services/settings/SettingsService.h"

#include <QApplication>
#include <QEvent>
#include <QWidget>

namespace studylab::ui {

namespace {
constexpr int kTickMs = 5000;
constexpr int kFlushTicks = 6;  // 30 s
}  // namespace

ReadingClock::ReadingClock(AppContext& ctx, qint64 documentId, QWidget* tab)
    : QObject(tab), m_ctx(ctx), m_documentId(documentId), m_tab(tab)
{
    m_session.documentId = documentId;
    m_sinceTick.start();
    m_sinceInput.start();
    m_timer.setInterval(kTickMs);
    connect(&m_timer, &QTimer::timeout, this, [this] {
        const qint64 elapsed = m_sinceTick.restart();
        tick(std::min<qint64>(elapsed, 2 * kTickMs));  // veille de l'ordinateur : pas de saut
        if (++m_ticksSinceFlush >= kFlushTicks)
            flush();
    });
    if (m_ctx.activity) {
        m_timer.start();
        qApp->installEventFilter(this);
    }
}

ReadingClock::~ReadingClock()
{
    if (qApp)
        qApp->removeEventFilter(this);
    flush();
}

bool ReadingClock::enabled() const
{
    return m_ctx.activity && m_ctx.settings.value(SettingKeys::TrackReading, true).toBool();
}

bool ReadingClock::isActive() const
{
    if (!enabled() || m_sinceInput.elapsed() > IdleLimitMs)
        return false;
    if (m_assumeActive)
        return true;
    return m_tab->isVisible() && m_tab->window()->isActiveWindow();
}

void ReadingClock::rollDay()
{
    const QDate day = ActivityService::activityDay(m_ctx.activity->now());
    if (m_day.isValid() && day != m_day) {
        flush();
        m_session = db::ReadingSessionRow{};
        m_session.documentId = m_documentId;
        m_ms = 0;
        m_pages.clear();
    }
    m_day = day;
}

void ReadingClock::tick(qint64 ms)
{
    if (ms <= 0 || !isActive())
        return;
    rollDay();
    const int before = seconds();
    m_ms += ms;
    if (seconds() != before)
        m_dirty = true;
}

void ReadingClock::notePage(int page)
{
    if (!enabled() || page < 0)
        return;
    markInput();
    if (!m_assumeActive && !m_tab->isVisible())
        return;
    rollDay();
    if (!m_pages.contains(page)) {
        m_pages.insert(page);
        m_dirty = true;
    }
}

void ReadingClock::flush()
{
    m_ticksSinceFlush = 0;
    if (!m_dirty || !enabled())
        return;
    m_session.seconds = seconds();
    m_session.pages = m_pages.values();
    if (m_ctx.activity->saveSession(m_session))
        m_dirty = false;
}

bool ReadingClock::eventFilter(QObject* watched, QEvent* event)
{
    switch (event->type()) {
    case QEvent::KeyPress:
    case QEvent::MouseButtonPress:
    case QEvent::MouseMove:
    case QEvent::Wheel:
    case QEvent::TouchBegin:
        if (auto* w = qobject_cast<QWidget*>(watched); w && w->window() == m_tab->window())
            m_sinceInput.restart();
        break;
    default:
        break;
    }
    return QObject::eventFilter(watched, event);
}

}  // namespace studylab::ui
