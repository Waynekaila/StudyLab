#include "data/migrations/Migrator.h"
#include "data/sqlite/Database.h"
#include "services/study/Scheduler.h"
#include "services/study/StudyService.h"

#include <QSignalSpy>
#include <QTest>

using namespace studylab;
using namespace studylab::db;

/// Cartes mémoire et répétition espacée (V2-1) : SM-2, paquets, cartes, file du jour, journal, statistiques.
class TstStudy : public QObject {
    Q_OBJECT

    std::unique_ptr<Database> m_db;
    QDateTime m_now;

    std::unique_ptr<StudyService> service()
    {
        auto s = std::make_unique<StudyService>(*m_db);
        s->setClock([this] { return m_now; });
        return s;
    }

    static QDateTime at(const QString& localTime)
    {
        return QDateTime::fromString(localTime, Qt::ISODate).toUTC();
    }

private slots:
    void init()
    {
        m_db = std::move(Database::openInMemory().value());
        QVERIFY(Migrator(Migrator::loadFrom().value()).migrate(*m_db).ok());
        // Mardi 10 h, heure locale
        m_now = QDateTime(QDate(2026, 11, 17), QTime(10, 0)).toUTC();
    }

    void clozeNumbersAndKeys()
    {
        QCOMPARE(clozeNumbers(QStringLiteral("{{c2::Carnot}} et {{c1::Clausius::nom}} puis {{c2::x}}")), (QList<int>{1, 2}));
        QVERIFY(clozeNumbers(QStringLiteral("aucun trou")).isEmpty());
        for (CardType t : {CardType::Basic, CardType::Reversed, CardType::Cloze, CardType::Formula})
            QCOMPARE(cardTypeFromKey(cardTypeKey(t)), t);
        for (CardState s : {CardState::New, CardState::Learning, CardState::Review, CardState::Relearning})
            QCOMPARE(cardStateFromKey(cardStateKey(s)), s);
    }

    void sm2Learning()
    {
        Sm2Scheduler sm2;
        ScheduleState s;
        // Nouvelle carte : 1 min, 6 min, 10 min, 4 j
        QCOMPARE(sm2.previewSeconds(s, m_now), (QList<qint64>{60, 360, 600, 4 * 86400}));
        s = sm2.next(s, Rating::Good, m_now);
        QCOMPARE(s.state, CardState::Learning);
        QCOMPARE(s.step, 1);
        QCOMPARE(m_now.secsTo(s.due), 600);
        s = sm2.next(s, Rating::Again, m_now);
        QCOMPARE(s.step, 0);
        QCOMPARE(m_now.secsTo(s.due), 60);
        s = sm2.next(s, Rating::Good, m_now);
        s = sm2.next(s, Rating::Good, m_now);  // diplômée : 1 jour
        QCOMPARE(s.state, CardState::Review);
        QCOMPARE(s.intervalDays, 1.0);
        QCOMPARE(s.reps, 4);
        QCOMPARE(s.ease, 2.5);
    }

    void sm2Reviews()
    {
        Sm2Scheduler sm2;
        ScheduleState s;
        s.state = CardState::Review;
        s.intervalDays = 10;
        s.ease = 2.5;
        s.due = m_now;
        // Difficile 12 j, Bien 25 j, Facile 33 j
        QCOMPARE(sm2.previewSeconds(s, m_now), (QList<qint64>{600, 12 * 86400, 25 * 86400, 33 * 86400}));
        const ScheduleState easy = sm2.next(s, Rating::Easy, m_now);
        QCOMPARE(easy.ease, 2.65);
        const ScheduleState hard = sm2.next(s, Rating::Hard, m_now);
        QCOMPARE(hard.ease, 2.35);
        // Retard de 4 jours : la moitié s'ajoute pour « Bien »
        const ScheduleState late = sm2.next(s, Rating::Good, m_now.addDays(4));
        QCOMPARE(late.intervalDays, 30.0);

        // Oubli : facilité − 0,2, réapprentissage 10 min, puis 1 jour
        ScheduleState lapse = sm2.next(s, Rating::Again, m_now);
        QCOMPARE(lapse.state, CardState::Relearning);
        QCOMPARE(lapse.lapses, 1);
        QCOMPARE(lapse.ease, 2.3);
        QCOMPARE(m_now.secsTo(lapse.due), 600);
        lapse = sm2.next(lapse, Rating::Good, m_now);
        QCOMPARE(lapse.state, CardState::Review);
        QCOMPARE(lapse.intervalDays, 1.0);

        // Facilité plancher
        s.ease = 1.35;
        QCOMPARE(sm2.next(s, Rating::Again, m_now).ease, 1.3);
        // Intervalle plafonné
        s.intervalDays = 30000;
        s.ease = 2.5;
        QCOMPARE(sm2.next(s, Rating::Good, m_now).intervalDays, 36500.0);
    }

