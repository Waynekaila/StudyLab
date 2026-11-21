# Guide utilisateur de StudyLab 1.0

StudyLab sert à lire, annoter, commenter, relier et réviser vos documents d'étude. Tout reste sur votre ordinateur :
pas de compte, pas d'envoi en ligne. Ce guide suit le chemin d'un document, de son import jusqu'aux révisions.
En cas de problème, voir le [guide de dépannage](../troubleshooting.md).

## Sommaire

1. [Premier lancement](#1-premier-lancement)
2. [La fenêtre](#2-la-fenêtre)
3. [Importer des documents](#3-importer-des-documents)
4. [La bibliothèque](#4-la-bibliothèque)
5. [Lire](#5-lire)
6. [Annoter](#6-annoter)
7. [Commenter](#7-commenter)
8. [Mes notes](#8-mes-notes)
9. [Matières et collections](#9-matières-et-collections)
10. [Le graphe](#10-le-graphe)
11. [Rechercher](#11-rechercher)
12. [Références bibliographiques](#12-références-bibliographiques)
13. [Réviser avec des cartes](#13-réviser-avec-des-cartes)
14. [Accueil, objectifs et activité](#14-accueil-objectifs-et-activité)
15. [Exporter et échanger](#15-exporter-et-échanger)
16. [Sauvegardes et données](#16-sauvegardes-et-données)
17. [Personnaliser](#17-personnaliser)
18. [Raccourcis clavier](#18-raccourcis-clavier)

## 1. Premier lancement

- **Debian 13** : `sudo apt install ./studylab_1.1.0_amd64.deb`, puis StudyLab dans le menu des applications.
- **Windows 10/11** : lancez `StudyLab-1.1.0-windows-x64-setup.exe`. L'installation se fait pour votre compte, sans
  droits administrateur. La désinstallation conserve vos données.

Au premier lancement, la bibliothèque est vide : l'accueil propose **Importer**. Pour lire les scans et les images,
installez en plus Tesseract (voir [OCR](#ocr-des-pages-numérisées)) ; c'est facultatif.

## 2. La fenêtre

À gauche, la barre de navigation mène aux espaces : **Accueil**, **Bibliothèque**, **Lecture**, **Mes notes**,
**Matières**, **Collections**, **Graphe**, **Révisions**, **Activité**, **Références** et **Paramètres**. En haut de
cette barre, la loupe ouvre la recherche partout (Ctrl + K). Ctrl + 1 à Ctrl + 9 vont aux neuf premiers espaces,
Ctrl + , aux Paramètres. L'ordre des espaces et ceux qui sont affichés se règlent (voir
[Personnaliser](#17-personnaliser)).

Les documents ouverts sont des onglets de l'espace **Lecture** ; ils sont rouverts au lancement suivant, chacun à
la position où vous l'avez laissé. F11 passe en plein écran.

**Faire de la place.** Le bouton à côté du nom StudyLab (ou F8) réduit la navigation à une colonne d'icônes ; le
nom de chaque espace s'affiche au survol. Les listes et panneaux latéraux des espaces (Mes notes, Matières,
Collections, Graphe, Révisions, Références, Paramètres, fiche d'un document, miniatures et volet Annotations du
lecteur) :

- s'élargissent ou se rétrécissent en tirant leur bord ;
- se replient par leur bouton en haut à droite, par F9 (panneau de gauche) ou F10 (panneau de droite), ou en les
  tirant jusqu'au bord. Un panneau replié laisse une fine bande : son bouton le rouvre à la même largeur.

Largeurs et panneaux repliés sont retrouvés au lancement suivant. Dans le lecteur, le choix vaut pour tous les
documents. La zone de saisie d'un commentaire tient sur une ligne tant qu'on n'y écrit pas.

## 3. Importer des documents

Formats acceptés :

| Format | S'ouvre dans | Remarques |
|---|---|---|
| PDF | lecteur PDF | PDF protégés par mot de passe : le mot de passe est demandé à l'ouverture |
| EPUB 2 et 3 | lecteur EPUB | EPUB protégés par DRM refusés |
| TXT, Markdown (`.md`) | lecteur EPUB | sommaire tiré des titres ; `title:` et `author:` du front matter repris |
| Word (`.docx`) | lecteur EPUB, lecture seule | colonnes, en-têtes, pieds de page, commentaires Word et objets incorporés ne sont pas repris |
| Images (PNG, JPEG, GIF, BMP, WebP, TIFF) | lecteur PDF, une page par image | texte retrouvé par l'OCR si Tesseract est installé |

Pour importer : bouton **Importer** de l'accueil ou de la bibliothèque (fichiers ou dossier entier), ou
glisser-déposer des fichiers n'importe où dans la fenêtre. L'import se fait en arrière-plan avec une barre de
progression et un bouton d'annulation, puis un rapport indique ce qui a été ajouté, ignoré (doublon, fichier
illisible) ou refusé.

- **Copie ou lien** (Paramètres › Bibliothèque et import) : par défaut, StudyLab copie le fichier dans son dossier de
  données ; en mode lien, il garde seulement le chemin (le fichier ne doit alors pas être déplacé).
- **Doublons** : un fichier déjà présent (même contenu) n'est pas réimporté, même renommé.
- Le parcours d'un dossier prend les PDF, EPUB, TXT, Markdown et `.docx`, **pas les images** (souvent des logos ou
  des icônes) : choisissez-les ou glissez-les directement.

## 4. La bibliothèque

- **Grille** de couvertures ou **liste** (sélecteur Grille / Liste).
- **Recherche instantanée** sur le titre, les auteurs, l'éditeur, les tags, l'ISBN et le DOI (accents ignorés).
- **Filtres** : statut de lecture, format, tag, favoris. **Tri** : ajoutés récemment, ouverts récemment, titre,
  auteur.
- Un clic sélectionne un document et ouvre son **panneau de détail** : statut (à lire, en cours, terminé), favori,
  métadonnées et tags modifiables, matières et collections, « Citer », « Lire », retrait de la bibliothèque.
- Double-clic ou **Lire** : ouvre le document dans un onglet.
- Ctrl + clic et Maj + clic sélectionnent plusieurs documents (pour l'export, par exemple).

## 5. Lire

### Lecteur PDF (et images)

- Défilement continu, zoom (ajuster à la largeur, page entière, 10 à 800 %, Ctrl + molette), rotation, double page.
- **Miniatures** et **sommaire** dans le volet de gauche ; **Aller à la page** (Ctrl + G).
- **Rechercher dans le document** (Ctrl + F), résultats surlignés.
- **Historique** : Alt + ← et Alt + → reviennent aux positions précédentes (après un lien, un résultat…).
- Sélection et copie du texte, liens internes cliquables.

### Lecteur EPUB (et TXT, Markdown, Word)

- Pages ou défilement continu, chapitre précédent et suivant, progression et temps restant dans le chapitre.
- Panneau **Affichage du texte** : police, taille, interligne, marges, mise en page et quatre thèmes de page (blanc,
  sépia, vert d'eau, sombre). Les annotations restent à leur place quand vous changez ces réglages.
- Recherche dans tout le livre (Ctrl + F), accents et casse ignorés.

### Lecture sans distraction

Le bouton **Lecture sans distraction** de la barre du lecteur masque les volets (Échap pour en sortir) ; F11 passe
en plein écran.

## 6. Annoter

Sélectionnez du texte : une **barre flottante** propose la couleur (5 couleurs), le type (surlignage,
soulignement, barré), une note, « Commenter » et « Copier ». Dans un PDF ou une image, l'outil **Zone** trace un
rectangle (schéma, formule) et l'outil **Note** pose une note ponctuelle. **Ctrl + D** pose un signet sur la page.

Le **volet Annotations** (à droite) liste les annotations groupées par chapitre : recherche, portée (document ou
page courante), filtres par couleur et par type. Un clic amène au passage, qui clignote. Clic droit sur une
annotation : modifier, changer la couleur, créer une note, **Créer une carte…**, supprimer. Une suppression
s'annule avec **Ctrl + Z** ou le bouton « Annuler » qui apparaît.

Les annotations restent ancrées au texte quels que soient le zoom, la rotation, la police ou la taille d'affichage.

### PDF déjà annotés ailleurs

Si un PDF contient des annotations faites dans un autre logiciel (Acrobat, Okular, Zotero, Xodo…), un bandeau propose
de les **importer** : surlignages, soulignements, barrés, notes et rectangles, avec leur texte, la couleur la plus
proche et leur commentaire. Un second import n'ajoute rien. Le fichier lui-même n'est jamais modifié.

## 7. Commenter

Le **volet Commentaires**, sous les annotations, accueille vos explications, questions, réflexions et références.
Un commentaire s'attache à un passage (bouton « Commenter » de la barre flottante), à une annotation, à une page ou
au document entier. Il s'écrit en Markdown, avec formules LaTeX (`$…$`, `$$…$$`) et liens `[[…]]`, et un aperçu.
Filtres : questions, explications, page ou chapitre courant ; une question peut être marquée **résolue**. Le
brouillon en cours est conservé même si StudyLab se ferme brutalement.

## 8. Mes notes

L'espace **Mes notes** contient vos notes Markdown, rangées dans des dossiers (« Thermodynamique/Cours »).

- **Nouvelle note** (Ctrl + N) : huit types avec leur modèle (note de lecture, fiche de cours, concept, formules,
  fiche de révision, recherche, compte rendu, note personnelle).
- **Éditeur** : modes Source, Partagé (côte à côte) et Aperçu ; formules KaTeX, tableaux, citations, images collées
  (copiées dans vos données). Enregistrement automatique ; Ctrl + S enregistre tout de suite.
- **Liens** : `[[Titre d'une note]]` ou `[[Titre d'un document]]` ; taper `[[` propose les titres. Les **rétroliens**
  (qui cite cette note) s'affichent sous l'éditeur, avec le **plan** de la note et les liens non résolus (« créer »).
  Renommer une note met à jour les liens qui la citent.
- **Tags** : `tags:` dans le front matter ou `#tag` dans le texte ; un clic sur un tag filtre la liste.
- **Notes depuis la lecture** : « Créer une note » depuis une annotation, les annotations filtrées du volet, un
  commentaire ou un document. La note cite les passages ; ses **sources** restent listées et rouvrent le passage.
- **Corbeille** : « Mettre à la corbeille » (clic droit) s'annule avec le bouton « Annuler » qui apparaît.
- **Importer un coffre Obsidian ou un dossier Markdown** : bouton « + » de Mes notes. Dossiers, front matter, images
  et liens sont repris ; les fichiers `.bib` deviennent des références.
- **Exporter en PDF ou HTML** : clic droit sur une note, un dossier ou une matière.

## 9. Matières et collections

- **Matières** : arborescence (« Physique › Thermodynamique ») avec couleur, glisser-déposer pour réordonner,
  archivage. La page d'une matière donne ses chiffres (documents, annotations, notes, progression) et ses listes,
  sous-matières comprises.
- **Collections** : manuelles, listes de lecture et projets (documents et notes, dans l'ordre voulu), ou
  **intelligentes**, décrites par des règles (« Matière est Thermodynamique · Statut n'est pas Terminé ») et mises à
  jour toutes seules.
- On rattache un document depuis son panneau de détail dans la bibliothèque, une note par clic droit.

## 10. Le graphe

Documents (rectangles), notes (ronds) et concepts (losanges) reliés par les `[[liens]]` et par la provenance des
notes de lecture, colorés par matière. Filtres par type et par matière, liens non résolus sur demande, profondeur
du voisinage (1, 2, 3 ou tout), regroupement par matière. Cliquer un nœud ouvre sa fiche (extrait, liens,
rétroliens, sources) avec **Ouvrir**, **Centrer** et **Isoler**. Molette pour zoomer, glisser pour déplacer.

## 11. Rechercher

**Ctrl + K** cherche partout : titres et métadonnées, annotations, commentaires, notes et **texte des documents**.
Les résultats sont groupés par catégorie ; Entrée ouvre le document au bon passage, le terme mis en évidence.

Le texte des documents est indexé en tâche de fond, une fois par document, sans gêner la lecture ; l'état de
l'index et **Réindexer** se trouvent dans Paramètres › Données.

### OCR des pages numérisées

Si Tesseract est installé, les pages sans texte (scans, images) sont lues en tâche de fond. Leur texte devient
cherchable, sélectionnable et annotable ; la page est marquée « Texte OCR ». Activation, langues et « Relire » dans
Paramètres › Données.

- Debian : `sudo apt install tesseract-ocr tesseract-ocr-fra`.
- Windows : installateur « Tesseract-OCR » (UB Mannheim) dans Program Files, avec les langues voulues.

## 12. Références bibliographiques

L'espace **Références** importe des fichiers BibTeX / BibLaTeX et CSL-JSON (Zotero, JabRef, Mendeley…), permet la
saisie, ou crée une fiche depuis un document (« Citer » dans la bibliothèque). Une référence est rattachée à son
document par DOI, ISBN ou titre. Dans une note, `[[@clé]]` cite la référence (« (Çengel et Boles, 2019, p. 12) »
dans l'aperçu) et ajoute la bibliographie en fin de note ; taper `[[@` propose les clés. Export en BibTeX ou
CSL-JSON.

## 13. Réviser avec des cartes

### Créer des cartes

Dans **Révisions**, créez un **paquet** (« + »), éventuellement rattaché à une matière, puis **Ajouter une carte**.
Quatre types : **basique** (recto / verso), **basique et inversée** (deux cartes), **texte à trous**
(`{{c1::réponse}}`, une carte par trou) et **formule** (LaTeX). Le texte est en Markdown avec KaTeX.

On crée aussi une carte par clic droit sur une annotation, ou depuis une note (avec ou sans sélection) : l'origine
reste cliquable pendant la révision.

### La séance

**Commencer la révision** présente les cartes dues : Espace pour **afficher la réponse**, puis **1** À revoir,
**2** Difficile, **3** Bien, **4** Facile (l'intervalle suivant est écrit sur chaque bouton). **E** modifie la carte,
**S** la suspend, Échap termine la séance. Ces touches se changent dans les paramètres.

### Planification

Chaque paquet utilise **SM-2** (comme Anki) ou **FSRS-5** (rétention visée réglable de 70 à 99 %). Dans **Réglages
du paquet** : nouvelles et révisions par jour (20 et 200 par défaut), étapes d'apprentissage (« 1 10 ») et après un
oubli (« 10 »), intervalle maximal. La journée de révision commence à 4 h du matin. L'historique d'une carte
(réponses, intervalles, stabilité, difficulté) s'ouvre depuis la liste des cartes.

### Fiches de révision

**Générer une fiche de révision** rassemble pour une matière les passages annotés (les commentés d'abord), les
définitions des notes « Concept », les formules, les questions en suspens et celles des cartes, chaque élément avec
son lien d'origine. La fiche est un **brouillon** à relire dans Mes notes, puis à marquer comme relue.

## 14. Accueil, objectifs et activité

L'**accueil** montre la lecture à reprendre, les révisions du jour par matière, l'objectif de la semaine, les
lectures en cours, la progression par matière, les dernières annotations et derniers commentaires, les favoris et
les notes récentes.

**Objectifs…** (accueil ou Activité) : pages par semaine, minutes de lecture et cartes par jour.

L'écran **Activité** donne les chiffres sur 7 jours, 30 jours ou 12 mois, le temps de lecture, la régularité sur
six mois avec la série en cours, et un journal filtrable dont chaque ligne rouvre ce qu'elle désigne.

Le temps de lecture n'est compté que si le document est à l'écran, la fenêtre au premier plan et que vous vous en
servez (pas plus de deux minutes sans action). La mesure se désactive dans Paramètres › Bibliothèque et import.

## 15. Exporter et échanger

- **Un document en Markdown / Obsidian** (bouton « Exporter les annotations en Markdown… » du volet Annotations) : fiche avec en-tête YAML, `Annotations.md`,
  `Commentaires.md`, `metadata.json`. Les liens `studylab://` de ces fichiers rouvrent le passage dans StudyLab.
- **La bibliothèque** (entière, affichée ou sélection) en coffre Obsidian, avec `annotations.csv` pour un tableur.
- **Copie annotée d'un PDF** (menu « … » du lecteur › « Exporter une copie annotée du PDF… ») : une copie lisible par
  tout lecteur PDF, avec vos annotations et commentaires. L'original n'est pas modifié.
- **Notes** en PDF ou HTML (clic droit) ; **références** en BibTeX ou CSL-JSON.

## 16. Sauvegardes et données

- Toutes vos données sont dans un dossier (Paramètres › Données › Dossier des données) :
  `~/.local/share/StudyLab/` sous Debian, `%LOCALAPPDATA%\StudyLab\` sous Windows.
- **Sauvegarde automatique** chaque jour au démarrage, **manuelle** depuis Paramètres › Données. Rotation : une par
  jour sur la semaine, une par semaine sur le mois, une par mois sur six mois ; les manuelles sont gardées.
- **Restaurer** une sauvegarde : Paramètres › Données. Une copie de sécurité des données actuelles est faite avant.
- Après une fermeture inattendue, StudyLab vérifie la base au démarrage et propose, si besoin, la dernière
  sauvegarde valide.

## 17. Personnaliser

- **Apparence** : thème clair, sombre ou celui du système ; taille de l'interface.
- **Navigation et raccourcis** :
  - ranger les espaces de la barre de gauche (glisser, Monter, Descendre) ou les masquer (case à cocher). Les
    Paramètres restent toujours visibles. « Rétablir » revient à l'ordre d'origine ;
  - cliquer dans le champ d'un raccourci puis taper la nouvelle combinaison ; la croix du champ le retire, « Tout
    rétablir » revient aux touches d'origine. Une touche déjà prise au même endroit, réservée (Échap, Entrée,
    Suppr, Ctrl + Z, copier, coller…) ou seule hors des révisions est refusée avec son motif.

Les changements s'appliquent immédiatement.

## 18. Raccourcis clavier

Touches par défaut (modifiables dans Paramètres › Navigation et raccourcis) :

| Où | Action | Touche |
|---|---|---|
| Partout | Rechercher partout | Ctrl + K |
| Partout | Plein écran | F11 |
| Partout | Réduire ou déplier la navigation | F8 |
| Partout | Replier ou afficher le panneau de gauche / de droite | F9 / F10 |
| Partout | Aller aux espaces 1 à 9 | Ctrl + 1 … Ctrl + 9 |
| Partout | Paramètres | Ctrl + , |
| Lecture | Rechercher dans le document | Ctrl + F |
| Lecture | Aller à la page (PDF) | Ctrl + G |
| Lecture | Signet | Ctrl + D |
| Lecture | Position précédente / suivante | Alt + ← / Alt + → |
| Mes notes | Nouvelle note / enregistrer | Ctrl + N / Ctrl + S |
| Séance de révision | Retourner la carte | Espace |
| Séance de révision | À revoir, Difficile, Bien, Facile | 1, 2, 3, 4 |
| Séance de révision | Modifier / suspendre la carte | E / S |

Touches fixes : Échap (fermer, terminer la séance), Entrée (valider), Suppr, Ctrl + Z (annuler), copier, couper,
coller, tout sélectionner et zoom.
