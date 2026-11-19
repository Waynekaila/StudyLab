#include "core/domain/Organize.h"
#include "data/migrations/Migrator.h"
#include "data/repositories/DocumentRepository.h"
#include "data/repositories/NoteRepository.h"
#include "data/sqlite/Database.h"
#include "services/organize/OrganizeService.h"

#include <QDateTime>
#include <QJsonArray>
#include <QSignalSpy>
#include <QTest>

using namespace studylab;
using namespace studylab::db;

/// Matières hiérarchiques et collections (manuelles, intelligentes).
class TstOrganize : public QObject {
    Q_OBJECT

    std::unique_ptr<Database> m_db;

    qint64 addDocument(const QString& title, ReadingStatus status, const QString& addedAt = {},
                       const QStringList& tags = {}, bool favorite = false)
    {
        DocumentRepository docs(*m_db);
        DocumentRecord d;
        d.title = title;
        d.fileHash = title;
        d.filePath = title + QStringLiteral(".pdf");
        d.status = status;
        d.tags = tags;
        d.favorite = favorite;
        if (!addedAt.isEmpty())
            d.addedAt = addedAt;
        if (!docs.insert(d))
            return 0;
        return d.id;
    }
    qint64 addNote(const QString& title)
    {
        NoteRepository notes(*m_db);
        NoteRecord n;
        n.title = title;
        n.slug = title;
        if (!notes.insert(n))
            return 0;
        return n.id;
    }

private slots:
    void init()
    {
        m_db = std::move(Database::openInMemory().value());
        QVERIFY(Migrator(Migrator::loadFrom().value()).migrate(*m_db).ok());
    }

    void smartQueryJson()
    {
        SmartQuery q;
        q.matchAll = false;
        q.rules << SmartRule{SmartRule::Field::Status, SmartRule::Op::IsNot, {QStringLiteral("finished")}}
                << SmartRule{SmartRule::Field::AddedWithin, SmartRule::Op::Is, {QStringLiteral("90")}};
        const auto back = SmartQuery::fromJson(q.toJson());
        QVERIFY(back.has_value());
        QCOMPARE(*back, q);
        QVERIFY(!SmartQuery::fromJson(QJsonObject{{QStringLiteral("rules"),
                                                   QJsonArray{QJsonObject{{QStringLiteral("field"), QStringLiteral("x")}}}}})
                     .has_value());
        QCOMPARE(collectionKindFromKey(QStringLiteral("reading_list")), CollectionKind::ReadingList);
    }

