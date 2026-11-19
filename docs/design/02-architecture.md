# 2. Architecture technique proposée

## 2.1 Socle

| Élément | Choix | Raison |
|---|---|---|
| Langage | C++20 | Concepts, `std::span`, `std::expected` (C++23, via `tl::expected` en attendant), coroutines inutiles ici |
| Framework | **Qt 6.8 LTS** | Debian 13 (trixie) fournit Qt 6.8.2 ; même version installable sur Windows (installeur Qt ou aqtinstall). LTS jusqu'en 2029. |
| UI | **Qt Widgets** + thème maison (QStyle proxy + QSS générée depuis des jetons) | Voir 2.3 |
| Build | CMake ≥ 3.25, presets (`CMakePresets.json`) pour MSVC 2022 et GCC 14 | Un seul système pour les deux OS |
| Base | SQLite 3 (via Qt SQL, pilote QSQLITE) + FTS5 | Local, transactionnel, une seule pièce |
| Tests | Qt Test + CTest ; tests d'interface avec `QTest` | Natif Qt, pas de dépendance supplémentaire |
| CI (plus tard) | GitHub Actions : Windows (MSVC) + conteneur Debian 13 | Les deux plateformes testées à chaque commit |

## 2.2 Couches

```
┌───────────────────────────────────────────────────────────────┐
│ app/            point d'entrée, composition (injection manuelle)│
├───────────────────────────────────────────────────────────────┤
│ ui/             Widgets, vues, délégués, thème. AUCUN SQL.     │
│                 ne parle qu'aux services et aux modèles Qt     │
├───────────────────────────────────────────────────────────────┤
│ viewmodels/     QAbstractItemModel adaptés (listes, arbres),   │
│                 état des écrans, commandes (undo/redo)          │
├───────────────────────────────────────────────────────────────┤
│ services/  (logique métier, sans Qt Widgets)                   │
│  Library · Annotation · Comment · Notes · Knowledge · Study ·  │
│  Search · ImportExport · Backup · Settings                      │
├───────────────────────────────────────────────────────────────┤
│ engines/        DocumentEngine (interface) ← PdfEngine,        │
│                 EpubEngine, (TxtEngine, MdEngine…) · OCR        │
├───────────────────────────────────────────────────────────────┤
│ data/           Database (connexion, migrations), Repositories │
│                 (un par agrégat), Unit of Work (transactions)   │
├───────────────────────────────────────────────────────────────┤
│ core/           types de domaine (Document, Annotation, Locator│
│                 …), Result<T>, Uuid, journalisation, événements │
└───────────────────────────────────────────────────────────────┘
```

Règle de dépendance : une couche ne dépend que des couches en dessous. `core` ne dépend que de QtCore. Chaque module est une **bibliothèque CMake statique** (`studylab_core`, `studylab_data`…) testable isolément ; l'exécutable ne fait que les assembler.

## 2.3 Qt Widgets plutôt que Qt Quick (QML) : décision argumentée

| Critère | Qt Widgets | Qt Quick / QML |
|---|---|---|
| Éditeur Markdown confortable (gros textes, coloration, IME) | `QPlainTextEdit` + `QSyntaxHighlighter`, éprouvé | `TextArea` limité sur les gros documents |
| Vues de listes/arbres denses (bibliothèque de 5 000 documents) | `QListView`/`QTreeView` + délégués, très performant | Possible, plus de travail |
| Splitters, docks, raccourcis, menus natifs | Natif | À reconstruire |
| Animations et rendu « doux » | `QPropertyAnimation`, `QGraphicsOpacityEffect`, peinture personnalisée : suffisant pour des transitions sobres | Point fort |
| Intégration lecteur PDF | `QPdfView` (widget) ou rendu maison | `PdfMultiPageView` |
| Risque global | Faible | Moyen |

**Décision** : Qt Widgets, avec un thème cohérent appliqué partout (jetons de couleur → `QPalette` + feuille QSS générée + `QProxyStyle` pour les rayons/focus). Le graphe de connaissances utilise `QGraphicsView`. Les rares zones riches en HTML (EPUB, aperçu Markdown avec formules) utilisent `QWebEngineView`, stylé avec les mêmes jetons injectés en variables CSS.

