#pragma once

#include "core/domain/Organize.h"
#include "ui/shell/AppContext.h"

#include <QDialog>
#include <QList>

class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QVBoxLayout;

namespace studylab::ui {

/// Libellé d'opérateur selon le champ : « est », « n'est pas », « depuis moins de », « oui »…
QString smartOpLabel(SmartRule::Field field, SmartRule::Op op);
/// Règles en une phrase : « Matière est Thermodynamique · Statut n'est pas Terminé, Archivé ».
QString smartQuerySummary(const SmartQuery& query, const QList<SubjectRecord>& subjects);

/// Éditeur de collection intelligente (05-maquettes §6) : règles lisibles, nombre de résultats en direct.
class SmartCollectionDialog : public QDialog {
    Q_OBJECT
public:
    /// collectionId = 0 : nouvelle collection.
    SmartCollectionDialog(AppContext& ctx, qint64 collectionId, QWidget* parent = nullptr);
    ~SmartCollectionDialog() override;

    QString name() const;
    SmartQuery query() const;
    /// Enregistre (crée ou modifie). Identifiant de la collection, 0 en cas d'échec.
    qint64 save();
    /// Collection enregistrée (0 tant que rien n'est enregistré).
    qint64 savedId() const { return m_id; }

    // Accès pour les tests
    QLineEdit* nameEdit() const { return m_name; }
    QComboBox* matchCombo() const { return m_match; }
    QLabel* resultLabel() const { return m_result; }
    QPushButton* saveButton() const { return m_save; }
    void addRule(const SmartRule& rule);
    int ruleCount() const { return int(m_rows.size()); }
    void removeRule(int index);

private:
    struct Row;
    void rebuildValueEditor(Row* row);
    void updateResult();

    AppContext& m_ctx;
    qint64 m_id;
    QList<SubjectRecord> m_subjects;
    QLineEdit* m_name;
    QComboBox* m_match;
    QVBoxLayout* m_rules;
    QLabel* m_result;
    QPushButton* m_save;
    QList<Row*> m_rows;
};

}  // namespace studylab::ui
