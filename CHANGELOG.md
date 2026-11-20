# Historique des versions

## 0.2.0 — V1 « Relier ses connaissances » (2026-11-20)

- **Mes notes** (itération V1-1) : espace de notes Markdown avec dossiers virtuels, huit types et leurs modèles,
  front matter YAML, tags, éditeur Source / Partagé / Aperçu (KaTeX, tableaux, images collées), liens `[[…]]`
  résolus par titre, nom de fichier ou alias, rétroliens, plan, liens non résolus (« créer »), mise à jour des liens
  au renommage, enregistrement automatique et corbeille annulable.
- **Notes depuis la lecture** (itération V1-2) : création d'une note depuis une annotation, les annotations
  affichées, un commentaire ou un document, avec citations sourcées et provenance conservée (panneau « Sources »
  qui rouvre le passage) ; `[[liens]]` vers les documents de la bibliothèque ; autocomplétion après `[[` ; liens
  `studylab://` cliquables dans l'aperçu.
- **Matières et collections** (itération V1-3) : matières hiérarchiques (couleur, glisser-déposer, archivage) et
  page de matière (chiffres clés, documents, notes) ; collections manuelles, listes de lecture, projets et
  collections intelligentes à règles (matière, statut, format, tag, favori, ajouté, annoté ou ouvert récemment),
  avec aperçu du nombre de résultats ; rattachement depuis la bibliothèque et les notes.
- **Graphe de connaissances** (itération V1-4) : documents, notes et concepts (notes de type « Concept ») reliés
  par les `[[liens]]` et la provenance des notes de lecture, couleur de la matière ; filtres par type et matière, liens
  non résolus, profondeur du voisinage, étiquettes, regroupement par matière ; fiche du nœud (Ouvrir, Centrer,
  Isoler, « Créer la note » pour un lien non résolu) ; disposition force-dirigée calculée en arrière-plan.
- **Recherche plein texte** (itération V1-5) : texte des PDF et des EPUB extrait et indexé en tâche de fond (une
  fois par document, reprise au démarrage, césures recollées) ; Ctrl + K trouve aussi les notes et les passages des
  documents (deux au plus par document), et un passage ouvre le document à la page ou à la section, terme cherché
  en évidence ; état de l'index et « Réindexer » dans Paramètres › Données.
- **Références bibliographiques** (itération V1-6) : import BibTeX / BibLaTeX (`@string`, accents LaTeX) et
  CSL-JSON, saisie, fiche tirée d'un document (« Citer »), rattachement au document par DOI, ISBN ou titre ;
  citations `[[@clé]]` dans les notes (auteur-date, page, autocomplétion, bibliographie en fin d'aperçu) ; changer
  une clé met à jour les notes ; export BibTeX ou CSL-JSON et `Références.bib` dans l'export Obsidian.
- **Import d'un coffre Obsidian ou d'un dossier Markdown** (itération V1-6b) : « + › Importer un coffre… » dans
  Mes notes. Une note par fichier `.md` (titre = nom du fichier), dossiers du coffre conservés sous un dossier à son
  nom, front matter gardé (`type:` donne le type de note), images `![[…]]` et `![…](…)` copiées dans la base,
  `[[liens]]` résolus une fois tout importé, fichiers `.bib` importés comme références ; dossiers cachés
  (`.obsidian`, `.trash`) ignorés. Un export StudyLab se réimporte tel quel (mêmes dossiers, types et uuid) ; les
  notes déjà présentes (même uuid, ou même titre, dossier et texte) ne sont pas dupliquées.
- **OCR facultatif des PDF numérisés** (itération V1-7) : Tesseract 5 chargé à l'exécution s'il est installé (aucune
  dépendance à la compilation). Les pages sans texte natif sont rendues à 300 dpi et lues en tâche de fond, à basse
  priorité, une page à la fois ; chaque page est enregistrée aussitôt (reprise après interruption). Le texte reconnu
  alimente Ctrl + K et la recherche du lecteur, se sélectionne, se copie et s'annote comme un texte natif ; la page
  est marquée « Texte OCR ». Paramètres › Données : activation, langues installées, état, « Relire ».
- **Export PDF et HTML des notes** (itération V1-7) : une note, un dossier de notes ou les notes d'une matière (et de
  ses sous-matières), depuis le clic droit. Même rendu que l'aperçu (KaTeX, images, citations, bibliographie), thème
  clair ; PDF A4 par Chromium, une note par page ; HTML autonome (feuilles de style, polices KaTeX et images en data:),
  liens internes rendus en texte.
- **Lecture des fichiers TXT et Markdown** (itération V1-6c) : importés dans la bibliothèque (choisis un par un ;
  le parcours d'un dossier reste limité aux PDF et EPUB), mis en forme de livre EPUB en mémoire et lus dans le
  lecteur EPUB avec annotations, commentaires, recherche, plein texte et export. Markdown : titres, listes, tableaux,
  code, citations, liens, images locales ; découpage en sections aux titres qui se répètent ; `title:`, `author:` et
  `lang:` du front matter repris. Texte brut en UTF-8 ou Windows-1252, en parties d'environ 40 000 caractères.
- Correction : « Lire » et le double-clic dans la bibliothèque n'ouvraient que les PDF ; les EPUB s'ouvrent aussi.
- **Export de la bibliothèque** : les notes sont jointes (toutes pour la bibliothèque entière, celles tirées des
  documents pour une sélection) dans `Notes/<dossier>/`, avec leurs images dans `_attachments/` ; la fiche de
  chaque document a son titre en alias, pour que `[[Titre]]` y mène dans Obsidian.
- Correction : les puces (tags) créées avant l'affichage d'un écran se superposaient.

## 0.1.0 — MVP « Lire, annoter, commenter, exporter » (2026-11-19)

- **Bibliothèque** : import de PDF et EPUB (fichiers, dossiers, glisser-déposer) en arrière-plan, doublons détectés
  par SHA-256, métadonnées et couvertures, grille ou liste, filtres, tri, recherche instantanée, panneau de détail.
- **Lecteur PDF** : rendu par tuiles, zoom, rotation, double page, miniatures, sommaire, liens internes, recherche,
  sélection et copie, onglets, reprise de la position, plein écran et mode sans distraction.
- **Lecteur EPUB 2/3** (foliate-js) : table des matières, pages ou défilement, affichage du texte réglable, quatre
  thèmes de page, recherche, reprise au CFI près ; hors ligne, scripts des livres bloqués.
- **Annotations** : surlignage, soulignement, barré, zone, note et signet en 5 couleurs, ancrage robuste, volet avec
  filtres et recherche, annulation.
- **Commentaires** Markdown avec formules LaTeX (KaTeX) et liens `[[…]]`, ancrés à un passage, une annotation, une
  page ou au document ; brouillons conservés après une fermeture brutale.
- **Recherche universelle** (Ctrl + K) sur les métadonnées, annotations et commentaires.
- **Sauvegardes** automatiques et manuelles, rotation, restauration, contrôle d'intégrité au démarrage.
- **Export Markdown / Obsidian** d'un document ou de la bibliothèque, CSV des annotations, liens `studylab://`.
- **Accueil** : reprendre la lecture, activité récente.
- **Paquets** : `.deb` Debian 13 et installateur Windows 10/11, testés par l'intégration continue.
