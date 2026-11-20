#pragma once

#include <QColor>
#include <QDate>
#include <QList>
#include <QStringList>
#include <QWidget>

namespace studylab::ui {

class ThemeManager;

/// Barre de progression fine à la couleur choisie (progression par matière, lectures en cours).
class ProgressLine : public QWidget {
    Q_OBJECT
public:
    explicit ProgressLine(ThemeManager& theme, QWidget* parent = nullptr);
    void setValue(double value);  ///< 0..1
    double value() const { return m_value; }
    void setColor(const QColor& color);
    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    ThemeManager& m_theme;
    double m_value = 0;
    QColor m_color;
};

/// Anneau d'objectif : valeur au centre, sous-titre (« / 200 p. »), arc sauge proportionnel.
class GoalRing : public QWidget {
    Q_OBJECT
public:
    explicit GoalRing(ThemeManager& theme, QWidget* parent = nullptr);
    /// `target` = 0 : pas d'objectif, l'anneau reste vide.
    void setValues(int value, int target, const QString& caption);
    int value() const { return m_value; }
    int target() const { return m_target; }
    QString caption() const { return m_caption; }
    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    ThemeManager& m_theme;
    int m_value = 0;
    int m_target = 0;
    QString m_caption;
};

/// Histogramme avec étiquettes sous les barres ; la barre mise en avant est en lavande.
class BarChart : public QWidget {
    Q_OBJECT
public:
    explicit BarChart(ThemeManager& theme, QWidget* parent = nullptr);
    /// `tooltips` : texte de chaque barre (infobulle et description accessible).
    void setData(const QList<int>& values, const QStringList& labels, const QStringList& tooltips, int highlight = -1);
    const QList<int>& values() const { return m_values; }
    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent* event) override;
    bool event(QEvent* event) override;

private:
    int barAt(const QPoint& pos) const;

    ThemeManager& m_theme;
    QList<int> m_values;
    QStringList m_labels;
    QStringList m_tooltips;
    int m_highlight = -1;
};

/// Calendrier de régularité : une case par jour (colonnes = semaines, lundi en haut), teinte selon l'intensité.
class ActivityHeatmap : public QWidget {
    Q_OBJECT
public:
    explicit ActivityHeatmap(ThemeManager& theme, QWidget* parent = nullptr);
    /// `levels` : un niveau 0..4 par jour depuis `first` ; `tooltips` : texte de chaque jour.
    void setData(const QDate& first, const QList<int>& levels, const QStringList& tooltips);
    const QList<int>& levels() const { return m_levels; }
    QSize sizeHint() const override;
    bool hasHeightForWidth() const override { return true; }
    int heightForWidth(int width) const override;

protected:
    void paintEvent(QPaintEvent* event) override;
    bool event(QEvent* event) override;

private:
    QRectF cellRect(int index) const;
    int columns() const;
    qreal cell() const;

    ThemeManager& m_theme;
    QDate m_first;
    QList<int> m_levels;
    QStringList m_tooltips;
};

}  // namespace studylab::ui