    void fsrsFormulas()
    {
        FsrsScheduler fsrs;
        QCOMPARE(fsrs.config().w.size(), 19);
        QCOMPARE(FsrsScheduler::retrievability(10, 10), 0.9);
        QVERIFY(qAbs(fsrs.initialDifficulty(Rating::Again) - 7.1949) < 1e-4);
        QVERIFY(qAbs(fsrs.initialDifficulty(Rating::Good) - 5.2824) < 1e-4);
        QVERIFY(qAbs(fsrs.initialDifficulty(Rating::Easy) - 3.2245) < 1e-4);
        QCOMPARE(fsrs.initialStability(Rating::Good), 3.173);
        // Rétention visée : 0,9 → l'intervalle vaut la stabilité ; 0,8 → plus long ; 0,95 → plus court.
        QCOMPARE(fsrs.interval(10), 10.0);
        FsrsScheduler::Config loose;
        loose.retention = 0.8;
        QCOMPARE(FsrsScheduler(loose).interval(10), 24.0);
        FsrsScheduler::Config strict;
        strict.retention = 0.95;
        QCOMPARE(FsrsScheduler(strict).interval(10), 5.0);
        // La difficulté reste dans [1, 10].
        double d = 9.9;
        for (int i = 0; i < 20; ++i)
            d = fsrs.nextDifficulty(d, Rating::Again);
        QVERIFY(d <= 10 && d > 9);
    }

    void fsrsSchedule()
    {
        FsrsScheduler fsrs;
        ScheduleState s;
        // Nouvelle carte : mêmes étapes que SM-2, « Facile » sort directement à 16 jours (stabilité initiale).
        QCOMPARE(fsrs.previewSeconds(s, m_now), (QList<qint64>{60, 360, 600, 16 * 86400}));
        s = fsrs.next(s, Rating::Good, m_now);
        QCOMPARE(s.state, CardState::Learning);
        QCOMPARE(s.stability, 3.173);
        QVERIFY(qAbs(s.difficulty - 5.2824) < 1e-4);
        s = fsrs.next(s, Rating::Good, m_now.addSecs(600));
        QCOMPARE(s.state, CardState::Review);
        QVERIFY(qAbs(s.stability - 4.4669) < 1e-3);  // révision du jour même : stabilité à court terme
        QCOMPARE(s.intervalDays, 4.0);

        // À l'échéance : Difficile 7 j, Bien 14 j, Facile 34 j.
        const QDateTime due = s.due;
        QCOMPARE(fsrs.previewSeconds(s, due), (QList<qint64>{600, 7 * 86400, 14 * 86400, 34 * 86400}));
        const ScheduleState good = fsrs.next(s, Rating::Good, due);
        QVERIFY(good.stability > s.stability);
        QVERIFY(good.difficulty < s.difficulty + 1e-9);
        // Oubli : stabilité réduite, réapprentissage, puis un jour.
        ScheduleState lapse = fsrs.next(s, Rating::Again, due);
        QCOMPARE(lapse.state, CardState::Relearning);
        QCOMPARE(lapse.lapses, 1);
        QVERIFY(qAbs(lapse.stability - 1.2976) < 1e-3);
        QVERIFY(lapse.difficulty > s.difficulty);
        lapse = fsrs.next(lapse, Rating::Good, due.addSecs(600));
        QCOMPARE(lapse.state, CardState::Review);
        QCOMPARE(lapse.intervalDays, 2.0);

        // Carte révisée sous SM-2 puis passée à FSRS : stabilité tirée de l'intervalle.
        ScheduleState old;
        old.state = CardState::Review;
        old.intervalDays = 10;
        old.ease = 2.5;
        old.due = m_now;
        const ScheduleState converted = fsrs.next(old, Rating::Good, m_now);
        QCOMPARE(converted.state, CardState::Review);
        QVERIFY(converted.stability > 10);
        QVERIFY(converted.difficulty > 5 && converted.difficulty < 7);
    }

