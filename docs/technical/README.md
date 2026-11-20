# Documentation technique (version 1.0.0)

Ce document décrit StudyLab **tel qu'il est construit**. La conception d'origine est dans
[`docs/design/`](../design/README.md) ; chaque écart y est consigné dans
[`09-ecarts-implementation.md`](../design/09-ecarts-implementation.md). Les mesures de performances sont dans
[`performances.md`](performances.md), la compilation dans [`../build.md`](../build.md).

## Socle

- C++20, Qt 6 (Widgets, PDF, WebEngine ; pas de Qt SQL), CMake ≥ 3.25 avec presets
  (`debian-debug`, `debian-release`, `debian-asan`, `windows-msvc-debug`, `windows-msvc-release`).
- SQLite embarqué (`third_party/sqlite`, FTS5 et JSON activés), en WAL, `foreign_keys=ON`.
- Aucune exception : toute opération faillible renvoie `studylab::Result<T>` (`src/core/Result.h`).
- Hors ligne : aucune requête réseau ; les livres EPUB sont servis par un schéma d'URL interne, leurs scripts ne
  s'exécutent pas.

## Couches et bibliothèques CMake

Une couche ne dépend que des couches en dessous : `core` → `data` → `engines` → `services` → `ui` → `app`.

| Dossier | Rôle | Classes principales |
|---|---|---|
| `src/core` | Types communs, chemins, horloge, journal, liens `studylab://` | `Result`, `AppPaths`, `Clock`, `AppLink`, `Uuid`, `domain/*` |
| `src/data` | Connexion SQLite, migrations, dépôts (seul endroit où l'on écrit du SQL) | `Database`, `Statement`, `Transaction`, `Migrator`, `*Repository` |
| `src/engines/pdf` | Rendu Qt PDF en arrière-plan, texte, lecteur d'objets PDF, images vues comme pages | `PdfRenderQueue`, `PdfTextExtractor`, `PdfObjects`, `PdfAnnotationIO`, `ImagePages` |
| `src/engines/epub` | Paquet EPUB, TXT/Markdown et DOCX convertis en livre EPUB 3 en mémoire | `EpubPackage`, `TextBook`, `DocxBook`, `BookWriter`, `EpubTextExtractor` |
| `src/engines/ocr` | Tesseract chargé à l'exécution (facultatif) | `TesseractOcr`, `OcrPage` |
| `src/services/library` | Import (tâche de fond), métadonnées, couvertures, doublons | `LibraryService`, `ImportWorker`, `FileInspector` |
| `src/services/annotations`, `comments` | Annotations ancrées, commentaires typés | `AnnotationService`, `CommentService` |
| `src/services/notes` | Notes Markdown, liens `[[…]]`, rétroliens, brouillons, import de coffre | `NoteService`, `NoteDrafts`, `VaultImporter` |
| `src/services/organize` | Matières, collections manuelles et intelligentes | `OrganizeService` |
| `src/services/knowledge` | Données du graphe et disposition force-dirigée | `KnowledgeService`, `ForceLayout` |
| `src/services/search` | Recherche universelle, indexation du texte et OCR en tâche de fond | `SearchService`, `TextIndexer`, `OcrIndexer` |
| `src/services/references` | Références BibTeX / CSL-JSON, citations | `ReferenceService` |
| `src/services/study` | Paquets, cartes, SM-2 et FSRS-5, séances | `StudyService`, `Scheduler` |
| `src/services/activity` | Temps de lecture, pages vues, journal, objectifs | `ActivityService` |
| `src/services/pdfinterop` | Import des annotations d'un PDF, copie annotée | `PdfInteropService` |
| `src/services/export` | Export Markdown / Obsidian d'un document ou de la bibliothèque | `MarkdownExporter`, `LibraryExporter` |
| `src/services/backup`, `settings` | Sauvegardes et rotation ; préférences en base | `BackupService`, `SettingsService` |
| `src/ui/*` | Fenêtre, navigation, écrans, lecteurs, thème | `MainWindow`, `Sidebar`, `Navigation`, `Shortcuts`, `InstanceChannel` (instance unique), `ReaderPage`… |
| `src/app` | `main`, ouverture de la base et composition des services | `Bootstrap` |

## Base de données

- Schéma initial : `src/data/migrations/0001_initial.sql` (reprend [`schema.sql`](../design/schema.sql)).
- Migrations suivantes : `0002_fts_documents_delete.sql`, `0003_cards_study.sql`. Une migration publiée n'est
  jamais modifiée ; toute évolution ajoute un fichier `NNNN_nom.sql` et un test dans `tests/unit/tst_migrator.cpp`.
- Une base créée par une version plus récente est refusée au démarrage (message dédié), plutôt que lue à moitié.
- Les préférences sont des valeurs JSON de la table `settings` (`SettingsService`), par exemple `ui/navigation`
  ou `shortcuts/<id>`.

## Fils d'exécution

- Interface dans le thread principal ; aucune requête longue n'y est lancée sans mesure (voir
  [`performances.md`](performances.md)).
- Rendu PDF : `PdfRenderQueue`, pool dédié, tuiles annulées quand on fait défiler.
- Import, export de la bibliothèque, indexation du texte, OCR et disposition du graphe : threads de travail avec
  leur propre connexion SQLite, résultats renvoyés par signaux.
- QtWebEngine (EPUB, aperçu Markdown, KaTeX) n'est créé qu'au premier affichage d'une vue qui en a besoin.

## Tests

`ctest --preset debian-debug` lance 37 suites QTest (`QT_QPA_PLATFORM=offscreen` pour celles d'interface) :

- `tests/unit` : dépôts, migrations, services, planificateurs, conversion DOCX, objets PDF ;
- `tests/integration` : import, texte intégral, OCR, export de la bibliothèque, import de coffre ;
- `tests/ui` : chaque écran piloté comme un utilisateur, plus `tst_volume`, le banc d'essai gros volume.

`tst_volume` remplit une base de 3 000 documents (60 000 pages de texte, 30 000 annotations, 20 000 cartes…) et
vérifie que chaque requête et chaque écran restent sous un plafond. `STUDYLAB_VOLUME_SCALE=5` multiplie les
volumes (et les plafonds) ; `STUDYLAB_VOLUME_KEEP=/chemin/base.db` garde la base pour l'analyser avec
`EXPLAIN QUERY PLAN`.

## Intégration continue et paquets

`.github/workflows/` compile et teste sur Debian 13 et Windows, construit le `.deb` et l'installateur Inno Setup,
puis les installe, lance l'application et les désinstalle. Le flux *Publication* (lancé à la main) crée l'étiquette
et la version GitHub avec les deux paquets.
