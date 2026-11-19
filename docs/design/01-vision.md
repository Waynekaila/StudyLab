# 1. Vision fonctionnelle finale de StudyLab

> **Read – Annotate – Understand – Connect – Learn**
> Un environnement personnel, hors ligne et sans compte, qui transforme une bibliothèque de documents en un système organisé de connaissances.

## 1.1 Positionnement

| Inspiration | Ce que StudyLab en retient | Ce que StudyLab ne reprend pas |
|---|---|---|
| GNOME Papers | Lecture sobre, miniatures, annotations simples et rapides | Le stockage des annotations dans le fichier PDF (StudyLab ne modifie jamais l'original) |
| Calibre | Import massif, métadonnées riches, couvertures, doublons, statuts | L'interface dense en colonnes, la conversion de formats |
| Obsidian | Markdown comme format pivot, liens `[[...]]`, rétroliens, graphe | Le stockage « tout fichier » : SQLite reste la source de vérité structurée |
| Notion | Matières/collections comme espaces organisés, tableaux de bord, vues | Les bases relationnelles génériques, le cloud obligatoire |

StudyLab n'est pas quatre applications collées : c'est **un seul modèle de données** (document → passage → annotation → commentaire → note → carte) parcouru par plusieurs vues.

## 1.2 Persona de référence

- **Étudiant ingénieur** (thermodynamique, RDM, mécanique des fluides…) : 300 à 2 000 PDF, cours + livres + TD scannés, révisions d'examen.
- **Ingénieur en poste** : normes, datasheets, documentation technique, notes de projet.
- **Chercheur** : articles (DOI), bibliographie, notes de lecture reliées entre articles.

Point commun : ils lisent beaucoup, annotent, puis perdent tout ce travail parce qu'il reste prisonnier des fichiers. StudyLab garde, relie et réutilise ce travail.

## 1.3 Le parcours fondamental (8 étapes) et comment StudyLab le sert

| # | Étape | Fonctionnalité StudyLab | Objet créé |
|---|---|---|---|
| 1 | Importer | Glisser-déposer, dossier, import multiple, détection de doublon par empreinte SHA-256, métadonnées extraites (PDF Info/XMP, OPF EPUB), couverture générée | `Document` |
| 2 | Lire | Lecteur PDF et EPUB, position reprise automatiquement, signets, plusieurs onglets | `ReadingProgress` |
| 3 | Annoter | Surlignage, soulignement, barré, note contextuelle, marqueur, signet, 5 couleurs | `Annotation` (ancre stable) |
| 4 | Commenter | Volet de commentaires Markdown lié à une annotation, un passage, une page ou libre | `Comment` |
| 5 | Structurer | « Créer une note depuis… » une annotation, une sélection d'annotations, un commentaire, un document | `Note` + liens vers les sources |
| 6 | Relier | `[[liens]]`, rétroliens, matières, tags, graphe | `Link` |
| 7 | Réviser | Cartes mémoire créées depuis annotation/note/formule, répétition espacée | `Card`, `Review` |
| 8 | Exporter | Coffre Markdown compatible Obsidian, CSV, HTML, BibTeX | Fichiers |

Chaque objet garde **sa provenance** : une carte sait de quelle note elle vient, la note sait de quelles annotations, l'annotation sait de quel passage de quelle page de quel document. Un clic remonte toujours jusqu'au texte original.

## 1.4 Les espaces de l'application

Navigation latérale principale (réordonnable, éléments masquables) :

1. **Accueil** : tableau de bord calme (reprendre la lecture, révisions du jour, dernières annotations, progression par matière).
2. **Bibliothèque** : grille de couvertures ou liste, filtres facettés, panneau de métadonnées.
3. **Lecture** : l'espace central, à trois zones (document, volet annotations, volet commentaires), onglets de documents.
4. **Mes notes** : éditeur Markdown, arborescence, rétroliens, aperçu avec formules.
5. **Matières** : espaces par discipline (documents, notes, cartes, progression).
6. **Collections** : manuelles, intelligentes (requêtes enregistrées), listes de lecture, projets.
7. **Graphe** : relations entre documents, notes, concepts, matières.
8. **Révisions** : paquets, session de révision, fiches.
9. **Activité** : journal (lu, annoté, noté, révisé) et statistiques facultatives.
10. **Paramètres**.

Transverses : **recherche universelle** (Ctrl+K), **palette de commandes** (Ctrl+Shift+P), barre d'état discrète (indexation, sauvegarde).

## 1.5 L'espace de lecture (cœur du produit)

```
┌──────────┬──────────────────────────────────────┬───────────────────────┐
│ Nav      │ Onglets : [Çengel – Thermo] [Cours…] │  ANNOTATIONS  (12) ▾  │
│ latérale │ ─────────────────────────────────────│  filtres · recherche  │
│ (réduite │                                      │  ▌« L'entropie d'un…» │
│ en mode  │        DOCUMENT (PDF / EPUB)         │  ▌« Le rendement de…» │
│ lecture) │                                      ├───────── ═ ───────────┤
│          │                                      │  COMMENTAIRES (4)     │
│          │                                      │  éditeur Markdown     │
│          │                                      │  + aperçu             │
└──────────┴──────────────────────────────────────┴───────────────────────┘
```

- Les deux volets sont dans un `QSplitter` vertical : chaque volet se redimensionne, se replie en en-tête, s'agrandit temporairement (double-clic sur l'en-tête), et la disposition est mémorisée par l'utilisateur.
- **Distinction visuelle immédiate** :
  - Texte original : police de lecture (serif), fond « papier ».
  - Annotations : carte avec une barre verticale colorée à gauche (couleur de l'annotation), citation en italique, référence de page en petites capitales.
  - Commentaires personnels : police d'interface (sans-serif), fond lavande très pâle, icône « bulle », jamais en italique.
- Créer une annotation la fait apparaître **immédiatement** dans le volet (signal Qt `annotationCreated` → modèle → vue), avec une brève animation de surbrillance.
- Clic sur une annotation : le lecteur saute au passage et le fait clignoter doucement.
- Mode sans distraction (F11 + Ctrl+.) : volets et navigation masqués, marges élargies.

## 1.6 Principes de conception

1. **Le fichier original est sacré** : jamais modifié sans action explicite (« Exporter une copie annotée »).
2. **SQLite = vérité structurée, Markdown = interopérabilité, PDF/EPUB = sources.**
3. **Hors ligne d'abord**, aucun compte, aucun appel réseau sauf action explicite (ex. récupérer des métadonnées par DOI, désactivé par défaut).
4. **Rien d'automatique non vérifié** : les fiches et cartes générées sont des brouillons que l'utilisateur valide.
5. **Toujours remonter à la source** : chaque élément dérivé garde son lien de provenance.
6. **Rapide sur gros volumes** : l'indexation et les miniatures tournent en arrière-plan, l'interface ne bloque jamais.
7. **Pas de gadget** : chaque fonction doit servir le parcours lire → apprendre.

## 1.7 Identité visuelle (résumé, détaillé dans les maquettes)

| Jeton | Clair | Sombre |
|---|---|---|
| Fond général | `#F6F5F2` (blanc cassé chaud) | `#1E2127` |
| Surface | `#FFFFFF` | `#262A31` |
| Surface 2 (volets) | `#FBFAF8` | `#2B3038` |
| Bordure | `#E6E3DD` | `#363C46` |
| Texte | `#2A2F36` | `#E3E6EA` |
| Texte secondaire | `#6B7280` | `#9AA3AE` |
| Primaire (bleu ardoise) | `#5B7DB1` | `#8AA6D1` |
| Secondaire (lavande) | `#9C8FD0` | `#B4A9DF` |
| Accent (sauge) | `#7FA88A` | `#9BBFA4` |
| Annotation jaune | `#F6E7A1` | `#7A6E3A` (40 % opacité sur la page) |
| Annotation verte | `#CDE7C9` | … |
| Annotation bleue | `#CFE0F5` | … |
| Annotation rose | `#F5D0DC` | … |
| Annotation violette | `#E1D5F3` | … |

Typographie : **Inter** (interface), **Literata** ou **Source Serif 4** (lecture EPUB, aperçu des notes), **JetBrains Mono** (code), **KaTeX Main / STIX Two Math** (formules). Toutes sous licence OFL, embarquées dans l'application. Échelle d'interface réglable (90 % à 150 %) indépendamment de la taille du texte de lecture.

Rayons 8 px (contrôles) et 12 px (cartes), ombres à 4–8 % d'opacité, espacement sur une grille de 4 px, animations de 120 à 180 ms (ease-out), respect du réglage système « réduire les animations ».
