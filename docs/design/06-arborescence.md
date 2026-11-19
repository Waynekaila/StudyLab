# 6. Arborescence du projet

```
StudyLab/
├── CMakeLists.txt                  # projet racine (C++20, options STUDYLAB_WITH_OCR, STUDYLAB_BUILD_TESTS…)
├── CMakePresets.json               # presets : windows-msvc-debug/release, debian-gcc-debug/release, asan
├── vcpkg.json                      # dépendances Windows (libzip, md4c, tesseract optionnel)
├── LICENSE
├── README.md
├── THIRD_PARTY_LICENSES.md
├── .clang-format / .clang-tidy / .editorconfig
├── .github/workflows/
│   ├── build-windows.yml
│   └── build-debian.yml            # conteneur debian:trixie
│
├── cmake/
│   ├── StudyLabCompilerOptions.cmake   # warnings, -Werror en CI, sanitizers
│   ├── StudyLabDeploy.cmake            # windeployqt, install()
│   └── FindTesseract.cmake
│
├── third_party/                    # sources figées, versions consignées
│   ├── sqlite/                     # sqlite3.c / sqlite3.h (amalgamation) + CMakeLists (FTS5, JSON)
│   └── web/                        # foliate-js, KaTeX, highlight.js (copies figées)
│
├── src/
│   ├── app/                        # main.cpp, Application, composition des services
│   ├── core/                       # → studylab_core
│   │   ├── domain/                 # Document, Annotation, Comment, Note, Card, Subject, Collection…
│   │   ├── locator/                # Locator, PdfLocator, EpubLocator, TextQuote, sérialisation JSON
│   │   ├── Result.h  Uuid.h  Clock.h  Log.h
│   │   └── EventBus.h/.cpp         # signaux de domaine (annotationCreated…)
│   ├── data/                       # → studylab_data
│   │   ├── sqlite/                 # Database, Statement, Transaction (wrapper RAII)
│   │   ├── migrations/             # 0001_initial.sql, … + Migrator.cpp
│   │   └── repositories/           # DocumentRepository, AnnotationRepository, NoteRepository…
│   ├── engines/
│   │   ├── document/               # IDocumentEngine, DocumentEngineRegistry (formats = adaptateurs)
│   │   ├── pdf/                    # QtPdfEngine, PageRenderer (tuiles, cache), TextGeometry
│   │   ├── epub/                   # EpubPackage (libzip + OPF/NCX), EpubSchemeHandler, EpubBridge (QWebChannel)
│   │   │   └── web/                # studylab-reader.js, reader.css (adaptateur foliate-js)
│   │   ├── text/                   # TXT / Markdown (V1)
│   │   └── ocr/                    # TesseractOcr (module optionnel)
│   ├── services/
│   │   ├── library/                # LibraryService, Importer, DuplicateDetector, MetadataExtractor, CoverGenerator, SmartCollectionQuery
│   │   ├── annotation/             # AnnotationService, Reanchorer (ré-ancrage par texte cité)
│   │   ├── comment/                # CommentService
│   │   ├── notes/                  # NoteService, MarkdownParser (md4c), WikiLinkResolver, NoteTemplates
│   │   ├── knowledge/              # LinkService, GraphBuilder, ForceLayout
│   │   ├── study/                  # DeckService, IScheduler, Sm2Scheduler, FsrsScheduler (V2)
│   │   ├── search/                 # SearchService, Indexer (tâche de fond), QueryParser
│   │   ├── io/                     # MarkdownExporter (coffre Obsidian), CsvExporter, HtmlExporter, BibtexExporter, MarkdownImporter
│   │   ├── backup/                 # BackupService (VACUUM INTO, rotation), RestoreService, IntegrityChecker
│   │   └── settings/               # SettingsService (préférences typées)
│   ├── viewmodels/                 # LibraryModel, AnnotationListModel, CommentListModel, NoteTreeModel, ReaderState, UndoCommands…
│   └── ui/
│       ├── theme/                  # Tokens.h, ThemeManager, StudyLabStyle (QProxyStyle), styles.qss.in
│       ├── components/             # Card, Chip, Pill, IconButton, SegmentedControl, Toggle, CollapsiblePane, Toast
│       ├── shell/                  # MainWindow, NavigationRail, CommandPalette, UniversalSearchPopup
│       ├── home/  library/  reader/  notes/  subjects/  collections/  graph/  review/  activity/  settings/
│       └── resources/
│           ├── icons/              # SVG au trait, une seule famille
│           ├── fonts/              # Inter, Literata, JetBrains Mono, KaTeX (OFL)
│           └── studylab.qrc
│
├── tests/
│   ├── unit/                       # core, locator, migrations, repositories, scheduler, markdown, exporters
│   ├── integration/                # import → annotation → export ; ré-ancrage ; sauvegarde/restauration
│   ├── ui/                         # QTest : création d'annotation visible dans le volet, splitters persistants
│   ├── perf/                       # base synthétique 10 000 documents, ouverture PDF 1 500 pages
│   └── fixtures/                   # PDF/EPUB libres de droits (générés ou domaine public), bases de version N-1
│
├── packaging/
│   ├── windows/                    # studylab.iss (Inno Setup), icône .ico, script de signature (optionnel)
│   └── debian/                     # control, rules, changelog, copyright, studylab.desktop, metainfo.xml, icônes hicolor
│
├── scripts/
│   ├── install-deps-debian.sh      # apt install qt6-base-dev qt6-pdf-dev qt6-webengine-dev libzip-dev libmd4c-dev …
│   ├── install-deps-windows.ps1    # aqtinstall Qt 6.8 msvc2022_64 + modules, vcpkg bootstrap, Inno Setup
│   ├── build.sh / build.ps1        # configure + build + tests via presets
│   ├── package-deb.sh              # dpkg-buildpackage
│   └── package-windows.ps1         # windeployqt + iscc
│
├── docs/
│   ├── user-guide/                 # guide utilisateur (FR)
│   ├── technical/                  # architecture, modèle d'ancrage, schéma, ADR (décisions)
│   ├── build.md                    # guide de compilation Windows / Debian
│   ├── troubleshooting.md          # dépannage (WebEngine, GPU, polices, OCR…)
│   └── design/                     # maquettes (ce dossier phase 1)
│
└── examples/
    └── export/                     # exemple réel de coffre Markdown exporté (voir ci-dessous)
```

