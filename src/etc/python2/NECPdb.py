# -*- coding: utf-8 -*-

"""
Outil d'aide à la mise au point d'un script python exécuté par un code C/C++.

Utilise à cet effet la classe pdb.Pdb, débogueur interactif Python, mais en
désactive la boucle interactive. La gestion des aspects interactifs est laissée
au code C/C++ appelant.

Auteur : Charles PIGNEROL, CEA/DAM/DSSI, sur une bonne idée de
Jean-Denis LESAGE, CEA/DAM/DSSI

Version 5.0.0
"""

import bdb
import pdb
import inspect
import QtPythonCalls


class NECPdb (pdb.Pdb):

	filename	= ''
	lineno		= -1
	dbginput	= None

	def __init__(self):
		pdb.Pdb.__init__ (self)
		self.filename		= ''
		self.lineno			= -1
		self.currentLineno	= -1
		self.dbginput	= None	# open ('/tmp/pignerol/input', 'r')

	def currentFile (self):
		return self.filename

	def currentLine (self):
		return self.lineno

	def set_cmd_stream (self, filename) :
		self.dbginput	= open (filename, 'r')

	def user_line (self, frame):
		self.filename		= frame.f_code.co_filename
		self.lineno			= frame.f_lineno
		self.currentLineno	= frame.f_lineno
		if self._wait_for_mainpyfile:
			if (self.mainpyfile != self.canonic(frame.f_code.co_filename)
				or frame.f_lineno<= 0):	
				return
		if frame.f_code.co_filename == "<string>":
			pdb.Pdb.set_continue (self)
			return
			self._wait_for_mainpyfile = 0
		if getattr(self,"currentbp",False):
#			print inspect.stack ( )
#			print "FRAME=",frame
#			print dir (frame)
#			print ("F_CODE :")
#			print dir (frame.f_code)
#			print "F_CODE.co_filename:", frame.f_code.co_filename," frame.f_lineno:", frame.f_lineno
			filename	= frame.f_code.co_filename
			lineno		= self.lineno
			QtPythonCalls.atBreakPoint (filename, lineno)
			QtPythonCalls.processEvents (filename, lineno)
# Pour sortir de la boucle principale python : raise bdb.BdbQuit
#			raise bdb.BdbQuit
		if self.bp_commands(frame):
			self.interaction(frame, None)


#	def interaction(self, frame, traceback):
#		self.setup(frame, traceback)
#		self.print_stack_entry(self.stack[self.curindex])
#		self.forget()


	def cmdloop (self, intro=None):
		self.preloop()
		if self.use_rawinput and self.completekey:
			try:
				import readline
				self.old_completer = readline.get_completer()
				readline.set_completer(self.complete)
				readline.parse_and_bind(self.completekey+": complete")
			except ImportError:
				pass
		try:
			if intro is not None:
				self.intro = intro
			if self.intro:
				self.stdout.write(str(self.intro)+"\n")
			stop = None
			while not stop:
				if self.cmdqueue:
					line = self.cmdqueue.pop(0)
				else:
					if self.use_rawinput:
						try:
							line = self.dbginput.readline (100)
#							line = raw_input(self.prompt)
							if 0 == len (line) :
								filename	= self.filename
								lineno		= self.lineno
# ATTENTION : en python >= 2.7.x on peut avoir self.lineno == None, ce qui
# pose des problèmes à PyArg_ParseTuple dans QtPythonCalls.
# => On passe -1 à la place.
#								if None == lineno :
#									lineno	= -1
								QtPythonCalls.processEvents (filename, lineno)
								continue
						except EOFError:
							line = 'EOF'
					else:
						self.stdout.write(self.prompt)
						self.stdout.flush()
						line = self.stdin.readline()
						if not len(line):
							line = 'EOF'
						else:
							line = line[:-1] # chop \n
				line = self.precmd(line)
				stop = self.onecmd(line)
				stop = self.postcmd(stop, line)
			self.postloop()
		finally:
			pass


	def trace_dispatch (self, frame, event, arg) :
#		print "TRACE_DISPATCH. LINENO=", frame.f_lineno
		self.filename	= frame.f_code.co_filename
		self.lineno		= frame.f_lineno
#		print "TRACE DISPATCH. FILE=", frame.f_code.co_filename, " LINE=", frame.f_lineno
# self.lineno est modifié durant le dispatching (-> éventuellement -1) :
		filename	= frame.f_code.co_filename
		lineno		= self.lineno
		ret	= bdb.Bdb.trace_dispatch (self, frame, event, arg)
		QtPythonCalls.lineIsBeingProcessed (filename, lineno)
		return ret

	def dispatch_line(self, frame):
		if self.use_rawinput:
			try :
				line	= self.dbginput.readline (100)
				if 0 != len (line) :
					cmd, arg, lineparsed	= self.parseline (line)
					if cmd == 'tbreak' :
						l	= int(arg)
						self.set_break (frame.f_code.co_filename, l, 1)
					elif cmd == 'stop' :
						self.set_break (frame.f_code.co_filename, frame.f_lineno, 1)
			except :
				pass

		return pdb.Pdb.dispatch_line (self, frame)


	def do_clear (self, arg) :
		"""Three possibilities, tried in this order:
		clear -> clear all breaks, ask for confirmation (not for NECPdb).
		clear file:lineno -> clear all breaks at file:lineno
		clear bpno bpno ... -> clear breakpoints by number"""
		if not arg :	# => clear all
			self.clear_all_breaks ( )
			return
		pdb.Pdb.do_clear (self, arg)


	def postcmd (self, stop, line) :
		filename	= self.filename
		lineno		= self.currentLineno
		if 0 != len (line) :
# V 1.14.0 : pour des commandes Pdb on n'exécute pas QtPythonCalls.lineProcessed
			cmd, arg, lineparsed	= self.parseline (line)
			if ((cmd == 'break') or (cmd == 'tbreak') or (cmd == 'clear')
				or (cmd == 'c') or (cmd == 'stop')) :
				return pdb.Pdb.postcmd (self, stop, line)
		QtPythonCalls.lineProcessed (filename, lineno)
		return pdb.Pdb.postcmd (self, stop, line)
