#pragma once

#include "core/domain/Document.h"
#include "core/domain/Note.h"
#include "ui/theme/Tokens.h"

#include <QIcon>
#include <QPushButton>
#include <QTreeWidget>

#include <functional>

namespace studylab::ui {

class ThemeManager;

/// Couleur du thème pour un jeton de matière ou de collection (« slate », « blue »…).
QColor organizeColor(const Palette& p, const QString& key);
/// Libellé du jeton : « Ardoise », « Bleu »…
QString organizeColorLabel(const QString& key);
/// Pastille ronde de la couleur (listes, menus).
QIcon colorDot(const Palette& p, const QString& key, int size = 10);

/// Ligne cliquable d'un document dans une liste de matière ou de collection : couverture réduite à une tranche
/// colorée, titre, progression et nombre d'annotations, statut en puce.
class DocumentRow : public QPushButton {
    Q_OBJECT
public:
    DocumentRow(ThemeManager& theme, const DocumentRecord& doc, int annotations, QWidget* parent = nullptr);
    qint64 documentId() const { return m_doc.id; }
    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    ThemeManager& m_theme;
    DocumentRecord m_doc;
    QString m_detail;
};

/// Ligne cliquable d'une note : icône du type, titre, type à droite.
class NoteRow : public QPushButton {
    Q_OBJECT
public:
    NoteRow(ThemeManager& theme, const NoteRecord& note, QWidget* parent = nullptr);
    qint64 noteId() const { return m_id; }
    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    ThemeManager& m_theme;
    qint64 m_id;
    QString m_title;
    NoteType m_type;
};

/// Arborescence qui signale les déplacements faits à la souris (glisser-déposer interne) :
/// élément déplacé, nouveau parent (nullptr = premier niveau) et position parmi ses frères.
class DragTree : public QTreeWidget {
    Q_OBJECT
public:
    explicit DragTree(QWidget* parent = nullptr);

signals:
    void itemMoved(QTreeWidgetItem* item, QTreeWidgetItem* newParent, int index);

protected:
    void dropEvent(QDropEvent* event) override;
};

}  // namespace studylab::ui
