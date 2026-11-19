# Historique des versions

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
