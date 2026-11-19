# Dépannage

## « StudyLab ne peut pas ouvrir sa base de données »
- Vérifiez que le dossier de données est accessible en écriture (voir `docs/build.md`, *Données de l'application*).
- Si le message indique **une version plus récente de StudyLab** : la base a été mise à jour par une version plus
  récente. Réinstallez cette version, ou restaurez une sauvegarde du dossier `backups/`.

## Message de vérification après une fermeture inattendue
StudyLab vérifie l'intégrité de la base au démarrage suivant. Si des problèmes sont signalés, fermez StudyLab,
copiez le fichier `studylab.db` de côté, puis remplacez-le par la sauvegarde la plus récente du dossier `backups/`
(renommée en `studylab.db`).

## Compilation : « Could not find a package configuration file provided by Qt6 »
Qt 6 n'est pas trouvé. Sur Windows, définissez `CMAKE_PREFIX_PATH` vers le dossier `msvc2022_64` de Qt.
Sur Debian, lancez `./scripts/install-deps-debian.sh`.

## Interface floue ou trop petite sur écran haute densité
Réglez **Paramètres › Apparence › Taille de l'interface**. Sous Windows, vérifiez aussi la mise à l'échelle d'affichage du système.