## 2.4 Modules (les 16 demandés)

| Module | Bibliothèque CMake | Responsabilités principales | Dépend de |
|---|---|---|---|
| Core | `core` | Types de domaine, `Locator`, `Result<T>`, `Uuid`, `EventBus` (signaux de domaine), journalisation | QtCore |
| Database | `data` | Connexion SQLite (WAL, `foreign_keys=ON`), migrations versionnées, repositories, transactions | core, QtSql |
| Settings | `services/settings` | Préférences typées (thème, polices, disposition des volets) stockées en base + `QSettings` pour la géométrie de fenêtre | data |
| Library | `services/library` | Documents, métadonnées, auteurs, statuts, doublons, couvertures, matières, collections (manuelles et intelligentes) | data, engines |
| Document Engine | `engines/document` | Interface commune `IDocumentEngine` : ouvrir, métadonnées, nombre de pages/sections, texte par page/section, rendu, recherche, table des matières | core |
| PDF Engine | `engines/pdf` | Implémentation Qt PDF : rendu de tuiles en arrière-plan, sélection de texte, géométrie des glyphes, recherche | document, QtPdf |
| EPUB Engine | `engines/epub` | Dézippage (`QZipReader`, voir 09-ecarts-implementation.md), OPF/NCX/nav, spine, rendu des chapitres dans QWebEngine, pagination CSS, ancres | document, QtWebEngine |
| Annotation Engine | `services/annotation` | Création/modification, **ancrage et ré-ancrage**, collecte automatique, filtres, export | data, engines |
| Comment Engine | `services/comment` | Commentaires liés (annotation, passage, page, libre), types (question, explication, réflexion) | data |
| Notes Engine | `services/notes` | Notes Markdown, analyse des `[[liens]]` et `#tags` (md4c), rétroliens, modèles de notes, création depuis annotations | data |
| Knowledge Graph | `services/knowledge` | Table `links` unifiée, calcul du graphe (voisinage, filtres), mise en page force-dirigée dans un thread | data |
| Study Engine | `services/study` | Cartes, paquets, planificateur (interface `IScheduler` : SM-2 puis FSRS), sessions, historique | data |
| Search Engine | `services/search` | Index FTS5 (métadonnées, texte des pages, annotations, commentaires, notes), requêtes groupées par catégorie, suggestions | data |
| Import/Export | `services/io` | Import fichiers/dossiers, file d'attente, export Markdown (coffre Obsidian), CSV, HTML, BibTeX, import Markdown | tous les services |
| Backup | `services/backup` | Sauvegarde à chaud (`sqlite3_backup` / `VACUUM INTO`), rotation, restauration, vérification `PRAGMA integrity_check` | data |
| UI | `ui` | Fenêtre principale, navigation, écrans, thème, composants | viewmodels |

## 2.5 Flux clés

### Création d'une annotation (doit apparaître « immédiatement »)

```
Utilisateur sélectionne du texte (PdfPageView)
  → PdfEngine::selectionAt(page, rect) → {texte, quads normalisés, offsets}
  → Commande CreateAnnotation (pile undo)
  → AnnotationService::create(locator, type, couleur)
       └ transaction SQLite (annotation + entrée FTS) ~1 ms
       └ émet EventBus::annotationCreated(id)
  → AnnotationListModel insère la ligne (beginInsertRows)   ← volet supérieur
  → PageOverlay repeint la page concernée                   ← document
```

Pas de rafraîchissement global, pas de requête de rechargement complète.

### Ouverture d'un gros PDF (1 500 pages)

1. `QPdfDocument::load` dans un thread de travail ; l'interface affiche le squelette des pages (tailles connues).
2. Seules les pages visibles ± 2 sont rendues, en tuiles, par un `QThreadPool` dédié avec annulation quand on fait défiler.
3. Cache LRU d'images limité en mémoire (par défaut 256 Mo) ; miniatures en cache disque (`cache/thumbs/<hash>/<page>.webp`).
4. L'extraction de texte pour l'index FTS se fait en tâche de fond basse priorité, une seule fois par empreinte de fichier.

