# -*- coding: utf-8 -*-

# Auteur : Charles PIGNEROL, CEA/DAM/DSSI, sur une bonne idée de
# Jean-Denis LESAGE, CEA/DAM/DSSI

import NECPdb

# Création d'une instance unique de NECPdb et accès à ses services
# depuis du code C/C++ via des fonctions.

# Instanciation de l'outil d'aide à la mise au point :
dbg = NECPdb.NECPdb ( )

# Accès à l'instance, par exemple via un exec de code python.
def instance ( ):
	return dbg

# Retourne le fichier où est arrêté l'outil de mise au point.
def fileName ( ):
	return dbg.currentFile ( )

# Retourne la ligne où est arrêté l'outil de mise au point.
def lineNo ( ):
	return dbg.currentLine ( )


