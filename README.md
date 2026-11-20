# StudyLab

> **Read – Annotate – Understand – Connect – Learn**

StudyLab est une application de bureau (C++20 / Qt 6) pour lire, annoter, commenter, organiser et réviser ses documents PDF et EPUB, et transformer une bibliothèque en base de connaissances personnelle. Hors ligne, sans compte, sans cloud.

Plateformes cibles : **Windows 10/11 x64** et **Debian 13 x64**.

## État du projet

| Phase | Contenu | État |
|---|---|---|
| 1 · Conception | Vision, architecture, choix des bibliothèques, schéma SQLite, maquettes, plan | ✅ Terminée — voir [`docs/design/`](docs/design/README.md) |
| 2 · MVP | Bibliothèque, lecteurs PDF/EPUB, annotations, commentaires, sauvegarde, export Markdown | ✅ Terminée — **version 0.1.0** (7 itérations) |
| 3 · Connaissances (V1) | Notes Markdown, liens, matières, collections, graphe, recherche universelle | 🚧 En cours — itération 6/7 (références bibliographiques) |
| 4 · Apprentissage (V2) | Cartes mémoire, répétition espacée, fiches, tableau de bord | — |
| 5 · Finalisation | Optimisation, packaging, documentation | — |

### Ce qui fonctionne aujourd'hui (MVP 0.1 et début de la V1)

- **Mes notes** (V1) : notes Markdown rangées dans des dossiers virtuels (« Thermodynamique/Cours »), huit types
  avec leur modèle (note de lecture, fiche de cours, concept, formules, fiche de révision, recherche, compte rendu,
  note personnelle), front matter YAML, tags (`tags:` ou `#tag`) cliquables pour filtrer. Éditeur avec coloration et
  aperçu côte à côte (modes Source, Partagé, Aperçu) : formules KaTeX, tableaux, citations, images collées
  (copiées dans le dossier de données, hors ligne). Liens `[[…]]` entre notes (par titre, nom de fichier ou alias),
  rétroliens avec extrait, plan cliquable, liens non résolus avec « créer » ; renommer une note met à jour les liens
  qui la citent. Enregistrement automatique, corbeille avec « Annuler ».
- **Notes depuis la lecture** (V1) : « Créer une note » depuis une annotation, les annotations affichées par les
  filtres du volet, un commentaire ou le document entier. La note reprend les passages en citation, avec l'auteur,
  un `[[lien]]` vers le document et la page reliée au passage ; ses **sources** restent listées à côté et rouvrent
  le passage dans le lecteur. Un `[[lien]]` peut viser un document de la bibliothèque ; taper `[[` propose les
  titres des notes et des documents.
- **Graphe de connaissances** (V1) : documents (rectangles), notes (ronds) et concepts (losanges) reliés par les
  `[[liens]]` et par la provenance des notes de lecture, colorés par matière. Filtres par type et par matière, liens
  non résolus sur demande, profondeur du voisinage (1, 2, 3 ou tout), regroupement par matière ; fiche du nœud
  (extrait, liens sortants, rétroliens, sources) avec Ouvrir, Centrer et Isoler. Disposition calculée en
  arrière-plan, zoom à la molette, nœuds déplaçables.
- **Recherche plein texte** (V1) : le texte des PDF (page par page) et des EPUB (section par section) est extrait
  et indexé en tâche de fond, une seule fois par document, sans gêner la lecture. Ctrl + K cherche désormais aussi
  dans les **notes** et **dans le texte des documents** : un passage ouvre le document à la bonne page ou section,
  le terme cherché mis en évidence. L'état de l'index et « Réindexer » se trouvent dans Paramètres › Données.
