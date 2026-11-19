#include "core/domain/Organize.h"

#include <QCoreApplication>
#include <QJsonArray>

namespace studylab {

namespace {

QString tr(const char* text)
{
    return QCoreApplication::translate("Organize", text);
}

}  // namespace

QStringList organizeColorKeys()
{
    return {QStringLiteral("slate"), QStringLiteral("blue"),  QStringLiteral("green"),
            QStringLiteral("yellow"), QStringLiteral("pink"), QStringLiteral("purple")};
}

QString collectionKindKey(CollectionKind k)
{
    switch (k) {
    case CollectionKind::Manual: return QStringLiteral("manual");
    case CollectionKind::Smart: return QStringLiteral("smart");
    case CollectionKind::ReadingList: return QStringLiteral("reading_list");
    case CollectionKind::Project: return QStringLiteral("project");
    }
    return QStringLiteral("manual");
}

std::optional<CollectionKind> collectionKindFromKey(const QString& key)
{
    for (CollectionKind k : {CollectionKind::Manual, CollectionKind::Smart, CollectionKind::ReadingList, CollectionKind::Project})
        if (collectionKindKey(k) == key)
            return k;
    return std::nullopt;
}

QString collectionKindLabel(CollectionKind k)
{
    switch (k) {
    case CollectionKind::Manual: return tr("Collection");
    case CollectionKind::Smart: return tr("Collection intelligente");
    case CollectionKind::ReadingList: return tr("Liste de lecture");
    case CollectionKind::Project: return tr("Projet");
    }
    return {};
}

QString collectionKindIcon(CollectionKind k)
{
    switch (k) {
    case CollectionKind::Manual: return QStringLiteral("layers");
    case CollectionKind::Smart: return QStringLiteral("sparkle");
    case CollectionKind::ReadingList: return QStringLiteral("list");
    case CollectionKind::Project: return QStringLiteral("folder");
    }
    return QStringLiteral("layers");
}

namespace {

struct FieldInfo {
    SmartRule::Field field;
    const char* key;
    const char* label;
};

const FieldInfo kFields[] = {
    {SmartRule::Field::Subject, "subject", QT_TRANSLATE_NOOP("Organize", "Matière")},
    {SmartRule::Field::Status, "status", QT_TRANSLATE_NOOP("Organize", "Statut")},
    {SmartRule::Field::Format, "format", QT_TRANSLATE_NOOP("Organize", "Format")},
    {SmartRule::Field::Tag, "tag", QT_TRANSLATE_NOOP("Organize", "Tag")},
    {SmartRule::Field::Favorite, "favorite", QT_TRANSLATE_NOOP("Organize", "Favori")},
    {SmartRule::Field::AddedWithin, "added_within", QT_TRANSLATE_NOOP("Organize", "Ajouté")},
    {SmartRule::Field::AnnotatedWithin, "annotated_within", QT_TRANSLATE_NOOP("Organize", "Annoté")},
    {SmartRule::Field::OpenedWithin, "opened_within", QT_TRANSLATE_NOOP("Organize", "Ouvert")},
};

}  // namespace

QString smartFieldKey(SmartRule::Field f)
{
    for (const auto& i : kFields)
        if (i.field == f)
            return QString::fromLatin1(i.key);
    return {};
}

std::optional<SmartRule::Field> smartFieldFromKey(const QString& key)
{
    for (const auto& i : kFields)
        if (key == QLatin1String(i.key))
            return i.field;
    return std::nullopt;
}

QString smartFieldLabel(SmartRule::Field f)
{
    for (const auto& i : kFields)
        if (i.field == f)
            return tr(i.label);
    return {};
}

bool smartFieldIsPeriod(SmartRule::Field f)
{
    return f == SmartRule::Field::AddedWithin || f == SmartRule::Field::AnnotatedWithin
        || f == SmartRule::Field::OpenedWithin;
}

bool smartFieldIsFlag(SmartRule::Field f)
{
    return f == SmartRule::Field::Favorite;
}

QJsonObject SmartQuery::toJson() const
{
    QJsonArray rs;
    for (const SmartRule& r : rules) {
        rs.append(QJsonObject{
            {QStringLiteral("field"), smartFieldKey(r.field)},
            {QStringLiteral("op"), r.op == SmartRule::Op::Is ? QStringLiteral("is") : QStringLiteral("is_not")},
            {QStringLiteral("values"), QJsonArray::fromStringList(r.values)},
        });
    }
    return {{QStringLiteral("version"), 1},
            {QStringLiteral("match"), matchAll ? QStringLiteral("all") : QStringLiteral("any")},
            {QStringLiteral("rules"), rs}};
}

std::optional<SmartQuery> SmartQuery::fromJson(const QJsonObject& o)
{
    SmartQuery q;
    q.matchAll = o.value(QStringLiteral("match")).toString(QStringLiteral("all")) != QLatin1String("any");
    for (const QJsonValue& v : o.value(QStringLiteral("rules")).toArray()) {
        const QJsonObject r = v.toObject();
        const auto field = smartFieldFromKey(r.value(QStringLiteral("field")).toString());
        if (!field)
            return std::nullopt;
        SmartRule rule;
        rule.field = *field;
        rule.op = r.value(QStringLiteral("op")).toString() == QLatin1String("is_not") ? SmartRule::Op::IsNot
                                                                                       : SmartRule::Op::Is;
        for (const QJsonValue& x : r.value(QStringLiteral("values")).toArray())
            rule.values << x.toString();
        q.rules << rule;
    }
    return q;
}

}  // namespace studylab
