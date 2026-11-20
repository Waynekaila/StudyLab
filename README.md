# StudyLab

> **Read – Annotate – Understand – Connect – Learn**

StudyLab est une application de bureau (C++20 / Qt 6) pour lire, annoter, commenter, organiser et réviser ses documents PDF et EPUB, et transformer une bibliothèque en base de connaissances personnelle. Hors ligne, sans compte, sans cloud.

Plateformes cibles : **Windows 10/11 x64** et **Debian 13 x64**.

## État du projet

| Phase | Contenu | État |
|---|---|---|
| 1 · Conception | Vision, architecture, choix des bibliothèques, schéma SQLite, maquettes, plan | ✅ Terminée — voir [`docs/design/`](docs/design/README.md) |
| 2 · MVP | Bibliothèque, lecteurs PDF/EPUB, annotations, commentaires, sauvegarde, export Markdown | ✅ Terminée — **version 0.1.0** (7 itérations) |
| 3 · Connaissances (V1) | Notes Markdown, liens, matières, collections, graphe, recherche universelle | ✅ Terminée — **version 0.2.0** (7 itérations) |
| 4 · Apprentissage (V2) | Cartes mémoire, répétition espacée, fiches, tableau de bord, formats Word et images, raccourcis | ✅ Terminée — **version 1.0.0** (7 itérations) |
| 5 · Finalisation | Profilage gros volume, packaging, guide utilisateur, documentation technique, dépannage | ✅ Faite avec la V2-7 |

### Ce qui fonctionne (version 1.0)

- **Révisions** (V2) : cartes mémoire basiques, inversées, à trous (`{{c1::…}}`, une carte par trou)
  ou formules (LaTeX), rangées en paquets ; « Créer une carte… » depuis une annotation (menu contextuel) ou une note
  (sélection), l'origine restant cliquable. Séance de révision : recto, « Afficher la réponse » (Espace), quatre
  réponses avec l'intervalle suivant (À revoir, Difficile, Bien, Facile ; touches 1 à 4), E pour modifier, S pour
  suspendre. Planification SM-2 ou **FSRS-5** au choix par paquet (rétention visée de 70 à 99 %) ; réglages par
  paquet : matière, nouvelles et révisions par jour (20 et 200 par défaut), étapes d'apprentissage et après un
  oubli, intervalle maximal ; journée qui commence à 4 h. Historique de chaque carte (réponses, intervalles,
  stabilité, difficulté, rappel estimé). Statistiques du jour et des 7 derniers jours, pastille des cartes dues.
- **Fiches de révision** (V2) : « Générer une fiche de révision » rassemble pour une matière les passages
  annotés (les commentés d'abord) avec leurs liens, les définitions des notes « Concept », les formules relevées
  dans les notes et les commentaires, les questions en suspens et celles des cartes. La fiche est un **brouillon**
  dans « Fiches de révision », rattaché à la matière ; un bandeau invite à la relire puis à la marquer comme relue.
- **Tableau de bord et Activité** (V2) : l'accueil montre la lecture à reprendre, les cartes du jour par
  matière, l'objectif de la semaine (pages lues, temps de lecture, comparaison avec la semaine précédente), les
  lectures en cours, la progression par matière, les dernières annotations et derniers commentaires, les favoris et
  les notes récentes. Objectifs au choix : pages par semaine, minutes de lecture et cartes par jour. L'écran
  **Activité** donne les chiffres sur 7 jours, 30 jours ou 12 mois, le temps de lecture par jour ou par mois, la
  régularité sur six mois et un journal filtrable (lu, annoté, commenté, noté, révisé, importé) dont chaque ligne
  rouvre ce qu'elle désigne. Le temps n'est compté que si le document est à l'écran et qu'on s'en sert ; la mesure se
  désactive dans les paramètres et rien ne quitte l'ordinateur.
- **Annotations PDF échangées** (V2) : un PDF déjà annoté ailleurs (Acrobat, Okular, Zotero, Xodo…)
  affiche un bandeau qui propose d'importer ses surlignages, soulignements, barrés, notes et rectangles, avec le texte
  couvert, la couleur la plus proche et le commentaire ; un second import n'ajoute rien. Dans l'autre sens,
  « Exporter une copie annotée du PDF… » (menu « … » du lecteur) écrit une copie qui montre dans n'importe quel
  lecteur PDF les annotations StudyLab, leurs commentaires et les commentaires de page. L'original n'est jamais
  modifié.
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
- **OCR des PDF numérisés** (V1, facultatif) : si Tesseract est installé, les pages sans texte sont lues en tâche de
  fond, à basse priorité, page par page (reprise après interruption). Leur texte devient cherchable (Ctrl + K et
  recherche du lecteur), sélectionnable, copiable et annotable comme un texte natif ; la page est marquée « Texte
  OCR ». Activation, langues et « Relire » dans Paramètres › Données. Sans Tesseract, rien ne change.
- **Références bibliographiques** (V1) : import BibTeX / BibLaTeX et CSL-JSON (Zotero, JabRef, Mendeley…),
  saisie ou fiche tirée d'un document (« Citer » dans la bibliothèque), rattachement automatique au document par
  DOI, ISBN ou titre. Dans une note, `[[@clé]]` cite la référence : l'aperçu affiche « (Çengel et Boles, 2019,
  p. 12) » et ajoute la bibliographie en fin de note ; taper `[[@` propose les clés. Changer une clé met à jour les
  notes qui la citent. Export BibTeX ou CSL-JSON, et `Références.bib` dans l'export Obsidian.
