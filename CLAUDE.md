# StudyLab — notes pour les contributeurs

- Application de bureau C++20 / Qt 6 (Widgets), CMake. Langue du projet : **français** (interface, commentaires, docs).
- Conception de référence : `docs/design/` (vision, architecture, schéma, maquettes, plan MVP/V1/V2). La suivre ;
  toute décision qui s'en écarte est documentée dans `docs/design/`.
- Couches : `core` → `data` → `services/*` → `ui` → `app`. Une couche ne dépend que des couches en dessous.
  Pas de SQL dans `ui/`. Erreurs via `studylab::Result<T>`, jamais d'exceptions.
- Base : SQLite embarqué (`third_party/sqlite`). Toute évolution du schéma = nouvelle migration
  `src/data/migrations/NNNN_nom.sql` (ne jamais modifier une migration publiée) + test dans `tests/unit/tst_migrator.cpp`.
- Thème : couleurs uniquement via `ui/theme/Tokens` ; styles dans la feuille générée par `ThemeManager`.
- Aucune entrée de menu ni bouton pour une fonctionnalité non terminée.
- Avant de pousser : `./scripts/build.sh debian-debug` (compile + tests). Les tests d'interface utilisent `QT_QPA_PLATFORM=offscreen`.
