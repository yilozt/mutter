#!/bin/bash
ver="40.5"

cd $(dirname $0)

rm *.[ch]
wget https://gitlab.gnome.org/GNOME/gnome-shell/-/raw/${ver}/src/shell-blur-effect.c
wget https://gitlab.gnome.org/GNOME/gnome-shell/-/raw/${ver}/src/shell-blur-effect.h