# Historique des versions

## Non publié — V1 « Relier ses connaissances »

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
