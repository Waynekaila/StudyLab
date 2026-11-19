# Composants tiers

StudyLab est distribué sous GPL-3.0-or-later. Les composants ci-dessous gardent leur propre licence, compatible avec la GPL-3.0.

| Composant | Version | Licence | Utilisation | Emplacement |
|---|---|---|---|---|
| Qt | 6.4 → 6.8 | LGPL-3.0 (liaison dynamique) | Socle de l'application | Installé sur le système |
| SQLite | 3.50.4 | Domaine public | Base de données, recherche FTS5 | `third_party/sqlite/` |
| Inter | 4.0 | SIL OFL 1.1 | Police de l'interface | `src/ui/resources/fonts/` (`OFL.txt`) |

Toute nouvelle dépendance doit être ajoutée ici avec sa licence, après vérification de sa compatibilité
(politique : aucune dépendance GPL incompatible ni AGPL ; voir `docs/design/03-comparaison-bibliotheques.md`).