- **Textes et fichiers Markdown** (V1) : un `.txt` ou un `.md` s'importe dans la bibliothèque comme un livre et
  s'ouvre dans le lecteur EPUB : sommaire tiré des titres, mise en forme (listes, tableaux, code, citations, images
  locales), surlignages, commentaires, recherche et export comme pour un EPUB. Front matter `title:` / `author:` repris.
- **Documents Word** (V2) : un `.docx` s'ouvre de la même façon, en lecture seule : titres et sommaire, mise en
  forme, listes, tableaux, liens, images, citations et notes de bas de page. La mise en page (colonnes, en-têtes,
  pieds de page), les commentaires Word et les objets incorporés ne sont pas repris.
- **Images** (V2) : une photo de tableau, un schéma ou un scan (PNG, JPEG, GIF, BMP, WebP, TIFF multipage)
  s'importe comme un document de pages et s'ouvre dans le lecteur PDF : zones et notes sur l'image, zoom,
  miniatures, et OCR pour retrouver le texte dans la recherche.
- **Navigation et raccourcis** (V2) : Paramètres › Navigation et raccourcis pour ranger ou masquer les espaces de la
  barre de gauche et changer les raccourcis clavier (Ctrl+1 à Ctrl+9 pour aller d'un espace à l'autre, Ctrl+K,
  Ctrl+F, Ctrl+D…). Les changements s'appliquent immédiatement.
- **Export PDF et HTML des notes** (V1) : clic droit sur une note, un dossier de notes ou une matière › « Exporter en
  PDF… » ou « Exporter en HTML… ». Rendu identique à l'aperçu (formules, images, citations et bibliographie), sur fond
  clair ; PDF A4 avec une note par page ; page HTML autonome (styles, polices des formules et images incluses).
- **Import d'un coffre Obsidian** (V1) : « + › Importer un coffre Obsidian ou un dossier Markdown… » dans Mes notes.
  Chaque fichier `.md` devient une note (dossiers conservés, front matter et `type:` repris), les images sont
  copiées, les `[[liens]]` résolus et les `.bib` importés comme références. Un export StudyLab se réimporte sans
  doublon, avec ses dossiers, types et identifiants.
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
| ![Coffre Obsidian importé](docs/screenshots/v1-6b-import-coffre.png) | ![Cours en Markdown dans le lecteur](docs/screenshots/v1-6c-markdown.png) |
| ![Notes exportées en PDF](docs/screenshots/v1-7-export-pdf.png) | ![Page numérisée lue par l'OCR](docs/screenshots/v1-7-ocr.png) |
| ![Révision d'une carte tirée d'une annotation (V2)](docs/screenshots/v2-1-revisions.png) | ![Réglages d'un paquet : FSRS](docs/screenshots/v2-2-reglages-paquet.png) |
| ![Fiche de révision générée, brouillon à relire](docs/screenshots/v2-3-fiche-revision.png) | ![Tableau de bord : révisions du jour, objectif de la semaine](docs/screenshots/v2-4-accueil.png) |
| ![Écran Activité](docs/screenshots/v2-4-activite.png) | ![Activité sur 30 jours, sombre](docs/screenshots/v2-4-activite-sombre.png) |
| ![PDF annoté dans un autre logiciel : proposition d'import](docs/screenshots/v2-5-annotations-du-pdf.png) | ![Annotations importées, sans double dessin](docs/screenshots/v2-5-annotations-importees.png) |
| ![Copie annotée enregistrée](docs/screenshots/v2-5-copie-annotee-enregistree.png) | ![Copie annotée vue par le moteur PDF](docs/screenshots/v2-5-copie-annotee-page.png) |

Documentation : [guide utilisateur](docs/user-guide/README.md), [dépannage](docs/troubleshooting.md),
[documentation technique](docs/technical/README.md) et [performances](docs/technical/performances.md),
[conception](docs/design/README.md). Historique : [`CHANGELOG.md`](CHANGELOG.md).

## Installer

Les paquets sont produits par l'intégration continue (onglet *Actions* du dépôt, artefacts `studylab-deb` et
`studylab-windows-installer`) :

- **Debian 13** : `sudo apt install ./studylab_1.0.0_amd64.deb` (les dépendances Qt sont installées par apt).
- **Windows 10/11** : lancer `StudyLab-1.0.0-windows-x64-setup.exe`. L'installation se fait pour l'utilisateur courant,
  sans droits administrateur ; la désinstallation conserve vos données.
- **OCR (facultatif)** : Debian `sudo apt install tesseract-ocr tesseract-ocr-fra` ; Windows, l'installateur
  « Tesseract-OCR » (UB Mannheim) dans Program Files, avec les langues voulues. StudyLab le trouve au démarrage
  (ou via `STUDYLAB_TESSERACT`, chemin de la bibliothèque, et `TESSDATA_PREFIX`).

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
