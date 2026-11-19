#pragma once

#include "core/domain/Document.h"

#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>

#include <optional>

namespace studylab {

/// Couleurs des matières et collections : jetons de palette (04-base-de-donnees), jamais une couleur brute.
/// L'interface les traduit en couleurs du thème (clair ou sombre).
QStringList organizeColorKeys();  ///< « slate », « blue », « green », « yellow », « pink », « purple »

/// Matière (table subjects) : hiérarchique, « Physique › Thermodynamique ».
struct SubjectRecord {
    qint64 id = 0;
    QString uuid;
    qint64 parentId = 0;  ///< 0 = matière de premier niveau
    QString name;
    QString description;
    QString color = QStringLiteral("slate");
    int position = 0;     ///< ordre parmi les matières de même parent
    QString createdAt;
    bool archived = false;
};

/// Ce qu'une matière regroupe (sous-matières comprises) : chiffres clés de sa page.
struct SubjectStats {
    int documents = 0;
    int annotations = 0;  ///< hors signets
    int notes = 0;
    double progress = 0.0;  ///< progression moyenne de lecture des documents, 0..1
};

/// Types de collections proposés (colonne collections.kind ; « folder » est réservé aux dossiers de notes).
enum class CollectionKind { Manual, Smart, ReadingList, Project };
QString collectionKindKey(CollectionKind k);  ///< « manual », « smart », « reading_list », « project »
std::optional<CollectionKind> collectionKindFromKey(const QString& key);
QString collectionKindLabel(CollectionKind k);  ///< « Collection », « Collection intelligente »…
QString collectionKindIcon(CollectionKind k);   ///< nom d'icône du thème

/// Règle d'une collection intelligente, lisible telle quelle : « Statut n'est pas Terminé, Archivé ».
struct SmartRule {
    enum class Field { Subject, Status, Format, Tag, Favorite, AddedWithin, AnnotatedWithin, OpenedWithin };
    enum class Op { Is, IsNot };
    Field field = Field::Status;
    Op op = Op::Is;
    QStringList values;  ///< uuid de matière, clé de statut ou de format, nom de tag ; nombre de jours pour « depuis »
    bool operator==(const SmartRule&) const = default;
};

/// Règles d'une collection intelligente (colonne query_json). Évaluées à la volée sur les documents actifs.
struct SmartQuery {
    bool matchAll = true;  ///< toutes les règles (sinon : au moins une)
    QList<SmartRule> rules;
    bool operator==(const SmartQuery&) const = default;

    QJsonObject toJson() const;
    static std::optional<SmartQuery> fromJson(const QJsonObject& o);
};

QString smartFieldKey(SmartRule::Field f);
std::optional<SmartRule::Field> smartFieldFromKey(const QString& key);
QString smartFieldLabel(SmartRule::Field f);  ///< « Matière », « Statut »…
/// Le champ prend-il un nombre de jours (« Ajouté depuis 90 jours ») plutôt qu'une liste de valeurs ?
bool smartFieldIsPeriod(SmartRule::Field f);
/// Le champ est-il un oui/non (« Favori est oui ») ?
bool smartFieldIsFlag(SmartRule::Field f);

struct CollectionRecord {
    qint64 id = 0;
    QString uuid;
    CollectionKind kind = CollectionKind::Manual;
    QString name;
    QString description;
    QString color = QStringLiteral("slate");
    SmartQuery query;  ///< collections intelligentes seulement
    int position = 0;
    QString createdAt;
    QString updatedAt;
};

/// Élément d'une collection manuelle, liste de lecture ou projet.
struct CollectionItem {
    enum class Type { Document, Note };
    Type type = Type::Document;
    qint64 id = 0;
    int position = 0;
    bool operator==(const CollectionItem&) const = default;
};

}  // namespace studylab
