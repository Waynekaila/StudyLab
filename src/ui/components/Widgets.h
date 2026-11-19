#pragma once

#include <QFrame>
#include <QLabel>
#include <QString>

class QLayout;

namespace studylab::ui {

/// Libellé avec un rôle typographique du thème : h1, h2, h3, subtitle, caption, muted, faint, big, mono.
QLabel* makeLabel(const QString& text, const char* role = nullptr, QWidget* parent = nullptr);

/// Carte (surface arrondie, bordure discrète) avec une mise en page verticale aux marges généreuses.
QFrame* makeCard(QWidget* parent = nullptr);

QFrame* makeSeparator(QWidget* parent = nullptr);

/// Nombre accordé en français, séparateur de milliers inclus : « 0 document », « 1 284 documents ».
QString countLabel(qint64 n, const QString& singular, const QString& plural);

/// Ligne « libellé … contrôle » d'un écran de réglages.
QWidget* makeSettingRow(const QString& title, const QString& description, QWidget* control, QWidget* parent = nullptr);

}  // namespace studylab::ui
