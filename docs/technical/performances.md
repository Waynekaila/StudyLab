# Performances (version 1.0.0)

Mesures faites avec le banc d'essai `tests/ui/tst_volume.cpp` (V2-7), qui remplit une base réaliste puis chronomètre
les requêtes des services et l'affichage de chaque écran de la fenêtre principale.

## Volumes

| | ×1 (CI) | ×5 |
|---|---|---|
| Documents | 3 000 | 15 000 |
| Pages de texte indexées | 60 000 | 300 000 |
| Annotations / commentaires | 30 000 / 6 000 | 150 000 / 30 000 |
| Notes reliées | 3 000 | 15 000 |
| Cartes / révisions | 20 000 / 60 000 | 100 000 / 300 000 |
| Séances de lecture | 10 000 | 50 000 |
| Taille de la base | 149 Mo | 747 Mo |

Le volume ×1 correspond déjà à plusieurs années d'études intensives ; ×5 est un cas extrême.

## Résultats

Compilation `debian-release`, machine de développement à 4 cœurs, plateforme `offscreen`, base dans le cache disque
(à froid, ajouter le temps de lecture du disque). En millisecondes.

| Mesure | ×1 | ×5 |
|---|---:|---:|
| Ouverture de la base et migrations | 1 | 1 |
| **Fenêtre principale affichée (accueil compris)** | **818** | 3 497 |
| Bibliothèque : tous les documents | 39 | 145 |
| Bibliothèque : tri par auteur | 26 | 193 |
| Bibliothèque : filtre texte | 2 | 11 |
| Recherche partout (Ctrl + K) : un mot | 82 | 391 |
| Recherche partout : trois mots | 140 | 562 |
| Recherche partout : préfixe | 1 | 1 |
| Annotations d'un document | < 1 | < 1 |
| Mes notes : liste / rétroliens | 31 / < 1 | 112 / < 1 |
| Graphe : données | 264 | 871 |
| Révisions : cartes dues, paquets, statistiques (chacune) | 53 | 324 |
| Activité : 12 mois / série en cours / journal 30 jours | 58 / 67 / 17 | 370 / 399 / 89 |
| Écran Bibliothèque | 56 | 63 |
| Écran Mes notes | 112 | 775 |
| Écran Matières / Collections | 45 / 43 | 36 / 33 |
| Écran Graphe (premier affichage) | 344 | 1 458 |
| Écran Révisions (premier affichage) | 241 | 985 |
| Écran Activité | 287 | 818 |
| Écran Références | 54 | 642 |
| Retour à l'Accueil | 408 | 1 458 |

L'objectif de conception (fenêtre utilisable en moins de 1,5 s avec 5 000 documents,
[02-architecture](../design/02-architecture.md#démarrage-rapide)) est tenu : 0,8 s pour 3 000 documents, temps qui
croît à peu près linéairement avec le volume.

## Optimisations de la V2-7

Avant / après, compilation de débogage, volume ×1 :

| Mesure | Avant | Après | Changement |
|---|---:|---:|---|
| Fenêtre principale | 3 930 | 1 029 | Écrans Graphe, Révisions, Références et Accueil remplis à leur premier affichage ; icônes SVG dessinées une fois (cache par nom, couleur et taille) ; aperçu Markdown (WebEngine) créé quand il devient visible ; couvertures absentes non ouvertes |
| Construction de Mes notes | 950 | 233 | Cache d'icônes (l'arbre en dessinait une par ligne) |
| Écran Graphe | 2 200 | 440 | Scène sans index spatial (`NoIndex`) : l'arbre BSP était reconstruit à chaque pas de la disposition ; étiquettes de moins de 7 px non dessinées ; texte abrégé mis en cache par nœud |
| Activité sur 12 mois | 276 | 123 | Lecture des seuls instants et actions, bornes des jours comparées comme chaînes ISO (recherche dichotomique) |
| Série en cours | 318 | 151 | Remontée par tranches de 35 jours, au plus 400 jours |
| Compteurs des paquets | 99 | 68 | Une requête groupée par paquet au lieu de deux requêtes par paquet |

## Pistes restantes

À partir de 15 000 documents, le démarrage dépasse 3 s. Les postes les plus lourds, dans l'ordre :

1. **Accueil** : il additionne une dizaine de requêtes de comptage (révisions du jour, objectif, progression par
   matière) relancées à chaque retour sur l'écran ; un cache invalidé par les signaux des services les éviterait.
2. **Révisions** : `card_state` est parcourue en entier pour chaque compteur ; un index partiel sur
   `(state, due_at)` ou des compteurs tenus à jour demanderaient une migration.
3. **Mes notes** : l'arbre est reconstruit en entier ; un modèle Qt paresseux par dossier.
4. **Recherche à plusieurs mots** : requêtes FTS5 par catégorie lancées l'une après l'autre ; elles pourraient
   l'être en parallèle sur une connexion de lecture.

## Reproduire

```sh
cmake --preset debian-release && cmake --build --preset debian-release --target tst_volume
QT_QPA_PLATFORM=offscreen QTWEBENGINE_DISABLE_SANDBOX=1 STUDYLAB_VOLUME_SCALE=5 build/debian-release/tests/tst_volume
```

Le récapitulatif s'affiche à la fin. `STUDYLAB_VOLUME_KEEP=/chemin/base.db` conserve la base pour l'examiner
(`sqlite3 base.db "EXPLAIN QUERY PLAN …"`). En CI, le test tourne au volume ×1 en compilation de débogage, avec des
plafonds larges (4 s pour la fenêtre, 2,5 s par écran) : il détecte une régression d'un ordre de grandeur, pas
quelques pour cent.
