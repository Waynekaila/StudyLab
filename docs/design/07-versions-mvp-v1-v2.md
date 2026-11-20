# 7. Découpage des versions : MVP, V1, V2

Principe : chaque version est **utilisable au quotidien** et testée sur Windows 11 et Debian 13 avant de passer à la suivante. Une fonctionnalité qui n'est pas finie n'apparaît pas dans l'interface (pas de bouton factice).

Correspondance avec les phases demandées : MVP = Phase 2 ; V1 = Phase 3 ; V2 = Phase 4 ; la Phase 5 (finalisation) est répartie : une partie à chaque version (packaging, tests), le reste en fin de V2.

## MVP — « Lire, annoter, commenter, exporter » (Phase 2)

| # | Fonction | Contenu précis | Critère d'acceptation |
|---|---|---|---|
| 1 | Interface principale | Fenêtre, navigation latérale (Accueil, Bibliothèque, Lecture, Paramètres ; les autres entrées masquées tant qu'elles n'existent pas), thème clair/sombre/système, jetons de design, échelle d'interface | Les 4 écrans du MVP correspondent aux maquettes ; bascule de thème sans redémarrage |
| 2 | Bibliothèque | Grille + liste, panneau de métadonnées éditable, statuts, favoris, tags, tri, filtres (statut, format, tag, favoris), recherche par titre/auteur | 5 000 documents : défilement fluide, filtre < 100 ms |
| 3 | Import PDF/EPUB | Fichier, multiple, dossier récursif, glisser-déposer ; SHA-256 et doublons ; vérification (fichier lisible, non chiffré ou mot de passe demandé) ; métadonnées (PDF Info/XMP, OPF) ; couverture (1ʳᵉ page / image OPF) ; mode copie ou lien | Import de 500 fichiers en arrière-plan sans geler l'interface ; rapport final |
| 4 | Lecteur PDF | Rendu par tuiles asynchrone, défilement continu, zoom, ajuster largeur/page, rotation, double page, miniatures, sommaire, liens internes, recherche, sélection et copie, signets, plein écran, sans distraction, onglets, historique (précédent/suivant), reprise de position | PDF de 1 500 pages ouvert < 1 s, défilement 60 i/s sur machine moyenne |
| 5 | Lecteur EPUB | WebEngine + foliate-js, sommaire, paginé/défilement, police, taille, interligne, marges, 4 thèmes, recherche, sélection, reprise de position | EPUB 3 et EPUB 2 de test affichés correctement, position restaurée au caractère près |
| 6 | Annotations | Surlignage, soulignement, barré, note contextuelle, marqueur, signet ; 5 couleurs ; modification, suppression (annulable), ancrage robuste (§2.6) | Annotations intactes après redémarrage, zoom, redimensionnement, rotation, changement de police EPUB |
| 7 | Collecte automatique | Volet Annotations : apparition immédiate, groupes par chapitre, filtres couleur/type, recherche, navigation vers le passage, export individuel ou groupé | Test UI : création → ligne visible dans le volet en < 50 ms |
| 8 | Volet Commentaires | Éditeur Markdown + aperçu (KaTeX), commentaire libre / lié à une annotation / à un passage / à une page, types (question, explication, réflexion, référence), liens `[[…]]` conservés en texte | Brouillon jamais perdu (sauvegarde 2 s + fermeture brutale testée) |
| 9 | Sauvegarde locale | SQLite WAL, migrations, sauvegarde automatique quotidienne + avant migration, restauration depuis Paramètres, contrôle d'intégrité au démarrage | Test : `kill -9` pendant une écriture → base intègre au redémarrage |
| 10 | Export Markdown | Par document (annotations, commentaires, combinés), par sélection, bibliothèque entière ; structure du §6 ; YAML ; CSV des annotations | Le dossier exporté s'ouvre dans Obsidian, liens et tags reconnus |
| + | Recherche (minimale) | FTS5 sur métadonnées, annotations, commentaires (le texte intégral des documents arrive en V1) | |
| + | Distribution | Installateur Windows (Inno Setup) et `.deb` Debian 13 dès le MVP, CI sur les deux OS | Installation propre sur une VM vierge de chaque OS |

Tests MVP : unitaires (locators, repositories, migrations, export), intégration (import → annotation → export), UI (volets), robustesse (fermeture brutale), performance (bibliothèque 5 000, PDF 1 500 pages).

## V1 — « Relier ses connaissances » (Phase 3)

- **Notes Markdown** : espace de notes complet (arborescence, éditeur + aperçu, modèles par type de note, front matter YAML, images collées, plan).
- **Création de notes depuis la lecture** : depuis une annotation, une sélection d'annotations, un commentaire, un document entier ; provenance conservée.
- **Relations** : `[[liens]]` avec autocomplétion, rétroliens, liens non résolus, alias, liens entre notes, documents, annotations, matières.
- **Matières** hiérarchiques avec page dédiée ; **collections** manuelles, listes de lecture, projets, dossiers virtuels, **collections intelligentes** (éditeur de règles).
- **Graphe de connaissances** interactif (filtres, voisinage, profondeur).
- **Recherche universelle** : texte intégral des PDF/EPUB indexé en tâche de fond, notes, tags, références ; résultats groupés, extraits, suggestions, palette Ctrl K.
- **Références bibliographiques** : fiche CSL-JSON, clé de citation, export BibTeX, insertion `[[@clé, p. 12]]` dans les notes.
- **Import Markdown** (coffre Obsidian ou dossier) avec résolution des liens.
- **Formats secondaires** : TXT et Markdown en lecture via l'adaptateur `IDocumentEngine`.
- **OCR facultatif** (Tesseract) : file de traitement en arrière-plan, recherche et sélection dans les PDF scannés.
- Export HTML et PDF (d'une note ou d'une matière, via WebEngine `printToPdf`).

