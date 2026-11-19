#include "data/migrations/Migrator.h"
#include "data/sqlite/Database.h"
#include "services/settings/SettingsService.h"

#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

using namespace studylab;
using namespace studylab::db;

class TestSettings : public QObject {
    Q_OBJECT
private slots:
    void defaults()
    {
        auto db = Database::openInMemory();
        QVERIFY(Migrator(Migrator::loadFrom().value()).migrate(*db.value()).ok());
        SettingsService s(*db.value());
        QCOMPARE(s.themeMode(), ThemeMode::System);
        QCOMPARE(s.uiScale(), 1.0);
        QVERIFY(s.animationsEnabled());
    }

    void persistsAcrossReopen()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("s.db"));
        {
            auto db = Database::open(path);
            QVERIFY(Migrator(Migrator::loadFrom().value()).migrate(*db.value()).ok());
            SettingsService s(*db.value());
            QSignalSpy spy(&s, &SettingsService::changed);
            s.setThemeMode(ThemeMode::Dark);
            s.setUiScale(1.25);
            s.setAnimationsEnabled(false);
            s.setValue(QStringLiteral("custom/text"), QStringLiteral("Éléments « guillemets »"));
            QCOMPARE(spy.count(), 4);
            s.setThemeMode(ThemeMode::Dark);  // valeur identique : pas de signal
            QCOMPARE(spy.count(), 4);
        }
        auto db = Database::open(path);
        SettingsService s(*db.value());
        QCOMPARE(s.themeMode(), ThemeMode::Dark);
        QCOMPARE(s.uiScale(), 1.25);
        QVERIFY(!s.animationsEnabled());
        QCOMPARE(s.value(QStringLiteral("custom/text")).toString(), QStringLiteral("Éléments « guillemets »"));
    }

    void scaleIsClamped()
    {
        auto db = Database::openInMemory();
        QVERIFY(Migrator(Migrator::loadFrom().value()).migrate(*db.value()).ok());
        SettingsService s(*db.value());
        s.setUiScale(5.0);
        QCOMPARE(s.uiScale(), 1.5);
        s.setUiScale(0.1);
        QCOMPARE(s.uiScale(), 0.9);
    }

    void themeModeStrings()
    {
        for (ThemeMode m : {ThemeMode::System, ThemeMode::Light, ThemeMode::Dark})
            QCOMPARE(SettingsService::themeModeFromString(SettingsService::themeModeToString(m)), m);
        QCOMPARE(SettingsService::themeModeFromString(QStringLiteral("rainbow")), ThemeMode::System);
    }
};

QTEST_GUILESS_MAIN(TestSettings)
#include "tst_settings.moc"
