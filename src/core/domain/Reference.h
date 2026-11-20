#pragma once

#include "core/Result.h"

#include <QJsonObject>
#include <QList>
#include <QSet>
#include <QString>
#include <QStringList>

namespace studylab {

/// Nom d'une personne au sens CSL : nom de famille et prénoms, ou nom « littéral » (institution).
struct PersonName {
    QString family;
    QString given;
    QString literal;

    bool isEmpty() const { return family.isEmpty() && given.isEmpty() && literal.isEmpty(); }
    /// « Çengel, Yunus A. » ; institution telle quelle.
    QString sortName() const;
    /// « Yunus A. Çengel ».
    QString displayName() const;
    /// « Çengel, Y. A. » (bibliographie).
    QString initialsName() const;
    /// Nom de famille, ou nom littéral.
    QString shortName() const;
    bool operator==(const PersonName&) const = default;
};

/// « Nom, Prénom » ou « Prénom Nom » ; un nom entre accolades, ou sans espace ni virgule mais long, reste littéral.
PersonName parsePersonName(const QString& text);

/// Référence bibliographique, stockée en CSL-JSON (table references_). Les champs CSL non modifiables dans
/// StudyLab sont conservés tels quels dans `extra` (import puis export sans perte).
struct Reference {
    qint64 id = 0;
    qint64 documentId = 0;       ///< document de la bibliothèque, 0 si aucun
    QString citekey;             ///< « cengel2019thermodynamique » : cité dans les notes par [[@clé]]
    QString type = QStringLiteral("book");  ///< type CSL : book, article-journal, chapter…
    QString title;
    QList<PersonName> authors;
    QList<PersonName> editors;
    int year = 0;                ///< 0 : sans date
    int month = 0;
    QString containerTitle;      ///< revue, ouvrage collectif, actes, site
    QString publisher;           ///< éditeur ; université (thèse), institution (rapport)
    QString place;
    QString volume;
    QString issue;
    QString pages;
    QString edition;
    QString doi;
    QString isbn;
    QString url;
    QString language;
    QString abstract;
    QString note;
    QJsonObject extra;
    QString createdAt;

    bool operator==(const Reference&) const = default;
};

/// Types CSL proposés dans l'interface, dans l'ordre d'affichage.
QStringList referenceTypes();
QString referenceTypeLabel(const QString& cslType);

/// CSL-JSON d'une référence (« id » = clé de citation).
QJsonObject toCslJson(const Reference& ref);
Reference fromCslJson(const QJsonObject& item);
/// Lit un fichier CSL-JSON : tableau d'éléments ou élément isolé.
Result<QList<Reference>> parseCslJson(const QByteArray& json);
QByteArray writeCslJson(const QList<Reference>& refs);

/// Résultat de la lecture d'un fichier BibTeX : les entrées comprises et ce qui a été ignoré.
struct BibtexParse {
    QList<Reference> references;
    QStringList warnings;        ///< « ligne 12 : entrée @article sans clé »…
};
/// BibTeX / BibLaTeX : @string, concaténation #, accents LaTeX courants ; @comment et @preamble ignorés.
BibtexParse parseBibtex(const QString& text);
/// BibTeX en UTF-8 (lisible par Biber, Zotero, JabRef) ; caractères spéciaux de LaTeX échappés.
QString writeBibtex(const QList<Reference>& refs);
/// Texte LaTeX vers Unicode : « {\'e} » → « é », « -- » → « – », accolades retirées.
QString latexToUnicode(const QString& latex);

/// Clé de citation valide : lettres, chiffres et « _ : . - », sans espace.
bool isValidCitekey(const QString& key);
/// « cengel2019thermodynamique » : premier auteur, année, premier mot significatif du titre ; suffixe a, b, c…
/// si la clé est déjà prise (comparaison sans casse).
QString makeCitekey(const Reference& ref, const QSet<QString>& takenLowercase);

/// Citation courte, auteur-date : « Çengel et Boles, 2019 », « Çengel et al., 2019 », « Titre, s. d. ».
QString formatCitation(const Reference& ref);
/// Entrée de bibliographie (style auteur-date, proche d'APA). En HTML, les titres d'ouvrage sont en italique.
QString formatBibliography(const Reference& ref, bool html = false);

/// Clés citées dans un texte Markdown : [[@clé]], [[@clé|texte]], [[@clé#p. 12]].
QStringList citedKeys(const QString& markdown);

}  // namespace studylab
