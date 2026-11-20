#include "engines/ocr/OcrPage.h"

#include <QJsonArray>
#include <QJsonDocument>

#include <cmath>

namespace studylab {

namespace {
QString separator(const OcrWord& w, bool first)
{
    if (first)
        return {};
    switch (w.breakBefore) {
    case OcrWord::Paragraph: return QStringLiteral("\n\n");
    case OcrWord::Line: return QStringLiteral("\n");
    default: return QStringLiteral(" ");
    }
}

double round4(double v) { return std::round(v * 10000.0) / 10000.0; }
}  // namespace

QString OcrPage::text() const
{
    QString out;
    for (int i = 0; i < words.size(); ++i)
        out += separator(words.at(i), i == 0) + words.at(i).text;
    return out;
}

QList<int> OcrPage::offsets() const
{
    QList<int> out;
    out.reserve(words.size());
    int pos = 0;
    for (int i = 0; i < words.size(); ++i) {
        pos += int(separator(words.at(i), i == 0).size());
        out << pos;
        pos += int(words.at(i).text.size());
    }
    return out;
}

QString OcrPage::toJson() const
{
    QJsonArray arr;
    for (const OcrWord& w : words) {
        QJsonArray item;
        item << w.text << round4(w.box.x()) << round4(w.box.y()) << round4(w.box.width()) << round4(w.box.height())
             << w.breakBefore;
        arr.append(item);
    }
    return QString::fromUtf8(QJsonDocument(arr).toJson(QJsonDocument::Compact));
}

OcrPage OcrPage::fromJson(const QString& json)
{
    OcrPage page;
    const QJsonArray arr = QJsonDocument::fromJson(json.toUtf8()).array();
    page.words.reserve(arr.size());
    for (const QJsonValue& v : arr) {
        const QJsonArray item = v.toArray();
        if (item.size() < 5)
            continue;
        OcrWord w;
        w.text = item.at(0).toString();
        w.box = QRectF(item.at(1).toDouble(), item.at(2).toDouble(), item.at(3).toDouble(), item.at(4).toDouble());
        w.breakBefore = item.size() > 5 ? item.at(5).toInt() : OcrWord::None;
        if (!w.text.isEmpty())
            page.words << w;
    }
    return page;
}

}  // namespace studylab
