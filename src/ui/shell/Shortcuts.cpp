#include "ui/shell/Shortcuts.h"

#include "services/settings/SettingsService.h"
#include "ui/shell/Navigation.h"

#include <QCoreApplication>
#include <QShortcut>
#include <QWidget>

namespace studylab::ui::Shortcuts {

namespace {

QString tr(const char* s) { return QCoreApplication::translate("Shortcuts", s); }

constexpr char kPrefix[] = "shortcuts/";

QList<Action> buildActions()
{
    QList<Action> out;
    out << Action{QStringLiteral("app.search"), Group::General, tr("Rechercher partout"), QKeySequence(Qt::CTRL | Qt::Key_K)};
    out << Action{QStringLiteral("app.fullScreen"), Group::General, tr("Plein écran"), QKeySequence(Qt::Key_F11)};
    // Aller à un espace : Ctrl+1 à Ctrl+9 pour les neuf premiers dans l'ordre par défaut, Ctrl+, pour les paramètres.
    int n = 0;
    for (const Navigation::Entry& e : Navigation::entries()) {
        QKeySequence key;
        if (e.id == QLatin1String("settings"))
            key = QKeySequence(Qt::CTRL | Qt::Key_Comma);
        else if (n < 9)
            key = QKeySequence(Qt::CTRL | Qt::Key(Qt::Key_1 + n));
        ++n;
        out << Action{QStringLiteral("nav.") + e.id, Group::General, tr("Aller à : %1").arg(e.label), key};
    }
    out << Action{QStringLiteral("reader.find"), Group::Reader, tr("Rechercher dans le document"), QKeySequence(Qt::CTRL | Qt::Key_F)};
    out << Action{QStringLiteral("reader.goToPage"), Group::Reader, tr("Aller à la page (PDF)"), QKeySequence(Qt::CTRL | Qt::Key_G)};
    out << Action{QStringLiteral("reader.bookmark"), Group::Reader, tr("Signet"), QKeySequence(Qt::CTRL | Qt::Key_D)};
    out << Action{QStringLiteral("reader.back"), Group::Reader, tr("Position précédente"), QKeySequence(Qt::ALT | Qt::Key_Left)};
    out << Action{QStringLiteral("reader.forward"), Group::Reader, tr("Position suivante"), QKeySequence(Qt::ALT | Qt::Key_Right)};
    out << Action{QStringLiteral("notes.new"), Group::Notes, tr("Nouvelle note"), QKeySequence(Qt::CTRL | Qt::Key_N)};
    out << Action{QStringLiteral("notes.save"), Group::Notes, tr("Enregistrer la note"), QKeySequence(Qt::CTRL | Qt::Key_S)};
    out << Action{QStringLiteral("revisions.reveal"), Group::Revisions, tr("Retourner la carte"), QKeySequence(Qt::Key_Space)};
    out << Action{QStringLiteral("revisions.again"), Group::Revisions, tr("Répondre « À revoir »"), QKeySequence(Qt::Key_1)};
    out << Action{QStringLiteral("revisions.hard"), Group::Revisions, tr("Répondre « Difficile »"), QKeySequence(Qt::Key_2)};
    out << Action{QStringLiteral("revisions.good"), Group::Revisions, tr("Répondre « Bien »"), QKeySequence(Qt::Key_3)};
    out << Action{QStringLiteral("revisions.easy"), Group::Revisions, tr("Répondre « Facile »"), QKeySequence(Qt::Key_4)};
    out << Action{QStringLiteral("revisions.edit"), Group::Revisions, tr("Modifier la carte"), QKeySequence(Qt::Key_E)};
    out << Action{QStringLiteral("revisions.suspend"), Group::Revisions, tr("Suspendre la carte"), QKeySequence(Qt::Key_S)};
    return out;
}

/// Deux groupes peuvent-ils agir au même moment ? Le groupe Général agit partout.
bool overlap(Group a, Group b) { return a == b || a == Group::General || b == Group::General; }

bool isReserved(const QKeyCombination& k)
{
    const QKeySequence seq(k);
    static const QList<QKeySequence::StandardKey> standard = {
        QKeySequence::Copy, QKeySequence::Cut, QKeySequence::Paste, QKeySequence::SelectAll, QKeySequence::Undo,
        QKeySequence::Redo, QKeySequence::ZoomIn, QKeySequence::ZoomOut};
    for (const auto s : standard)
        if (QKeySequence::keyBindings(s).contains(seq))
            return true;
    const Qt::Key key = k.key();
    const Qt::KeyboardModifiers mods = k.keyboardModifiers() & ~Qt::KeypadModifier;
    if (mods == Qt::NoModifier || mods == Qt::ShiftModifier) {
        switch (key) {
        case Qt::Key_Escape:
        case Qt::Key_Return:
        case Qt::Key_Enter:
        case Qt::Key_Delete:
        case Qt::Key_Backspace:
        case Qt::Key_Tab:
        case Qt::Key_Backtab:
            return true;
        default:
            break;
        }
    }
    return false;
}

/// Une touche sans Ctrl, Alt ni Méta (hors touches de fonction) gênerait la saisie dans un champ de texte.
bool typesText(const QKeyCombination& k)
{
    const Qt::KeyboardModifiers mods = k.keyboardModifiers() & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier);
    if (mods != Qt::NoModifier)
        return false;
    return !(k.key() >= Qt::Key_F1 && k.key() <= Qt::Key_F35);
}

}  // namespace

