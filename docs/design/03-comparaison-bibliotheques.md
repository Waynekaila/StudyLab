# 3. Comparaison des bibliothèques C++ et Qt

Critères : **licence** (compatibilité avec une distribution libre ou propriétaire de StudyLab), **maintenance**, **performances**, **disponibilité sur Debian 13 et Windows x64**, **fonctionnalités utiles** à StudyLab.

> Contrainte de licence retenue : n'utiliser que des dépendances **permissives ou LGPL en liaison dynamique**. Ainsi le choix de la licence de StudyLab (GPL, MIT, propriétaire…) reste libre. Une dépendance GPL ou AGPL imposerait sa licence à tout StudyLab.
>
> Les informations de licence sont celles connues à la date de rédaction ; elles seront revérifiées (fichiers `LICENSE` des versions exactes) au moment d'intégrer chaque dépendance, et consignées dans `THIRD_PARTY_LICENSES.md`.

## 3.1 Moteur PDF

| | **Qt PDF** (QtPdf) | Poppler (poppler-qt6) | PDFium (direct) | MuPDF |
|---|---|---|---|---|
| Licence | LGPLv3 / GPL / commerciale | **GPL-2.0+** | BSD-3 / Apache-2.0 | **AGPLv3** / commerciale |
| Base | PDFium (Google) embarqué par Qt | Propre (héritier de Xpdf) | Moteur de Chrome | Artifex |
| Maintenance | Qt (LTS) | Très active (Okular, Papers) | Très active (Chrome) | Très active |
| Rendu / vitesse | Bon (PDFium) | Bon, un peu plus lent sur gros fichiers | Très bon | Excellent |
| Texte + géométrie (sélection, offsets) | Oui (`getSelection`, `getSelectionAtIndex`, `QPdfSearchModel`) | Oui, très complet | Oui, très complet | Oui |
| Lire/écrire les annotations du fichier | **Non** | Oui | Oui | Oui |
| Table des matières, liens | Oui (`QPdfBookmarkModel`, `QPdfLinkModel`) | Oui | Oui | Oui |
| Debian 13 | Paquets Qt 6.8 (source qt6-webengine) | Oui | Pas de paquet officiel | Oui |
| Windows | Fourni avec Qt | Compilation via vcpkg, lourde | Binaires précompilés tiers ou build `gn` complexe | Compilation simple |
| Intégration Qt | Native (`QPdfDocument`, `QPdfView`) | Bonne | Manuelle | Manuelle |

**Recommandation : Qt PDF** pour le MVP et la V1.
- Licence compatible, aucune dépendance à compiler, même moteur (PDFium) sur les deux OS, intégré à Qt 6.8 LTS.
- StudyLab stocke ses annotations **en base**, pas dans le PDF : l'absence d'écriture d'annotations dans Qt PDF n'est pas bloquante.
- Limites assumées et plan : *importer* les annotations déjà présentes dans un PDF (faites avec un autre logiciel) et *exporter une copie annotée* sont prévus en V2 via **PDFium en direct** (même moteur, licence BSD), isolé derrière l'interface `IPdfAnnotationIO`. *Réalisé autrement en V2-5* : Qt PDF n'expose pas les fonctions de PDFium, StudyLab lit et écrit lui-même les objets d'annotation (`engines/pdf/PdfObjects`, `PdfAnnotationIO`) ; voir 09-ecarts.
- Poppler et MuPDF sont écartés pour des raisons de licence (GPL / AGPL), pas de qualité.

## 3.2 Moteur EPUB

Un EPUB est un ZIP de pages XHTML + CSS. Il faut un vrai moteur HTML/CSS pour respecter la mise en page des livres (tableaux, images, MathML).

| | **QtWebEngine + foliate-js** | QTextBrowser | litehtml + rendu Qt | MuPDF (EPUB) | Readium C++ SDK |
|---|---|---|---|---|---|
| Licence | LGPLv3 + **MIT** | LGPLv3 | BSD-3 | AGPLv3 | BSD-3 |
| Fidélité CSS | Complète (Chromium) | Sous-ensemble CSS 2 limité | CSS 2 partiel | Moyenne | Bonne |
| MathML, SVG | Oui | Non | Non | Partiel | Oui |
| Pagination, CFI, recherche | foliate-js les fournit (paginateur en colonnes CSS, CFI EPUB, recherche) | À tout écrire | À tout écrire | Pagination fixe | Oui |
| Maintenance | Qt LTS + foliate-js actif (utilisé par Foliate) | Qt | Active | Active | **Abandonné** |
| Coût | ~100 Mo installés, démarrage ~300 ms | Léger | Léger | Moyen | — |