## Structure d'exportation Markdown (améliorée, compatible Obsidian)

```
StudyLab Export 2026-11-19/
├── README.md                          # date, version, comment ouvrir dans Obsidian
├── Bibliothèque/
│   └── Cengel - Thermodynamique (2019)/
│       ├── Cengel - Thermodynamique (2019).md   # fiche document : YAML (titre, auteurs, isbn, doi, tags, statut, uuid)
│       ├── Annotations.md                         # groupées par chapitre/page, couleur → callout, lien « studylab:// » vers le passage
│       ├── Commentaires.md
│       └── metadata.json                          # métadonnées complètes, machine-lisibles
├── Notes/
│   ├── Thermodynamique/
│   │   ├── Second principe.md                     # [[liens]] et #tags conservés, YAML (type, matière, sources, uuid)
│   │   └── Cycle de Carnot.md
│   └── Boîte de réception/
├── Matières/
│   └── Thermodynamique.md                         # index : documents, notes, cartes (liens [[…]])
├── Collections/
│   └── Prépa examen S5.md
├── Révisions/
│   ├── Thermodynamique.md                         # cartes au format « question :: réponse » (lisible, compatible plugins Obsidian)
│   └── cartes.csv                                 # import Anki possible
├── Références/
│   ├── bibliotheque.bib                           # BibTeX
│   └── bibliotheque.json                          # CSL-JSON
└── _attachments/                                  # images des notes
```

Choix : un dossier par document avec des noms lisibles (et l'`uuid` dans le YAML pour le ré-import sans doublon), des `[[liens]]` résolus par titre de note (comme Obsidian), des citations d'annotations sous forme de *callouts* Obsidian (`> [!quote] p. 412`) qui restent lisibles dans n'importe quel éditeur Markdown.