## V2 — « Apprendre et consolider » (Phase 4 + finalisation)

- **Cartes mémoire** : création depuis annotation, définition, formule, question, note ; types basique, inversée, texte à trous, formule.
- **Répétition espacée** : SM-2 puis **FSRS** au choix, paramètres par paquet, historique complet.
- **Fiches de révision** générées depuis annotations/notes sous forme de **brouillon à valider**.
- **Tableau de bord complet** (objectifs, progression par matière, révisions du jour) et écran **Activité** avec statistiques facultatives (temps, pages, annotations, activité hebdomadaire).
- **Interopérabilité PDF avancée** (PDFium direct) : importer les annotations existantes d'un PDF, exporter une **copie** annotée.
- **Formats** : DJVU (via libdjvulibre, GPL : à isoler en greffon séparé ou à écarter, décision à prendre), DOCX (conversion en lecture seule), images.
- Système de **greffons** d'adaptateurs de formats (`QPluginLoader`) une fois l'interface `IDocumentEngine` stabilisée.
- Navigation configurable (réordonner/masquer), raccourcis personnalisables.
- **Finalisation** : profilage, optimisation mémoire, campagne de tests sur gros volumes, guide utilisateur complet, documentation technique, guide de dépannage.

## Plan de développement indicatif du MVP (itérations de 2 semaines)

| Itération | Livrable vérifiable |
|---|---|
| 1 | Squelette CMake + CI Windows/Debian, `core`, wrapper SQLite, migrations, thème et fenêtre principale vide |
| 2 | Bibliothèque : import, doublons, métadonnées, couvertures, grille/liste, panneau de détail |
| 3 | Lecteur PDF : rendu tuilé, navigation, zoom, miniatures, sélection, recherche, reprise de position |
| 4 | Annotations PDF : modèle d'ancrage, création/édition, volet Annotations, export Markdown d'un document |
| 5 | Lecteur EPUB : package, schéma `studylab-epub://`, foliate-js, réglages typographiques, annotations EPUB (CFI) |
| 6 | Volet Commentaires (éditeur + aperçu KaTeX), recherche FTS minimale, sauvegarde/restauration |
| 7 | Export complet, accueil simplifié, installateurs, campagne de tests, corrections → **MVP 0.1** |

## Plan de développement indicatif de la V1

Même rythme que le MVP : une PR par itération, testée sur les deux OS par la CI, rien dans l'interface qui ne soit terminé.

| Itération | Livrable vérifiable |
|---|---|
| V1-1 ✅ | **Espace de notes** : arborescence par dossiers virtuels, 8 types avec modèles, éditeur Markdown (Source / Partagé / Aperçu), front matter YAML, images collées, plan, `[[liens]]` entre notes, rétroliens et liens non résolus (« créer ») |
| V1-2 ✅ | Notes depuis la lecture (annotation, sélection d'annotations, commentaire, document) avec provenance ; autocomplétion des `[[liens]]` ; liens vers documents et annotations ; notes dans l'export Markdown |
| V1-3 ✅ | Matières hiérarchiques (page dédiée), collections manuelles et intelligentes |
| V1-4 ✅ | Graphe de connaissances interactif |
| V1-5 ✅ | Recherche universelle : texte intégral des PDF/EPUB indexé en tâche de fond, notes, palette Ctrl K enrichie |
| V1-6 ✅ | Références bibliographiques (CSL-JSON, BibTeX, `[[@clé]]`), import d'un coffre Obsidian, lecture TXT/Markdown |
| V1-7 ✅ | Export HTML/PDF d'une note ou d'une matière, OCR facultatif (Tesseract) → **V1 0.2.0** (2026-11-20) |

## Plan de développement indicatif de la V2

| Itération | Livrable vérifiable |
|---|---|
| V2-1 ✅ | **Cartes mémoire et révisions** : paquets, cartes basiques / inversées / à trous / formules, création depuis une annotation ou une note, séance de révision au clavier, SM-2, statistiques du jour et des 7 jours, pastille de navigation |
| V2-2 ✅ | **FSRS** au choix par paquet, réglages par paquet (limites, étapes), historique d'une carte |
| V2-3 ✅ | **Fiches de révision** : brouillon de note généré depuis les annotations et notes d'une matière, à relire |
| V2-4 ✅ | **Tableau de bord** complet (objectifs, progression par matière) et écran **Activité** |
| V2-5 ✅ | **Interopérabilité PDF** : import des annotations d'un PDF (bandeau à l'ouverture, sans doublon), export d'une copie annotée lisible partout |
| V2-6 ✅ | Formats supplémentaires (images, DOCX en lecture) ; navigation et raccourcis configurables |
| V2-7 ✅ | Finalisation : profilage, gros volumes, guide utilisateur, dépannage → **V2 1.0.0** (2026-11-20) |
