#pragma once

#include "core/domain/Card.h"
#include "ui/shell/AppContext.h"

#include <QDialog>

class QComboBox;
class QLabel;
class QPlainTextEdit;

namespace studylab {
class StudyService;
}

namespace studylab::ui {

/// Saisie ou modification d'une carte : paquet, type (basique, basique et inversée, texte à trous, formule), recto,
/// verso. Créée depuis l'espace Révisions, une annotation ou une note (origine conservée).
class CardEditorDialog : public QDialog {
    Q_OBJECT
public:
    /// Nouvelle carte, éventuellement préremplie et rattachée à son origine.
    CardEditorDialog(AppContext& ctx, StudyService& study, QWidget* parent = nullptr);

    void setDeck(qint64 deckId);
    void setContent(const QString& front, const QString& back);
    void setSource(CardSource source, qint64 sourceId, const QString& label);
    /// Modification d'une carte existante (type figé, sauf entre basique et formule).
    void editCard(const Card& card);

    QComboBox* deckCombo() const { return m_deck; }
    QComboBox* typeCombo() const { return m_type; }
    QPlainTextEdit* frontEdit() const { return m_front; }
    QPlainTextEdit* backEdit() const { return m_back; }
    QLabel* errorLabel() const { return m_error; }
    /// Cartes créées ou modifiée, après accept().
    const QList<Card>& saved() const { return m_saved; }

    /// Enregistre ; false (et message affiché) si la saisie est incomplète. Appelé par « Enregistrer ».
    bool save();

private:
    void refreshHints();
    void fillDecks();

    AppContext& m_ctx;
    StudyService& m_study;
    QComboBox* m_deck;
    QComboBox* m_type;
    QPlainTextEdit* m_front;
    QPlainTextEdit* m_back;
    QLabel* m_frontLabel;
    QLabel* m_backLabel;
    QLabel* m_hint;
    QLabel* m_sourceLabel;
    QLabel* m_error;
    Card m_editing;
    CardSource m_source = CardSource::None;
    qint64 m_sourceId = 0;
    QList<Card> m_saved;
};

}  // namespace studylab::ui