    void stepsParsing()
    {
        QCOMPARE(parseSteps(QStringLiteral("1 10")), std::optional<QList<int>>(QList<int>{1, 10}));
        QCOMPARE(parseSteps(QStringLiteral("1m, 10m; 1h")), std::optional<QList<int>>(QList<int>{1, 10, 60}));
        QCOMPARE(parseSteps(QStringLiteral("1.5h 1j")), std::optional<QList<int>>(QList<int>{90, 1440}));
        QCOMPARE(parseSteps(QString()), std::optional<QList<int>>(QList<int>{}));
        QVERIFY(!parseSteps(QStringLiteral("10 x")));
        QVERIFY(!parseSteps(QStringLiteral("2j")));  // plus d'un jour : c'est un intervalle, pas une étape
        QCOMPARE(stepsText({1, 10, 60, 90}), QStringLiteral("1 10 1h 90"));
    }

    void deckSettingsAndHistory()
    {
        auto study = service();
        Deck deck = study->createDeck(QStringLiteral("Mécanique")).value();
        const qint64 card = study->addCards(deck.id, CardType::Basic, QStringLiteral("F = ?"), QStringLiteral("m a"))
                                .value()
                                .first()
                                .id;
        deck.scheduler = QStringLiteral("fsrs");
        deck.newPerDay = 5;
        deck.learningSteps = {5};
        deck.relearningSteps = {};
        deck.retention = 1.5;  // ramenée à 0,99
        QVERIFY(study->updateDeck(deck));
        const Deck saved = study->deckSummary(deck.id).value().deck;
        QCOMPARE(saved.scheduler, QStringLiteral("fsrs"));
        QCOMPARE(saved.newPerDay, 5);
        QCOMPARE(saved.learningSteps, (QList<int>{5}));
        QVERIFY(saved.relearningSteps.isEmpty());
        QCOMPARE(saved.retention, 0.99);
        deck.learningSteps = {};
        QVERIFY(!study->updateDeck(deck));
        deck.learningSteps = {5};
        deck.scheduler = QStringLiteral("anki");
        QVERIFY(!study->updateDeck(deck));

        // Une seule étape (5 min) : « À revoir » la ramène à 5 min, « Bien » la diplôme (1 jour à 99 % de
        // rétention visée) ; la stabilité est enregistrée.
        const auto row = study->card(card).value();
        QCOMPARE(study->preview(row).at(0), 5 * 60);
        QCOMPARE(study->preview(row).at(2), 86400);
        QVERIFY(study->answer(card, Rating::Again));
        m_now = m_now.addSecs(300);
        const ScheduleState after = study->answer(card, Rating::Good).value();
        QCOMPARE(after.state, CardState::Review);
        const auto reloaded = study->card(card).value();
        QCOMPARE(reloaded.state.state, CardState::Review);
        QVERIFY(reloaded.state.stability > 0);
        QCOMPARE(reloaded.state.stability, after.stability);
        QCOMPARE(reloaded.state.difficulty, after.difficulty);

        const auto history = study->history(card).value();
        QCOMPARE(history.size(), 2);
        QCOMPARE(history.at(0).rating, Rating::Again);
        QCOMPARE(history.at(1).rating, Rating::Good);
        QCOMPARE(history.at(1).after.value(QStringLiteral("state")).toString(), QStringLiteral("review"));
        QVERIFY(history.at(1).after.value(QStringLiteral("stability")).toDouble() > 0);
        QCOMPARE(history.at(1).at, m_now);
    }

    void dayBoundary()
    {
        // La journée de révision commence à 4 h : 2 h du matin compte pour la veille.
        const QDateTime night = QDateTime(QDate(2026, 11, 18), QTime(2, 0)).toUTC();
        QCOMPARE(StudyService::dayStart(night).toLocalTime(), QDateTime(QDate(2026, 11, 17), QTime(4, 0)));
        QCOMPARE(StudyService::dayStart(m_now).toLocalTime(), QDateTime(QDate(2026, 11, 17), QTime(4, 0)));
    }

