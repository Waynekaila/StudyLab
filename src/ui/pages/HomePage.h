#pragma once

#include "ui/shell/AppContext.h"

#include <QWidget>

class QLabel;
class QProgressBar;
class QPushButton;
class QVBoxLayout;

namespace studylab::ui {

class ResumeCover;

/// Accueil simplifié du MVP (05-maquettes §1, sans révisions ni objectifs qui arrivent en V2) :
/// reprendre la lecture, autres lectures en cours, dernières annotations et derniers commentaires,
/// état de la bibliothèque et de la sauvegarde. Toutes les valeurs sont lues en base.
class HomePage : public QWidget {
    Q_OBJECT
public:
    explicit HomePage(AppContext& ctx, QWidget* parent = nullptr);

    void refresh();

    // Pour les tests
    QLabel* documentCountLabel() const { return m_docCount; }
    QLabel* resumeTitle() const { return m_resumeTitle; }
    QLabel* resumePosition() const { return m_resumePosition; }
    QPushButton* continueButton() const { return m_continue; }
    QList<QPushButton*> annotationRows() const { return m_annotationRows; }
    QList<QPushButton*> commentRows() const { return m_commentRows; }
    QList<QPushButton*> readingRows() const { return m_readingRows; }
    QLabel* backupLabel() const { return m_backup; }

signals:
    void navigateRequested(const QString& pageId);
    void openDocumentRequested(qint64 documentId);
    void openItemRequested(qint64 documentId, bool comment, qint64 itemId);

protected:
    void showEvent(QShowEvent* event) override;

private:
    QWidget* buildResume();
    QWidget* buildLibrary();
    QPushButton* makeRow(const QPixmap& icon, const QString& text, const QString& caption);
    void clearRows(QVBoxLayout* layout, QList<QPushButton*>& rows);
    void refreshResume();
    void refreshActivity();

    AppContext& m_ctx;
    QLabel* m_greeting = nullptr;
    QLabel* m_date = nullptr;

    QWidget* m_resumeFilled = nullptr;
    QWidget* m_resumeEmpty = nullptr;
    ResumeCover* m_cover = nullptr;
    QLabel* m_resumeTitle = nullptr;
    QLabel* m_resumeAuthor = nullptr;
    QLabel* m_resumePosition = nullptr;
    QProgressBar* m_resumeProgress = nullptr;
    QLabel* m_resumeQuote = nullptr;
    QPushButton* m_continue = nullptr;
    qint64 m_resumeId = 0;
    QVBoxLayout* m_readingList = nullptr;
    QLabel* m_readingEmpty = nullptr;
    QList<QPushButton*> m_readingRows;

    QLabel* m_docCount = nullptr;
    QLabel* m_docHint = nullptr;
    QLabel* m_backup = nullptr;
    QLabel* m_dataDir = nullptr;

    QVBoxLayout* m_annotationList = nullptr;
    QLabel* m_annotationEmpty = nullptr;
    QList<QPushButton*> m_annotationRows;
    QVBoxLayout* m_commentList = nullptr;
    QLabel* m_commentEmpty = nullptr;
    QList<QPushButton*> m_commentRows;
};

}  // namespace studylab::ui