- **Références bibliographiques** (V1) : import BibTeX / BibLaTeX et CSL-JSON (Zotero, JabRef, Mendeley…),
  saisie ou fiche tirée d'un document (« Citer » dans la bibliothèque), rattachement automatique au document par
  DOI, ISBN ou titre. Dans une note, `[[@clé]]` cite la référence : l'aperçu affiche « (Çengel et Boles, 2019,
  p. 12) » et ajoute la bibliographie en fin de note ; taper `[[@` propose les clés. Changer une clé met à jour les
  notes qui la citent. Export BibTeX ou CSL-JSON, et `Références.bib` dans l'export Obsidian.
- **Matières et collections** (V1) : matières hiérarchiques (« Physique › Thermodynamique ») avec couleur,
  glisser-déposer pour réordonner ou ranger, archivage ; page de matière avec chiffres clés (documents, annotations,
  notes, progression) et listes, sous-matières comprises. Collections manuelles, listes de lecture et projets
  (documents et notes, dans l'ordre voulu) ; **collections intelligentes** décrites par des règles lisibles
  (« Matière est Thermodynamique · Statut n'est pas Terminé ») avec le nombre de résultats en direct. Rattachement
  depuis le panneau d'un document de la bibliothèque ou par clic droit sur une note.
- **Accueil** : reprendre la lecture à la page exacte, autres lectures en cours, dernières annotations et derniers
  commentaires (un clic ouvre le passage), état de la bibliothèque et de la dernière sauvegarde.
- **Export de la bibliothèque** (entière, documents affichés ou sélection avec Ctrl / Maj) en coffre Obsidian :
  `README.md` avec l'index des documents, un dossier par document (fiche YAML, `Annotations.md`, `Commentaires.md`,
  `metadata.json`), chapitres lus dans le sommaire du PDF ou la table des matières de l'EPUB, et `annotations.csv`
  pour un tableur. En arrière-plan, avec progression et annulation.
- **Liens `studylab://`** dans les notes exportées : un clic dans Obsidian rouvre StudyLab au passage annoté ou
  commenté (une seule instance : le lien s'ouvre dans la fenêtre déjà lancée).
- **Installeurs** : paquet `.deb` pour Debian 13 et installateur Windows 10/11 (sans droits administrateur),
  construits et testés (installation, lancement, désinstallation) par l'intégration continue.
- **Commentaires** (PDF et EPUB) : volet sous les annotations, rédigés en Markdown avec formules LaTeX (`$…$`,
  `$$…$$`, rendu KaTeX hors ligne) et liens `[[…]]`. Quatre types (question, explication, réflexion, référence),
  ancrés à un passage, à une annotation, à une page ou au document entier. Filtres (questions, explications, page ou
  chapitre courant), recherche, aperçu, question marquée résolue, suppression avec « Annuler ». Le brouillon en
  cours survit à une fermeture brutale. Bouton « Commenter » dans la barre flottante de sélection.
- **Recherche universelle** (Ctrl + K) : métadonnées, annotations et commentaires de toute la bibliothèque, avec
  extraits surlignés ; Entrée ouvre le document au bon endroit.
- **Sauvegardes** : automatique chaque jour au démarrage, manuelle depuis *Paramètres › Données*, rotation
  (semaine, mois, six mois ; les manuelles sont gardées), restauration au prochain démarrage ou immédiate avec
  redémarrage, copie de sécurité avant chaque restauration. En cas de base endommagée au démarrage, StudyLab propose
  de restaurer la dernière sauvegarde valide.
- **Export Markdown** : `Commentaires.md` (callouts par type, identifiants `^com-…`) en plus des annotations.
- **Lecteur EPUB** (EPUB 2 et 3) : table des matières avec le chapitre courant en évidence, pages ou défilement
  continu, progression et temps restant estimé dans le chapitre, chapitre précédent/suivant, historique,
  recherche dans tout le livre (accents et casse ignorés). Panneau **Affichage du texte** : police, taille,
  interligne, marges, mise en page et quatre thèmes de page (blanc, sépia, vert d'eau, sombre). Reprise à la
  position exacte (CFI) à la réouverture. Hors ligne : les fichiers sont lus dans l'archive sans extraction,
  aucune requête réseau, les scripts des livres ne s'exécutent pas.
- **Annotations EPUB** : surlignage, soulignement, barré, note et signet, en 5 couleurs, avec la même barre
  flottante, le même volet et le même export Markdown que pour les PDF ; elles restent ancrées après un
  changement de police, de taille ou de mise en page.
- **Annotations PDF** : surlignage, soulignement, barré, zone rectangulaire, note ponctuelle et signet (Ctrl + D),
  en 5 couleurs. Une sélection fait apparaître une barre flottante (couleur, type, note, copier) ; un outil actif
  annote directement. Les annotations restent ancrées au texte quel que soit le zoom, la rotation ou le redémarrage
  (zones normalisées + citation exacte avec contexte). Modification, note, suppression avec « Annuler » (Ctrl + Z).
- **Volet Annotations** : liste groupée par chapitre, recherche (accents ignorés), portée document / page courante,
  filtres par couleur (avec compteurs) et par type ; un clic amène au passage et le fait clignoter.
- **Export Markdown / Obsidian** d'un document : fiche avec en-tête YAML, `Annotations.md` (callouts, identifiants
  de bloc `^ann-…`, groupes par chapitre, signets) et `metadata.json`.
- **Lecteur PDF** : rendu par tuiles en arrière-plan (l'interface ne se bloque jamais, un PDF de 1 500 pages s'ouvre
  en moins d'une seconde), défilement continu, zoom (ajuster à la largeur, page entière, 10 à 800 %, Ctrl + molette),
  rotation, double page, sélection et copie du texte, liens internes (Qt ≥ 6.6), miniatures, sommaire,
  historique précédent/suivant, aller à la page, recherche dans le document avec surlignage.
- **Onglets** : plusieurs documents ouverts, rouverts au lancement suivant ; chaque document reprend à la page,
  au zoom et à la rotation où on l'a laissé. Plein écran (F11) et mode sans distraction. PDF protégés : mot de passe
  demandé à l'ouverture. « Lire » depuis la bibliothèque (double-clic ou bouton du panneau de détail).
- **Bibliothèque** : grille de couvertures ou liste, recherche instantanée (titre, auteur, éditeur, tags, ISBN, DOI ;
  accents ignorés), filtres par statut, format, tag et favoris, tri (récents, ouverts récemment, titre, auteur).
- **Import** de PDF et EPUB : fichiers, dossiers (récursif) ou glisser-déposer n'importe où dans la fenêtre ;
  en arrière-plan avec progression et annulation, détection des doublons par empreinte SHA-256, vérification du
  contenu (faux PDF, EPUB invalide ou protégé par DRM), rapport détaillé. Mode **copie** (par défaut) ou **lien**.
- **Métadonnées** lues dans les fichiers (dictionnaire PDF, OPF EPUB 2/3 : titre, auteurs, éditeur, date, langue,
  ISBN, DOI, sujets) et **couvertures** (première page du PDF, image de couverture de l'EPUB) ; couverture générée
  pour les documents qui n'en ont pas.
- **Panneau de détail** : statut de lecture, favori, modification des métadonnées et des tags, retrait.
- Socle (itération 1) : thème clair / sombre / système, taille d'interface, SQLite avec migrations, contrôle
  d'intégrité, sauvegardes, tests automatisés et intégration continue Windows + Debian 13.

| | |
|---|---|
| ![Bibliothèque](docs/screenshots/iteration2-bibliotheque-grille.png) | ![Bibliothèque sombre](docs/screenshots/iteration2-bibliotheque-sombre.png) |
| ![Lecteur PDF](docs/screenshots/iteration3-lecteur-pdf.png) | ![Lecteur PDF sombre, recherche](docs/screenshots/iteration3-lecteur-pdf-sombre.png) |
| ![Annotations](docs/screenshots/iteration4-annotations.png) | ![Annotations sombre](docs/screenshots/iteration4-annotations-sombre.png) |
| ![Lecteur EPUB, affichage du texte](docs/screenshots/iteration5-epub.png) | ![Lecteur EPUB sombre](docs/screenshots/iteration5-epub-sombre.png) |
| ![Commentaires](docs/screenshots/iteration6-commentaires.png) | ![Commentaires sombre](docs/screenshots/iteration6-commentaires-sombre.png) |
| ![Recherche universelle](docs/screenshots/iteration6-recherche.png) | ![Sauvegardes](docs/screenshots/iteration6-sauvegardes.png) |
| ![Accueil](docs/screenshots/iteration7-accueil.png) | ![Accueil sombre](docs/screenshots/iteration7-accueil-sombre.png) |
| ![Export de la bibliothèque](docs/screenshots/iteration7-export.png) | |
| ![Mes notes (V1)](docs/screenshots/v1-1-notes.png) | ![Mes notes, sombre](docs/screenshots/v1-1-notes-sombre.png) |
| ![Note de lecture et ses sources](docs/screenshots/v1-2-note-lecture.png) | ![Note de lecture, sombre](docs/screenshots/v1-2-note-lecture-sombre.png) |
| ![Matières](docs/screenshots/v1-3-matieres.png) | ![Collection intelligente, sombre](docs/screenshots/v1-3-collections-sombre.png) |
| ![Graphe de connaissances](docs/screenshots/v1-4-graphe.png) | ![Graphe, sombre](docs/screenshots/v1-4-graphe-sombre.png) |
| ![Recherche dans les notes et le texte des documents](docs/screenshots/v1-5-recherche.png) | ![Passage ouvert à la bonne page](docs/screenshots/v1-5-passage.png) |
| ![Références bibliographiques](docs/screenshots/v1-6-references.png) | ![Citations et bibliographie dans une note](docs/screenshots/v1-6-citations.png) |

Suite de la V1 : import d'un coffre Obsidian, lecture TXT/Markdown, OCR et export HTML/PDF ; puis cartes mémoire et répétition espacée (V2) —
plan : [`docs/design/07-versions-mvp-v1-v2.md`](docs/design/07-versions-mvp-v1-v2.md). Historique : [`CHANGELOG.md`](CHANGELOG.md).

## Installer

Les paquets sont produits par l'intégration continue (onglet *Actions* du dépôt, artefacts `studylab-deb` et
`studylab-windows-installer`) :

- **Debian 13** : `sudo apt install ./studylab_0.1.0_amd64.deb` (les dépendances Qt sont installées par apt).
- **Windows 10/11** : lancer `StudyLab-0.1.0-windows-x64-setup.exe`. L'installation se fait pour l'utilisateur courant,
  sans droits administrateur ; la désinstallation conserve vos données.

## Compiler

Voir [`docs/build.md`](docs/build.md). En bref, sur Debian 13 :

```sh
./scripts/install-deps-debian.sh && ./scripts/build.sh
```

## Maquettes

| | |
|---|---|
| ![Accueil](docs/design/maquettes/png/01-accueil.png) | ![Lecteur PDF](docs/design/maquettes/png/03-lecteur-pdf.png) |
| ![Lecteur EPUB](docs/design/maquettes/png/04-lecteur-epub.png) | ![Notes](docs/design/maquettes/png/05-notes.png) |

Toutes les maquettes : [`docs/design/05-maquettes.md`](docs/design/05-maquettes.md).

## Pile technique

Qt 6.8 LTS (Widgets, PDF, WebEngine) · CMake · SQLite (FTS5) · md4c · KaTeX · foliate-js · libzip · Tesseract (optionnel). Détails et licences : [`docs/design/03-comparaison-bibliotheques.md`](docs/design/03-comparaison-bibliotheques.md).

## Licence

StudyLab est un logiciel libre distribué sous **GNU General Public License v3.0 ou ultérieure** (GPL-3.0-or-later). Voir [`LICENSE`](LICENSE).

Les dépendances tierces conservent leurs propres licences (LGPL-3.0, MIT, BSD, Apache-2.0, domaine public, OFL), toutes compatibles avec la GPL-3.0.
