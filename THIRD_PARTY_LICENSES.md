# Composants tiers

StudyLab est distribué sous GPL-3.0-or-later. Les composants ci-dessous gardent leur propre licence, compatible avec la GPL-3.0.

| Composant | Version | Licence | Utilisation | Emplacement |
|---|---|---|---|---|
| Qt (Core, Gui, Widgets, Svg, Concurrent) | 6.4 → 6.8 | LGPL-3.0 (liaison dynamique) | Socle de l'application | Installé sur le système |
| Qt PDF (moteur PDFium) | 6.4 → 6.8 | LGPL-3.0 (Qt) ; PDFium : BSD-3-Clause | Métadonnées et rendu PDF | Installé sur le système |
| Qt WebEngine, Qt WebChannel (moteur Chromium) | 6.4 → 6.8 | LGPL-3.0 (Qt) ; Chromium : BSD-3-Clause et autres licences permissives | Rendu des EPUB | Installé sur le système |
| foliate-js | commit `78914ae` (2026-05-01) | MIT | Pagination, CFI, recherche et surlignage des EPUB | `src/ui/resources/epub-reader/foliate/` (`LICENSE`, `VERSION`) |
| markdown-it | 14.1.0 | MIT | Rendu Markdown des commentaires | `src/ui/resources/epub-reader/markdown/vendor/` (`LICENSE-markdown-it`, `VERSION`) |
| KaTeX (et ses polices) | 0.16.22 | MIT (polices : MIT) | Rendu des formules LaTeX | `src/ui/resources/epub-reader/markdown/vendor/katex/` (`LICENSE`) |
| SQLite | 3.50.4 | Domaine public | Base de données, recherche FTS5 | `third_party/sqlite/` |
| Inter | 4.0 | SIL OFL 1.1 | Police de l'interface | `src/ui/resources/fonts/` (`OFL.txt`) |

Toute nouvelle dépendance doit être ajoutée ici avec sa licence, après vérification de sa compatibilité
(politique : aucune dépendance GPL incompatible ni AGPL ; voir `docs/design/03-comparaison-bibliotheques.md`).
