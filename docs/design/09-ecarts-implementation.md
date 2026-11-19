# 9. Écarts entre la conception et l'implémentation

Toute décision qui s'écarte des documents 01 à 08 est consignée ici, avec sa raison.

| Itération | Décision | Raison | Réversible ? |
|---|---|---|---|
| 1 | SQLite 3.50.4 récupéré depuis le paquet npm `better-sqlite3` (amalgamation officielle inchangée, un correctif de constante MSVC) | sqlite.org inaccessible depuis l'environnement de développement | Oui : remplacer `third_party/sqlite/` |
| 2 | Lecture des EPUB avec `QZipReader` (classe privée de Qt) au lieu de **libzip** | Évite une dépendance supplémentaire (vcpkg sur Windows) ; `QZipReader` est stable depuis Qt 4 et sert à Qt pour les formats ODF/OOXML. L'adaptateur reste isolé dans `engines/epub/EpubPackage` : passer à libzip ne touche qu'un fichier. | Oui |
| 2 | Migration `0002` : `fts_documents` passe en `contentless_delete=1` | Le schéma initial (table *contentless* sans cette option) ne permettait pas de supprimer ou mettre à jour l'index d'un document sans connaître ses anciennes valeurs | — (migration publiée) |
| 2 | `authors.sort_name` stocke une clé de tri sans accents ni majuscules (« cengel, yunus a. ») | Tri correct des auteurs accentués (Çengel avant Dajoz), sans collation ICU | Oui |
| 2 | L'année d'un PDF n'est pas déduite de `CreationDate` | C'est la date de production du fichier, pas de publication : l'utilisateur saisit l'année dans le panneau de détail | — |
| 2 | Un PDF protégé par mot de passe est importé (titre tiré du nom du fichier, sans couverture) ; le mot de passe sera demandé à l'ouverture (itération 3) | Pas de boîte de dialogue bloquante au milieu d'un import de 500 fichiers | — |
| 2 | Mode d'import par défaut : **copie** dans la bibliothèque (réglable : *Paramètres › Bibliothèque et import*) | Point ouvert n° 3 de `08-risques.md`, tranché selon la recommandation | Oui (réglage) |
| 3 | Rendu PDF par **Qt PDF** (`QPdfDocument`) dans une vue maison (`PdfView`, tuiles de 512 px, cache LRU de 160 Mo) plutôt que `QPdfView` | `QPdfView` ne permet ni la rotation, ni la double page avec couverture, ni le dessin des annotations à venir | Oui |
| 3 | Les liens internes des PDF ne sont actifs qu'avec Qt ≥ 6.6 (`QPdfLinkModel`) | Debian 13 et la CI Windows fournissent Qt ≥ 6.8 ; Qt 6.4 (Ubuntu 24.04) compile sans les liens | — |
| 3 | La position de lecture est enregistrée comme (page, décalage en points) ; la progression affichée est (page courante + 1) / nombre de pages | Position stable quel que soit le zoom ou la taille de la fenêtre | — |
| 3 | Les signets de lecture sont reportés à l'itération 4, avec les annotations | Ils partagent la table `annotations` (type `bookmark`) et le volet Annotations | — |
| 4 | La note d'une annotation est un commentaire lié (`comments.anchor_kind = 'annotation'`, type « réflexion ») | Une seule source pour le texte libre : le volet Commentaires (itération 6) l'affichera sans migration | — |
| 4 | Le type « marker » du schéma est exposé comme **zone rectangulaire** (figures, équations, texte non sélectionnable) | Le surligneur libre à main levée n'apporte rien de plus pour l'étude et ne s'ancre pas au texte | Oui |
| 4 | Sur une page en thème sombre, les couleurs d'annotation restent celles du thème clair | La page PDF reste blanche : les teintes sombres y seraient illisibles | Oui |
| 4 | L'export Markdown n'inclut pas encore de liens `studylab://` | Aucun gestionnaire d'URL n'est installé avant les installeurs (itération 7) ; un lien mort n'a pas sa place dans une note | Oui |
| 4 | `Commentaires.md` est reporté à l'itération 6 | Il dépend du volet Commentaires | — |
| 5 | Les fichiers d'un EPUB sont lus par `XMLHttpRequest` sur le schéma `studylab-epub://` (et non `fetch`) | `fetch` n'accepte les schémas personnalisés qu'à partir de Qt 6.6 ; Qt 6.4 (Ubuntu 24.04) reste pris en charge | Oui |
| 5 | foliate-js est figé (`foliate/VERSION`) et complété de trois polyfills (`Object.groupBy`, `Map.groupBy`, `Promise.withResolvers`) | Chromium 102 de Qt 6.4 ne les a pas ; sans effet avec Qt 6.8 | Oui |
| 5 | Les scripts des livres sont bloqués par la politique de sécurité (CSP) de la page du lecteur, héritée par les chapitres (documents `blob:`) | foliate-js a besoin de `allow-scripts` sur ses cadres pour ses propres écouteurs ; la CSP bloque les scripts en ligne et `blob:` du livre | — |
| 5 | Réglages d'affichage du texte communs à tous les EPUB (pas par livre) | Un lecteur garde en général la même typographie ; le réglage par livre pourra s'ajouter sans migration (`reading_positions.view`) | Oui |
| 5 | Polices proposées : celle du livre, Georgia (empattements) ou Inter (sans) ; Literata n'est pas embarquée | Évite ~1 Mo de polices dans l'exécutable pour le MVP | Oui |
| 5 | Pas d'outil Zone ni de note ponctuelle dans les EPUB : une note s'attache à un passage | Le texte est recomposé : une zone ou un point n'a pas de position stable | — |
| 5 | Emplacement d'une annotation EPUB affiché en pourcentage du livre (« 42 % ») ; groupe de repli « Section N » | Un EPUB n'a pas de pages fixes | — |
| 5 | Signet EPUB : point CFI au début de la page affichée ; le bouton reflète un signet présent dans la page visible | Équivalent du signet de page des PDF | — |