    void decksAndCards()
    {
        auto s = service();
        QSignalSpy changed(s.get(), &StudyService::changed);
        QVERIFY(!s->createDeck(QStringLiteral("  ")).ok());
        auto deck = s->createDeck(QStringLiteral("Thermodynamique"));
        QVERIFY(deck.ok());
        QCOMPARE(deck.value().newPerDay, 20);

        // Basique + inversée : deux cartes
        auto pair = s->addCards(deck.value().id, CardType::Basic, QStringLiteral("Premier principe ?"),
                                QStringLiteral("$\\Delta U = W + Q$"), CardSource::None, 0, true);
        QVERIFY(pair.ok());
        QCOMPARE(pair.value().size(), 2);
        QCOMPARE(pair.value().at(1).type, CardType::Reversed);
        // Texte à trous : une carte par numéro
        auto cloze = s->addCards(deck.value().id, CardType::Cloze,
                                 QStringLiteral("Le rendement de {{c1::Carnot}} vaut {{c2::$1 - T_F/T_C$}}."), QString());
        QVERIFY2(cloze.ok(), cloze.ok() ? "" : qPrintable(cloze.error().message));
        QCOMPARE(cloze.value().size(), 2);
        QCOMPARE(cloze.value().at(1).ordinal, 2);
        QVERIFY(!s->addCards(deck.value().id, CardType::Cloze, QStringLiteral("sans trou"), QString()).ok());
        QVERIFY(!s->addCards(deck.value().id, CardType::Basic, QStringLiteral("Recto"), QStringLiteral(" ")).ok());
        QVERIFY(changed.size() >= 3);

        auto summary = s->deckSummary(deck.value().id).value();
        QCOMPARE(summary.counts.total, 4);
        QCOMPARE(summary.newToday, 4);
        QCOMPARE(s->dueTotal(), 4);

        // Modifier, suspendre, supprimer
        Card c = cloze.value().first();
        c.front = QStringLiteral("Le rendement de {{c3::Carnot}}.");
        auto updated = s->updateCard(c);
        QVERIFY(updated.ok());
        QCOMPARE(updated.value().ordinal, 3);  // trou renuméroté
        QVERIFY(s->setSuspended(c.id, true).ok());
        QCOMPARE(s->deckSummary(deck.value().id).value().newToday, 3);
        QVERIFY(s->removeCard(pair.value().at(1).id).ok());
        QCOMPARE(s->cards(deck.value().id).value().size(), 3);

        // Limite de nouvelles cartes par jour
        Deck d = deck.value();
        d.newPerDay = 1;
        QVERIFY(s->updateDeck(d).ok());
        QCOMPARE(s->deckSummary(d.id).value().newToday, 1);
        QCOMPARE(s->deckSummary(d.id).value().deck.newPerDay, 1);

        QVERIFY(s->removeDeck(d.id).ok());
        QCOMPARE(s->decks().value().size(), 0);
    }

    void reviewSession()
    {
        auto s = service();
        const qint64 deck = s->createDeck(QStringLiteral("Mécanique")).value().id;
        for (int i = 0; i < 3; ++i)
            QVERIFY(s->addCards(deck, CardType::Basic, QStringLiteral("Q%1").arg(i), QStringLiteral("R%1").arg(i)).ok());
        Deck d = s->deckSummary(deck).value().deck;
        d.newPerDay = 2;
        QVERIFY(s->updateDeck(d).ok());

        // Deux nouvelles au plus ; la première est la plus ancienne
        auto first = s->nextCard(deck).value();
        QVERIFY(first.has_value());
        QCOMPARE(first->card.front, QStringLiteral("Q0"));
        QCOMPARE(s->answer(first->card.id, Rating::Easy, 4200).value().state, CardState::Review);
        auto second = s->nextCard(deck).value();
        QCOMPARE(second->card.front, QStringLiteral("Q1"));
        QCOMPARE(s->answer(second->card.id, Rating::Good).value().state, CardState::Learning);
        // Plus de nouvelles aujourd'hui ; Q1 revient en avance (apprentissage dû dans 10 min)
        auto ahead = s->nextCard(deck).value();
        QVERIFY(ahead.has_value());
        QCOMPARE(ahead->card.id, second->card.id);
        QVERIFY(s->answer(ahead->card.id, Rating::Good).ok());  // diplômée : 1 j
        QVERIFY(!s->nextCard(deck).value().has_value());
        QCOMPARE(s->deckSummary(deck).value().due(), 0);

        auto stats = s->stats(deck).value();
        QCOMPARE(stats.reviewed, 3);
        QCOMPARE(stats.newDone, 2);
        QCOMPARE(stats.newLimit, 2);
        QCOMPARE(stats.successRate, 1.0);
        QCOMPARE(stats.lastDays.size(), 7);
        QCOMPARE(stats.lastDays.last(), 3);

        // Le lendemain : Q1 à revoir (1 j), Q2 nouvelle ; Q0 dans 4 jours
        m_now = m_now.addDays(1);
        auto summary = s->deckSummary(deck).value();
        QCOMPARE(summary.reviews, 1);
        QCOMPARE(summary.newToday, 1);
        auto review = s->nextCard(deck).value();
        QCOMPARE(review->card.front, QStringLiteral("Q1"));
        QCOMPARE(s->answer(review->card.id, Rating::Again).value().state, CardState::Relearning);
        QCOMPARE(s->stats(deck).value().successRate, 0.0);
        QCOMPARE(s->stats(deck).value().lastDays.mid(5), (QList<int>{3, 1}));
        QCOMPARE(s->stats().value().reviewed, 1);  // tous paquets

        // Journal complet : états avant / après
        QCOMPARE(m_db->queryInt(QStringLiteral("SELECT count(*) FROM card_reviews")).value(), 4);
        QCOMPARE(m_db->queryString(QStringLiteral("SELECT json_extract(prev_state_json, '$.state') FROM card_reviews "
                                                  "ORDER BY id DESC LIMIT 1")).value(),
                 QStringLiteral("review"));
        QCOMPARE(m_db->queryInt(QStringLiteral("SELECT elapsed_ms FROM card_reviews ORDER BY id LIMIT 1")).value(), 4200);
        QCOMPARE(m_db->queryInt(QStringLiteral("SELECT count(*) FROM activity WHERE action = 'reviewed'")).value(), 4);
    }

