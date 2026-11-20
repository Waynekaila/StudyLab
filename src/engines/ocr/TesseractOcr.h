#pragma once

#include "core/Result.h"
#include "engines/ocr/OcrPage.h"

#include <QImage>
#include <QString>
#include <QStringList>

namespace studylab {

/// Reconnaissance de texte par Tesseract 5 (03-comparaison §3.7), module facultatif : la bibliothèque est chargée
/// à l'exécution (QLibrary, API C de Tesseract). Absente, l'application fonctionne sans OCR et rien n'est proposé.
///
/// Recherchée, dans l'ordre : variable STUDYLAB_TESSERACT (chemin de la bibliothèque), dossier de l'application,
/// puis les emplacements usuels (libtesseract.so.5 sous Linux ; « Tesseract-OCR » de Program Files sous Windows).
/// Les modèles de langue (*.traineddata) sont lus dans TESSDATA_PREFIX ou les dossiers tessdata habituels.
///
/// Une instance n'est utilisée que par un seul thread à la fois ; plusieurs instances peuvent coexister.
class TesseractOcr {
public:
    /// Bibliothèque trouvée et au moins un modèle de langue installé.
    static bool isAvailable();
    /// Version de Tesseract (« 5.3.0 »), vide si la bibliothèque est absente.
    static QString version();
    /// Dossier tessdata retenu (vide si aucun).
    static QString dataPath();
    /// Codes des langues installées (« eng », « fra »…), triés ; sans « osd ».
    static QStringList installedLanguages();
    /// Langues par défaut parmi celles installées : français et anglais si présents, sinon la première.
    static QStringList defaultLanguages();
    /// Pourquoi l'OCR n'est pas disponible (message destiné à l'utilisateur), vide s'il l'est.
    static QString unavailableReason();

    TesseractOcr();
    ~TesseractOcr();
    TesseractOcr(const TesseractOcr&) = delete;
    TesseractOcr& operator=(const TesseractOcr&) = delete;

    /// Prépare la reconnaissance pour des langues installées (ex. {"fra", "eng"}).
    Result<void> init(const QStringList& languages);
    /// Reconnaît les mots d'une image (convertie en niveaux de gris) ; `dpi` : résolution du rendu.
    Result<OcrPage> recognize(const QImage& image, int dpi);

private:
    void* m_api = nullptr;
};

}  // namespace studylab
