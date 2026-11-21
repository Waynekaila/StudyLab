# Historique des versions

## Non publié

- **Ergonomie** : navigation réductible en colonne d'icônes (bouton à côté du nom, F8) ; listes et panneaux latéraux
  de tous les espaces redimensionnables en tirant leur bord et rétractables (bouton, F9 / F10, ou en les tirant
  jusqu'au bord), avec une bande pour les rouvrir ; largeurs et panneaux repliés retrouvés au lancement suivant.
  Lecteur : volets plus étroits par défaut et réglables, choix des volets valable pour tous les documents, saisie
  de commentaire sur une ligne au repos. Marges des écrans resserrées. Textes d'accueil des écrans vides (Références,
  Graphe, Matières, Collections, Révisions) affichés en entier.

## 1.0.0 — V2 « Apprendre et consolider » (2026-11-20)

- **Cartes mémoire et révisions** (itération V2-1) : paquets ; cartes basiques, basiques et inversées, à trous
  (une carte par trou) et formules, en Markdown avec KaTeX ; création depuis l'espace Révisions, une annotation
  (« Créer une carte… ») ou une note (sélection), origine cliquable pendant la révision. Séance au clavier (Espace,
  1 à 4, E, S, Échap), intervalles affichés sur les boutons, planification SM-2, limites quotidiennes par paquet,
  journal complet des réponses ; statistiques du jour, histogramme des 7 derniers jours et pastille « Révisions »
  dans la navigation. Migration `0003` (numéro du trou, étape d'apprentissage, index du journal).
- **FSRS et réglages par paquet** (itération V2-2) : planificateur FSRS-5 (19 paramètres par défaut, stabilité et
  difficulté par carte, rétention visée réglable), au choix de SM-2 pour chaque paquet ; les cartes déjà revues
  passent de l'un à l'autre sans perdre leur échéance. Réglages du paquet : nom, matière, limites quotidiennes,
  étapes d'apprentissage et après un oubli (« 1 10 », « 10 1h »), intervalle maximal. Historique d'une carte.
- **Fiches de révision** (itération V2-3) : brouillon de note généré pour une matière (sous-matières comprises)
  depuis ses passages annotés, ses notes « Concept », les formules de ses notes et commentaires, ses questions non
  résolues et les cartes de ses paquets ; chaque élément renvoie à son origine. Rubriques au choix, fiche rattachée
  à la matière, bandeau « brouillon à relire » et « Marquer comme relue » (annulable) dans Mes notes.
- **Tableau de bord et écran Activité** (itération V2-4) : temps de lecture actif (onglet visible, fenêtre au premier
  plan, pas de pause de plus de deux minutes) et pages vues mesurés par le lecteur, désactivables dans les
  paramètres ; journal des ouvertures, imports, annotations, commentaires, notes et révisions. Accueil complet :
  révisions du jour par matière, objectif de la semaine (anneau, temps de lecture, comparaison avec la semaine
  précédente), lectures en cours, progression par matière, favoris, notes récentes, boutons Importer et Nouvelle
  note. Objectifs : pages par semaine, minutes de lecture et cartes par jour. Écran Activité : chiffres sur 7 jours,
  30 jours ou 12 mois, temps de lecture par jour ou par mois, régularité sur six mois et série en cours, journal
  regroupé par jour et filtrable, chaque entrée rouvrant le document, le passage ou la note.
- **Interopérabilité PDF** (itération V2-5) : à l'ouverture, un bandeau signale les annotations faites dans un autre
  logiciel (surlignages, soulignements, barrés, notes et leurs réponses, rectangles ; tracés, tampons et ellipses
  réduits à une zone) et propose de les importer avec leur couleur la plus proche, leur texte et leur commentaire ;
  les doublons sont écartés, et le dessin de celles du fichier s'arrête une fois toutes importées (réglable dans le
  menu « … »). « Exporter une copie annotée du PDF… » écrit une copie lisible par tout lecteur PDF : surlignages,
  soulignements, barrés, zones et notes avec leur commentaire, commentaires de page ou de passage en notes. Le
  fichier de la bibliothèque n'est jamais modifié ; la copie ajoute une révision au PDF d'origine sans réécrire ses
  octets. Lecteur d'objets PDF intégré (tables de références classiques ou en flux, flux d'objets, réparation).
- **Documents Word et images** (itération V2-6, première partie) : un `.docx` s'importe et s'ouvre dans le lecteur
  EPUB (titres et sommaire, gras, italique, souligné, barré, exposants et indices, listes imbriquées, tableaux avec
  cellules fusionnées, liens, images, citations, notes de bas de page ; titre, auteurs et langue des propriétés ;
  modifications suivies montrées acceptées). Une image (PNG, JPEG, GIF, BMP, WebP, TIFF de plusieurs pages) s'ouvre
  dans le lecteur PDF, une page par image, à sa taille réelle : zones, notes, signets, zoom, miniatures et OCR
  (texte reconnu et cherchable) comme pour un scan. Le parcours d'un dossier prend les `.docx` mais pas les images.
- **Navigation et raccourcis configurables** (itération V2-6, seconde partie) : dans Paramètres › Navigation et
  raccourcis, les espaces de la barre de gauche se rangent (glisser, Monter, Descendre) et se masquent ; les
  raccourcis clavier se changent ou se retirent (recherche, plein écran, aller à un espace avec Ctrl+1 à Ctrl+9 et
  Ctrl+, , recherche, page, signet et historique du lecteur, nouvelle note et enregistrement, touches de la séance de
  révision). Appliqué sans redémarrage, infobulles et aide de la séance comprises ; une touche déjà prise au même
  endroit, réservée ou seule hors des révisions est refusée avec son motif.
- **Finalisation** (itération V2-7) : banc d'essai gros volume (`tst_volume` : 3 000 documents, 60 000 pages de
  texte, 30 000 annotations, 20 000 cartes, multipliable par 5) qui vérifie un plafond pour chaque requête et chaque
  écran. Démarrage avec une grosse bibliothèque presque quatre fois plus rapide : les écrans Graphe, Révisions,
  Références et Accueil ne se remplissent qu'à leur premier affichage, l'aperçu Markdown ne lance son moteur web
  qu'une fois visible, les icônes ne sont dessinées qu'une fois. Graphe de milliers de nœuds cinq fois plus fluide
  (pas d'index spatial, étiquettes illisibles non dessinées, texte abrégé mis en cache) ; activité sur 12 mois et
  série en cours deux fois plus rapides, compteurs de tous les paquets en deux requêtes. Guide utilisateur
  ([`docs/user-guide/`](docs/user-guide/README.md)), documentation technique
  ([`docs/technical/`](docs/technical/README.md)) avec les mesures de performances, guide de dépannage complété.

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
