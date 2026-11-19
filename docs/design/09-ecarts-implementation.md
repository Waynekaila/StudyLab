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
