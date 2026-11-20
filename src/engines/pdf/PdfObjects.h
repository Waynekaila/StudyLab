#pragma once

#include "core/Result.h"

#include <QByteArray>
#include <QHash>
#include <QString>

#include <functional>
#include <optional>
#include <utility>
#include <vector>

namespace studylab::pdf {

/// Référence indirecte « 12 0 R ».
struct PdfRef {
    int num = 0;
    int gen = 0;
    bool operator==(const PdfRef& o) const { return num == o.num && gen == o.gen; }
};

/// Objet PDF (ISO 32000) : valeur simple, tableau, dictionnaire (ordre conservé), référence ou flux.
/// Juste ce qu'il faut pour lire et réécrire les annotations ; le contenu des pages n'est jamais interprété.
struct PdfObject {
    enum class Type { Null, Bool, Int, Real, String, Name, Array, Dict, Ref, Stream };
    using Entry = std::pair<QByteArray, PdfObject>;

    Type type = Type::Null;
    bool boolean = false;
    qint64 integer = 0;
    double real = 0;
    QByteArray bytes;  ///< octets d'une chaîne, ou nom sans « / »
    bool hex = false;  ///< chaîne écrite en hexadécimal
    std::vector<PdfObject> array;
    std::vector<Entry> dict;  ///< aussi le dictionnaire d'un flux
    PdfRef ref;
    QByteArray stream;  ///< données brutes (encore encodées) d'un flux

    static PdfObject null() { return {}; }
    static PdfObject fromBool(bool v);
    static PdfObject fromInt(qint64 v);
    static PdfObject fromReal(double v);
    static PdfObject fromName(const QByteArray& name);
    static PdfObject fromString(const QByteArray& bytes, bool hex = false);
    /// Chaîne de texte : ASCII tel quel, sinon UTF-16BE avec marque d'ordre.
    static PdfObject fromText(const QString& text);
    static PdfObject fromRef(PdfRef r);
    static PdfObject makeArray(std::vector<PdfObject> items = {});
    static PdfObject makeDict();

    bool isNull() const { return type == Type::Null; }
    bool isNumber() const { return type == Type::Int || type == Type::Real; }
    bool isName(const char* name = nullptr) const;
    bool isDict() const { return type == Type::Dict || type == Type::Stream; }
    double number(double fallback = 0) const;
    qint64 toInt(qint64 fallback = 0) const;
    /// Valeur d'une clé (dictionnaire ou flux) ; nul si absente.
    const PdfObject* find(const QByteArray& key) const;
    PdfObject value(const QByteArray& key) const;
    void set(const QByteArray& key, PdfObject v);
    void remove(const QByteArray& key);
    /// Chaîne de texte décodée (UTF-16BE, UTF-8 avec marque, ou PDFDocEncoding).
    QString text() const;
};

/// Écriture d'un objet en syntaxe PDF.
QByteArray serialize(const PdfObject& o);

/// Analyse d'objets dans un tampon : utilisé par le lecteur de fichier et les tests.
class PdfParser {
public:
    explicit PdfParser(const QByteArray& data, qsizetype pos = 0) : m_data(data), m_pos(pos) {}
    /// Objet suivant ; `lengthResolver` donne la longueur d'un flux dont /Length est une référence.
    std::optional<PdfObject> parse(const std::function<qint64(PdfRef)>& lengthResolver = {});
    /// « N G obj … endobj » à la position courante.
    std::optional<std::pair<PdfRef, PdfObject>> parseIndirect(const std::function<qint64(PdfRef)>& lengthResolver = {});
    qsizetype pos() const { return m_pos; }
    void setPos(qsizetype p) { m_pos = p; }
    void skipSpace();
    /// Mot-clé suivant (« xref », « trailer », « obj »…) sans avancer si `peek`.
    QByteArray keyword(bool peek = false);
    bool atEnd() const { return m_pos >= m_data.size(); }

private:
    std::optional<PdfObject> parseString();
    std::optional<PdfObject> parseHexString();
    PdfObject parseName();
    std::optional<PdfObject> parseNumberOrRef();

    const QByteArray m_data;  ///< copie partagée (sans recopie des octets) : un temporaire peut être analysé
    qsizetype m_pos;
    int m_depth = 0;
};

/// Fichier PDF chargé en mémoire : table des références (tables classiques, flux de références, flux d'objets,
/// révisions successives), objets à la demande et liste des pages. Reconstruit la table en parcourant le fichier
/// si elle est absente ou abîmée.
class PdfFile {
public:
    static Result<PdfFile> load(const QByteArray& data);

    const QByteArray& data() const { return m_data; }
    const PdfObject& trailer() const { return m_trailer; }
    bool encrypted() const { return m_trailer.find("Encrypt") != nullptr; }
    bool usesXrefStream() const { return m_xrefStream; }
    qint64 lastXref() const { return m_lastXref; }
    /// Plus grand numéro d'objet + 1 (taille de la table).
    int size() const { return m_size; }

    /// Objet indirect (nul s'il n'existe pas).
    PdfObject object(int num) const;
    /// Suit les références jusqu'à un objet direct.
    PdfObject resolve(const PdfObject& o) const;
    /// Données décodées d'un flux (FlateDecode avec prédicteur PNG, ou sans filtre) ; vide si non pris en charge.
    QByteArray decodeStream(const PdfObject& stream) const;
    /// Pages dans l'ordre : référence et dictionnaire (attributs hérités recopiés : MediaBox, CropBox, Rotate).
    struct Page {
        PdfRef ref;
        PdfObject dict;
    };
    const std::vector<Page>& pages() const { return m_pages; }

private:
    struct Entry {
        int type = 0;  ///< 1 : à un décalage, 2 : dans un flux d'objets
        qint64 offset = 0;
        int gen = 0;
        int stream = 0;
        int index = 0;
    };
    bool readXrefChain(qint64 start);
    bool readXrefTable(qint64 offset, PdfObject& trailer);
    bool readXrefStream(qint64 offset, PdfObject& trailer);
    void rebuildXref();
    void collectPages(const PdfObject& node, PdfRef ref, const PdfObject& inherited, int depth);
    qint64 streamLength(PdfRef r) const;

    QByteArray m_data;
    QHash<int, Entry> m_xref;
    PdfObject m_trailer;
    bool m_xrefStream = false;
    qint64 m_lastXref = 0;
    int m_size = 0;
    std::vector<Page> m_pages;
    mutable QHash<int, std::vector<std::pair<int, PdfObject>>> m_objectStreams;
};

/// Décodage FlateDecode (zlib) ; vide en cas d'erreur.
QByteArray inflate(const QByteArray& data);
/// Annule un prédicteur PNG (10 à 15) sur des lignes de `columns` octets.
QByteArray undoPngPredictor(const QByteArray& data, int columns, int colors = 1, int bitsPerComponent = 8);

}  // namespace studylab::pdf