### Démarrage rapide

- Pas de WebEngine au démarrage : il est instancié paresseusement à la première ouverture d'un EPUB ou d'un aperçu Markdown (QtWebEngine coûte ~300 ms et ~100 Mo).
- Migrations SQLite exécutées avant l'affichage, mais ce sont des opérations rapides ; l'affichage de l'accueil lit au plus 6 petites requêtes.
- Objectif mesuré : fenêtre utilisable < 1,5 s à froid sur une machine moyenne avec 5 000 documents.

## 2.6 Modèle d'ancrage des annotations (exigence critique)

Inspiré du modèle W3C Web Annotation (plusieurs sélecteurs redondants) :

```jsonc
// Locator PDF
{
  "kind": "pdf",
  "page": 41,                        // index 0
  "rects": [[0.112,0.304,0.874,0.322], ...], // coordonnées normalisées 0..1
                                     // dans l'espace de la page NON tournée
  "textQuote": { "exact": "L'entropie d'un système isolé…",
                 "prefix": "…ce qui conduit à ", "suffix": " ne peut que croître" },
  "textPosition": { "start": 1832, "end": 1904 } // offsets dans le texte de la page
}

// Locator EPUB
{
  "kind": "epub",
  "spineIndex": 7, "href": "OEBPS/ch05.xhtml",
  "cfi": "epubcfi(/6/16!/4/2[sec5]/10/1:24,/1:96)", // CFI de plage (standard EPUB)
  "textQuote": { "exact": "...", "prefix": "...", "suffix": "..." },
  "textPosition": { "start": 5120, "end": 5192 }, // offsets dans le texte du chapitre
  "progression": 0.4312                            // position globale indicative
}
```

- Les coordonnées normalisées rendent l'ancre **indépendante du zoom, de la taille de fenêtre, des volets et de la rotation d'affichage**.
- Le texte cité + préfixe/suffixe permet la **ré-ancrage** si le fichier change (voir risques, §8).
- L'empreinte SHA-256 du fichier est stockée : si elle change, StudyLab lance la procédure de ré-ancrage et signale les annotations « orphelines » au lieu de les perdre.

## 2.7 Concurrence

- Un seul thread écrit dans SQLite (le thread principal ou un `DbWriter` dédié), les lectures lourdes (recherche, statistiques, graphe) utilisent leur propre connexion en lecture (WAL autorise lecteurs concurrents).
- Traitements longs : `QtConcurrent::run` + `QFutureWatcher`, toujours annulables, avec progression dans la barre d'état.
- Aucune fonction de service appelée depuis l'UI ne fait d'E/S fichier lourde de façon synchrone.

## 2.8 Stockage sur disque

```
Windows : %APPDATA%\StudyLab\        Debian : ~/.local/share/StudyLab/
  studylab.db            base principale (WAL)
  library/               copies gérées des fichiers (option par défaut)
     ab/abcdef….pdf      rangées par empreinte (pas de conflit de noms)
  covers/                couvertures (WebP)
  attachments/           images collées dans les notes
  backups/               sauvegardes horodatées (rotation : 7 jours, 4 semaines, 6 mois)
Cache (régénérable) : %LOCALAPPDATA%\StudyLab\cache  |  ~/.cache/StudyLab
```

Deux modes d'import au choix : **copier dans la bibliothèque** (défaut, robuste) ou **lier le fichier en place** (pour les bibliothèques déjà rangées, avec détection de déplacement par empreinte).

## 2.9 Sûreté des données

- `PRAGMA journal_mode=WAL; synchronous=NORMAL; foreign_keys=ON;`
- Toutes les écritures en transactions courtes ; brouillons de commentaires/notes sauvegardés toutes les 2 s (debounce) et à la perte de focus : aucune perte en cas de plantage.
- Sauvegarde automatique au démarrage (si la dernière date de plus de 24 h) et avant chaque migration de schéma.
- Au démarrage après une fermeture anormale : `PRAGMA quick_check`, proposition de restaurer la dernière sauvegarde si erreur.
- Suppressions « douces » (`deleted_at`) avec corbeille 30 jours pour documents, notes et annotations.
