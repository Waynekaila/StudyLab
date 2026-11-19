#include "ui/theme/Icons.h"
#include "ui/theme/ThemeManager.h"
#include "ui/theme/Tokens.h"

#include <QApplication>
#include <QDir>
#include <QTest>

using namespace studylab::ui;

class TestTheme : public QObject {
    Q_OBJECT
private slots:
    void textContrastMeetsWcagAA_data()
    {
        QTest::addColumn<bool>("dark");
        QTest::newRow("clair") << false;
        QTest::newRow("sombre") << true;
    }

    // Lisibilité : texte principal et secondaire ≥ 4,5:1 sur les fonds où ils apparaissent.
    void textContrastMeetsWcagAA()
    {
        QFETCH(bool, dark);
        const Palette p = dark ? Palette::darkPalette() : Palette::light();
        for (const QColor& bg : {p.bg, p.surface, p.surface2, p.sunken}) {
            QVERIFY2(contrastRatio(p.text, bg) >= 7.0, qPrintable(bg.name()));
            QVERIFY2(contrastRatio(p.text2, bg) >= 4.5, qPrintable(bg.name()));
        }
        QVERIFY(contrastRatio(p.primaryStrong, p.primarySoft) >= 4.5);  // élément de navigation actif
        QVERIFY(contrastRatio(p.onPrimary, p.primary) >= 3.0);           // bouton principal (texte ≥ 13 px, gras)
        QVERIFY(contrastRatio(p.text3, p.surface) >= 3.0);               // texte tertiaire, non essentiel
    }

    void annotationBackgroundsKeepTextReadable()
    {
        for (const Palette& p : {Palette::light(), Palette::darkPalette()}) {
            for (const QColor& hl : {p.hlYellow, p.hlGreen, p.hlBlue, p.hlPink, p.hlPurple})
                QVERIFY2(contrastRatio(p.text, hl) >= 7.0, qPrintable(hl.name()));
        }
    }

    void contrastFormula()
    {
        QCOMPARE(qRound(contrastRatio(Qt::black, Qt::white)), 21);
        QCOMPARE(contrastRatio(Qt::white, Qt::white), 1.0);
    }

    void styleSheetHasNoUnresolvedTokens()
    {
        for (double scale : {0.9, 1.0, 1.5}) {
            const QString qss = ThemeManager::buildStyleSheet(Palette::light(), scale, QStringLiteral("Inter"));
            QVERIFY(!qss.contains(QStringLiteral("%{")));
            QVERIFY(qss.contains(QStringLiteral("#5b7db1")));
        }
        const QString dark = ThemeManager::buildStyleSheet(Palette::darkPalette(), 1.0, QStringLiteral("Inter"));
        QVERIFY(dark.contains(QStringLiteral("#1d2026")));
    }

    void allIconsRender()
    {
        const QStringList icons = QDir(QStringLiteral(":/icons")).entryList({QStringLiteral("*.svg")});
        QVERIFY(icons.size() >= 10);
        for (QString name : icons) {
            name.chop(4);
            QVERIFY(Icons::exists(name));
            const QImage img = Icons::get(name, Qt::black, 18).pixmap(18, 18).toImage();
            QVERIFY2(!img.isNull(), qPrintable(name));
            bool painted = false;
            for (int y = 0; y < img.height() && !painted; ++y)
                for (int x = 0; x < img.width() && !painted; ++x)
                    painted = qAlpha(img.pixel(x, y)) > 0;
            QVERIFY2(painted, qPrintable(name));
        }
        QVERIFY(!Icons::exists(QStringLiteral("does-not-exist")));
    }

    void bundledFontLoads()
    {
        QCOMPARE(ThemeManager::loadBundledFonts(), QStringLiteral("Inter"));
    }
};

QTEST_MAIN(TestTheme)
#include "tst_theme.moc"
