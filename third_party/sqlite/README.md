# SQLite 3.50.4 (amalgamation)

- Fichiers : `sqlite3.c`, `sqlite3.h` — domaine public (https://www.sqlite.org/copyright.html).
- Provenance : amalgamation 3.50.4 telle que distribuée dans le paquet npm `better-sqlite3@12.4.1` (`deps/sqlite3/`),
  générée à partir de l'archive officielle `sqlite-src-3500400.zip`. Ce paquet applique un seul correctif,
  purement numérique, pour la compilation MSVC (constantes `30.0*86400.0` → `2592000.0` et `365.0*86400.0` → `31536000.0`
  dans la table des unités de date) : le comportement est identique à l'original.
- Options de compilation : voir `CMakeLists.txt` (FTS5 activé, JSON intégré, extensions dynamiques désactivées).

Mise à jour : remplacer les deux fichiers par l'amalgamation officielle d'une version plus récente
(https://www.sqlite.org/download.html), puis lancer les tests (`tst_database` vérifie la présence de FTS5).
