#pragma once

#include <QString>
#include <QStringList>

namespace studylab::test {

struct PdfSpec {
    QString title;
    int pages = 2;
    QString bodyText = QStringLiteral("Le second principe de la thermodynamique.");
};

/// Génère un PDF réel (QPdfWriter) avec un titre dans le dictionnaire Info.
bool writePdf(const QString& path, const PdfSpec& spec);

struct EpubSpec {
    QString title;
    QStringList authors;
    QString language = QStringLiteral("fr");
    QString isbn;
    QString date = QStringLiteral("2019-05-01");
    QStringList subjects;
    QStringList chapters = {QStringLiteral("<p>Chapitre un.</p>"), QStringLiteral("<p>Chapitre deux.</p>")};
    bool epub3 = true;
    bool withCover = true;
    bool drm = false;
};

/// Génère un EPUB 2 ou 3 minimal mais conforme (mimetype non compressé en premier).
bool writeEpub(const QString& path, const EpubSpec& spec);

}  // namespace studylab::test