    void sourceOfCard()
    {
        auto s = service();
        const qint64 deck = s->createDeck(QStringLiteral("Thermo")).value().id;
        QVERIFY(m_db->exec(QStringLiteral(
                    "INSERT INTO documents(uuid, title, sort_title, format, file_path, file_hash, file_size, added_at, updated_at) "
                    "VALUES ('d', 'Çengel', 'cengel', 'pdf', 'a.pdf', 'h', 1, 'x', 'x');"
                    "INSERT INTO annotations(uuid, document_id, type, unit_index, page_label, locator_json, sort_key, "
                    "selected_text, doc_hash, created_at, updated_at) VALUES ('a', 1, 'highlight', 297, '298', '{}', 297, "
                    "'Le rendement de Carnot  ne dépend que', 'h', 'x', 'x');"
                    "INSERT INTO notes(uuid, title, slug, created_at, updated_at) VALUES ('n', 'Cycle de Carnot', 'cycle', 'x', 'x')"))
                    .ok());
        auto fromAnnotation = s->addCards(deck, CardType::Basic, QStringLiteral("Rendement de Carnot ?"),
                                          QStringLiteral("$1 - T_F/T_C$"), CardSource::Annotation, 1);
        QVERIFY(fromAnnotation.ok());
        auto info = s->source(fromAnnotation.value().first());
        QVERIFY(info.ok());
        QCOMPARE(info.value().documentId, 1);
        QCOMPARE(info.value().quote, QStringLiteral("Le rendement de Carnot ne dépend que"));
        QCOMPARE(info.value().label, QStringLiteral("Çengel · p. 298"));
        auto fromNote = s->addCards(deck, CardType::Basic, QStringLiteral("Cycle ?"), QStringLiteral("Deux isothermes"),
                                    CardSource::Note, 1);
        QCOMPARE(s->source(fromNote.value().first()).value().label, QStringLiteral("Cycle de Carnot"));
        // Annotation supprimée : la carte reste, sans origine
        QVERIFY(m_db->exec(QStringLiteral("UPDATE annotations SET deleted_at = 'x'")).ok());
        QVERIFY(!s->source(fromAnnotation.value().first()).ok());
        QCOMPARE(s->cards(deck).value().size(), 2);
    }

    void intervalLabels()
    {
        QCOMPARE(formatInterval(30), QStringLiteral("< 1 min"));
        QCOMPARE(formatInterval(600), QStringLiteral("10 min"));
        QCOMPARE(formatInterval(3 * 3600), QStringLiteral("3 h"));
        QCOMPARE(formatInterval(4 * 86400), QStringLiteral("4 j"));
        QCOMPARE(formatInterval(76 * 86400), QStringLiteral("2,5 mois"));
        QCOMPARE(formatInterval(438 * 86400), QStringLiteral("1,2 an"));
    }
};

QTEST_GUILESS_MAIN(TstStudy)
#include "tst_study.moc"