    void subjectsTreeAndStats()
    {
        OrganizeService org(*m_db);
        QSignalSpy changed(&org, &OrganizeService::subjectsChanged);
        const auto physique = org.createSubject(QStringLiteral("Physique")).value();
        const auto thermo = org.createSubject(QStringLiteral("Thermodynamique"), physique.id).value();
        const auto fluides = org.createSubject(QStringLiteral("Mécanique des fluides"), physique.id).value();
        const auto maths = org.createSubject(QStringLiteral("  Mathématiques  ")).value();
        QCOMPARE(changed.size(), 4);
        QCOMPARE(maths.name, QStringLiteral("Mathématiques"));
        QCOMPARE(thermo.position, 0);
        QCOMPARE(fluides.position, 1);
        QVERIFY(physique.color != thermo.color);  // nuancier parcouru
        QVERIFY(!org.createSubject(QStringLiteral("   ")));
        QVERIFY(!org.createSubject(QStringLiteral("X"), 999));

        auto all = org.subjects().value();
        QCOMPARE(org.subjectPath(thermo.id, all), QStringLiteral("Physique › Thermodynamique"));
        QCOMPARE(org.withDescendants(physique.id, all).size(), 3);

        // Pas de cycle
        QVERIFY(!org.moveSubject(physique.id, thermo.id));
        // Réordonner : fluides en premier
        QVERIFY(org.moveSubject(fluides.id, physique.id, 0));
        all = org.subjects().value();
        QCOMPARE(org.subject(fluides.id).value().position, 0);
        QCOMPARE(org.subject(thermo.id).value().position, 1);

        // Rattachements et chiffres (sous-matières comprises, sans doublon)
        const qint64 cengel = addDocument(QStringLiteral("Thermodynamique, Çengel"), ReadingStatus::Reading);
        const qint64 poly = addDocument(QStringLiteral("Cours Thermo S5"), ReadingStatus::ToReread);
        const qint64 white = addDocument(QStringLiteral("White, Fluid Mechanics"), ReadingStatus::ToRead);
        const qint64 note = addNote(QStringLiteral("Second principe"));
        QVERIFY(org.attach(thermo.id, OrganizeService::Entity::Document, cengel));
        QVERIFY(org.attach(thermo.id, OrganizeService::Entity::Document, poly));
        QVERIFY(org.attach(physique.id, OrganizeService::Entity::Document, cengel));  // aussi au parent
        QVERIFY(org.attach(fluides.id, OrganizeService::Entity::Document, white));
        QVERIFY(org.attach(thermo.id, OrganizeService::Entity::Note, note));
        QVERIFY(org.attach(thermo.id, OrganizeService::Entity::Note, note));  // idempotent

        const SubjectStats st = org.subjectStats(physique.id).value();
        QCOMPARE(st.documents, 3);
        QCOMPARE(st.notes, 1);
        const auto counts = org.subjectCounts().value();
        QCOMPARE(counts.value(physique.id), 4);
        QCOMPARE(counts.value(thermo.id), 3);
        QCOMPARE(counts.value(maths.id), 0);
        QCOMPARE(org.subjectItems(thermo.id, OrganizeService::Entity::Document).value().size(), 2);

        QVERIFY(org.setSubjects(OrganizeService::Entity::Document, cengel, {maths.id}));
        QCOMPARE(org.subjectsOf(OrganizeService::Entity::Document, cengel).value(), QList<qint64>{maths.id});

        // Document à la corbeille : plus compté
        QVERIFY(DocumentRepository(*m_db).remove(poly));
        QCOMPARE(org.subjectStats(thermo.id).value().documents, 0);

        // Suppression : les sous-matières remontent, les documents restent
        QVERIFY(org.removeSubject(physique.id));
        QCOMPARE(org.subject(thermo.id).value().parentId, 0);
        QCOMPARE(org.subject(fluides.id).value().parentId, 0);
        QCOMPARE(DocumentRepository(*m_db).findById(white).value().title, QStringLiteral("White, Fluid Mechanics"));

        QVERIFY(org.setSubjectColor(maths.id, QStringLiteral("pink")));
        QVERIFY(!org.setSubjectColor(maths.id, QStringLiteral("#ff0000")));
        QVERIFY(org.setSubjectArchived(maths.id, true).value().archived);
    }

    void manualCollectionsKeepTheirOrder()
    {
        OrganizeService org(*m_db);
        const auto prepa = org.createCollection(QStringLiteral("Prépa examen S5"), CollectionKind::ReadingList).value();
        const qint64 a = addDocument(QStringLiteral("A"), ReadingStatus::ToRead);
        const qint64 b = addDocument(QStringLiteral("B"), ReadingStatus::ToRead);
        const qint64 n = addNote(QStringLiteral("Fiche"));
        QVERIFY(org.addToCollection(prepa.id, OrganizeService::Entity::Document, a));
        QVERIFY(org.addToCollection(prepa.id, OrganizeService::Entity::Note, n));
        QVERIFY(org.addToCollection(prepa.id, OrganizeService::Entity::Document, b));
        QVERIFY(org.addToCollection(prepa.id, OrganizeService::Entity::Document, a));  // déjà présent
        QList<CollectionItem> items = org.collectionItems(prepa.id).value();
        QCOMPARE(items.size(), 3);
        QCOMPARE(items.at(1).type, CollectionItem::Type::Note);

        std::reverse(items.begin(), items.end());
        QVERIFY(org.reorder(prepa.id, items));
        QCOMPARE(org.collectionItems(prepa.id).value().first().id, b);
        QCOMPARE(org.collectionsOf(OrganizeService::Entity::Note, n).value(), QList<qint64>{prepa.id});

        QVERIFY(org.removeFromCollection(prepa.id, OrganizeService::Entity::Note, n));
        QCOMPARE(org.collectionCounts().value().value(prepa.id), 2);
        QCOMPARE(org.renameCollection(prepa.id, QStringLiteral("Prépa")).value().name, QStringLiteral("Prépa"));
        QVERIFY(org.removeCollection(prepa.id));
        QVERIFY(org.collections().value().isEmpty());
    }

