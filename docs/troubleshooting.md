# Dépannage

Voir aussi le [guide utilisateur](user-guide/README.md). Vos données sont dans `~/.local/share/StudyLab/` (Debian)
ou `%LOCALAPPDATA%\StudyLab\` (Windows) ; la variable `STUDYLAB_DATA_DIR` permet d'en utiliser un autre.

## Démarrage et base de données

### « StudyLab ne peut pas ouvrir sa base de données »
- Vérifiez que le dossier de données est accessible en écriture (voir `docs/build.md`, *Données de l'application*).
- Si le message indique **une version plus récente de StudyLab** : la base a été mise à jour par une version plus
  récente. Réinstallez cette version, ou restaurez une sauvegarde du dossier `backups/`.

### Message de vérification après une fermeture inattendue
StudyLab vérifie l'intégrité de la base au démarrage suivant et propose « Restaurer la dernière sauvegarde ». La base
actuelle est alors conservée dans le dossier des sauvegardes. Pour le faire à la main : fermez StudyLab, copiez
`studylab.db` de côté, puis remplacez-le par la sauvegarde la plus récente du dossier `backups/` (renommée en
`studylab.db`).

### Rien ne s'ouvre quand je lance StudyLab une seconde fois
C'est voulu : une seule fenêtre par dossier de données. Le second lancement (ou un lien `studylab://` cliqué dans un
export) active la fenêtre déjà ouverte, qui est peut-être réduite ou sur un autre bureau.

### Démarrage lent
Avec plusieurs milliers de documents, la fenêtre s'affiche en environ une seconde (voir
[`technical/performances.md`](technical/performances.md)). Si c'est bien plus lent :
- un antivirus qui analyse `studylab.db` à chaque accès peut ralentir fortement : excluez le dossier de données ;
- un dossier de données sur un partage réseau ou une clé USB est déconseillé (SQLite en mode WAL).

## Import

### Un fichier est « ignoré » ou « refusé » dans le rapport d'import
- **Doublon** : le même contenu est déjà dans la bibliothèque, même sous un autre nom.
- **Fichier invalide** : extension trompeuse (faux PDF), archive EPUB abîmée.
- **Protégé (DRM)** : les EPUB chiffrés ne peuvent pas être lus.
- **Image dans un dossier** : le parcours d'un dossier ignore les images ; choisissez-les ou glissez-les.

### Un document importé en mode « Lier » ne s'ouvre plus
Le fichier d'origine a été déplacé ou renommé. Remettez-le à sa place, ou retirez le document et réimportez-le
(en mode Copier, recommandé).

## Lecture

### Un EPUB, un fichier Markdown ou Word reste blanc
Ces formats sont affichés par Qt WebEngine (Chromium). Sur certaines cartes graphiques ou machines virtuelles,
l'accélération matérielle échoue :
- Debian : lancez `QTWEBENGINE_CHROMIUM_FLAGS="--disable-gpu" studylab` ; si cela suffit, ajoutez la variable à
  votre session ;
- Windows : mettez à jour le pilote graphique ; à défaut, définissez la variable d'environnement utilisateur
  `QTWEBENGINE_CHROMIUM_FLAGS` à `--disable-gpu`.

L'aperçu des notes et les formules utilisent le même moteur : le même réglage s'applique.

### Un PDF demande un mot de passe
Il est chiffré. Le mot de passe est demandé à chaque ouverture et n'est pas enregistré. La copie annotée d'un PDF
chiffré n'est pas possible.

### « Exporter une copie annotée » est refusé
StudyLab ajoute les annotations sans réécrire le fichier ; il refuse les PDF chiffrés et ceux dont la structure a dû
être réparée à la lecture, pour ne jamais produire un fichier abîmé. Les pages tournées sont ignorées et comptées
dans le rapport.

### Les annotations d'un PDF apparaissent deux fois
Après un import partiel (formes simplifiées, formulaires), le lecteur continue de dessiner celles du fichier. Le menu
« … » du lecteur permet de cesser de les dessiner pour ce document.

## Recherche et OCR

### Le texte d'un document n'est pas trouvé par Ctrl + K
- L'indexation se fait en tâche de fond : voyez son état dans Paramètres › Données, et **Réindexer** au besoin.
- Un scan ou une image n'a pas de texte : il faut l'OCR.

### L'OCR ne se propose pas, ou ne lit rien
- Tesseract doit être installé : Debian `sudo apt install tesseract-ocr tesseract-ocr-fra` ; Windows, l'installateur
  « Tesseract-OCR » (UB Mannheim) dans Program Files.
- Installation ailleurs : `STUDYLAB_TESSERACT` = chemin de la bibliothèque Tesseract, `TESSDATA_PREFIX` = dossier des
  langues.
- La langue du document doit faire partie des langues choisies (Paramètres › Données). Après un changement,
  « Relire » relance les documents déjà lus.

## Révisions

### Aucune carte n'est due alors que j'en ai créé
Les nouvelles cartes sont limitées par jour (20 par défaut, Réglages du paquet). La journée commence à 4 h : une
séance après minuit compte pour la veille. Une carte suspendue n'est plus présentée.

### Un raccourci de la séance ne répond pas
Vérifiez Paramètres › Navigation et raccourcis : la touche a peut-être été changée ou retirée. « Tout rétablir »
revient aux touches d'origine.

## Affichage

### Interface floue ou trop petite sur écran haute densité
Réglez **Paramètres › Apparence › Taille de l'interface**. Sous Windows, vérifiez aussi la mise à l'échelle
d'affichage du système.

### Un espace a disparu de la barre de gauche
Il a été masqué : Paramètres › Navigation et raccourcis, cochez-le, ou « Rétablir ». Ctrl + 1 à Ctrl + 9 y mènent
aussi tant qu'ils ne sont pas retirés.

## Signaler un problème

Lancez StudyLab depuis un terminal avec `QT_LOGGING_RULES="studylab.*.debug=true"` pour obtenir le journal détaillé,
et joignez-le (sans vos documents) à un ticket sur le dépôt GitHub, avec la version (`studylab --version`) et le
système.

## Compilation

### « Could not find a package configuration file provided by Qt6 »
Qt 6 n'est pas trouvé. Sur Windows, définissez `CMAKE_PREFIX_PATH` vers le dossier `msvc2022_64` de Qt.
Sur Debian, lancez `./scripts/install-deps-debian.sh`.

### Les tests d'interface échouent sans écran
Lancez-les avec `QT_QPA_PLATFORM=offscreen` ; dans un conteneur root, ajoutez `QTWEBENGINE_DISABLE_SANDBOX=1`.
