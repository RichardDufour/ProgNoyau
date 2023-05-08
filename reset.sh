#!/bin/bash

echo -e "\e[31m ~ Nettoyage des fichiers\e[0m"
make clean
echo -e "\e[31m ~ On supprime le module du kernel\e[0m"
sudo umount /dev/periph_blk1
sudo rmmod periph_blk.ko