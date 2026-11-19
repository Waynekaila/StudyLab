# 8. Risques techniques et solutions proposées

Échelle : probabilité (P) et impact (I) de 1 (faible) à 3 (fort).

| # | Risque | P | I | Solution / mitigation |
|---|---|---|---|---|
| R1 | **Annotations décalées ou perdues** après changement de zoom, de police EPUB, de version du fichier | 2 | 3 | Ancres multi-sélecteurs (coordonnées normalisées + texte cité avec préfixe/suffixe + offsets + CFI). Jamais de coordonnées écran stockées. Si l'empreinte du fichier change : ré-ancrage automatique par recherche exacte puis approchée du texte cité (distance d'édition bornée, fenêtre autour de la page d'origine), statut `reanchored` / `fuzzy` / `orphan` visible, écran de revue des orphelines. Batterie de tests dédiée (fichier modifié, pages insérées, EPUB réédité). |
| R2 | **EPUB : rendu et pagination** variables selon les livres (CSS exotique, mises en page fixes, MathML), API de foliate-js non stable | 2 | 2 | foliate-js figé dans le dépôt derrière un adaptateur à nous ; corpus de test de 30 EPUB variés (EPUB 2/3, fixed layout, MathML, RTL) ; plan B documenté (paginateur maison + CFI d'epub.js). JavaScript des livres désactivé. |
| R3 | **Poids de Qt WebEngine** (~100–150 Mo installés, démarrage, mémoire, pilotes GPU) | 3 | 2 | Chargement paresseux (jamais au démarrage) ; un seul profil partagé ; option de rendu logiciel (`--disable-gpu`) documentée dans le dépannage ; mesure du temps de démarrage en CI. Acceptable car WebEngine sert à EPUB **et** à l'aperçu Markdown. |
| R4 | **Qt PDF limité** (pas d'écriture d'annotations, pas d'accès aux annotations existantes du fichier, API plus jeune que Poppler) | 2 | 2 | StudyLab ne dépend pas de l'écriture PDF (annotations en base). Moteur isolé derrière `IDocumentEngine` : remplaçable. Import/export d'annotations PDF prévu en V2 via PDFium direct. Prototype de validation en itération 3 (sélection précise, offsets de caractères, PDF tordus). |
| R5 | **Performance des gros PDF** (1 500+ pages, scans haute résolution) | 2 | 3 | Rendu par tuiles uniquement des pages visibles, annulation des rendus obsolètes, cache LRU borné, miniatures en cache disque, extraction de texte différée. Tests de performance automatisés avec seuils. |
| R6 | **Interface qui gèle** pendant import, indexation, OCR, graphe | 2 | 3 | Toute tâche > 16 ms hors thread UI (`QtConcurrent`, `QThreadPool` à priorité basse pour l'indexation), progression et annulation, une connexion SQLite de lecture séparée. Détecteur de blocage en mode debug (minuteur de surveillance du thread UI qui journalise les blocages > 100 ms). |
| R7 | **Corruption ou perte de données** (plantage, coupure, disque plein) | 1 | 3 | WAL + transactions courtes ; sauvegardes automatiques avec rotation ; `integrity_check` au démarrage après arrêt anormal ; brouillons auto-enregistrés ; tests de fermeture brutale ; vérification d'espace disque avant import/sauvegarde. |
| R8 | **FTS5 différent selon la plateforme** | 2 | 2 | SQLite embarqué (amalgamation) compilé identiquement partout ; test de démarrage qui vérifie `fts5` et `json1`. |
| R9 | **Écarts Windows / Debian** (chemins Unicode, polices, mise à l'échelle HiDPI fractionnaire, Wayland/X11, glisser-déposer) | 3 | 2 | CI sur les deux OS dès l'itération 1 ; chemins toujours en `QString`/`std::filesystem::path` ; polices embarquées ; tests manuels sur Windows 11 (125 %, 150 %) et Debian 13 GNOME Wayland et KDE ; liste de contrôle de recette par version. |
| R10 | **Licences** : intégrer par mégarde une dépendance GPL/AGPL (Poppler, MuPDF, djvulibre…) | 2 | 3 | Politique écrite (§3) ; `THIRD_PARTY_LICENSES.md` tenu à jour ; revue de licence obligatoire pour toute nouvelle dépendance ; les formats sous GPL seulement en greffon séparé et facultatif, ou écartés. |
| R11 | **Packaging Windows** de WebEngine (QtWebEngineProcess, ressources ICU, redistribuable MSVC) | 2 | 2 | `windeployqt --release` + vérification automatisée de l'installateur sur VM vierge en CI ; redistribuable VC++ embarqué dans l'installateur. |
| R12 | **Paquet Debian** : Qt 6.8 système vs version de développement | 1 | 2 | Développer et tester avec Qt 6.8 LTS sur les deux OS (même version que Debian 13) ; dépendances calculées par `dh_shlibdeps`. |
| R13 | **Périmètre trop large** (4 logiciels en un) → rien de fini | 3 | 3 | Découpage MVP/V1/V2 strict, critères d'acceptation mesurables, aucune entrée de navigation pour une fonctionnalité absente, revue à la fin de chaque itération. |
| R14 | **Markdown : divergences avec Obsidian** (liens, callouts, YAML, embeds) | 2 | 1 | Un coffre d'exemple exporté versionné dans `examples/export` et vérifié à chaque version ; tests d'aller-retour import/export. |
| R15 | **Accessibilité et lisibilité** (contrastes des couleurs pastel, navigation clavier) | 2 | 2 | Contrastes vérifiés (texte ≥ 4,5:1) ; couleurs d'annotation utilisées en fond, jamais pour du texte ; tout accessible au clavier ; noms accessibles (`setAccessibleName`) ; respect du réglage « réduire les animations ». |
| R16 | **OCR lent ou imprécis** | 2 | 1 | Facultatif, en file d'attente à basse priorité, page par page, reprise après interruption ; choix des langues ; résultat clairement marqué « texte OCR ». |
| R17 | **Environnement de compilation absent** côté Claude (pas de Windows, pas de GUI) | 3 | 2 | Le code est compilé et testé en CI (GitHub Actions) sur Windows et Debian ; je ne présenterai jamais un binaire comme fonctionnel sans une exécution vérifiable (tests verts, journaux de CI, captures). Les tests manuels d'interface sur ton poste restent nécessaires. |

## Points de décision ouverts

1. **Licence de StudyLab** (GPL-3.0, MIT, Apache-2.0…) : toutes les dépendances retenues la laissent libre.
2. **Dépôt GitHub** : à créer de ton côté (je ne peux pas créer de dépôt), puis je l'attache pour le MVP.
3. **Mode d'import par défaut** : copier dans la bibliothèque (recommandé) ou lier en place.
4. **DJVU** : accepter un greffon GPL séparé, ou ne pas prendre en charge ce format.
