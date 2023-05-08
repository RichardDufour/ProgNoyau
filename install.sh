#!/bin/bash

echo -e "\e[31m ~ Test du module périphérique bloc\e[0m"
sudo -v

echo -e "\e[31m ~ Compilation\e[0m"
make
echo -e "\e[31m ~ On insére le module\e[0m"
sudo insmod periph_blk.ko

echo -e "\e[31m ~ Création d'une partition\e[0m"
sudo fdisk /dev/periph_blk <<EOF
n
p



w
EOF

echo -e "\e[31m ~ Formatage de la partition\e[0m"
sudo mkfs.ext4 /dev/periph_blk1
echo -e "\e[31m ~ Montage de la partition\e[0m"
sudo mount /dev/periph_blk1 periph_blk/

echo -e "\e[31m ~ Nettoyage des fichiers\e[0m"
make clean
