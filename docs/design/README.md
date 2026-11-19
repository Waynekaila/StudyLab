# StudyLab — Phase 1 : recherche et conception

Livrables, dans l'ordre demandé :

1. [Vision fonctionnelle finale](01-vision.md)
2. [Architecture technique](02-architecture.md)
3. [Comparaison des bibliothèques C++ / Qt (licences, maintenance, performances)](03-comparaison-bibliotheques.md)
4. [Structure de la base de données](04-base-de-donnees.md) + [`schema.sql`](schema.sql) (testé sur SQLite 3.53)
5. [Maquettes détaillées des 9 écrans](05-maquettes.md) + [`maquettes/studylab-maquettes.html`](maquettes/studylab-maquettes.html) + [`maquettes/png/`](maquettes/png/)
6. [Arborescence du projet et structure d'export](06-arborescence.md)
7. [Découpage MVP, V1, V2 et plan d'itérations](07-versions-mvp-v1-v2.md)
8. [Risques techniques et solutions](08-risques.md)

## Décisions clés en une ligne chacune

- Qt 6.8 LTS, C++20, CMake, **Qt Widgets** avec un thème maison (pas de QML).
- PDF : **Qt PDF** (moteur PDFium, LGPL). EPUB : **QtWebEngine + foliate-js** (MIT). Aucun composant GPL/AGPL.
- Données : **SQLite embarqué** (FTS5, JSON, sauvegarde à chaud), ancres d'annotations multi-sélecteurs indépendantes du zoom et de la mise en page.
- Markdown : **md4c** + aperçu WebEngine + **KaTeX** ; export en coffre compatible Obsidian.
- OCR : **Tesseract** en module facultatif. Révision : SM-2 puis FSRS.
- Distribution : **Inno Setup** (Windows x64) et **.deb** via debhelper (Debian 13), CI sur les deux OS dès le MVP.
