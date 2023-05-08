## Description

Ce répertoire a été créé pour le TP5 du module programmation noyau du Master 1 SSI

## Installation

1. Cloner le dépôt avec `git clone https://github.com/RichardDufour/ProgNoyau.git`.
2. Se rendre dans le dossier du projet avec `cd ProgNoyau`.
3. Compiler le projet avec `make`.

Vous pouvez supprimer les fichiers créés avec un `make clean`

## Utilisation

Pour insérer le module créé dans le kernel, utilisez la commande suivante :
```
sudo insmod periph_blk.ko
```

Pour supprimer le module dans le kernel, utilisez la commande suivante :
```
sudo rmmod periph_blk.ko
```

## Auteur
Richard DUFOUR |
N°Etudiant : 22108382