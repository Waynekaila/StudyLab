#pragma once

#include <QDateTime>
#include <QJsonObject>
#include <QList>
#include <QString>

namespace studylab {

/// Type d'une carte mémoire (07-versions V2) : basique (recto → verso), inversée (verso → recto), texte à trous
/// ({{c1::…}}, une carte par numéro de trou), formule (question → formule LaTeX).
enum class CardType { Basic, Reversed, Cloze, Formula };
QString cardTypeKey(CardType type);  ///< « basic », « reversed », « cloze », « formula »
CardType cardTypeFromKey(const QString& key);

/// État d'une carte pour le planificateur.
enum class CardState { New, Learning, Review, Relearning };
QString cardStateKey(CardState state);
CardState cardStateFromKey(const QString& key);

/// Réponse à une carte : 1 À revoir, 2 Difficile, 3 Bien, 4 Facile.
enum class Rating { Again = 1, Hard = 2, Good = 3, Easy = 4 };

/// Origine d'une carte créée depuis la lecture ou une note.
enum class CardSource { None, Annotation, Note, Comment, Document };
QString cardSourceKey(CardSource source);  ///< vide pour None
CardSource cardSourceFromKey(const QString& key);

/// Paquet de cartes (table decks), rattaché ou non à une matière.
struct Deck {
    qint64 id = 0;
    QString uuid;
    QString name;
    qint64 subjectId = 0;          ///< 0 : aucune matière
    QString scheduler = QStringLiteral("sm2");
    int newPerDay = 20;            ///< config_json « newPerDay »
    int reviewsPerDay = 200;       ///< config_json « reviewsPerDay »
    QString createdAt;

    QJsonObject configJson() const;
    void applyConfig(const QJsonObject& config);
};

/// Carte (table cards).
struct Card {
    qint64 id = 0;
    QString uuid;
    qint64 deckId = 0;
    CardType type = CardType::Basic;
    QString front;  ///< Markdown ; texte à trous : le texte avec ses {{cN::…}}
    QString back;   ///< Markdown ; texte à trous : remarque affichée avec la réponse
    int ordinal = 0;  ///< numéro du trou (texte à trous), 0 sinon
    CardSource sourceType = CardSource::None;
    qint64 sourceId = 0;
    bool suspended = false;
    QString createdAt;
    QString updatedAt;
};

/// État courant du planificateur (table card_state).
struct ScheduleState {
    CardState state = CardState::New;
    QDateTime due;          ///< UTC ; invalide pour une carte nouvelle
    double intervalDays = 0;
    double ease = 2.5;      ///< SM-2
    int reps = 0;
    int lapses = 0;
    int step = 0;           ///< étape d'apprentissage

    QJsonObject toJson() const;
    bool operator==(const ScheduleState&) const = default;
};

/// Numéros des trous d'un texte ({{c1::…}}, {{c2::…}}), triés, sans doublon.
QList<int> clozeNumbers(const QString& text);

}  // namespace studylab