const QList<Action>& actions()
{
    static const QList<Action> list = buildActions();
    return list;
}

const Action* find(const QString& id)
{
    for (const Action& a : actions())
        if (a.id == id)
            return &a;
    return nullptr;
}

QString groupLabel(Group group)
{
    switch (group) {
    case Group::General:
        return tr("Partout");
    case Group::Reader:
        return tr("Lecture");
    case Group::Notes:
        return tr("Mes notes");
    case Group::Revisions:
        return tr("Séance de révision");
    }
    return {};
}

QString settingKey(const QString& id) { return QLatin1String(kPrefix) + id; }

QKeySequence key(const SettingsService& settings, const QString& id)
{
    const Action* a = find(id);
    if (!a)
        return {};
    const QJsonValue v = settings.value(settingKey(id));
    if (!v.isString())
        return a->defaultKey;
    return QKeySequence::fromString(v.toString(), QKeySequence::PortableText);
}

bool isCustomized(const SettingsService& settings, const QString& id)
{
    const Action* a = find(id);
    return a && key(settings, id) != a->defaultKey;
}

QString validate(const SettingsService& settings, const QString& id, const QKeySequence& sequence, QString* conflictId)
{
    const Action* a = find(id);
    if (!a)
        return tr("Action inconnue.");
    if (sequence.isEmpty())
        return {};
    if (sequence.count() > 1)
        return tr("Une seule combinaison de touches par raccourci.");
    const QKeyCombination k = sequence[0];
    if (isReserved(k))
        return tr("%1 est réservé (édition, validation ou fermeture).").arg(sequence.toString(QKeySequence::NativeText));
    if (a->group != Group::Revisions && typesText(k))
        return tr("Ajoutez Ctrl ou Alt : une touche seule gênerait la saisie de texte.");
    for (const Action& other : actions()) {
        if (other.id == id || !overlap(a->group, other.group))
            continue;
        if (key(settings, other.id) == sequence) {
            if (conflictId)
                *conflictId = other.id;
            return tr("%1 est déjà utilisé pour « %2 ».").arg(sequence.toString(QKeySequence::NativeText), other.label);
        }
    }
    return {};
}

bool setKey(SettingsService& settings, const QString& id, const QKeySequence& sequence)
{
    if (!validate(settings, id, sequence).isEmpty())
        return false;
    const Action* a = find(id);
    if (sequence == a->defaultKey) {
        reset(settings, id);
        return true;
    }
    return settings.setValue(settingKey(id), sequence.toString(QKeySequence::PortableText));
}

void reset(SettingsService& settings, const QString& id)
{
    if (settings.value(settingKey(id)).isString())
        settings.setValue(settingKey(id), QJsonValue(QJsonValue::Null));
}

void resetAll(SettingsService& settings)
{
    for (const Action& a : actions())
        reset(settings, a.id);
}

QString keyText(const SettingsService& settings, const QString& id)
{
    return key(settings, id).toString(QKeySequence::NativeText);
}

QString withKey(const SettingsService& settings, const QString& label, const QString& id)
{
    const QString k = keyText(settings, id);
    return k.isEmpty() ? label : QStringLiteral("%1 (%2)").arg(label, k);
}

QShortcut* create(SettingsService& settings, const QString& id, QWidget* parent)
{
    auto* s = new QShortcut(parent);
    s->setObjectName(QStringLiteral("shortcut:") + id);
    s->setKey(key(settings, id));
    QObject::connect(&settings, &SettingsService::changed, s, [s, &settings, id](const QString& changed) {
        if (changed == settingKey(id))
            s->setKey(key(settings, id));
    });
    return s;
}

void onChanged(SettingsService& settings, QObject* context, const std::function<void()>& update)
{
    update();
    QObject::connect(&settings, &SettingsService::changed, context, [update](const QString& changed) {
        if (changed.startsWith(QLatin1String(kPrefix)))
            update();
    });
}

}  // namespace studylab::ui::Shortcuts
