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
