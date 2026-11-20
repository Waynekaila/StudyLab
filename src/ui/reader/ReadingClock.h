#pragma once

#include "data/repositories/ActivityRepository.h"
#include "ui/shell/AppContext.h"

#include <QDate>
#include <QElapsedTimer>
#include <QObject>
#include <QSet>
#include <QTimer>

class QWidget;

namespace studylab::ui {

/// Temps de lecture actif d'un onglet et pages vues (V2-4, table reading_sessions).
///
/// Le temps compte quand l'onglet est visible, la fenêtre au premier plan et qu'une action (clavier, souris,
/// défilement, changement de page) a eu lieu dans les deux dernières minutes. La séance est écrite toutes les
/// 30 secondes et à la fermeture ; une nouvelle séance commence avec un nouveau jour (4 h). Rien n'est mesuré si
/// le réglage « Mesurer le temps de lecture » est désactivé.
class ReadingClock : public QObject {
    Q_OBJECT
public:
    static constexpr int IdleLimitMs = 2 * 60 * 1000;

    ReadingClock(AppContext& ctx, qint64 documentId, QWidget* tab);
    ~ReadingClock() override;

    /// Page (PDF) ou page équivalente (EPUB) affichée ; compte aussi comme une action.
    void notePage(int page);
    /// Écrit la séance si elle a changé.
    void flush();

    bool enabled() const;
    bool isActive() const;
    /// Ajoute `ms` de lecture si l'onglet est actif (appelé par la minuterie ; public pour les tests).
    void tick(qint64 ms);
    /// Tests : considérer l'onglet comme visible et au premier plan.
    void setAssumeActive(bool on) { m_assumeActive = on; }
    void markInput() { m_sinceInput.restart(); }

    int seconds() const { return int(m_ms / 1000); }
    QList<int> pages() const { return m_pages.values(); }
    qint64 sessionId() const { return m_session.id; }

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void rollDay();

    AppContext& m_ctx;
    qint64 m_documentId;
    QWidget* m_tab;
    QTimer m_timer;
    QElapsedTimer m_sinceTick;
    QElapsedTimer m_sinceInput;
    qint64 m_ms = 0;
    QSet<int> m_pages;
    db::ReadingSessionRow m_session;
    QDate m_day;
    bool m_dirty = false;
    bool m_assumeActive = false;
    int m_ticksSinceFlush = 0;
};

}  // namespace studylab::ui
