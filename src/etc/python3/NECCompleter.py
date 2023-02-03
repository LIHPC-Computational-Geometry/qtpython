# -*- coding: utf-8 -*-

# Auteur : Charles PIGNEROL, CEA/DAM/DSSI
# Version pour Python 3

import rlcompleter
import inspect
import builtins
import __main__

# Retourne par récursion l'objet à la base de l'instruction
# transmise en argument.
def getObject (instruction):
	# Y a-t-il indirection ?
	list_names = instruction.split(".")
	# On va chercher dans l'ensemble des fonctions de l'application :
	globals	= __main__.__dict__
	if (1 >= len (list_names)):
		return globals [instruction]	# Pas d'indirection, on a le nom de l'instance
	else:	# On décompose jusquà tomber sur un nom sans indirection
		return globals [list_names [0]]

# Retourne True si l'objet invoqué dans le morceau d'instruction transmis en
# argument est présumé être un proxy SWIG, sinon False.
def isSwigProxy (instruction):
	obj	= getObject (instruction)
	infos	= str (obj)
	if -1 != infos.find ('Swig Object'):
		return True
	return False


# Retourne la fonction dont le nom est transmis en argument.
# Procède de manière récursive lorsqu'il y a des indirections via '.' :
# x.y.z ...
def getFunction (name):
	def inner (obj, list_name):
		if list_name:
			return inner (getattr(obj, list_name[0]), list_name[1:]) 
		return obj
	# Y a-t-il indirection ?
	list_names = name.split(".")
	# On va chercher dans l'ensemble des fonctions de l'application :
	globals	= __main__.__dict__
	if (1 >= len (list_names)):
		return globals [name]	# Pas d'indirection, on a le nom e la fonction
	else:	# On décompose jusquà tomber sur un nom sans indirection
		return inner (globals [list_names [0]], list_names [1:]) 


class NECCompleter (rlcompleter.Completer):
	completion	= None
	isProxy		= 'False'


	def __init__(self, namespace = None):
		rlcompleter.Completer.__init__ (self, namespace)

	def currentCompletion ( ):
		return self.completion

	def complete (self, text, state):
		self.completion		= None
		self.isProxy		= 'False'
		# rlcompleter (completion via readline) nous retourne un nom de
		# fonction possible, avec la parenthèse ouvrante, mais pas d'arguments,
		# par exemple : sys.stdout.write (
		self.completion	=  rlcompleter.Completer.complete (self, text, state)
		try:
		# On essaye d'avoir les noms des arguments, en python (< 3.5) c'est non
		# typé. La méthode suivante ne marche pas avec les fonctions builtin.
		# On dépouille lasortie de rlcompleter de manière à ne conserver que le
		# nom de la fonction :
			if (-1 != self.completion.find ('(')):
				funcname	= self.completion.split ('(')[0]
			else:
				funcname	= self.completion;
			funcname	= funcname.strip ( )
			if True == isSwigProxy (funcname):
				self.swigCompletion (funcname)
			else:
				self.normalCompletion (funcname)
		except:
			pass

	# Complétion dans un context normal (non SWIG).
	def normalCompletion (self, funcname):
		self.isProxy	= 'False'
		# On récupère les noms des arguments, et on reconstitue la signature :
		argSpec	= inspect.getargspec (getFunction (funcname))
		args	= argSpec[0]
		argNum	= len (args)
		funcname	= funcname + ' ('
		if 0 != argNum:
			for i in range (argNum - 1):
				funcname	= funcname + args[i] +', '
			funcname	= funcname + args[argNum - 1]
		funcname	= funcname + ')'
		self.completion	= funcname

	# Complétion dans un contexte SWIG : minimale, l'objet est balisé comme
	# proxy Swig.
	def swigCompletion (self, funcname):
		self.isProxy	= 'True'
		self.completion	= funcname + ' ( )'

#try:
#	import readline
#	readline.parse_and_bind ('tab: complete')
#except ImportError:
#	pass
#else:
#	readline.set_completer(NECCompleter().complete)
