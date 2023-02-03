# -*- coding: utf-8 -*-

# Auteur : Charles PIGNEROL, CEA/DAM/DSSI
# Version pour Python 3

import NECCompleter

# Création d'une instance unique de NECCompleter et accès à ses services
# depuis du code C/C++ via des fonctions.

# Instanciation de l'outil de completion :
completer = NECCompleter.NECCompleter ( )

# Accès à l'instance, par exemple via un exec de code python.
def instance ( ):
	return completer

# Retourne True si l'instance est un proxy Swig, False dans le cas contraire.
def isSwigProxy ( ):
	return completer.isProxy

# Retourne la dernière complétion obtenue.
def completion ( ):
	return completer.completion



