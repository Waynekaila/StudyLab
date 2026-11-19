#pragma once

#include "ui/shell/AppContext.h"

#include <QWidget>

class QLabel;
class QPushButton;

namespace studylab::ui {

/// Accueil. Pour l'instant : état de la bibliothèque et des données locales (valeurs réelles lues en base).
class HomePage : public QWidget {
    Q_OBJECT
public:
    explicit HomePage(AppContext& ctx, QWidget* parent = nullptr);

    void refresh();

    // Pour les tests
    QLabel* documentCountLabel() const { return m_docCount; }

protected:
    void showEvent(QShowEvent* event) override;

private:
    AppContext& m_ctx;
    QLabel* m_date = nullptr;
    QLabel* m_docCount = nullptr;
    QLabel* m_docHint = nullptr;
    QLabel* m_dataDir = nullptr;
    QLabel* m_schema = nullptr;
    QLabel* m_engine = nullptr;
    QLabel* m_fts = nullptr;
};

}  // namespace studylab::ui