**Recommandation : QtWebEngine (`QWebEngineView`) + foliate-js**, version figée et embarquée dans les ressources.
- Les fichiers du livre sont servis depuis C++ par un `QWebEngineUrlSchemeHandler` (`studylab-epub://<uuid>/OEBPS/ch05.xhtml`) lisant directement le ZIP : pas d'extraction sur disque, pas d'accès réseau (WebEngine configuré hors ligne, JavaScript du livre désactivé).
- Le pont C++ ↔ JavaScript passe par `QWebChannel` : sélection → CFI + texte, création d'annotation, surlignage à l'affichage, saut à une ancre.
- Thèmes de lecture, police, interligne, marges : injectés en CSS depuis les préférences.
- WebEngine sert **aussi** à l'aperçu Markdown (même dépendance, pas de doublon).
- Risque : l'API de foliate-js n'est pas déclarée stable → on l'isole derrière un petit adaptateur JS à nous (`studylab-reader.js`) ; plan B : notre propre paginateur en colonnes CSS (~600 lignes JS) et la bibliothèque CFI d'epub.js (BSD-2).

Lecture de la structure EPUB en C++ (métadonnées OPF, table des matières, texte pour l'index) : **libzip** (BSD-3) + `QXmlStreamReader`.

## 3.3 Archive ZIP

| | **libzip** | minizip-ng | QuaZip | KArchive |
|---|---|---|---|---|
| Licence | BSD-3 | zlib | LGPL-2.1 | LGPL-2.0+ |
| Debian 13 | `libzip-dev` | `libminizip-ng-dev` | `libquazip1-qt6-dev` | `libkf6archive-dev` |
| Windows (vcpkg) | Oui | Oui | Oui | Oui (plus lourd) |
| API | C simple, lecture aléatoire | C | Qt (QIODevice) | Qt |

**Recommandation : libzip** (permissive, simple, lecture aléatoire d'une entrée sans tout décompresser). QuaZip serait aussi acceptable.

## 3.4 Base de données

| | Qt SQL (pilote QSQLITE) | **SQLite amalgamation embarquée + wrapper RAII maison** | SQLiteCpp |
|---|---|---|---|
| Licence | LGPLv3 (SQLite domaine public) | Domaine public | MIT |
| FTS5 garanti | Dépend de la compilation du pilote (Debian utilise la SQLite système, Windows celle de Qt) : à vérifier au démarrage | **Oui**, on compile avec `SQLITE_ENABLE_FTS5` | Selon SQLite liée |
| Même version sur les 2 OS | Non | **Oui** | Selon liaison |
| API backup, fonctions personnalisées, `sqlite3_progress_handler` | Via `driver()->handle()` (fragile) | Directes | Oui |
| Déploiement | Plugin `sqldrivers/` à déployer | Lié statiquement | Lié |
| Confort | `QSqlQuery`, intégration modèles | Wrapper ~400 lignes (`Statement`, `Transaction`, liaison de `QString`/`QByteArray`) | Bon |

**Recommandation : SQLite embarqué (amalgamation 3.4x, figée) + wrapper maison minimal.** C'est la base de toutes les données : on veut exactement la même SQLite, avec FTS5, JSON et l'API de sauvegarde, sur Windows et Debian. Qt SQL n'apporte que du confort, que le wrapper fournit.

## 3.5 Recherche plein texte

| | **SQLite FTS5** | Xapian | Lucene++ / CLucene |
|---|---|---|---|
| Licence | Domaine public | GPL-2.0+ | Apache/LGPL, maintenance faible |
| Intégration | Dans la base, transactionnelle | Index séparé | Index séparé |
| Performances (10 000 documents, ~5 M pages de texte) | Bonnes avec tokenizer `unicode61 remove_diacritics 2` + `prefix='2 3'` | Excellentes | Bonnes |
| Classement | `bm25()` pondérable par colonne | BM25 | TF-IDF |

**Recommandation : FTS5.** Une seule base, une seule transaction, accents ignorés (« entropie » trouve « Entropie », « énergie » trouve « energie »), extraits avec `snippet()`. Xapian est écarté (GPL, deuxième index à synchroniser).

## 3.6 Markdown, formules et code

| Besoin | Candidats | Choix | Licence |
|---|---|---|---|
| Analyse Markdown (liens `[[…]]`, tags, export) | **md4c**, cmark-gfm, Markdown intégré à `QTextDocument` | **md4c** : rapide, GFM (tableaux, cases), `MD_FLAG_WIKILINKS`, `MD_FLAG_LATEXMATHSPANS`. C'est déjà le parseur interne de Qt. | MIT |
| Éditeur | `QPlainTextEdit` + `QSyntaxHighlighter` maison, QScintilla (GPL), KTextEditor (LGPL, lourd) | **`QPlainTextEdit` + surligneur maison** (titres, emphase, liens, formules, code) | — |
| Aperçu | `QTextBrowser`, **QWebEngine** | **QWebEngine** (déjà présent pour EPUB), HTML produit par md4c | LGPLv3 |
| Formules dans l'aperçu | **KaTeX**, MathJax | **KaTeX** (rapide, hors ligne, polices incluses) | MIT |
| Formules natives (cartes mémoire hors WebEngine) | MicroTeX | MicroTeX, à évaluer en V1 ; sinon cartes rendues via WebEngine | MIT |
| Coloration du code | **KSyntaxHighlighting**, highlight.js | KSyntaxHighlighting dans l'éditeur (Debian : `libkf6syntaxhighlighting-dev`), highlight.js dans l'aperçu | MIT / BSD-3 |

## 3.7 OCR

| | **Tesseract 5** (+ Leptonica) | OCRmyPDF | PaddleOCR |
|---|---|---|---|
| Licence | Apache-2.0 (+ BSD-2) | MPL-2.0 (Python) | Apache-2.0 (Python, lourd) |
| Intégration C++ | API C++ native | Processus externe | Non |
| Langues | fra, eng, deu… (`traineddata` séparés) | Via Tesseract | — |
| Debian 13 | `libtesseract-dev`, `tesseract-ocr-fra` | Oui | Non |

**Recommandation : Tesseract 5**, chargé **dynamiquement comme module optionnel** (`studylab-ocr`) : si absent, l'application fonctionne sans OCR. Le texte reconnu et ses boîtes de mots sont stockés **en base** (table `page_text`), jamais réécrits dans le PDF. La sélection du texte reconnu utilise ces boîtes (même modèle de coordonnées normalisées que les annotations).

## 3.8 Graphe de connaissances

| | **QGraphicsView + force-dirigé maison** | Qt Quick + Shape | OGDF | Graphviz |
|---|---|---|---|---|
| Licence | — | — | GPL | EPL-1.0 |
| Interactif (glisser, zoom, survol) | Oui | Oui | Mise en page seulement | Statique |
| Volume | Barnes-Hut dans un thread : ~5 000 nœuds fluides | Similaire | — | — |

**Recommandation : implémentation maison** (Fruchterman-Reingold + approximation Barnes-Hut, ~500 lignes) calculée hors thread UI, rendue dans `QGraphicsView`. Pas de dépendance.

## 3.9 Répétition espacée

| | SM-2 | **FSRS** |
|---|---|---|
| Statut | Algorithme public (SuperMemo 1987), utilisé historiquement par Anki | Algorithme ouvert récent, défaut d'Anki depuis 23.10, implémentations de référence MIT |
| Qualité | Correcte | Meilleure (≈20–30 % de révisions en moins à rétention égale selon ses auteurs) |
| Complexité | ~50 lignes | ~300 lignes, paramètres par défaut publiés |

**Recommandation** : interface `IScheduler`, **SM-2 d'abord** (simple, prévisible), **FSRS en V2** sélectionnable dans les paramètres. Le journal des révisions est conservé intégralement pour permettre de basculer et de recalculer.

## 3.10 Packaging et outillage

| Besoin | Choix | Licence | Remarque |
|---|---|---|---|
| Dépendances Windows | **vcpkg** (mode manifeste : libzip, md4c, tesseract…) ; Qt via **aqtinstall** ou l'installeur officiel | MIT | Compiler Qt WebEngine via vcpkg est trop long : on utilise les binaires Qt officiels MSVC 2022 x64 |
| Dépendances Debian | Paquets de Debian 13 (Qt 6.8.2) | — | Script `scripts/install-deps-debian.sh` |
| Déploiement Windows | `windeployqt` (copie Qt, WebEngine, plugins) | — | |
| Installateur Windows | **Inno Setup 6** | Licence Inno Setup (permissive, gratuite) | Simple, installation par utilisateur sans droits admin possible. Alternative : NSIS (zlib) via CPack |
| Paquet Debian | Dossier `debian/` + `dpkg-buildpackage` (debhelper) | — | `dh_shlibdeps` calcule les dépendances exactes ; fichier `.desktop`, icônes, AppStream metainfo |
| Analyse | clang-tidy, clang-format, AddressSanitizer/UBSan (Debian) | — | En CI |

## 3.11 Synthèse des dépendances retenues

| Dépendance | Rôle | Licence | Obligatoire |
|---|---|---|---|
| Qt 6.8 LTS (Core, Gui, Widgets, Concurrent, Pdf, PdfWidgets, WebEngineWidgets, WebChannel, Svg) | Socle | LGPLv3 | Oui |
| SQLite 3 (amalgamation, FTS5, JSON) | Données, recherche | Domaine public | Oui |
| libzip (+ zlib) | EPUB | BSD-3 / zlib | Oui |
| md4c | Markdown | MIT | Oui |
| foliate-js (figé) | Rendu EPUB | MIT | Oui |
| KaTeX (figé) | Formules | MIT | Oui |
| highlight.js (figé) | Code dans l'aperçu | BSD-3 | Oui |
| Polices Inter, Literata, JetBrains Mono, KaTeX | Typographie | OFL-1.1 | Oui |
| KSyntaxHighlighting | Coloration dans l'éditeur | MIT | Non (V1) |
| Tesseract 5 + Leptonica | OCR | Apache-2.0 / BSD-2 | Non (module) |
| PDFium (direct) | Import/export d'annotations PDF | BSD-3 / Apache-2.0 | Non (V2) |

Aucune dépendance GPL ou AGPL : la licence de StudyLab reste au choix de l'auteur. Obligation LGPL : lier Qt dynamiquement (cas par défaut sur les deux OS) et fournir les mentions de licence.
