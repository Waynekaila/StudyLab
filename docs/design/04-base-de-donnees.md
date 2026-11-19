# 4. Structure de la base de données

Le DDL complet est dans [`schema.sql`](schema.sql). Il a été **exécuté et vérifié** sur SQLite 3.53 : création des 110 objets (tables, index, tables FTS5, triggers) sans erreur, et une recherche `energie` retrouve bien une note contenant « énergie » (accents ignorés).

## 4.1 Vue d'ensemble

```
                       ┌──────────────┐
             ┌─────────│   subjects   │◄── subject_items ──┐
             │         └──────────────┘                    │
             ▼                                              │
┌──────────┐   document_authors   ┌───────────┐            │
│ authors  │◄────────────────────►│ documents │◄───────────┤
└──────────┘                      └─────┬─────┘            │
                                        │ 1..n             │
          ┌──────────────┬──────────────┼──────────────┐   │
          ▼              ▼              ▼              ▼   │
 reading_progress  reading_sessions  page_text    annotations
                                    (texte, OCR)       │ 1..n
                                                       ▼
                                                   comments
                                                       │
     ┌──────────── links (graphe : source → cible) ────┴──────────┐
     ▼                                                            ▼
   notes ── note_aliases, attachments                 concepts, subjects…
     │
     ▼ derived_from
   cards ── card_state ── card_reviews       decks ── subjects

 Transverses : tags/entity_tags · collections/collection_items · activity · goals · settings
 Recherche  : fts_documents · fts_page_text · fts_annotations · fts_comments · fts_notes
```

## 4.2 Tables principales

| Table | Rôle | Points d'attention |
|---|---|---|
| `documents` | Un fichier de la bibliothèque et toutes ses métadonnées | `file_hash` SHA-256 unique (détection de doublons), `storage_mode` géré/lié, `status` parmi les 6 statuts demandés, suppression douce |
| `authors`, `document_authors` | Auteurs multiples, rôles, ordre | Normalisation (« Çengel, Yunus A. ») et `sort_name` |
| `tags`, `entity_tags` | Tags/mots-clés **communs** à documents, notes, annotations, commentaires, cartes | Hiérarchie par `/` (`thermo/entropie`), comme Obsidian |
| `subjects`, `subject_items` | Matières hiérarchiques, rattachement de documents, notes, paquets | Couleur = jeton de palette (cohérence visuelle) |
| `collections`, `collection_items` | Collections manuelles, intelligentes (`query_json`), listes de lecture ordonnées, projets, dossiers virtuels | Les collections intelligentes sont évaluées à la volée (requête SQL générée) |
| `reading_progress` | Dernière position exacte (`locator_json`) et réglages d'affichage par document | Écrite avec un délai (debounce 1 s) et à la fermeture |
| `reading_sessions` | Temps et pages lues | Alimente statistiques et activité |
| `page_text` | Texte par page/chapitre, natif ou OCR, + boîtes de mots OCR | Source de l'index plein texte, régénérable |
| `annotations` | Les 6 types, 5 couleurs, ancre complète, texte sélectionné, statut d'ancrage | `sort_key` = ordre de lecture ; `doc_hash` pour détecter un fichier modifié |
| `comments` | Libres ou liés à une annotation, un passage, une page ; type (question, explication, réflexion…) | Markdown |
| `notes` | Notes Markdown, 8 types, dossier virtuel, front matter | `slug` unique = nom de fichier exporté et cible des `[[liens]]` |
| `links` | **Toutes** les relations : `[[liens]]`, provenance (note dérivée d'annotations), références entre documents, prérequis entre concepts | Les rétroliens sont la requête inverse ; les liens non résolus sont conservés (`target_text`) |
| `concepts` | Nœuds légers du graphe (notion nommée) | Optionnel, promotion d'un tag en concept |
| `references_` | Références bibliographiques en CSL-JSON | Export BibTeX / CSL |
| `decks`, `cards`, `card_state`, `card_reviews` | Révision : paquets par matière, cartes avec provenance, état du planificateur, journal complet | Le journal permet de changer d'algorithme (SM-2 → FSRS) sans perte |
| `activity`, `goals` | Journal d'activité, objectifs | Tableau de bord |
| `settings` | Préférences (JSON) | La géométrie des fenêtres reste dans `QSettings` |

## 4.3 Choix de conception

1. **Clés** : `INTEGER PRIMARY KEY` pour la vitesse des jointures, plus un `uuid` stable pour l'export, les liens dans les fichiers Markdown exportés et une éventuelle synchronisation future.
2. **Polymorphisme maîtrisé** : `entity_tags`, `subject_items`, `links` utilisent `(type, id)`. SQLite ne peut pas imposer la clé étrangère ; la cohérence est assurée par les services (suppression = nettoyage dans la même transaction) et vérifiée par un test d'intégrité (`IntegrityChecker`) lancé après chaque migration et dans les tests.
3. **Ancres en JSON** (`locator_json`) : le format évolue sans migration lourde ; `unit_index` et `sort_key` sont extraits en colonnes pour trier et filtrer vite.
4. **FTS5 « external content »** : le texte n'est stocké qu'une fois ; les triggers maintiennent l'index dans la même transaction.
5. **Suppression douce** sur les objets créés par l'utilisateur (documents, annotations, commentaires, notes, cartes) : corbeille 30 jours puis purge.
6. **Pas de BLOB lourds** : couvertures, pièces jointes et fichiers restent sur disque ; la base reste petite (sauvegarde rapide).

## 4.4 Migrations

- Dossier `src/data/migrations/` : `0001_initial.sql`, `0002_….sql`, compilés en ressources Qt.
- Au démarrage : lecture de `MAX(version)` dans `schema_migrations`, **sauvegarde automatique** de la base, puis application de chaque migration manquante dans **une transaction** (`BEGIN IMMEDIATE … COMMIT`), puis `PRAGMA foreign_key_check` et `integrity_check`.
- Une base plus récente que l'application (retour à une ancienne version) est ouverte **en lecture seule** avec un message clair.
- Chaque migration a un test : base de la version N-1 avec données → migration → vérifications.

## 4.5 Requêtes types (performances visées sur 10 000 documents)

| Requête | Index utilisé | Cible |
|---|---|---|
| Annotations d'un document dans l'ordre de lecture | `ix_annotations_doc` | < 2 ms |
| Bibliothèque filtrée (statut + matière + tag), triée par titre, page de 200 | `ix_documents_status`, `ix_subject_items_entity`, `ix_entity_tags_entity` | < 20 ms |
| Recherche universelle, 5 catégories × 10 résultats avec extraits | 5 requêtes FTS5 `bm25()` + `snippet()` en parallèle sur une connexion lecture | < 150 ms |
| Rétroliens d'une note | `ix_links_target` | < 2 ms |
| Cartes dues aujourd'hui | `ix_card_state_due` | < 5 ms |

Ces cibles seront mesurées par un test de performance avec une base synthétique de 10 000 documents et 200 000 annotations.
