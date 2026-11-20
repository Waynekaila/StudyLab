#pragma once

#include "core/domain/Card.h"
#include "ui/shell/AppContext.h"

#include <QDialog>

class QComboBox;
class QLabel;
class QLineEdit;
class QSpinBox;
class QTreeWidget;

namespace studylab {
class StudyService;
}

namespace studylab::ui {

/// Réglages d'un paquet (V2-2) : nom, matière, planificateur (SM-2 ou FSRS et sa rétention visée), limites
/// quotidiennes, étapes d'apprentissage et de réapprentissage, intervalle maximal.
class DeckSettingsDialog : public QDialog {
    Q_OBJECT
public:
    DeckSettingsDialog(AppContext& ctx, StudyService& study, const Deck& deck, QWidget* parent = nullptr);

    /// Enregistre ; false (et message affiché) si un réglage est invalide. Appelé par « Enregistrer ».
    bool save();

    QLineEdit* nameField() const { return m_name; }
    QComboBox* subjectBox() const { return m_subject; }
    QComboBox* schedulerBox() const { return m_scheduler; }
    QSpinBox* retentionBox() const { return m_retention; }
    QSpinBox* newPerDayBox() const { return m_newPerDay; }
    QSpinBox* reviewsPerDayBox() const { return m_reviewsPerDay; }
    QLineEdit* learningStepsField() const { return m_learning; }
    QLineEdit* relearningStepsField() const { return m_relearning; }
    QSpinBox* maxIntervalBox() const { return m_maxInterval; }
    QLabel* errorLabel() const { return m_error; }

private:
    void refreshScheduler();

    StudyService& m_study;
    Deck m_deck;
    QLineEdit* m_name;
    QComboBox* m_subject;
    QComboBox* m_scheduler;
    QSpinBox* m_retention;
    QLabel* m_retentionHint;
    QLabel* m_schedulerHint;
    QSpinBox* m_newPerDay;
    QSpinBox* m_reviewsPerDay;
    QLineEdit* m_learning;
    QLineEdit* m_relearning;
    QSpinBox* m_maxInterval;
    QLabel* m_error;
};

/// Journal d'une carte : chaque réponse, sa date, l'intervalle qui en a résulté et le temps de réponse.
class CardHistoryDialog : public QDialog {
    Q_OBJECT
public:
    CardHistoryDialog(AppContext& ctx, StudyService& study, qint64 cardId, QWidget* parent = nullptr);

    QTreeWidget* list() const { return m_list; }
    QLabel* summary() const { return m_summary; }

private:
    QTreeWidget* m_list;
    QLabel* m_summary;
};

}  // namespace studylab::ui