    void smartCollectionsFollowTheirRules()
    {
        OrganizeService org(*m_db);
        const auto thermo = org.createSubject(QStringLiteral("Thermodynamique")).value();
        const auto cycles = org.createSubject(QStringLiteral("Cycles"), thermo.id).value();
        const QString old = QDateTime::currentDateTimeUtc().addDays(-200).toString(Qt::ISODate);
        const qint64 cengel = addDocument(QStringLiteral("Cengel"), ReadingStatus::Reading, {}, {QStringLiteral("examen")}, true);
        const qint64 poly = addDocument(QStringLiteral("Poly"), ReadingStatus::Finished);
        const qint64 td = addDocument(QStringLiteral("TD"), ReadingStatus::ToRead, old);
        const qint64 autre = addDocument(QStringLiteral("Autre"), ReadingStatus::ToRead);
        QVERIFY(org.attach(thermo.id, OrganizeService::Entity::Document, cengel));
        QVERIFY(org.attach(thermo.id, OrganizeService::Entity::Document, poly));
        QVERIFY(org.attach(cycles.id, OrganizeService::Entity::Document, td));

        // « Thermo non terminés » : matière (sous-matières comprises) et statut
        SmartQuery q;
        q.rules << SmartRule{SmartRule::Field::Subject, SmartRule::Op::Is, {thermo.uuid}}
                << SmartRule{SmartRule::Field::Status, SmartRule::Op::IsNot,
                             {QStringLiteral("finished"), QStringLiteral("archived")}};
        QCOMPARE(org.evaluate(q).value(), (QList<qint64>{cengel, td}));

        // Ajouté depuis 90 jours
        q.rules << SmartRule{SmartRule::Field::AddedWithin, SmartRule::Op::Is, {QStringLiteral("90")}};
        QCOMPARE(org.evaluate(q).value(), QList<qint64>{cengel});

        // Au moins une règle
        SmartQuery any;
        any.matchAll = false;
        any.rules << SmartRule{SmartRule::Field::Tag, SmartRule::Op::Is, {QStringLiteral("EXAMEN")}}
                  << SmartRule{SmartRule::Field::Subject, SmartRule::Op::Is, {cycles.uuid}};
        QCOMPARE(org.evaluate(any).value(), (QList<qint64>{cengel, td}));

        SmartQuery fav;
        fav.rules << SmartRule{SmartRule::Field::Favorite, SmartRule::Op::Is, {}};
        QCOMPARE(org.evaluate(fav).value(), QList<qint64>{cengel});
        SmartQuery notFav;
        notFav.rules << SmartRule{SmartRule::Field::Favorite, SmartRule::Op::IsNot, {}};
        QCOMPARE(org.evaluate(notFav).value().size(), 3);

        // Aucune règle : toute la bibliothèque ; règle sans valeur : rien
        QCOMPARE(org.evaluate({}).value().size(), 4);
        SmartQuery empty;
        empty.rules << SmartRule{SmartRule::Field::Status, SmartRule::Op::Is, {}};
        QVERIFY(org.evaluate(empty).value().isEmpty());

        // Enregistrée : évaluée à chaque lecture, non modifiable à la main
        const auto smart = org.createCollection(QStringLiteral("Thermo non terminés"), CollectionKind::Smart, q).value();
        QCOMPARE(org.collection(smart.id).value().query, q);
        QCOMPARE(org.collectionItems(smart.id).value().size(), 1);
        QVERIFY(!org.addToCollection(smart.id, OrganizeService::Entity::Document, autre));
        QVERIFY(org.setQuery(smart.id, any));
        QCOMPARE(org.collectionCounts().value().value(smart.id), 2);
        QVERIFY(DocumentRepository(*m_db).setStatus(cengel, ReadingStatus::Finished));
        QCOMPARE(org.collectionCounts().value().value(smart.id), 2);  // la règle « tag » suffit
    }
};

QTEST_MAIN(TstOrganize)
#include "tst_organize.moc"
