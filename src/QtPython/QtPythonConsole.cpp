#include "QtPython/QtPythonConsole.h"
#include "QtPython/QtPython.h"
#include "QtPython/QtPythonCalls.h"
#include "QtPython/QtPythonSyntaxHighlighter.h"

#include <QtUtil/QtActionAutoLock.h>
#include <QtUtil/QtMessageBox.h>
#include <QtUtil/QtUnicodeHelper.h>
#include <TkUtil/ErrorLog.h>
#include <TkUtil/Exception.h>
#include <TkUtil/InternalError.h>
#include <TkUtil/MemoryError.h>
#include <TkUtil/NumericConversions.h>
#include <TkUtil/ProcessLog.h>
#include <PythonUtil/PythonLogOutputStream.h>
#include <TkUtil/ScriptingLog.h>

#include <QThread>
#include <QPainter>
#ifndef QT_5
#include <QtGui/QAction>
#include <QtGui/QFileDialog>
#include <QtGui/QMenu>
#else	// QT_5
#include <QAction>
#include <QFileDialog>
#include <QMenu>
#endif	// QT_5

#include <sstream>	// v 2.6.0
#include <strstream>
#include <Python.h>
#include <stdio.h>
#include <algorithm>

using namespace TkUtil;
using namespace std;

static const TkUtil::Charset	charset ("àéèùô");

USE_ENCODING_AUTODETECTION


/* On utilise la fonction python builtin execfile pour exécuter sous Pdb
 * des fichiers. Fonctionne bien avec Python 2.6 et 2.7, Pdb s'arrête bien aux
 * points d'arrêt demandés.
 * Pb : cette fonction n'existe plus en Python 3.x, il faudra la réécrire.
 */
#define USE_EXECFILE_PYTHON_FUNCTION	1



// ============================================================================
//                             FONCTIONS STATIQUES
// ============================================================================

/**
 * @return	Une chaîne de caractère relatant l'éventuelle ereeur python
 * 			rencontrée.
 */
static UTF8String get_python_error ( )  // CP
{
	if (0 == PyErr_Occurred ( ))
		return UTF8String ("", Charset::UTF_8);

	PyObject    *errobj         = 0,    *errdata    = 0,
				*errtraceback   = 0,    *pystring   = 0;

	// Récupération des infos sur la dernière erreur python :
	PyErr_Fetch (&errobj, &errdata, &errtraceback);
    
	UTF8String		error (charset);	// v 3.3.0
//	ostringstream	error;
	error << "Erreur Python. Type : ";
	if ((0 != errobj) && (0 != (pystring = PyObject_Str(errobj))) &&
		(0 != PyString_Check(pystring)))
		error << PyString_AsString (pystring);
	else
		error << "inconnu";
	Py_XDECREF (pystring);
    
	error << ". Informations complémentaires : ";

	if ((0 != errdata) && (0 != (pystring = PyObject_Str(errdata))) &&
		(0 != PyString_Check (pystring)))
		error << PyString_AsString (pystring);
	else
		error << "absence de données complémentaires.\n";
	Py_XDECREF(pystring);

//	return error.str ( );
	return error;
}   // get_python_error


#define CHECK_PYTHON_CALL(result,call,name)                                  \
    {                                                                        \
	result	= call;                                                          \
	if (NULL == result)                                                      \
	{                                                                        \
		UTF8String	msg (charset);                                           \
		msg << "Erreur lors de l'appel à la fonction " << #name << " :"      \
		    << "\n" << get_python_error ( );                                 \
		PyErr_Clear ( );                                                     \
		throw Exception (msg);                                               \
	}                                                                        \
	}

//
// REDIRECTIONS stdout/err des commandes pythons
// ATTENTION : c'est du static => non partageable entre plusieures sessions.
// Issu de qpyconsole.cpp
// //
static PyObject* redirector_init(PyObject *, PyObject *)
{
	Py_INCREF(Py_None);
	return Py_None;
}

//static QString	outputsString;					// v 3.3.0
static UTF8String	outputsString (Charset::UTF_8);	// v 3.3.0
static PyObject* redirector_write(PyObject *, PyObject *args)
{
	char* output;
	PyObject *selfi;

	if (!PyArg_ParseTuple(args,"Os",&selfi,&output))
	{
		return NULL;
	}

	try
	{
		UTF8String	lines (QtPython::getConsoleCharset ( ));	// v 3.3.0
		lines << output;
		outputsString << lines;	// v 3.3.0
	}
	catch (...)
	{	// Remarque : si QtPython::getConsoleCharset ( ) vaut ASCII ou UNKNOWN
		// ça risque de mal se passer => on assure ici un service minimum :
		UTF8String	lines (Charset::UTF_8);	// v 3.3.0
		lines << output;
		outputsString << lines;	// v 3.3.0
	}
//	QString outputString = QString::fromLocal8Bit(output);
//	outputsString.append(outputString);

	Py_INCREF(Py_None);
	return Py_None;
}

static PyMethodDef redirectorMethods[] =
{
	{"__init__", redirector_init, METH_VARARGS,
	"initialize the stdout/err redirector"},
	{"write", redirector_write, METH_VARARGS,
	"implement the write method to redirect stdout/err"},
	{NULL,NULL,0,NULL},
};


static PyMethodDef ModuleMethods[] = { {NULL,NULL,0,NULL} };


static void initredirector ( )	// From qpyconsole.cpp
{
	PyMethodDef*	def	= NULL;

	/* create a new module and class */
	PyObject *module = Py_InitModule("redirector", ModuleMethods);
	PyObject *moduleDict = PyModule_GetDict(module);
	PyObject *classDict = PyDict_New();
	PyObject *className = PyString_FromString("redirector");
	PyObject *fooClass = PyClass_New(NULL, classDict, className);
	PyDict_SetItemString(moduleDict, "redirector", fooClass);
	Py_DECREF(classDict);
	Py_DECREF(className);
	Py_DECREF(fooClass);

	/* add methods to class */
	for (def = redirectorMethods; def->ml_name != NULL; def++) {
		PyObject *func = PyCFunction_New(def, NULL);
		PyObject *method = PyMethod_New(func, NULL, fooClass);
		PyDict_SetItemString(classDict, def->ml_name, method);
		Py_DECREF(func);
		Py_DECREF(method);
	}
}


//
// Retourne le nombre de lignes d'une instruction (au moins 1).
//
static size_t lineNumber (const string& lines)
{
	const size_t	num	= count (lines.begin ( ), lines.end ( ), '\n');
	return num + 1;
}	// lineNumber


// ============================================================================
//                       LA CLASSE QtPythonConsole::Instruction
// ============================================================================

QtPythonConsole::Instruction::Instruction (const string& command)
	: _command (command), _runnable (false)
{
	_runnable	= Instruction::isRunnable (command);
}	// Instruction::Instruction


QtPythonConsole::Instruction::Instruction (const QtPythonConsole::Instruction& ins)
	: _command (ins._command), _runnable (ins._runnable)
{
}	// Instruction::Instruction


QtPythonConsole::Instruction& QtPythonConsole::Instruction::operator = (const QtPythonConsole::Instruction& ins)
{
	if (&ins != this)
	{
		_command	= ins._command;
		_runnable	= ins._runnable;
	}	// if (&ins != this)

	return *this;
}	// Instruction::operator =


QtPythonConsole::Instruction::~Instruction ( )
{
}	// Instruction::~Instruction


const string& QtPythonConsole::Instruction::command ( ) const
{
	return _command;
}	// Instruction::command


size_t QtPythonConsole::Instruction::lineCount ( ) const
{
	size_t	count	= 1;
	for (string::size_type i = 0; i < _command.size ( ); i++)
		if ('\n' == _command [i])
			count++;

	return count;
}	// QtPythonConsole::Instruction::lineCount


bool QtPythonConsole::Instruction::runnable ( ) const
{
	return _runnable;
}	// QtPythonConsole::Instruction::runnable


bool QtPythonConsole::Instruction::partOfBlock ( ) const
{
	if (false == _command.empty ( ))
	{
		switch (_command [0])
		{
			case ' ' :
			case '\t' :
				return true;
		}	// switch (_command [0])
	}	// if (false == _command.empty ( ))

	return false;
}	// Instruction::partOfBlock


bool QtPythonConsole::Instruction::isMultiline (const string& instruction)
{
	UTF8String	trimed	= UTF8String (instruction).trim (true);
	if (trimed.length ( ) > 2)
	{
		if (0 == trimed.find ("if"))
			return true;

		if (trimed.length ( ) > 3)
		{
			if ((0 == trimed.find ("for")) ||
			    (0 == trimed.find ("try")))
				return true;

			if (trimed.length ( ) > 5)
			{
				if (0 == trimed.find ("while"))
					return true;
			}	// if (trimed.length ( ) > 5)
		}	// if (trimed.length ( ) > 3)
	}	// if ((trimed.length ( ) > 2)

	return false;
}	// Instruction::isMultiline


bool QtPythonConsole::Instruction::isRunnable (const string& instruction)
{
	const UTF8String	trimed	= UTF8String (instruction).trim ( );

//	if ((0 == trimed.length ( )) || ('#' == trimed [0]))
	if ((0 == trimed.length ( )) || ('#' == *trimed [0]))	// v 3.0.0
		return false;

	return true;
}	// Instruction::isRunnable


// ============================================================================
//                LA CLASSE QtPythonConsole::QtScriptTextFormat
// ============================================================================


const QtPythonConsole::QtScriptTextFormat QtPythonConsole::QtScriptTextFormat::commentFormat (QtPythonConsole::QtScriptTextFormat::COMMENT);
const QtPythonConsole::QtScriptTextFormat QtPythonConsole::QtScriptTextFormat::emptyLineFormat (QtPythonConsole::QtScriptTextFormat::BLANK);
const QtPythonConsole::QtScriptTextFormat QtPythonConsole::QtScriptTextFormat::instructionFormat (QtPythonConsole::QtScriptTextFormat::INSTRUCTION);
const QtPythonConsole::QtScriptTextFormat QtPythonConsole::QtScriptTextFormat::ranInstructionFormat (QtPythonConsole::QtScriptTextFormat::RAN_INSTRUCTION);
const QtPythonConsole::QtScriptTextFormat QtPythonConsole::QtScriptTextFormat::failedInstructionFormat (QtPythonConsole::QtScriptTextFormat::FAILED_INSTRUCTION);
const QtPythonConsole::QtScriptTextFormat QtPythonConsole::QtScriptTextFormat::tryFormat (QtPythonConsole::QtScriptTextFormat::TRY);

QtPythonConsole::QtScriptTextFormat::QtScriptTextFormat (QtPythonConsole::QtScriptTextFormat::LINE_TYPE type)
	: QTextCharFormat ( ), _type (type)
{
	QBrush	foregroundBrush (Qt::black), backgroundBrush (Qt::white);
	switch (type)
	{
		case QtPythonConsole::QtScriptTextFormat::BLANK				:
			break;
		case QtPythonConsole::QtScriptTextFormat::COMMENT			:
			 foregroundBrush.setColor (Qt::blue);
			 backgroundBrush.setColor (Qt::lightGray);
			break;
		case QtPythonConsole::QtScriptTextFormat::INSTRUCTION		:
//backgroundBrush.setColor (Qt::cyan);
//foregroundBrush.setColor (Qt::red);
			break;
		case QtPythonConsole::QtScriptTextFormat::RAN_INSTRUCTION	:
			backgroundBrush.setColor (Qt::darkGray);
			break;
		case QtPythonConsole::QtScriptTextFormat::FAILED_INSTRUCTION:
			backgroundBrush.setColor (QColor (255, 166, 0));	// orange
			break;
		case QtPythonConsole::QtScriptTextFormat::TRY				:
			break;
	}	// switch (type)

	setForeground (foregroundBrush);
	setBackground (backgroundBrush);
}	// QtScriptTextFormat::QtScriptTextFormat


const QtPythonConsole::QtScriptTextFormat& QtPythonConsole::QtScriptTextFormat::textFormat (const QtPythonConsole::Instruction& instruction)
{
	const string	trimed (UTF8String (instruction.command ( )).trim ( ).iso ( ));
	if (true == trimed.empty ( ))
		return QtPythonConsole::QtScriptTextFormat::emptyLineFormat;
	if ('#' == trimed [0])
		return QtPythonConsole::QtScriptTextFormat::commentFormat;
	if (0 == trimed.find ("try"))
		return QtPythonConsole::QtScriptTextFormat::tryFormat;

	return QtPythonConsole::QtScriptTextFormat::instructionFormat;
}	// QtScriptTextFormat::textFormat


// ============================================================================
//                LA CLASSE QtPythonConsole::InstructionsFile
// ============================================================================

QtPythonConsole::InstructionsFile::InstructionsFile (const string& filePrefix, bool iso8859, bool autoDelete)
	: _tmpFile (new TemporaryFile (true, filePrefix, true, autoDelete)),
	  _tmpStream ( ), _lines ( ), _lineCount (0), _firstLineNum (0), _iso8859 (iso8859), _maxProcessedLine (0), _requestedBreakPoint (0)
{
	_tmpStream.reset (new ofstream (_tmpFile->getFullFileName ( ).c_str( ), ios::trunc));

	if (true == _iso8859)
	{
		// Indispensable pour les caractères accentués bien de chez nous :
//		 (*_tmpStream.get ( )) << "# -*- coding: iso-8859-15 -*-" << endl;
		 (*_tmpStream.get ( )) << "# -*- coding: iso-8859-15 -*-\n" << flush;
		_lineCount	+= 1;
	}	// if (true == _iso8859)

#ifndef USE_EXECFILE_PYTHON_FUNCTION
	// A partir de la version 1.9.0 on ne fait plus un
	// Pdb ( ).run (execfile (...)), d'autant plus que execfile n'existe plus
	// en Python 3.x, mais directement un PyRun_FileFlags ...
	// => Pour être sous contrôle de Pdb on insère un Pdb ( ).set_trace ( ).
	// => il faudra donner l'ordre à Pdb d'avancer, via, par exemple, un appel
	// préalable à cont ( ) qui insère un 'c' dans le flux de commandes.
	(*_tmpStream.get ( )) << "NECDebuggingSession.instance ( ).set_trace ( )\n" << flush;
	_lineCount	+= 1;
#endif	// USE_EXECFILE_PYTHON_FUNCTION
}	// InstructionsFile::InstructionsFile


QtPythonConsole::InstructionsFile::InstructionsFile (const QtPythonConsole::InstructionsFile&)
	: _tmpFile ( ), _tmpStream ( ), _lines ( ),
	  _lineCount (0), _firstLineNum (0), _iso8859 (false), _maxProcessedLine (0), _requestedBreakPoint (0)
{
	assert (0 && "InstructionsFile copy constructor is not allowed.");
}	// InstructionsFile::InstructionsFile


QtPythonConsole::InstructionsFile& QtPythonConsole::InstructionsFile::operator = (const QtPythonConsole::InstructionsFile&)
{
	assert (0 && "InstructionsFile assignment operator is not allowed.");
	return *this;
}	// InstructionsFile::operator =


QtPythonConsole::InstructionsFile::~InstructionsFile ( )
{
	try
	{
		reset ( );
	}
	catch (...)
	{
	}
}	// InstructionsFile::~InstructionsFile


void QtPythonConsole::InstructionsFile::reset ( )
{
	_tmpFile.reset (0);
	_tmpStream.reset (0);
	_lines.clear ( );
	_lineCount		= 0;
	_firstLineNum	= 0;
}	// InstructionsFile::reset


void QtPythonConsole::InstructionsFile::addLines (const vector<string>& lines, size_t firstLineNum)
{
	if (0 == _tmpStream.get ( ))
	{
		INTERNAL_ERROR (exc, "Flux de lignes non instancié.", "InstructionsFile::addLines")
		throw exc;
	}	// if (0 == _tmpStream.get ( ))

	// Attention : on peut appeler addLines plusieurs fois pour le même
	// script ... _firstLineNum sert ensuite pour faire les conversions
	// num script <-> num fichier
	if (0 == _firstLineNum)
		_firstLineNum	= firstLineNum;
	for (vector<string>::const_iterator it = lines.begin ( ); lines.end ( ) != it; it++)
	{
//		(*_tmpStream.get ( )) << *it << endl;
		(*_tmpStream.get ( )) << *it << "\n" << flush;
		_lines.push_back (*it);
		_lineCount++;
	}	// for (vector<string>::const_iterator it = lines.begin ( ); ...
}	// InstructionsFile::addLines


string QtPythonConsole::InstructionsFile::getFileName ( ) const
{
	if (0 == _tmpFile.get ( ))
	{
		INTERNAL_ERROR (exc, "Fichier non instancié.", "InstructionsFile::getFileName")
		throw exc;
	}	// if (0 == _tmpFile.get ( ))

	return _tmpFile->getFullFileName ( );
}	// InstructionsFile::getFileName


size_t QtPythonConsole::InstructionsFile::getLineCount ( ) const
{
	return _lineCount - getLineOffset ( );
}	// InstructionsFile::getLineCount


size_t QtPythonConsole::InstructionsFile::getRealLineCount ( ) const
{
	return _lineCount;
}	// InstructionsFile::getRealLineCount


size_t QtPythonConsole::InstructionsFile::getFirstLineNum ( ) const
{
	return _firstLineNum;
}	// InstructionsFile::getFirstLineNum


size_t QtPythonConsole::InstructionsFile::getScriptLineNum (size_t consoleLineNum) const
{
	return consoleLineNum - _firstLineNum + 1 + getLineOffset ( );
}	// InstructionsFile::getScriptLineNum


size_t QtPythonConsole::InstructionsFile::getConsoleLineNum (size_t scriptLineNum) const
{
	return _firstLineNum + scriptLineNum - 1 - getLineOffset ( );
}	// InstructionsFile::getConsoleLineNum


string QtPythonConsole::InstructionsFile::getConsoleLine (size_t num) const
{
	if (num < _firstLineNum)
	{
		UTF8String	message (charset);
		message << "Ligne " << num << " demandée alors que le fichier " << getFileName ( ) << " ne contient que " << _lineCount << " lignes.";
		throw Exception (message);
	}	// if (num < _firstLineNum)

	const size_t	index	= num - _firstLineNum;

	return _lines [index];
}	// InstructionsFile::getConsoleLine


size_t QtPythonConsole::InstructionsFile::getLineOffset ( ) const
{
	// 1 éventuel pour _iso8859
#ifdef USE_EXECFILE_PYTHON_FUNCTION
	return true == _iso8859 ? 1 : 0;
#else	// USE_EXECFILE_PYTHON_FUNCTION
	// 1 pour la ligne Pdb ( ).set_trace ( )
	return true == _iso8859 ? 2 : 1;
#endif	// USE_EXECFILE_PYTHON_FUNCTION
}	// InstructionsFile::getLineOffset


size_t QtPythonConsole::InstructionsFile::maxProcessedLine ( ) const
{
	return _maxProcessedLine;
}	// InstructionsFile::maxProcessedLine


void QtPythonConsole::InstructionsFile::setMaxProcessedLine (size_t num)
{
	_maxProcessedLine	= num > _maxProcessedLine ? num : _maxProcessedLine;
}	// InstructionsFile::setMaxProcessedLine


size_t QtPythonConsole::InstructionsFile::getRequestedBreakPoint ( ) const
{
	return _requestedBreakPoint;
}	// InstructionsFile::getRequestedBreakPoint


void QtPythonConsole::InstructionsFile::setRequestedBreakPoint (size_t num)
{
	_requestedBreakPoint	= num;
}	// InstructionsFile::setRequestedBreakPoint


size_t QtPythonConsole::InstructionsFile::nextRunnableLine (size_t after, bool withLineOffset) const
{
	const size_t	max	= _lines.size ( );

	if (true == withLineOffset)
	{
		if (getLineOffset ( ) > after)
		{
			UTF8String	message (charset);
			message << "Instruction demandée après la ligne " << after << ", offset : " << getLineOffset ( ) << ".";
			INTERNAL_ERROR (exc, message, "InstructionsFile::nextRunnableLine")
			throw exc;
		}	// if (getLineOffset ( ) > after)
		after	-= getLineOffset ( );
	}	// if (true == withLineOffset)

	for (size_t i = after; i < max; i++)
		if (true == Instruction::isRunnable (_lines [i]))
			return i + 1;	// La numérotation commence à 1

	return 0;
}	// InstructionsFile::nextRunnableLine


// ============================================================================
//                            LA CLASSE EndOfDocCursor
// ============================================================================

QtPythonConsole::EndOfDocCursor::EndOfDocCursor (QtPythonConsole& console)
	: _console (console), _cursor (console.textCursor ( )), _enabled (true), _initialCursorPosition (console.textCursor ( ))
{
	// V 2.5.0. L'objectif est ici de positionner, en fin d'exécution, le
	// curseur en fin de script à une position où l'on puisse insérer un
	// fichier (icône "insérer" active).
	// => On mémorise la position courante. Si elle répond au besoin on
	// l'utilise telle que, sinon on se positionne à cette position que l'on
	// mémorise.
	if (false == _cursor.atEnd ( ))
	{	// On se positionne à la fin et on remonte tant qu'on a des lignes vides
		_cursor.movePosition (QTextCursor::End);
		QTextBlock	b		= _cursor.block ( );
		string		text	= b.text ( ).toStdString ( );
		while (true == text.empty ( ))
		{
			if (false == b.isValid ( ))
				break;
			if (false == b.previous ( ).isValid ( ))
				break;
			b	= b.previous ( );
			text	= b.text ( ).toStdString ( );
		}	// while (true == text.empty ( ))
		// On est sur le dernier bloc non vide, se positionner à sa fin :
		_cursor.setPosition (b.position ( ));
		_cursor.movePosition (QTextCursor::EndOfBlock);
		int	pos	= _cursor.position ( );
		// On se positionne à la ligne suivante :
		if (true ==  _cursor.atEnd ( ))
			_cursor.insertText ("\n");	// v 2.6.0
		_cursor.setPosition (pos + 1);
	}	// if (false == endDocCursor.atEnd ( ))
}	// EndOfDocCursor::EndOfDocCursor


QtPythonConsole::EndOfDocCursor::EndOfDocCursor (const QtPythonConsole::EndOfDocCursor&)
	: _console (*new QtPythonConsole (0, "Invalid application")), _cursor ( )
{
	assert (0 && "EndOfDocCursor copy constructor is not allowed.");
}	// EndOfDocCursor::EndOfDocCursor


QtPythonConsole::EndOfDocCursor& QtPythonConsole::EndOfDocCursor::operator = (const QtPythonConsole::EndOfDocCursor&)
{
	assert (0 && "EndOfDocCursor assignment operator is not allowed.");
	return *this;
}	// EndOfDocCursor::EndOfDocCursor


QtPythonConsole::EndOfDocCursor::~EndOfDocCursor ( )
{
	if (true == isEnabled ( ))	// v 6.2.0
		_console.setTextCursor (_cursor);
}	// EndOfDocCursor::~EndOfDocCursor


void QtPythonConsole::EndOfDocCursor::setEnabled (bool enabled)	// v 6.2.0
{
	_enabled	= enabled;
}	// EndOfDocCursor::setEnabled


bool QtPythonConsole::EndOfDocCursor::isEnabled ( ) const	// v 6.2.0
{
	return _enabled;
}	// EndOfDocCursor::isEnabled


// ============================================================================
//                            LA CLASSE QtPythonConsole
// ============================================================================


bool		QtPythonConsole::enableCodingIso8859_15	= true;
bool		QtPythonConsole::enableSwigCompletion	= false;
bool		QtPythonConsole::stopOnError			= true;
const QSize	QtPythonConsole::iconSize (32, 32);
bool		QtPythonConsole::_catchStdOutputs		= true;

static QTextBlockFormat	defaultBlockFormat;
static QTextCharFormat	defaultCharFormat;
static QBrush			defaultBackground;	// v 2.0.1


static bool isUtf16 (const string& path)	// v 3.3.0
{	// Semble fonctionner mais pas pour UTF-16 LE/BE
	bool	isUtf16	= false;
	FILE*	f	= fopen (path.c_str ( ), "r");
	if (NULL == f)
		throw Exception ("Fichier invalide.");
	char	buffer [10001];
	size_t	count	= fread (buffer, 10000, sizeof (char), f);
	const UTF8String	utf8 (buffer, Charset::UTF_16);
	const string&		str	= utf8.utf8 ( );
	if (NULL != strcasestr (str.c_str ( ), "coding"))
	{
		if ((NULL != strcasestr (str.c_str ( ), "utf16")) ||
		    (NULL != strcasestr (str.c_str ( ), "utf-16")))
			isUtf16	=true;
	}	// if (NULL != strcasestr (str.c_str ( ), "coding"))

	fclose (f);

	return isUtf16;
}	// isUtf16


Charset::CHARSET QtPythonConsole::getFileCharset (const string& path) // v 3.3.0
{	// Normalement on a le jeu de caractères en 2ème ligne :
	// #!/usr/bin/python
	// #-*- coding: iso-8859-15 -*-
	// ...
	ifstream	stream (path.c_str ( ));
	if ((false == stream.good ( )) && (false == stream.eof ( )))
		throw Exception ("Fichier invalide.");
	char	buffer [10001];
	size_t	line	= 0;
	while ((true == stream.good ( )) && (false == stream.eof ( )) && (line < 9))
	{
		memset (buffer, '\0', 10001);
		stream.getline (buffer, 10000, '\n');
		const char*	coding	= strcasestr (buffer, "coding");
		line++;
		if (NULL != coding)
		{
			const char*	source	= coding + 7;
			if (0 != strcasestr (source, "ascii"))
				return Charset::ASCII;
			else if ((0 != strcasestr (source, "iso8859")) || (0 != strcasestr (source, "iso-8859")))
				return Charset::ISO_8859;
			else if ((0 != strcasestr (source, "utf8")) || (0 != strcasestr (source, "utf-8")))
				return Charset::UTF_8;
		}	// if (NULL != coding)
	}	// while ((true == stream.good ( )) && ...

	return true == isUtf16 (path) ? Charset::UTF_16 : Charset::UNKNOWN;
}	// QtPythonConsole::getFileCharset


QtPythonConsole::QtPythonConsole (QWidget* parent, const string& appName)
	: QtTextEditor (parent, true),
	  _appName (appName),
	  _runningMode (QtPythonConsole::RM_DEBUG),
	  _running (false), _executingFile (false), _halted (false),
	  _waitingForRunning (false),		// v 1.14.0
	  _currentExecLine (1), _previousExecLine (0),
	  _maxExecLine (0), _currentScript ( ),
	  _pendingString ( ), _currentPendingLine (1),
	  _commandsFile (0), _commandsStream (0),
	  _breakpoints ( ),
	  _toolBar (0),
	  _runningModeAction (0), _runAction (0), _stopAction (0),
	  _nextAction (0), _addBreakPointAction (0), _removeBreakPointAction (0),
	  _clearBreakPointsAction (0), _insertScriptAction (0),
	  _completionComboBox (0), _checkingCompletion (false),
	  _breakPointIcon (":/images/breakpoint.png"),
	  _currentLineIcon (":/images/current_line.png"),
	  _globalDict (0), _localDict (0), _dbgModule (0), _dbgSessionModule (0),
	  _dbgCompletionModule (0),
	  _history ( ), _historyIndex ((size_t)-1),
#ifdef MULTITHREADED_APPLICATION
	  _mutex (new Mutex ( )),
#endif	// MULTITHREADED_APPLICATION
	  _logDispatcher ( ), _logStream (0),
	  _interpreterName ("QtPythonConsole interpreter"),
	  _resultFilter ( )
{
	setCenterOnScroll (true);
	QTextCursor	cursor	= textCursor ( );
	defaultCharFormat	= cursor.charFormat ( );
	defaultBlockFormat	= cursor.blockFormat ( );
	defaultBackground	= defaultBlockFormat.background ( );	// v 2.0.1
	
	// Mise en évidence des mots clés :
	new QtPythonSyntaxHighlighter (document ( ));

	// Les actions associées à la console :
	// Le mode d'exécution :
	_runningModeAction	= new QAction (QIcon (":/images/dbg_mode.png"), "Mode debug/normal",this);
	_runningModeAction->setCheckable (true);
	_runningModeAction->setChecked (false);
	connect (_runningModeAction, SIGNAL (toggled (bool)), this, SLOT (runningModeCallback (bool)));
	// L'exécution à partir du point courant :
	_runAction	= new QAction (QIcon (":/images/run.png"), QSTR ("Exécuter"), this);
	connect (_runAction, SIGNAL (triggered ( )), this, SLOT (runCallback ( )));
	// Arrêt de l'exécution :
	_stopAction	= new QAction (QIcon (":/images/stop.png"), QSTR ("Arrêter"), this);
	connect (_stopAction, SIGNAL (triggered ( )), this, SLOT (stopCallback( )));
	// Exécution de l'instruction suivante :
	_nextAction	= new QAction (QIcon (":/images/next.png"), QSTR ("Suivant"), this);
	connect (_nextAction, SIGNAL (triggered ( )), this, SLOT (nextCallback( )));
	// Ajoute un point d'arrêt à la ligne du curseur.
	_addBreakPointAction	= new QAction (QIcon (":/images/add_breakpoint.png"), QSTR ("Ajouter un point d'arrêt"), this);
	connect (_addBreakPointAction, SIGNAL (triggered ( )), this, SLOT (addBreakPointCallback( )));
	// Enlève le point d'arrêt de la ligne du curseur.
	_removeBreakPointAction	= new QAction (QIcon (":/images/remove_breakpoint.png"), QSTR ("Enlever le point d'arrêt"),this);
	connect (_removeBreakPointAction, SIGNAL (triggered ( )), this, SLOT (removeBreakPointCallback( )));
	// Enlève tous les points d'arrêt.
	_clearBreakPointsAction	= new QAction (QIcon (":/images/clear_breakpoints.png"), QSTR ("Enlever tous les points d'arrêt"),this);
	connect (_clearBreakPointsAction, SIGNAL (triggered ( )), this, SLOT (clearBreakPointsCallback( )));
	// Affiche un sélecteur de fichier de chargement de script au point
	// d'édition courant :
	_insertScriptAction	= new QAction (QIcon (":/images/load_script.png"), QSTR ("Insérer un script ..."), this);
	connect (_insertScriptAction, SIGNAL (triggered ( )), this, SLOT (insertScriptCallback( )));

	// L'exécution à partir du point courant :
	if (false == Py_IsInitialized ( ))
		Py_Initialize ( );
	initQtPythonCalls ( );

	registerConsole (*this);

	PyObject*	module	= NULL;
	CHECK_PYTHON_CALL (module, PyImport_ImportModule ("__main__"), "PyImport_ImportModule")
	_localDict	= _globalDict	= PyModule_GetDict (module);
	if (NULL == module)
		throw Exception ("Echec de l'importation du module __main__ par la console Python.");
	execInstruction ("import bdb", false);
	execInstruction ("import pdb", false);
	static const char*	scriptsLocation (getenv ("QT_PYTHON_SCRIPTS_DIR"));
	if (0 == scriptsLocation)
	{
		UTF8String	mess (charset);
		mess << "ATTENTION, variable QT_PYTHON_SCRIPTS_DIR non définie, la console python risque de ne pas fonctionner.\a";
		ConsoleOutput::cerr ( ) << mess << co_endl;
	}
	UTF8String	moduleScript (charset);
	moduleScript << (0 == scriptsLocation ? "." : scriptsLocation) << "/NECPdb.py";
	execFile (moduleScript);
	moduleScript.clear ( );
	moduleScript << (0 == scriptsLocation ? "." : scriptsLocation) << "/NECDebuggingSession.py";
	execFile (moduleScript);
	execInstruction ("import NECDebuggingSession", false);
	PyObject*	dbgName		= NULL;
	CHECK_PYTHON_CALL (
		dbgName, PyString_FromString ("NECPdb"), "PyString_FromString")
	CHECK_PYTHON_CALL (
		_dbgModule, PyImport_Import (dbgName), "PyImport_Import")
	Py_DECREF (dbgName);
	dbgName	= NULL;
	PyObject*	dbgSessionName	= NULL;
	CHECK_PYTHON_CALL (dbgSessionName, PyString_FromString ("NECDebuggingSession"), "PyString_FromString")
	CHECK_PYTHON_CALL (_dbgSessionModule, PyImport_Import (dbgSessionName), "PyImport_Import")
	Py_DECREF (dbgSessionName);
	dbgSessionName	= NULL;
	// Complétion via readline :
	moduleScript.clear ( );
	moduleScript << (0 == scriptsLocation ? "." : scriptsLocation) << "/NECCompleter.py";
	execFile (moduleScript);
	execInstruction ("import NECCompleter", false);
	moduleScript.clear ( );
	moduleScript << (0 == scriptsLocation ? "." : scriptsLocation) << "/NECCompletionSession.py";
	execFile (moduleScript);
	execInstruction ("import NECCompletionSession", false);
	PyObject*	dbgCompletionName	= NULL;
	CHECK_PYTHON_CALL (dbgCompletionName, PyString_FromString ("NECCompletionSession"), "PyString_FromString")
	_dbgCompletionModule	= PyImport_Import (dbgCompletionName);
	Py_DECREF (dbgCompletionName);
	dbgCompletionName	= NULL;

	_completionComboBox	= new QComboBox (this);
	_completionComboBox->hide ( );
	connect (_completionComboBox, SIGNAL (activated (int)), this, SLOT (completionCallback (int)));

	setRunningMode (QtPythonConsole::RM_CONTINUOUS);
	updateActions ( );
	// On a augmenté la marge gauche pour y mettre des icônes => redimensionner
	// le viewport de la zone texte de la console :
	updateLineNumberAreaWidthCallback (0);
// NEW
	initredirector ( );
	PyRun_SimpleString ("import redirector\n"
	                    "import sys\n"
	                    "sys.stdout = redirector.redirector()\n"
	                    "sys.stderr = sys.stdout"
	                    );
// !NEW
}	// QtPythonConsole::QtPythonConsole


QtPythonConsole::QtPythonConsole (const QtPythonConsole& console)
	: QtTextEditor (0, true),
	  _appName (console._appName),
	  _runningMode (QtPythonConsole::RM_CONTINUOUS),
	  _running (false), _executingFile (false), _halted (false),
	  _waitingForRunning (false),		// v 1.14.0
	  _currentExecLine (1), _previousExecLine (0),
	  _maxExecLine (0), _currentScript ( ),
	  _pendingString ( ), _currentPendingLine (1),
	  _commandsFile (0), _commandsStream (0),
	  _breakpoints ( ),
	  _toolBar (0),
	  _runningModeAction (0), _runAction (0), _stopAction (0),
	  _nextAction (0), _addBreakPointAction (0), _removeBreakPointAction (0),
	  _clearBreakPointsAction (0),
	  _insertScriptAction (0),
	  _completionComboBox (0),
	  _breakPointIcon (":/images/breakpoint.png"),
	  _currentLineIcon (":/images/current_line.png"),
	  _globalDict (0), _localDict (0), _dbgModule (0), _dbgSessionModule (0),
	  _dbgCompletionModule (0),
	  _history ( ), _historyIndex ((size_t)-1),
#ifdef MULTITHREADED_APPLICATION
	  _mutex ( ),
#endif	// MULTITHREADED_APPLICATION
	  _logDispatcher ( ), _logStream (0),
	  _interpreterName ("QtPythonConsole interpreter"),
	  _resultFilter ( )
{
	assert (0 && "QtPythonConsole copy constructor is not allowed.");
}	// QtPythonConsole::QtPythonConsole


QtPythonConsole& QtPythonConsole::operator = (const QtPythonConsole& console)
{
	assert (0 && "QtPythonConsole assignment constructor is not allowed.");
	return *this;
}	// QtPythonConsole::QtPythonConsole


QtPythonConsole::~QtPythonConsole ( )
{
	const bool	running	= isRunning ( );
	_running		= false;
	_executingFile	= false;
	_halted			= true;

	try
	{
		unregisterConsole (*this);
	}
	catch (...)
	{
	}

	try
	{
//		if (true == running)
			quitDbg ( );
	}
	catch (...)
	{
	}

	if (NULL != _dbgCompletionModule)
		Py_DECREF (_dbgCompletionModule);
	_dbgCompletionModule	= NULL;
	if (NULL != _dbgSessionModule)
		Py_DECREF (_dbgSessionModule);
	_dbgSessionModule	= NULL;
	if (NULL != _dbgModule)
		Py_DECREF (_dbgModule);
	_dbgModule	= NULL;
	if (true == Py_IsInitialized ( ))
		Py_Finalize ( );
}	// QtPythonConsole::~QtPythonConsole


int QtPythonConsole::lineNumberAreaWidth ( ) const
{
	int	width	= QtTextEditor::lineNumberAreaWidth ( );
	width		+= iconSize.width ( ) + 3;
	return width;
}	// QtPythonConsole::lineNumberAreaWidth


void QtPythonConsole::drawLinesNumbers (const QRect& rect)
{
	QtTextEditor::drawLinesNumbers (rect);

	if (false == displayLinesNumbers ( ))
		return;

	const int	width	= lineNumberAreaWidth ( );
	
	// On affiche les icônes associées aux numéros de lignes visibles :
	QPainter	painter (&getLineNumberArea ( ));
	QTextBlock	block	= firstVisibleBlock ( );
	int			number	= block.blockNumber ( ) + 1;
	int			top		=
		blockBoundingGeometry (block).translated (contentOffset ( )).top( );
	int			bottom	= top + blockBoundingRect (block).height ( );
	const int	height	= getIconSize ( );

	while ((block.isValid ( )) && (top <= rect.bottom ( )))
	{
		if ((true == block.isVisible ( )) && (bottom >= rect.top ( )))
		{
			// Rem : sous Qt 4.7.4 la surimpression (breakpoint, et par dessus
			// ligne actuelle) ne fonctionne pas ... (pixmap ? autre ?)
			// => on n'affiche pas le BP si ligne courrante.
			if ((_breakpoints.end ( ) != _breakpoints.find (number)) && (currentInstruction ( ) != number))
			{
				_breakPointIcon.paint (&painter, 3, top, width, height);
			}	// if (_breakpoints.end ( ) != _breakpoints.find (number))

			if (currentInstruction ( ) == number)
			{
				_currentLineIcon.paint (&painter, 3, top, width, height);
			}	// if (currentInstruction ( ) == number)
		}	// if ((true == block.isVisible ( )) && (bottom >= rect.top ( )))

		block	= block.next ( );
		top		= bottom;
		bottom	= top + blockBoundingRect (block).height ( );
		number	+= 1;
	}	// while ((block.isValid ( )) && (top <= rect.bottom ( )))
}	// QtPythonConsole::drawLinesNumbers


string QtPythonConsole::getAppName ( ) const
{
	return _appName;
}	// QtPythonConsole::getAppName


QtPythonConsole::RUNNING_MODE QtPythonConsole::getRunningMode ( ) const
{
	return _runningMode;
}	// QtPythonConsole::getRunningMode


void QtPythonConsole::setRunningMode (QtPythonConsole::RUNNING_MODE mode)
{
	if (mode == _runningMode)
		return;

	const QtPythonConsole::RUNNING_MODE	currentMode	= _runningMode;
	bool								editable	= false;
	_halted			= true;
	switch (currentMode)
	{
		case	QtPythonConsole::RM_CONTINUOUS	: _waitingForRunning	= true;		// v 1.14.0
		break;	// QtPythonConsole::RM_CONTINUOUS
		case	QtPythonConsole::RM_DEBUG		:
		try
		{
			_waitingForRunning	= false;	// v 1.14.0
			quitDbg ( );
		}
		catch (...)
		{
		}
		editable	= true;
		break;	// QtPythonConsole::RM_DEBUG
	}	// switch (currentMode)

	_runningMode	= mode;
	setReadOnly (!editable);
	setAcceptDrops (!editable);
}	// QtPythonConsole::setRunningMode


void QtPythonConsole::insert (const string& fileName, string& warnings)
{
	_pendingString.clear ( );
	// On se met en début de ligne courante :
	moveCursor (QTextCursor::StartOfBlock, QTextCursor::MoveAnchor);

	Charset::CHARSET	streamCharset	= getFileCharset (fileName); // v 3.3.0
//	streamCharset						= Charset::UNKNOWN == streamCharset ? Charset::ASCII : streamCharset;
	streamCharset						= Charset::UNKNOWN == streamCharset ? Charset::UTF_8 : streamCharset;	// v 5.1.7, éviter un rejet de conversion si caractère accentué
	// UTF-16 : les sauts de ligne ne sont pas des \n => réécrire différemment
	// la lecture du fichier.
	if (Charset::UTF_16 == streamCharset)
		throw Exception ("Encodage UTF-16 non supporté dans cette version.");

	ifstream	stream (fileName.c_str ( ));
	if ((false == stream.good ( )) && (false == stream.eof ( )))
		throw Exception ("Fichier invalide.");
	char	buffer [10001];
	const size_t	currentExecLine	= _currentExecLine;	// v 5.1.7
	while ((true == stream.good ( )) && (false == stream.eof ( )))
	{
		memset (buffer, '\0', 10001);
		stream.getline (buffer, 10000, '\n');
		UTF8String	instruction (buffer, streamCharset);
		addInstruction (instruction.utf8 ( ));	// v 3.3.0
//		addInstruction (buffer);
	}	// while ((true == stream.good ( )) && (false == stream.eof ( )))
	_currentExecLine	= currentExecLine;	// v 5.1.7. addInstruction incrémente _currentExecLine si instruction multiligne ...
	
	if (0 != _pendingString.size ( ))
	{
		UTF8String	error (charset);
		error << "Le texte suivant ne compile pas :\n" << _pendingString;
		if (0 != warnings.size ( ))
			warnings	+= '\n';
		warnings	+= error.iso ( );
	}	// if (0 != _pendingString.size ( ))
}	// QtPythonConsole::insert


void QtPythonConsole::addBreakpoint (size_t line)
{
	_breakpoints.insert (line);
	getLineNumberArea ( ).update ( );

	if (true == isRunning ( ))
	{
		// En fait le script ne commence pas forcément à la première ligne de
		// la console, on en est peut être à la n-ième session de mode debug :
		line	= _currentScript->getScriptLineNum (line);
		UTF8String	cmd (charset);
		cmd << "break " << line;
		sendDbgCommand (cmd.iso ( ));
	}	// if (true == isRunning ( ))
}	// QtPythonConsole::addBreakpoint


void QtPythonConsole::removeBreakpoint (size_t line)
{
	_breakpoints.erase (line);
	getLineNumberArea ( ).update ( );

	if (true == isRunning ( ))
	{
		// En fait le script ne commence pas forcément à la première ligne de
		// la console, on en est peut être à la n-ième session de mode debug :
		line	= _currentScript->getScriptLineNum (line);
		UTF8String	cmd (charset);
		cmd << "clear " << _currentScript->getFileName ( ) << ":" << line;
		sendDbgCommand (cmd.iso ( ));
	}	// if (true == isRunning ( ))
}	// QtPythonConsole::removeBreakpoint


void QtPythonConsole::clearBreakpoints ( )
{
	_breakpoints.clear ( );
	getLineNumberArea ( ).update ( );

	if (true == isRunning ( ))
	{	// Rem : NECPdb pour fonctionner sans confirmation.
		UTF8String	cmd (charset);
		cmd << "clear";
		sendDbgCommand (cmd.iso ( ));
	}	// if (true == isRunning ( ))

	// En l'absence de breakpoint posé le handler lineProcessed de QtPythonCalls
	// n'est pas appelé, et on n'a donc pas de retour sur la position courante.
	// => mettre un breakpoint temporaire en fin de script.
	if (true == isRunning ( ))
	{
		UTF8String	bp (charset);
		bp << "tbreak " << _currentScript->getFileName ( ) << ":" << _currentScript->getLineCount ( );
		sendDbgCommand (bp.iso ( ));
	}	// if (true == isRunning ( ))
}	// QtPythonConsole::clearBreakpoints


void QtPythonConsole::run ( )
{
	_waitingForRunning	= false;	// v 1.14.0
	if (true == isRunning ( ))
	{
		if ((QtPythonConsole::RM_DEBUG == getRunningMode ( )) && (true == isHalted ( )))
			cont ( );

		return;
	}	// if (true == isRunning ( ))

	const size_t	pos	= currentInstruction ( );
	if ((size_t)-1 == pos)
		throw Exception ("Exécution du script impossible : dernière ligne atteinte.");

	if (QtPythonConsole::RM_DEBUG == getRunningMode ( ))
		execDbgInstructions (false);
	else
		execInstructions ( );
}	// QtPythonConsole::run


void QtPythonConsole::stop ( )
{
	if (true == isRunning ( ))
	{
		_halted	= true;
		string	filename;
		size_t	line	= 0;
		where (filename, line);
		line	= _currentScript->getScriptLineNum (currentInstruction ( ));
		UTF8String	cmd (charset);
		cmd << "stop";
		sendDbgCommand (cmd.iso ( ));
	}	// if (true == isRunning ( ))
}	// QtPythonConsole::stop


bool QtPythonConsole::isRunning ( ) const
{
	return _running;
}	// QtPythonConsole::isRunning


bool QtPythonConsole::isExecutingFile ( ) const	// 2.10.0
{
	return _executingFile;
}	// QtPythonConsole::isExecutingFile


bool QtPythonConsole::isHalted ( ) const
{
	return _halted;
}	// QtPythonConsole::isHalted


bool QtPythonConsole::isWaitingForRunning ( ) const
{
	return _waitingForRunning;
}	// QtPythonConsole::isWaitingForRunning


void QtPythonConsole::cont ( )
{
	if (QtPythonConsole::RM_DEBUG != getRunningMode ( ))
	{
		INTERNAL_ERROR (exc, "Mode de fonctionnement autre que debug.",
		                "QtPythonConsole::cont")
		throw exc;
	}	// if (QtPythonConsole::RM_DEBUG != getRunningMode ( ))

	_waitingForRunning	= false;	// v 1.14.0
	_halted				= false;
	sendDbgCommand ("c");
}	// QtPythonConsole::cont


void QtPythonConsole::next ( )
{
	if (QtPythonConsole::RM_DEBUG != getRunningMode ( ))
	{
		INTERNAL_ERROR (exc, "Mode de fonctionnement autre que debug.",
		                "QtPythonConsole::next")
		throw exc;
	}	// if (QtPythonConsole::RM_DEBUG != getRunningMode ( ))

	_waitingForRunning	= false;	// v 1.14.0
	if (true == isRunning ( ))
//		sendDbgCommand ("n");	// v 1.10.0
	{	// v 1.10.0 : on remplace 'n (next) par un point d'arrêt temporaire + continue. Pourquoi ? Parce-qu'en mode next python semble filer et ne
		// remonte pas les exceptions rencontrées ...
		// Doc next de Pdb v 2.7.3 :
		// Continue execution until the next line in the current function is reached or it returns. (The difference between next and step is that
		// step stops inside a called function, while next executes called functions at (nearly) full speed, only stopping at the next line in
		// the current function.)
		size_t	num	= _currentScript->nextRunnableLine (_currentScript->getScriptLineNum (_currentExecLine), true) + _currentScript->getLineOffset ( );
		UTF8String	bp (charset);
		bp << "tbreak " << _currentScript->getFileName ( ) << ":" << num;
		sendDbgCommand (bp.iso ( ));
		cont ( );
	}
	else
		execDbgInstructions (true);
}	// QtPythonConsole::next


void QtPythonConsole::quitDbg ( )
{
	try
	{
		if (0 != _currentScript.get ( ))
			// v 1.13.2 : _currentScript->setRequestedBreakPoint (...
			_currentScript->setRequestedBreakPoint (_currentScript->getScriptLineNum (_currentExecLine));
		sendDbgCommand ("q");
	}
	catch (const Exception& exc)
	{
//		cerr << "Erreur dans QtPythonConsole::quitDbg : "
//		     << exc.getFullMessage ( ) << endl;
	}
	catch (...)
	{
		UTF8String	mess (charset);
		mess << "Erreur non documentée dans QtPythonConsole::quitDbg.";
		ConsoleOutput::cerr ( ) << mess << co_endl;
	}
}	// QtPythonConsole::quitDbg


size_t QtPythonConsole::lineCount ( ) const
{
	return document ( )->lineCount ( );
}	// QtPythonConsole::lineCount


int QtPythonConsole::getIconSize ( ) const
{
	const int	height	= iconSize.height ( ) < fontMetrics ( ).height ( ) ? iconSize.height ( ) : fontMetrics ( ).height ( );

	return height;
}	// QtPythonConsole::getIconSize


bool QtPythonConsole::allowEditionAtCursorPos ( ) const
{
	const QTextCursor	cursor	= textCursor ( );
	const int			line	= cursor.blockNumber ( ) + 1;

	return allowEditionAtLine (line);
}	// QtPythonConsole::allowEditionAtCursorPos


bool QtPythonConsole::allowEditionAtLine (size_t line) const
{
	switch (getRunningMode ( ))
	{
		case QtPythonConsole::RM_DEBUG		: return false;
		case QtPythonConsole::RM_CONTINUOUS	:
		default								: ;
	}	// switch (getRunningMode ( ))

	return line >= currentInstruction ( ) ? true : false;
}	// QtPythonConsole::allowEditionAtLine


void QtPythonConsole::validateCursorPosition ( )
{
	try
	{
		CHECK_NULL_PTR_ERROR (document ( ))

		// Attention : si on est en fin de document le numéro de ligne
		// n'existe pas forcément.
		QTextBlock	block;
		QTextCursor	cursor	= textCursor ( );
		if (_currentExecLine > document ( )->blockCount ( ))
		{	// 2 possibilités : la dernière ligne est blanche et convient,
			// sinon on en rajoute une.
			block	= document ( )->findBlockByNumber (_currentExecLine - 2);
			if ((true == block.isValid ( )) && (true == block.text ( ).isEmpty ( )))
				_currentExecLine--;
			else
			{
				block				= document ( )->lastBlock ( );
				cursor.movePosition (QTextCursor::EndOfBlock, QTextCursor::MoveAnchor);
				cursor	= textCursor ( );
//				cursor.setCharFormat (defaultCharFormat);
				// Les 2 instructions suivantes sont nécessaires pour que les lignes ajoutées ne soient pas en mode "exécutées" :
				cursor.setBlockFormat (defaultBlockFormat);
				setCurrentCharFormat (defaultCharFormat);
//				cursor.insertText ("\n");	// v 1.14.0
				appendPlainText ("");		// v 1.14.0
			}
		}	// if (_currentExecLine > document ( )->blockCount ( ))
		else
		{
			block	= document ( )->findBlockByNumber (_currentExecLine - 1);
			cursor.setPosition (block.position ( ), QTextCursor::MoveAnchor);
			cursor.setCharFormat (defaultCharFormat);
			cursor.setBlockFormat (defaultBlockFormat);
		}	// else if (_currentExecLine > document ( )->blockCount ( ))
	}
	catch (...)
	{
	}
}	// QtPythonConsole::validateCursorPosition


static string fooArgs ( )
{
	static UTF8String	args (charset);

	if (true == args.empty ( ))
	{
		args << " (";
		for (int a = 0; a < 50; a++)
			args << (size_t)a << ", ";
		args << "50)";
	}	// if (true == args.empty ( ))

	return args.iso ( );
}	// fooArgs


void QtPythonConsole::addSwigCompletions (
//					set<string>& completions, const string& instruction)
					vector<string>& completions, const string& instruction)
{
	const int	parenthesis	= QString (instruction.c_str ( )).indexOf ('(');
	string		expression	= instruction;
	if (-1 != parenthesis)
		expression	= QString (expression.c_str ( )).left (parenthesis).trimmed ( ).toStdString ( );
	// On va faire un appel Swig avec une signature hautement improbable.
	// 2 cas de figure attendus :
	// - 1 seule signature est acceptée par swig, et on va la reconstituer argument par argument ...
	// - Plusieures signatures sont possibles, et Swig a la bon goût, dans ce cas, de les donner.
	UTF8String	foo (charset);	// La signature bidon
	foo << expression << fooArgs ( );
	// Dans les lignes de l'exception levée on va trouver les complétions acceptées par Swig. On recherche la fonction finale, et non
	// l'expression, à savoir funct de x ( ).funct ( ) :
	string					function	= expression;
	const string::size_type	dot			= function.find ('.');
	if (string::npos != dot)
		function	= function.substr (dot + 1);
	try
	{
		execInstruction (foo.iso ( ), false);
	}
	catch (const Exception& exc)
	{
		const string	msg	= exc.getFullMessage ( ).iso( );
		const QString	qmsg (msg.c_str ( ));
		QStringList		lines	= qmsg.split ("\n", QString::KeepEmptyParts);
		const	size_t	num		= lines.size ( );
		if (1 == num)	// On fait comme on peut ...

			completions.push_back(cppToPython (getSwigCompletion (expression)));
//			completions.insert (cppToPython (getSwigCompletion (expression)));
		else
		{
			for (size_t i = 0; i < num; i++)
			{
				if (-1 != lines [i].indexOf (foo.iso ( ).c_str ( )))
					continue;	// Notre appel bidon
				if (-1 != lines [i].indexOf ("UnusedStructForSwigCompletion"))
					continue;	// Convention v 2.6.0 pour completion exacte

				// On récupère les arguments proposés :
				const int start	= lines [i].indexOf (function.c_str ( ));
				const int end	= lines [i].indexOf (')');
				if ((-1 != start) && (-1 != end) && (start < end))
				{
					UTF8String	prop	= UTF8String (lines [i].toStdString ( )).substring (start, end);
					const size_t	s	= prop.find ('(');
					const size_t	e	= prop.find (')');
					UTF8String	finalized (charset);
					finalized << expression << " " << prop.substring (s, e);
					completions.push_back (cppToPython (finalized));
				}	// if ((-1 != start) && (-1 != end) && ...
			}	// for (size_t i = 0; i < num; i++)
		}	// else if (num == 1)
	}	// catch (const Exception& exc)
}	// QtPythonConsole::addSwigCompletions


string QtPythonConsole::getSwigCompletion (const string& instruction)
{
	UTF8String	completion (charset);
	completion << instruction << " (";
	size_t	argNum	= 0;
	try
	{
		UTF8String	expression (charset);
		expression << instruction << fooArgs ( );
		execInstruction (expression.iso ( ), false);
	}
	catch (const Exception& exc)
	{
		const string	msg	= exc.getFullMessage ( ).iso ( );
		const QString	qmsg (msg.c_str ( ));
		const int		takes	= qmsg.indexOf ("takes exactly ");
		const int		args	= qmsg.indexOf ("arguments");
		if ((-1 != takes) && (-1 != args) && (takes + 15 < args))
		{
			UTF8String	argnum	= UTF8String (msg).substring (takes + 14, args - 2);
			argnum	= argnum.trim ( );
			argNum	= NumericConversions::strToULong (argnum);
		}	// if ((-1 != takes) && (-1 != args) && (takes + 15 < args))
		// ATTENTION : le cas -1 != takes et -1 == args existe, ça correspond par exemple à "instance." où instance est une instance d'une classe
		// quelconque. Le message est dans ce cas du type :
		//  ... __init__() takes exactly 1 argument (52 given)
		if ((0 != argNum) && (-1 != qmsg.indexOf (".")))
			argNum	-= 1;	// il y a l'instance
	}

// L'expérience s'arrête là. Selon le type d'argument le message est variable (cf. fichiers _wrap.cxx) :
// - argument 3 of type 'std::string ... : c'est clair
// - On attendait une liste : c'est moins clair, et ne dit pas le type de la liste (réels, chaines, ...) ni éventuellement sa taille.
// - ... Autre problèmes possibles ?
// - Oui : lorsque tout est trouvé, ou compatible, la fonction est réellement exécutée => mieux vaut ne pas trouver ...
// Donc ... :
	for (size_t i = 0; i < argNum; i++)
	{
		if (0 != i)
			completion << ", ";
		completion << "type_" << i << "?";
	}	// for (size_t i = 0; i < argNum; i++)
/*
	vector<string>	args;
	for (size_t i = 0; i < argNum; i++)
	{
		try
		{
			UnicodeString	test	= instruction;
			test << " (";
			int	j	= 0;
			for (j = 0; j < args.size ( ); j++)
			{
				if (0 != j)
					test << ", ";
				test << args [j];
			}	// for (j = 0; j < args.size ( ); j++)

			// L'argument testé (et les suivants) :
			for ( ; j < argNum; j++)
			{
				if (0 != j)
					test << ", ";
if (1 == j)
	test << "\"otto\"";
else
				test << "[]";
			}	// for (; j < argNum; j++)
			test << ")";

			execInstruction (test.iso ( ), false);
// ATTENTION : COMBIEN D'ARGS AJOUTER A args ???
			if (0 != argNum)
				completion << ", ";
			completion << "int";	// C'est le hasard
		}
		catch (const Exception& exc)
		{
cout << "ERR : " << exc.getFullMessage ( ) << endl;
		}
	}	// for (size_t i = 0; i < argNum; i++)
*/

	completion << ")";
	return completion.iso ( );
}	// QtPythonConsole::getSwigCompletion


string QtPythonConsole::cppToPython (const string& instruction) const
{
	QString	qinstruction (instruction.c_str ( ));

	qinstruction	= qinstruction.replace ("*", "");
	qinstruction	= qinstruction.replace ("&", "");
	qinstruction	= qinstruction.replace ("const", "");
	qinstruction	= qinstruction.replace ("std::", "");
	qinstruction	= qinstruction.replace ("::", ".");
	qinstruction	= qinstruction.replace (" ,", ",");
	qinstruction	= qinstruction.replace (" )", ")");

	return qinstruction.toStdString ( );
}	// QtPythonConsole::cppToPython


#ifdef MULTITHREADED_APPLICATION

Mutex& QtPythonConsole::getMutex ( )
{
	assert (0 != _mutex.get ( ));
	return *(_mutex.get ( ));
}	// QtPythonConsole::getMutex

#endif	// MULTITHREADED_APPLICATION


QAction& QtPythonConsole::runningModeAction ( )
{
	CHECK_NULL_PTR_ERROR (_runningModeAction)
	return *_runningModeAction;
}	// QtPythonConsole::runningModeAction


QAction& QtPythonConsole::runAction ( )
{
	CHECK_NULL_PTR_ERROR (_runAction)
	return *_runAction;
}	// QtPythonConsole::runAction


QAction& QtPythonConsole::stopAction ( )
{
	CHECK_NULL_PTR_ERROR (_stopAction)
	return *_stopAction;
}	// QtPythonConsole::stopAction


QAction& QtPythonConsole::nextAction ( )
{
	CHECK_NULL_PTR_ERROR (_nextAction)
	return *_nextAction;
}	// QtPythonConsole::nextAction


QAction& QtPythonConsole::addBreakPointAction ( )
{
	CHECK_NULL_PTR_ERROR (_addBreakPointAction)
	return *_addBreakPointAction;
}	// QtPythonConsole::addBreakPointAction


QAction& QtPythonConsole::removeBreakPointAction ( )
{
	CHECK_NULL_PTR_ERROR (_removeBreakPointAction)
	return *_removeBreakPointAction;
}	// QtPythonConsole::removeBreakPointAction


QAction& QtPythonConsole::removeAllBreakPointsAction ( )
{
	CHECK_NULL_PTR_ERROR (_clearBreakPointsAction)
	return *_clearBreakPointsAction;
}	// QtPythonConsole::removeAllBreakPointsAction


QAction& QtPythonConsole::insertScriptAction ( )
{
	CHECK_NULL_PTR_ERROR (_insertScriptAction)
	return *_insertScriptAction;
}	// QtPythonConsole::insertScriptAction


QToolBar& QtPythonConsole::getToolBar ( )
{
	CHECK_NULL_PTR_ERROR (_runningModeAction)
	CHECK_NULL_PTR_ERROR (_runAction)
	CHECK_NULL_PTR_ERROR (_stopAction)
	CHECK_NULL_PTR_ERROR (_nextAction)
	CHECK_NULL_PTR_ERROR (_addBreakPointAction)
	CHECK_NULL_PTR_ERROR (_removeBreakPointAction)
	CHECK_NULL_PTR_ERROR (_clearBreakPointsAction)
	CHECK_NULL_PTR_ERROR (_insertScriptAction)

	if (0 == _toolBar)
	{
		_toolBar	= new QToolBar ( );
		_toolBar->addAction (_runningModeAction);
		_toolBar->addSeparator ( );
		_toolBar->addAction (_runAction);
		_toolBar->addAction (_stopAction);
		_toolBar->addAction (_nextAction);
		_toolBar->addSeparator ( );
		_toolBar->addAction (_addBreakPointAction);
		_toolBar->addAction (_removeBreakPointAction);
		_toolBar->addAction (_clearBreakPointsAction);
		_toolBar->addSeparator ( );
		_toolBar->addAction (_insertScriptAction);
	}	// if (0 == _toolBar)

	CHECK_NULL_PTR_ERROR (_toolBar)
	return *_toolBar;
}	// QtPythonConsole::getToolBar


string QtPythonConsole::getPythonScript ( ) const
{
	if (0 == _currentScript.get ( ))
	{
		INTERNAL_ERROR (exc, "Fichier non instancié.", "QtPythonConsole::getPythonScript")
		throw exc;
	}	// if (0 == _currentScript.get ( ))

	return _currentScript->getFileName ( );
}	// QtPythonConsole::getPythonScript


void QtPythonConsole::lineIsBeingProcessedCallback (const string& fileName, size_t line)
{
	line	= _currentScript->getConsoleLineNum (line);
	lineProcessedCallback (line, true);
}	// QtPythonConsole::lineIsBeingProcessedCallback


void QtPythonConsole::lineProcessedCallback (const string& fileName, size_t line, bool ok)
{
	try
	{
		const size_t	scriptLine	= line;

		if (0 == _currentScript.get ( ))
		{
			INTERNAL_ERROR (exc, "Fichier non instancié.", "QtPythonConsole::lineProcessedCallback")
			throw exc;
		}	// if (0 == _currentScript.get ( ))

		if (fileName != _currentScript->getFileName ( ))
		{
			UTF8String	error (charset);
			error << "Erreur de fichier. Reçu : " << fileName << ", attendu : " << _currentScript->getFileName ( ) << ".";
			INTERNAL_ERROR(exc, error, "QtPythonConsole::lineProcessedCallback")
			throw exc;
		}	// if (fileName != _currentScript->getFileName ( ))

		if (scriptLine == _currentScript->getRequestedBreakPoint ( ))
			return;

		// ATTENTION : pour une raison inconnue NECPdb::trace_dispatch est appelé à 2 reprises pour la première ligne exécutée, donc
		// QtPythonCalls.lineProcessed est appelé deux fois, et donc QtPythonConsole::lineProcessedCallback de même ...
		if (scriptLine > _currentScript->maxProcessedLine ( ))
		{
			_currentScript->setMaxProcessedLine (scriptLine);

			// En fait le script ne commence pas forcément à la première ligne 
			// de la console, on en est peut être à la n-ième session de mode
			// debug :
			line	= _currentScript->getConsoleLineNum (line);

			lineProcessedCallback (line, ok);
		}	// if (scriptLine > _currentScript->maxProcessedLine ( ))
	}
	catch (const Exception& exc)
	{
		UTF8String	mess (charset);
		mess << "Erreur dans QtPythonConsole::lineProcessedCallback : "
		     << exc.getFullMessage ( );
		ConsoleOutput::cerr ( ) << mess << co_endl;
	}
	catch (...)
	{
		UTF8String	mess (charset);
		mess << "Erreur non documentée dans QtPythonConsole::lineProcessedCallback.";
		ConsoleOutput::cerr ( ) << mess << co_endl;
	}
}	// QtPythonConsole::lineProcessedCallback


void QtPythonConsole::lineProcessedCallback (size_t line, bool ok)
{
	try
	{
		// Gérer les sorties du script python :
		processPythonOutputs ( );
		
		// Gérer les sorties du script python :
		if (true == _checkingCompletion)
			return;		// v 5.3.0

		// Cette ligne a été jouée (rem : les numéros Qt commencent à 0 ...) :
		QTextBlock			block	= document ( )->findBlockByNumber (line-1);
		const string		instruction	= block.text ( ).toStdString ( );
		const bool			isComment	= '#' == instruction [0] ? true : false;

		if (line >= maxExecLine ( ))	// Eviter de réécrire les boucles
		{
			if (false == isComment)
			{
				// Tant que les lignes précédentes sont des commentaires on les
				// récupère pour les ajouter au fichier script.
				UTF8String	comments (Charset::UTF_8);
				size_t			bl	= true == isComment ? line - 1 : line - 2;
				bool			stopped	= 2 > bl ? true : false;
				while (false == stopped)
				{
					const QTextBlock	b	= document()->findBlockByNumber(bl);
					const string		l	= b.text ( ).toStdString ( );
					if ((true == l.empty ( )) || ('#' != l [0]) || (bl <= 0))
					{
						stopped	= true;
						bl	= bl < line - 1 ? bl + 1 : line - 1;
					}
					else
						bl--;
				}	// while (false == stopped)
				for (size_t i = bl; i < line - 1; i++)
				{
					const QTextBlock	b	= document ( )->findBlockByNumber (i);
					const UTF8String	l (b.text ( ).toStdString ( ), Charset::UTF_8);
					comments << l;
					if (i < line - 2)
						comments << '\n';
				}	// for (size_t i = bl; i < line - 1; i++)
//				if ((false == comments.empty ( )) && ('#' == comments [0]))
				if ((false == comments.empty ( )) && ('#' == *comments [0]))	// v 3.0.0
					comments	= comments.substring (1, comments.length ( ));
				getLogDispatcher ( ).log (ScriptingLog (instruction, comments));
			}	// if (false == isComment)
		}	// if (line >= maxExecLine ( ))
		// QTextCharFormat	 : zone avec du texte
		// QTextBlockFormat : le reste du bloc
		QTextCharFormat		cformat	= block.charFormat ( );
		QTextBlockFormat	bformat	= block.blockFormat ( );
		cformat.setProperty (QTextFormat::FullWidthSelection, true);
		bformat.setProperty (QTextFormat::FullWidthSelection, true);
		cformat.setBackground (true == ok ? QtScriptTextFormat::ranInstructionFormat.background ( ) : QtScriptTextFormat::failedInstructionFormat.background ( ));
		bformat.setBackground (true == ok ? QtScriptTextFormat::ranInstructionFormat.background ( ) : QtScriptTextFormat::failedInstructionFormat.background ( ));
		QTextCursor			cursor	= textCursor ( );
		const int			position= cursor.position ( );
		cursor.setPosition (block.position ( ), QTextCursor::MoveAnchor);
		cursor.select (QTextCursor::BlockUnderCursor);
		cursor.setCharFormat (cformat);		// v 5.0.0
		cursor.setBlockFormat (bformat);	// v 5.0.0
		if (true == cursor.atEnd ( ))
		{	// Bloc : v 5.0.0 à la place de appendPlainText ("");
			cursor.clearSelection ( );
			QTextBlockFormat	newformat	= block.blockFormat ( );
			bformat.setBackground (QtScriptTextFormat::emptyLineFormat.background ( ));
			//cursor.insertBlock (newformat, QtScriptTextFormat::emptyLineFormat);
			cursor.setBlockFormat (newformat);
			cursor.setCharFormat (QtScriptTextFormat::emptyLineFormat);
			cursor.insertBlock (newformat, QtScriptTextFormat::emptyLineFormat);
		}	// if (true == cursor.atEnd ( ))
		addToHistoric (instruction);
//		cursor.setCharFormat (cformat);		// v 5.0.0
//		cursor.setBlockFormat (bformat);	// v 5.0.0
		cursor.clearSelection ( );	// v 2.5.0
		// On en profite pour s'assurer que la ligne courante est visible :
		setTextCursor (cursor);
		ensureCursorVisible ( );
		// On se repositionne là où on était :
		cursor.setPosition (position, QTextCursor::MoveAnchor);

		// On passe à la ligne suivante :
		_maxExecLine		= line > _maxExecLine ? line : _maxExecLine;
		_previousExecLine	= line;
		_currentExecLine	= followingInstruction (line);
		// Rem : l'instruction ci-dessus n'est pas forcément vraie, par exemple
		// dans le cas d'un test conditionnel ...

		validateCursorPosition ( );
	}
	catch (const Exception& exc)
	{
		UTF8String	mess (charset);
		cerr << "Erreur dans QtPythonConsole::lineProcessedCallback : " << exc.getFullMessage ( );
		ConsoleOutput::cout ( ) << mess << co_endl;
	}
	catch (...)
	{
		UTF8String	mess (charset);
		cerr << "Erreur non documentée dans QtPythonConsole::lineProcessedCallback.";
		ConsoleOutput::cout ( ) << mess << co_endl;
	}

	getLineNumberArea ( ).update ( );
	updateActions ( );
}	// QtPythonConsole::lineProcessedCallback


void QtPythonConsole::atBreakPointCallback (const string& fileName, size_t line)
{
	const size_t	scriptLine	= line;

	try
	{
		if (0 == _currentScript.get ( ))
		{
			INTERNAL_ERROR (exc, "Fichier non instancié.", "QtPythonConsole::atBreakPointCallback")
			throw exc;
		}	// if (0 == _currentScript.get ( ))
		if (fileName != _currentScript->getFileName ( ))
		{
			UTF8String	error (charset);
			error << "Erreur de fichier. Reçu : " << fileName << ", attendu : " << _currentScript->getFileName ( ) << ".";
			INTERNAL_ERROR(exc, error, "QtPythonConsole::atBreakPointCallback")
			throw exc;
		}	// if (fileName != _currentScript->getFileName ( ))

		// En fait le script ne commence pas forcément à la première ligne de
		// la console, on en est peut être à la n-ième session de mode debug :
		line	= _currentScript->getConsoleLineNum (line);
		_currentExecLine	= line;
		_maxExecLine		= line > _maxExecLine ? line : _maxExecLine;

		_halted				= true;
		getLineNumberArea ( ).update ( );
		_waitingForRunning	= true;	// v 1.14.0
		updateActions ( );
	}
	catch (const Exception& exc)
	{
		UTF8String	mess (charset);
		mess << "Erreur dans QtPythonConsole::atBreakPointCallback : " << exc.getFullMessage ( );
		ConsoleOutput::cerr ( ) << mess << co_endl;
	}
	catch (...)
	{
		UTF8String	mess (charset);
		mess << "Erreur non documentée dans QtPythonConsole::atBreakPointCallback.";
		ConsoleOutput::cerr ( ) << mess << co_endl;
	}

	// Affichage stdout/stderr de python dans la vue logs de la console :
	processPythonOutputs ( );

	try
	{
		if (scriptLine == _currentScript->getRealLineCount ( ))
			sendDbgCommand ("c");
	}
	catch (...)
	{
	}
}	// QtPythonConsole::atBreakPointCallback


LogDispatcher& QtPythonConsole::getLogDispatcher ( )
{
	return _logDispatcher;
}	// QtPythonConsole::getDispatcher


LogOutputStream* QtPythonConsole::getLogStream ( )
{
	return _logStream;
}	// QtPythonConsole::getLogStream


void QtPythonConsole::setLogStream (LogOutputStream* stream)
{
#ifdef MULTITHREADED_APPLICATION
	AutoMutex	mutex (_mutex.get ( ));
#endif	// MULTITHREADED_APPLICATION

	_logStream	= stream;
}	// QtPythonConsole::setLogStream


void QtPythonConsole::log (const TkUtil::Log& l)
{
#ifdef MULTITHREADED_APPLICATION
	AutoMutex	mutex (_mutex.get ( ));
#endif	// MULTITHREADED_APPLICATION

	LogOutputStream*	stream	= getLogStream ( );

	if (0 != stream)
		stream->log (l);
}	// QtPythonConsole::log


void QtPythonConsole::hideResult (const string& str)
{
	if (0 != str.length ( ))
		_resultFilter.push_back (str);
}	// QtPythonConsole::hideResult


void QtPythonConsole::addToHistoric (const UTF8String& command, const UTF8String& comments, const UTF8String& commandOutput, bool statusErr, bool fromKernel)
{
#ifdef MULTITHREADED_APPLICATION
	AutoMutex	mutex (_mutex.get ( ));
#endif	// MULTITHREADED_APPLICATION
	// v 2.7.0 : protection contre des commandes multilignes.
	vector<string>	lines;
	const bool		multilines	= PythonLogOutputStream::isMultiLines (command.utf8 ( ), lines);
	const size_t	linesCount	= lines.size ( );
	if (linesCount > 1)
	{
		size_t	i	= 0;
		for (vector<string>::const_iterator itl = lines.begin ( );
		     lines.end ( ) != itl; itl++, i++)
		{
			const string	comms (0 == i ? comments.utf8 ( ) : string ( ));
			const string	out (0 == i ? commandOutput.utf8 ( )  : string ( ));
			addToHistoric (UTF8String (*itl, Charset::UTF_8), UTF8String (comms, Charset::UTF_8), UTF8String (out, Charset::UTF_8), statusErr, fromKernel);
		}	// for (vector<string>::const_iterator itl = lines.begin ( ); ...

		return;
	}	// if (linesCount > 1)

	size_t	line	= currentInstruction ( );
	if ((false == isRunning ( )) || (true == isExecutingFile ( )))	// v 2.10.0
	{
		if (line < currentInstruction ( ))	// Eviter de réécrire les boucles
			return;

		const ScriptingLog	scriptingLog (command, comments);
		QTextBlock		block	= document ( )->findBlockByNumber (line - 1);
		QTextCursor		cursor	= textCursor ( );
		cursor.setPosition (block.position ( ), QTextCursor::MoveAnchor);
		setTextCursor (cursor);
		if (false == scriptingLog.getComment ( ).empty ( ))	// v 2.7.0
		{
			const UTF8String	comment (PythonLogOutputStream::toComment (scriptingLog.getComment ( )), Charset::UTF_8);
			line	+= lineNumber (comment.utf8 ( ));
//			cursor.insertText ("# ");	// v 3.5.0
			cursor.insertText (UTF8TOQSTRING (comment));
			cursor.insertText ("\n");	// v 1.14.0
//			appendPlainText ("");		// v 1.14.0
			block	= block.next ( );
			cursor.setPosition (block.position ( ), QTextCursor::MoveAnchor);
			setTextCursor (cursor);
		}	// if (false == scriptingLog.getComment ( ).empty ( ))
		if (true == statusErr)
		{
//			line++;	// CP 1.11.0
			UTF8String	error (charset);
			if (false == commandOutput.empty ( ))
				error << commandOutput;
			else
				error << "Erreur non renseignée.";
			line	+= lineNumber (error.utf8 ( ));	// CP 1.11.0
			// Faire ressortir l'erreur. Pb, avec le syntax highlighting on ne peut pas jouer sur la couleur du texte => background
			QTextCharFormat	blockCharFormat	= cursor.blockCharFormat ( );
			blockCharFormat.setBackground (Qt::red);
			cursor.setBlockCharFormat (blockCharFormat);
			const UTF8String	err	=	// v 4.0.0
						PythonLogOutputStream::toComment (error);
			cursor.insertText (UTF8TOQSTRING (err));	// v 4.0.0
//			cursor.insertText (err.c_str ( ));	// v 3.5.0
			cursor.insertText ("\n");	// v 1.14.0
			log (ErrorLog (UTF8String (error, Charset::UTF_8)));
//			appendPlainText ("");		// v 1.14.0
			block	= block.next ( );
			cursor.setPosition (block.position ( ), QTextCursor::MoveAnchor);
			setTextCursor (cursor);
		}	// if (true == statusErr)
		cursor.setBlockFormat (defaultBlockFormat);
		QTextCharFormat	defaultBlockCharFormat	= cursor.blockCharFormat ( );
		defaultBlockCharFormat.setBackground (defaultBackground);	// CP 2.0.1
		cursor.setBlockCharFormat (defaultBlockCharFormat);			// CP 2.0.1
		setCurrentCharFormat (defaultCharFormat);
		line	+= lineNumber (scriptingLog.getText ( ).utf8 ( )) - 1;// CP 1.11.0
		cursor.insertText (UTF8TOQSTRING (scriptingLog.getText ( )));
		cursor.insertText ("\n");	// v 1.14.0
		line++;	// v 6.1.1
//		appendPlainText ("");		// v 1.14.0
		setTextCursor (cursor);
		lineProcessedCallback (line, true);
	}	// if (false == isRunning ( ))
	else
	{
		if (true == statusErr)
		{
			line	= previousInstruction ( );
			QTextBlock		block	= document ( )->findBlockByNumber(line - 1);
			QTextCursor		cursor	= textCursor ( );
			cursor.setPosition (block.position ( ), QTextCursor::MoveAnchor);
			cursor.movePosition (QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);
			QTextCharFormat		charFormat	= cursor.charFormat ( );
			charFormat.setBackground (QtScriptTextFormat::failedInstructionFormat.background ( ));
			cursor.setCharFormat (charFormat);
			cursor.movePosition (QTextCursor::EndOfBlock, QTextCursor::MoveAnchor);
			setTextCursor (cursor);
			cursor.setBlockFormat (defaultBlockFormat);
			setCurrentCharFormat (defaultCharFormat);
			setTextCursor (cursor);

			// Faut-il arrêter l'exécution du script ?
			if (true == stopOnError)
				stop ( );
		}	// if (true == statusErr)
	}	// else if (false == isRunning ( ))
}	// QtPythonConsole::addToHistoric


const string& QtPythonConsole::getInterpreterName ( ) const
{
	return _interpreterName;
}	// QtPythonConsole::getInterpreterName


void QtPythonConsole::setInterpreterName (const string& name)
{
	_interpreterName	= name;
}	// QtPythonConsole::setInterpreterName



void QtPythonConsole::writeSettings (QSettings& settings)
{
	settings.beginGroup ("PythonConsole");
	settings.setValue ("size", size ( ));
	settings.endGroup ( );
}	// QtPythonConsole::writeSettings


void QtPythonConsole::readSettings (QSettings& settings)
{
	settings.beginGroup ("PythonConsole");
	resize (settings.value ("size", size ( )).toSize ( ));
	settings.endGroup ( );
}	// QtPythonConsole::readSettings


void QtPythonConsole::setUsabled (bool enable)
{
#ifdef MULTITHREADED_APPLICATION
	AutoMutex	mutex (_mutex.get ( ));
#endif	// MULTITHREADED_APPLICATION
	emit setUsabledCalled (enable);
}	// QtPythonConsole::setUsabled


void QtPythonConsole::setEnabled (bool enable)
{
#ifndef MULTITHREADED_APPLICATION
	QtTextEditor::setEnabled (enable);
#else	// MULTITHREADED_APPLICATION
	if (thread( ) == QThread::currentThread ( ))
	{
		QtTextEditor::setEnabled (enable);
	}	// if (thread( ) == QThread::currentThread ( ))
	else
	{
	}	// else if (thread( ) == QThread::currentThread ( ))
#endif	// MULTITHREADED_APPLICATION
}	// QtPythonConsole::setEnabled


bool QtPythonConsole::event (QEvent* e)
{
	if (0 == e)
		return false;

	if (true == allowEditionAtCursorPos ( ))
		return QtTextEditor::event (e);

	// Evènement de suppression de texte :
	QKeyEvent*	ke	= dynamic_cast<QKeyEvent*>(e);
	switch (e->type ( ))
	{
		case	QEvent::KeyPress	:
		case	QEvent::KeyRelease	:
			// On laisse passer certains évènements :
			CHECK_NULL_PTR_ERROR (ke)
			switch (ke->key ( ))
			{
				case	Qt::Key_Up	:
				case	Qt::Key_Down		:
				case	Qt::Key_Left		:
				case	Qt::Key_Right		:
				case	Qt::Key_PageUp		:
				case	Qt::Key_PageDown			:
				case	Qt::Key_Home		:
				case	Qt::Key_End		:
				case	Qt::Key_Print		:
				case	Qt::Key_Pause		:
				case	Qt::Key_Escape		:
					return QtTextEditor::event (e);
			}	// switch (ke->key ( ))
			// Sinon :
		case	QEvent::Clipboard	:
			return QtTextEditor::event (e);		// v 5.1.9 : On laisse passer pour Copy
		case	QEvent::RequestSoftwareInputPanel	:
				e->accept ( );
				return true;
		case	QEvent::ShortcutOverride	:
		{	// Les évènements d'éditions sont interdits dans cette zone :
			CHECK_NULL_PTR_ERROR (ke)
			switch (ke->key ( ))
			{
				case	Qt::Key_Backspace	:
				case	Qt::Key_Return		:
				case	Qt::Key_Enter		:
				case	Qt::Key_Delete		:
				case	Qt::Key_Clear		:
				case	Qt::Key_Cut			:
				case	Qt::Key_Paste		:
					e->accept ( );
					return true;
			}	// switch (ke->key ( ))
		}
	}	// switch (e->type ( ))

	return QtTextEditor::event (e);
}	// QtPythonConsole::event


void QtPythonConsole::dragEnterEvent (QDragEnterEvent* e)
{
	// Pb : e->source ( ) != this
	// dragEnterEvent appelé des 2 côtés, mais e->source ( ) côté receveur (au moins si c'est dans une appli différente) ...
	if ((0 == e) || (0 != e->source ( )))
		return;

	// On suppose à ce stade qu'on est côté receveur. On accepte le drop si on est dans une zone non exécutée :
	const QTextCursor	cursor	= cursorForPosition (e->pos ( ));
	const int			line	= cursor.blockNumber ( ) + 1;
	if (false == allowEditionAtLine (line))
		return;

	QtTextEditor::dragEnterEvent (e);
}	// QtPythonConsole::dragEnterEvent


void QtPythonConsole::dragLeaveEvent (QDragLeaveEvent* e)
{
	// Aboutit à la suppression du texte dragué quoiqu'on fasse
	// => refuser d'initier le mouvement (cf. dragEnterEvent).
//	QtTextEditor::dragLeaveEvent (e); */
}	// QtPythonConsole::dragLeaveEvent


void QtPythonConsole::dragMoveEvent (QDragMoveEvent* e)
{
	if (0 == e)
		return;

	const QTextCursor	cursor	= cursorForPosition (e->pos ( ));
	const int			line	= cursor.blockNumber ( ) + 1;
	if (false == allowEditionAtLine (line))
		return;

	QtTextEditor::dragMoveEvent (e);
}	// QtPythonConsole::dragMoveEvent


void QtPythonConsole::dropEvent (QDropEvent* e)
{
	if (0 == e)
		return;

	const QTextCursor	cursor	= cursorForPosition (e->pos ( ));
	const int			line	= cursor.blockNumber ( ) + 1;
	if (false == allowEditionAtLine (line))
		return;

	QtTextEditor::dropEvent (e);
}	// QtPythonConsole::dropEvent


void QtPythonConsole::keyPressEvent (QKeyEvent* event)
{
	if ((0 != event) && (true == allowEditionAtCursorPos ( )))
	{
		switch (event->key ( ))
		{
			case	Qt::Key_Down	:
				if (true == handleDownKeyPress (*event))
					return;
			break;
			case	Qt::Key_Up		:
				if (true == handleUpKeyPress (*event))
					return;
			break;
			case	Qt::Key_Tab		:
				if (0 != (Qt::ControlModifier & event->modifiers ( )))
					if (true == handleComplete (*event))
						return;
			break;
			case	Qt::Key_Escape	:
				CHECK_NULL_PTR_ERROR (_completionComboBox)
				_completionComboBox->hide ( );
				return;
		}	// switch (event->key ( ))
	}	// if ((0 != event) && (true == allowEditionAtCursorPos ( )))

	_historyIndex	= _history.size ( ) - 1;
	QtTextEditor::keyPressEvent (event);
}	// QtPythonConsole::keyPressEvent


bool QtPythonConsole::handleDownKeyPress (QKeyEvent& event)
{
	if (0 == (Qt::ShiftModifier & event.modifiers ( )))
		return false;

	const size_t	size	= _history.size ( );
	if (_historyIndex < size)
	{
		const string	currentLine	= editedInstruction ( );
		string			instruction;
		do 
		{
			instruction	= _history [_historyIndex];
			_historyIndex++;
		}	while ((_historyIndex < size) && (currentLine == instruction));
		_historyIndex	-= 1;

		if (currentLine != instruction)
		{
			QTextCursor	cursor	= textCursor ( );
//			cursor.movePosition (QTextCursor::StartOfLine);
//			cursor.movePosition (QTextCursor::EndOfLine, QTextCursor::KeepAnchor);
			cursor.movePosition (QTextCursor::StartOfBlock);							// v 5.0.0
			cursor.movePosition (QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);		// v 5.0.0
			cursor.insertText (instruction.c_str ( ));
		}	// if (currentLine != instruction)
	}	// if (_historyIndex < size)

	event.accept ( );
	return true;
}	// QtPythonConsole::handleDownKeyPress


bool QtPythonConsole::handleUpKeyPress (QKeyEvent& event)
{
	if (0 == (Qt::ShiftModifier & event.modifiers ( )))
		return false;

	if (_historyIndex < _history.size ( )) // Rem : si -1 alors > h.size ( )
	{
		const string	currentLine	= editedInstruction ( );
		string			instruction;
		do 
		{
			instruction	= _history [_historyIndex];
			_historyIndex--;
		}	while (((size_t)-1 != _historyIndex) && (currentLine==instruction));
		_historyIndex	+= 1;

		if (currentLine != instruction)
		{
			QTextCursor	cursor	= textCursor ( );
//			cursor.movePosition (QTextCursor::StartOfLine);
//			cursor.movePosition (QTextCursor::EndOfLine, QTextCursor::KeepAnchor);
			cursor.movePosition (QTextCursor::StartOfBlock);							// v 5.0.0
			cursor.movePosition (QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);		// v 5.0.0
			cursor.insertText (instruction.c_str ( ));
		}	// if (currentLine != instruction)
	}	// if (_historyIndex < _history.count ( ))

	event.accept ( );
	return true;
}	// QtPythonConsole::handleUpKeyPress


static bool has (const vector<string>completions, const string& comp)
{
	for (vector<string>::const_iterator it = completions.begin ( ); completions.end ( ) != it; it++)
		if (*it == comp)
			return true;

	return false;
}	// has


bool QtPythonConsole::handleComplete (QKeyEvent& event)
{
	CHECK_NULL_PTR_ERROR (_completionComboBox)
	_completionComboBox->clear ( );

	// La chaîne à compléter :
	UTF8String	instruction (editedInstruction ( ), charset), left (charset), right (editedInstruction ( ), charset);	// v 5.2.1
	size_t		equalPos	= instruction.rfind ('=');

	if ((size_t)-1 != equalPos)	// => A = ...
	{
		left	= instruction.substring (0, equalPos);
		right	= instruction.substring (equalPos + 1);
	}	// if ((size_t)-1 != equalPos)
	// On enlève les espaces/tabs de début et fin de chaîne car ils perturbent python dans la recherche de complétion. On les mémorise
	// pour les réinjecter une fois la complétion faite.
	UTF8String	trimedInstruction (right.trim ( )), head (charset), tail (charset);
	if (trimedInstruction.length ( ) != right.length ( ))
	{
		const size_t	first	= right.find (trimedInstruction);
		if (0 != first)
			head	= right.substring (0, first - 1);
		const size_t	last	= first + trimedInstruction.length ( );
		if (right.length ( ) - 1 != last)
			tail	= right.substring (last);
	}	// if (trimedInstruction.length ( ) != right.length ( ))

	// Recherche des complétions possibles :
	vector<string>	completions;
	_checkingCompletion	= true;
	// L'exécution d'instructions python pour évaluer les complétions possibles provoquera le décalage des compteurs de lignes. Or ces
	// appels python ne sont pas ici considérés comme des instructions (utilisateur), donc on restaurera les compteurs de ligne en fin
	// d'évaluation :
	const size_t	currentLine	= _currentExecLine, previousLine	= _previousExecLine;
	for (size_t i = 0; i < 20; i++)
	{	
		try
		{
			UTF8String	request (charset);
//			request << "NECCompletionSession.instance ( ).complete ('" << editedInstruction ( ) << "'," << i << ")";
			request << "NECCompletionSession.instance ( ).complete ('" << trimedInstruction << "'," << i << ")";	// v 5.3.0

			PyCompilerFlags	flags;
//			flags.cf_flags	= CO_FUTURE_DIVISION;
			flags.cf_flags	= 0;	// v 1.13.1
			PyObject*	result	= PyRun_StringFlags (request.iso ( ).c_str ( ), Py_file_input, _globalDict, _localDict, &flags);
			if (NULL == result)
			{
				PyErr_Clear ( );
				break;
			}	// if (NULL == result)

			// Est-ce un proxy Swig ? Auquel cas la signature de la fonction, de type varargs, ne sera pas renseignée par la filière classique
			// (inspect). On provoquera une levée d'exception au niveau du wrapper Swig. Le message d'erreur swig contiendra les signatures
			// possibles ...
			PyObject*	strResult	= 0;
			bool	isSwigProxy		= false;
			if (true == enableSwigCompletion)
			{
				PyObject*	swigProxy	= PyObject_GetAttrString(_dbgCompletionModule, "isSwigProxy");
				CHECK_NULL_PTR_ERROR (swigProxy)
				PyObject*	strResult	= PyObject_CallObject (swigProxy, NULL);
				if (0 != strResult)
				{
					const string	strProxy	= PyString_AsString (strResult);
					if (strProxy == "True")
						isSwigProxy	= true;
				}	// if (0 != strResult)
				if (0 != strResult)
					Py_DECREF (strResult);
				strResult	= 0;
				Py_DECREF (swigProxy);
				swigProxy	= 0;
			}	// if (true == enableSwigCompletion)

			PyObject*	completionFunct	= PyObject_GetAttrString (_dbgCompletionModule, "completion");
			CHECK_NULL_PTR_ERROR (completionFunct)
			strResult		= PyObject_CallObject (completionFunct, NULL);
			if (0 != strResult)
			{
				string	completed	= PyString_AsString (strResult);
//				if (completions.end ( ) == completions.find (completed))
				if (false == has (completions, completed))
				{
					if (false == isSwigProxy)
//						completions.insert (completed);
						completions.push_back (completed);
					else
						addSwigCompletions (completions, completed);
				}	// if (completions.end ( ) == completions.find (completed))
			}	// if (0 != strResult)

			Py_DECREF (completionFunct);
			completionFunct	= 0;
			if (0 != strResult)
				Py_DECREF (strResult);
			Py_DECREF (result);
		}
		catch (...)
		{
			PyErr_Clear ( );
			break;
		}
	}	// for (size_t i = 0; i < 20; i++)

	if (0 != completions.size ( ))
	{
		QRect	rect	= cursorRect ( );
		rect.setX (lineNumberAreaWidth ( ));
		rect.setWidth (width ( ) - lineNumberAreaWidth ( ));
		_completionComboBox->addItem ("");
		set<string>	cmps;
		cmps.insert (completions.begin ( ), completions.end ( ));
		for (set<string>::const_iterator it = cmps.begin ( ); cmps.end ( ) != it; it++)
		{
//			_completionComboBox->addItem ((*it).c_str ( ));		// v 5.3.0
			UTF8String	completion (charset);
			if ((size_t)-1 != equalPos)	// => A = ...
				completion	= left + head.utf8 ( )+ *it + tail.utf8 ( );
			else
				completion	= head.utf8 ( )+ *it + tail.utf8 ( );
			_completionComboBox->addItem (completion.utf8 ( ).c_str ( ));	// v 5.3.0
		}	// for (set<string>::const_iterator it = cmps.begin ( ); cmps.end ( ) != it; it++)
		_completionComboBox->setCurrentIndex (0);
		_completionComboBox->setGeometry (rect);
		_completionComboBox->show ( );
		_completionComboBox->showPopup ( );

		_checkingCompletion	= true;
		try
		{	// Python attend le choix (rlcompleter est interactif) :
			execInstruction ("", false);
		}
		catch (...)
		{
		}
		_currentExecLine	= currentLine;				// v 5.3.0 déplacé par mécanisme de complétion
		_previousExecLine	= _checkingCompletion;		// v 5.3.0
		_checkingCompletion	= false;
	}	// if (0 != completions.size ( ))

	return true;
}	// QtPythonConsole::handleComplete


void QtPythonConsole::addToHistoric (const string& instruction)
{	// On ne stocke pas 2 fois successivement la même instruction
	if (true == _checkingCompletion)	// v 5.3.0
		return;
		
	const size_t	size	= _history.size ( );

	if ((0 == size) || (_history [size - 1] != instruction))
		_history.push_back (instruction);

	_historyIndex	= _history.size ( ) -1 ;
}	// QtPythonConsole::addToHistoric


QMenu* QtPythonConsole::createPopupMenu ( )
{
	QMenu*	menu	= QtTextEditor::createPopupMenu ( );

	try
	{
		CHECK_NULL_PTR_ERROR (menu)

		menu->addSeparator ( );
		menu->addAction (&runningModeAction ( ));

		if (QtPythonConsole::RM_DEBUG == getRunningMode ( ))
		{
			menu->addAction (&runAction ( ));
			menu->addAction (&nextAction ( ));
			menu->addAction (&addBreakPointAction ( ));
			menu->addAction (&removeBreakPointAction ( ));
			menu->addAction (&removeAllBreakPointsAction ( ));
			menu->addAction (&insertScriptAction ( ));
		}	// if (QtPythonConsole::RM_DEBUG == getRunningMode ( ))
		else
		{
			menu->addAction (&runAction ( ));
			menu->addAction (&insertScriptAction ( ));
		}	// else if (QtPythonConsole::RM_DEBUG == getRunningMode ( ))
	}
	catch (...)
	{
	}

	return menu;
}	// QtPythonConsole::createPopupMenu


void QtPythonConsole::updateActions ( )
{
	if (QtPythonConsole::RM_CONTINUOUS == getRunningMode ( ))
	{
		runAction ( ).setEnabled (true);
		stopAction ( ).setEnabled (false);
		nextAction ( ).setEnabled (false);
		addBreakPointAction ( ).setEnabled (false);
		removeBreakPointAction ( ).setEnabled (false);
		removeAllBreakPointsAction ( ).setEnabled (false);
		insertScriptAction ( ).setEnabled (allowEditionAtCursorPos ( ));
	}
	else
	{
		runAction ( ).setEnabled (isHalted ( ));
		// Stop : tout le temps sinon pas forcément d'opportunité d'envoyer cet
		// évènement :
		stopAction ( ).setEnabled (true);
//		nextAction ( ).setEnabled (isHalted ( ));
//		addBreakPointAction ( ).setEnabled (isHalted ( ));
//		removeBreakPointAction ( ).setEnabled (isHalted ( ));
//		removeAllBreakPointsAction ( ).setEnabled (isHalted ( ));
		nextAction ( ).setEnabled (isWaitingForRunning ( ));		// v 1.14.0
		addBreakPointAction ( ).setEnabled (isWaitingForRunning ( ));
		removeBreakPointAction ( ).setEnabled (isWaitingForRunning ( ));
		removeAllBreakPointsAction ( ).setEnabled (isWaitingForRunning ( ));
		if ((true == allowEditionAtCursorPos ( )) && (false == isRunning ( )))
			insertScriptAction ( ).setEnabled (true);
		else
			insertScriptAction ( ).setEnabled (false);
	}	// else if (QtPythonConsole::RM_CONTINUOUS == getRunningMode ( ))
	
	setReadOnly (!allowEditionAtCursorPos ( ));	// v 5.1.9 : éviter Ctrl-X et assimilés
}	// QtPythonConsole::updateActions


size_t QtPythonConsole::currentInstruction ( ) const
{
	return _currentExecLine;
}	// QtPythonConsole::currentInstruction


size_t QtPythonConsole::maxExecLine ( ) const
{
	return _maxExecLine;
}	// QtPythonConsole::maxExecLine


size_t QtPythonConsole::previousInstruction ( ) const
{
	return _previousExecLine;
}	// QtPythonConsole::previousInstruction


string QtPythonConsole::editedInstruction ( ) const
{
	const QTextCursor	cursor		= textCursor ( );
	const string		instruction	= cursor.block ( ).text ( ).toStdString ( );

	return instruction;
}	// QtPythonConsole::editedInstruction


size_t QtPythonConsole::followingInstruction (size_t line) const
{
	if (0 == document ( ))
		return 1;

	// Les numéros de blocs comment à 0, on ne rajoute donc pas 1 pour le
	// suivant :
	QTextBlock	block	= document ( )->findBlockByNumber (line);

	size_t	nonEmpty	= line;
	while (true == block.isValid ( ))
	{
		const string	code	= block.text ( ).toStdString ( );
		if (false == Instruction::isRunnable (code))
		{
			UTF8String	tmp	= UTF8String (code).trim (true);
			if (false == tmp.empty ( ))
				nonEmpty	= block.blockNumber ( );
			block	= block.next ( );
		}
		else
			return block.blockNumber ( ) + 1;	// index commence à 0
	}	// while (true == block.isValid ( ))

	const QTextBlock	lastBlock	= document ( )->lastBlock ( );

	// +1 : index commence à 0
	// +1 : ligne suivante
	return nonEmpty + 1;
}	// QtPythonConsole::followingInstruction


void QtPythonConsole::addInstruction (const string& instruction)
{
	// Un instruction en cours est peut être à compléter :
	_pendingString	+= instruction;

	// Commentaire, ligne blanche, ... : on ajoute et on passe à la suite.
	bool		completed	= false;
	if ((0 == _pendingString.size ( )) || ('#' == _pendingString [0]))
		completed	= true;

	// On évalue l'instruction en cours. Si elle est complète alors on l'affiche et on la recence :
	if (false == completed)
	{
		// Si c'est une instruction multiligne on attend une ligne blanche qui clôture le bloc. Sinon on traite :
		if ((false == Instruction::isMultiline (_pendingString)) || (true == instruction.empty ( )))
		{

			// PyErr_Clear : indispensable car si erreur avant le test d'une
			// instruction OK retournera malgré tout une erreur ...
			PyErr_Clear ( );
			PyObject*	py_result	= Py_CompileString (_pendingString.c_str ( ), "<stdin>", Py_single_input);
			if (0 != py_result)
				completed	= true;
			Py_XDECREF (py_result);
		}
	}	// if (false == completed)
	else
	{	// v 5.1.5 : cas de l'insertion d'un commentaire multiligne. A noter qu'un bogue subsiste et qu'on peut
		// observer un décalage entre commentaires et instructions associées.
		if ((0 != _pendingString.size ( )) && ('#' == _pendingString [0]))
		{
			_pendingString	= '\n' + _pendingString;
			const size_t	count	= Instruction (_pendingString).lineCount ( );
			_currentExecLine	+= count;
		}	// if ((0 != _pendingString.size ( )) && ('#' == _pendingString [0]))
	}	// else if (false == completed)

	if (true == completed)
	{
		Instruction		ins (_pendingString);
		setCurrentCharFormat (QtPythonConsole::QtScriptTextFormat::textFormat (ins));
		_pendingString	+= '\n';
		insertPlainText  (QString::fromUtf8(_pendingString.c_str( )));// v 3.3.0
//		insertPlainText  (_pendingString.c_str ( ));

		_pendingString.clear ( );
		_currentPendingLine	= document ( )->lineCount ( ) + 1;
	}	// if (true == completed)
	else
		_pendingString	+= '\n';
}	// QtPythonConsole::addInstruction


void QtPythonConsole::execInstructions ( )
{
	EndOfDocCursor	endOfDocCursor (*this);	// v 2.5.0

	setRunningMode (QtPythonConsole::RM_CONTINUOUS);
	_running	= true;
	_halted		= true;
	updateActions ( );

	// On mémorise le point courrant, pour actualisation finale IHM en une passe
	const size_t	startAt	= _currentExecLine;

	vector<string>	instructions	= getRunnableInstructions(_currentExecLine);
	const bool		autoDelete	= true;
	_currentScript.reset (new QtPythonConsole::InstructionsFile ("python_panel_script_", QtPythonConsole::enableCodingIso8859_15, autoDelete));
	_commandsFile.reset (0);
	_commandsStream.reset (0);
	_currentScript->addLines (instructions, _currentExecLine);

	// ATTENTION : selon le cas l'offset n'est pas le même.
	size_t	lineNum			= 0;	// Nombre de lignes exécutées.
	size_t	lineOffset		= 0;
	size_t	lineCorrection	= 0;	// Y'a un truc qui m'échappe !
	// 2 cas de figure : une ligne uniquement, on la traite telle que, permet d'avoir dans stdout des retours type "4" pour une ligne t.q. "2 + 2".
	// C'est le fait d'être en une seule instruction + Py_single_input qui offre cette possibilité là.
	// Pour les autres cas on reste avec Py_file_input qui est dédié aux cas multilignes.
	PyObject*		result			= 0;
	PyCompilerFlags	flags;
//	flags.cf_flags					= CO_FUTURE_DIVISION;
	flags.cf_flags					= 0;	// v 1.13.1
	PyErr_Clear ( );
	if (1 == instructions.size ( ))
	{	// CP v 2.5.0 : on pourrait évaluer la qualité de l'instruction (générée en python) à l'aide par exemple de Py_CompileString (cf.
		// QPyConsole::interpretCommand). En cas d'absence d'instruction (commentaire, ligne finissant par ':') on pourrait afficher un
		// message personnalisé.
		// Mais l'utilisateur est supposé le savoir => on choisit de ne pas allourdir le code car cela n'a pas lieu d'être et on fait un effort
		// en cas d'erreur (cf. fin de traitement).
		lineNum			= 1;
		lineCorrection	= 1;	// Why ???

		try
		{	// v 2.6.0 : gérer les erreurs C++ de l'API appelée
			result	= PyRun_StringFlags (instructions [0].c_str ( ), Py_single_input, _globalDict, _localDict, &flags);
		}
		catch (...)
		{
		}

		// C'est fini :
//		if (NULL != result)	// v 2.6.0 : lignes non-éditables
			_currentExecLine	+= _currentScript->getLineCount ( );
	}	// if (1 == instructions.size ( ))
	else
	{
		// v 2.6.0 : on se positionne en fin de document quoi qu'il arrive pour offrir un curseur sur fond blanc.
		EndOfDocCursor	endOfDocCursor (*this);

		FILE*		f		= fopen (_currentScript->getFileName ( ).c_str( ), "r");

		try
		{	// v 2.6.0 : gérer les erreurs C++ de l'API appelée
			result	= PyRun_FileExFlags (f, _currentScript->getFileName ( ).c_str ( ), Py_file_input, _globalDict, _localDict, 1, &flags);
		}
		catch (...)
		{
		}

		// C'est fini :
//		if (NULL != result)	// v 2.6.0 : lignes non-éditables
		{
			_currentExecLine	+= _currentScript->getLineCount ( );
			lineNum				= _currentScript->getLineCount ( );
		}	// if (NULL != result)
		lineOffset	= _currentScript->getLineOffset ( );
	}	// else if (1 == instructions.size ( ))

	if (NULL != result)	// v 1.13.1
		PyErr_Clear ( );

	// Affichage stdout/stderr de python dans la vue logs de la console :
	processPythonOutputs ( );

	_running					= false;
	_halted						= true;

	// Actualisation IHM :
	const size_t	lastAt	= _currentExecLine;
	_currentExecLine	= startAt;
	// 2 sauts de lignes nécessaire pour qu'en fin de document on ne reparte pas sur un fond gris ou orange ...
	if (true == textCursor ( ).atEnd ( ))
		textCursor ( ).insertText ("\n");
	// On prend ici lineOffset en compte car dans lineProcessedCallback il est déduit par getConsoleLine ( ).
	for (size_t i = 1; i <= lineNum; i++)
		lineProcessedCallback (_currentScript->getFileName ( ), i + lineOffset + lineCorrection, NULL != result ? true : false);
	_currentExecLine	= lastAt;
	_previousExecLine	= lastAt - 1;	// A affiner
	_maxExecLine		= _currentExecLine - 1;
	_currentScript.reset (0);

	validateCursorPosition ( );

	updateActions ( );

	UTF8String	pythonError (NULL == result ? get_python_error( ) : UTF8String(Charset::UTF_8));
	PyErr_Clear ( );	// v 1.13.1
	if (NULL == result)
	{
		if (1 == instructions.size ( ))
		{	// CP v 2.5.0 : personnaliser certains cas
			const char	lastChar	= instructions [0][instructions[0].size ( ) - 1];
			if ((string::npos != pythonError.find ("unexpected EOF while parsing")) && (':' != lastChar))
			{
				UTF8String	msg (charset);
				msg << "Absence d'instruction à traiter dans la ligne :\n" << instructions [0];
				pythonError	= msg;

				// On essaie d'actualiser l'IHM en prenant en compte cette
				// non-erreur :
				_currentExecLine	+= 1;
				_previousExecLine	= _currentExecLine - 1;
				_maxExecLine		= _currentExecLine - 1;
				validateCursorPosition ( );
			}	// if ((string::npos != pythonError.find (...
		}
		throw Exception (pythonError);	// v 2.0.1
	}

	Py_DECREF (result);
}	// QtPythonConsole::execInstructions


void QtPythonConsole::execDbgInstructions (bool stopImmediatly)
{
	size_t			line	= currentInstruction ( );						// v 6.2.0
	QTextBlock		block	= document ( )->findBlockByNumber (line - 1);	// v 6.2.0
	QTextCursor		cursor	= textCursor ( );								// v 6.2.0
	cursor.setPosition (block.position ( ), QTextCursor::MoveAnchor);		// v 6.2.0
	EndOfDocCursor	endOfDocCursor (*this);									// v 2.5.0
	endOfDocCursor.setEnabled (false);										// v 6.2.0

	setRunningMode (QtPythonConsole::RM_DEBUG);
	_halted	= false;
	updateActions ( );

	QString				text	= document ( )->toPlainText ( );
	const bool			autoDelete	= false;
	_currentScript.reset (new QtPythonConsole::InstructionsFile ("python_panel_script_", QtPythonConsole::enableCodingIso8859_15, autoDelete));
	_commandsFile.reset (new TemporaryFile (true, "python_panel_commands_", true, autoDelete));
	_commandsStream.reset (new ofstream (_commandsFile->getFullFileName ( ).c_str( ), ios::trunc));

	vector<string>	instructions	= getRunnableInstructions(_currentExecLine);
	_currentScript->addLines (instructions, _currentExecLine);

	// Le fichier de communication avec Pdb :
	UTF8String	cmd (charset);
	cmd <<  "NECDebuggingSession.instance ( ).set_cmd_stream ('" << _commandsFile->getFullFileName ( ) << "')";
	execInstruction (cmd.iso ( ), false);
	
	// On pose les éventuels breakpoints :
	bool	bpSet	= false;
	for (set<size_t>::const_iterator itbp = _breakpoints.begin ( ); _breakpoints.end ( ) != itbp; itbp++)
	{
		if (*itbp <= _currentExecLine)
			continue;

		UTF8String	bp (charset);
		bp << "NECDebuggingSession.instance ( ).set_break ('" << _currentScript->getFileName ( ) << "', int("
		   << (*itbp - _currentExecLine + 1 + _currentScript->getLineOffset ( )) << "))";
		execInstruction (bp.iso ( ), false);
		bpSet	= true;
	}	// for (set<size_t>::const_iterator itb = breakpoints.begin ( ); ...

	// En l'absence de breakpoint posé le handler lineProcessed de QtPythonCalls n'est pas appelé, et on n'a donc pas de retour sur la position courante.
	// => mettre un breakpoint temporaire en fin de script.
	// Pour être sûr que ce breakpoint soit accepté on insère une instruction bidon (sinon risque d'avoir "*** Blank or comment" ... On insère dès
	// maintenant cette instruction bidon car on peut avoir suppression des points d'arrêt par la suite (par ex : clearBreakpoints ( )).
	instructions.clear ( );
	instructions.push_back ("0 + 0");
	_currentScript->addLines (instructions, 0);
	_currentScript->setRequestedBreakPoint(_currentScript->getRealLineCount( ));
	if (false == bpSet)
	{
		UTF8String	bp (charset);
		bp << "NECDebuggingSession.instance ( ).set_break ('" << _currentScript->getFileName ( ) << "', " << _currentScript->getRealLineCount ( ) << ", 1)";
		execInstruction (bp.iso ( ), false);
	}	// if (false == bpSet)

	// Faudra t-il s'arrêter après la première instruction  (démarrage par "suivant") ?
	if (true == stopImmediatly)
	{
		const size_t	first	= _currentScript->nextRunnableLine (0, false);
		const size_t	stop	=
				_currentScript->nextRunnableLine (first, false) + _currentScript->getLineOffset ( );
		if (0 != stop)
		{
			UTF8String	bp (charset);
			bp << "NECDebuggingSession.instance ( ).set_break ('" << _currentScript->getFileName ( ) << "', " << stop << ", 1)";
			execInstruction (bp.iso ( ), false);
		}	// if (0 != stop)
	}	// if (true == stopImmediatly)

	UTF8String	calledFunction (charset);
#ifdef USE_EXECFILE_PYTHON_FUNCTION
	calledFunction << "NECDebuggingSession.instance ( ).run (\"execfile ('" << _currentScript->getFileName ( ) << "')\")";
#else	// USE_EXECFILE_PYTHON_FUNCTION
	calledFunction << _currentScript->getFileName ( );
#endif	// USE_EXECFILE_PYTHON_FUNCTION
	_running	= true;
	_halted		= false;

	string	error;
	try
	{
#ifdef USE_EXECFILE_PYTHON_FUNCTION
		execInstruction (calledFunction, false);
#else	// USE_EXECFILE_PYTHON_FUNCTION
		// Cont : au tout début du fichier on a mis un Pdb ( ).set_trace ( )
		// => Pdb sera en attente d'instructions
		// => on fait un continue, il s'arrêtera au premier point d'arrêt demandé.
		cont ( );
		execFile (calledFunction);	// Ne marche pas lors de la 2nde session
#endif	// USE_EXECFILE_PYTHON_FUNCTION
	}
	catch (const Exception& exc)
	{
		error	= exc.getFullMessage ( );
		// addToHistoric ici car il faut false == isRunning ( )
		addToHistoric ("", "", error, true, false);
	}
	catch (...)
	{
		// addToHistoric ici car il faut false == isRunning ( )
		addToHistoric ("", "", error, true, false);
		error	= "Erreur non documentée.";
	}

	// C'est fini :
	_currentScript.reset (0);
	_running					= false;
	_halted						= true;
	line	= currentInstruction ( );										// v 6.2.0
	block	= document ( )->findBlockByNumber (line - 1);					// v 6.2.0
	cursor	= textCursor ( );												// v 6.2.0
	cursor.setPosition (block.position ( ), QTextCursor::MoveAnchor);		// v 6.2.0
	setTextCursor (cursor);													// v 6.2.0
	updateActions ( );

	if (false == error.empty ( ))
	{
		UTF8String	message (charset);
		message << "Erreur lors de l'exécution des instructions python en mode debug :" << "\n" << error;
		throw Exception (message);
	}	// if (false == error.empty ( ))
}	// QtPythonConsole::execDbgInstructions


void QtPythonConsole::sendDbgCommand (const string& cmd)
{
	if (QtPythonConsole::RM_DEBUG != getRunningMode ( ))
		return;

	if (0 == _commandsStream.get ( ))
	{
		UTF8String	message (charset);
		message << "Absence de flux de transmission de commandes pour la commande " << cmd << ".";
		INTERNAL_ERROR (exc, message, "QtPythonConsole::sendDbgCommand")
		throw exc;
	}	// if (0 == _commandsStream.get ( ))

	(*_commandsStream.get ( )) << cmd << "\n" << std::flush;
}	// QtPythonConsole::sendDbgCommand


void QtPythonConsole::execInstruction (const string& instruction, bool insert)
{
	EndOfDocCursor	endOfDocCursor (*this);	// v 2.5.0

	CHECK_NULL_PTR_ERROR (document ( ))

	if (false == _checkingCompletion)	// v 5.3.0
	{
		if (true == insert)
		{
			const size_t	line	= currentInstruction ( );
			QTextBlock		block	= document ( )->findBlockByNumber (line - 1);
			QTextCursor		cursor	= textCursor ( );
			cursor.setPosition (block.position ( ), QTextCursor::MoveAnchor);
			cursor.insertText (instruction.c_str ( ));
			cursor.insertText ("\n");
//			appendPlainText ("");	// v 1.14.0
		}	// if (true == insert)
	}	// if (false == _checkingCompletion)

	PyCompilerFlags	flags;
//	flags.cf_flags		= CO_FUTURE_DIVISION;
	flags.cf_flags		= 0;	// v 1.13.1
	PyObject*	result	= 0;
	try
	{	// v 2.6.0 : gérer les erreurs C++ de l'API appelée
		result	= PyRun_StringFlags (instruction.c_str ( ), Py_file_input, _globalDict, _localDict, &flags);
	}
	catch (...)
	{	// v 2.6.0 : on se retrouve en mode normal.
		// A noter que Pdb est instable, l'idéal serait de faire un reset UTF8String	bp (charset);
		// bp << "NECDebuggingSession.instance ( ).reset ( )";
		// execInstruction (bp.iso ( ), false);
		// Le pb c'est que ça attend une instruction pour être effectif ...
		// Là, en l'état, il faut basculer via l'IHM 2 fois en mode debug pour que ce mode fonctionne à nouveau ...
		if ((QtPythonConsole::RM_DEBUG == getRunningMode ( )) && (0 != _runningModeAction))
			_runningModeAction->setChecked (false);	// v 2.6.0
	}
	processPythonOutputs ( );

	if (NULL == result)
	{
		UTF8String	error (charset);
		error << "Erreur lors de l'exécution de l'instruction \"" << instruction;
		error << " : " << get_python_error ( );
		PyErr_Clear ( );

		throw Exception (error);
	}	// if (0 != retCode)
	Py_DECREF (result);

	// Actualisation IHM :
	if (true == insert)
			lineProcessedCallback (_currentExecLine, true);
}	// QtPythonConsole::execInstruction


void QtPythonConsole::execFile (const string& fileName)
{
	EndOfDocCursor	endOfDocCursor (*this);	// v 2.5.0

	errno	= 0;
	FILE*	file	= fopen (fileName.c_str ( ), "r");
	if (NULL == file)
	{
		UTF8String	error (charset);
		error << "Impossibilité d'exécuter le script python " << fileName << " :" << "\n" << strerror (errno);
		throw Exception (error);
	}	// if (NULL == file)

	const bool	wasRunning	= _running;		// v 2.9.1
	_running		= true;					// v 2.9.1
	_executingFile	= true;					// v 2.10.0

	PyCompilerFlags	flags;
//	flags.cf_flags	= CO_FUTURE_DIVISION;
	flags.cf_flags	= 0;	// v 1.13.1
	PyErr_Clear ( );
	PyObject*	result	= 0;
	try
	{	// v 2.9.1 : gérer les erreurs C++ de l'API appelée
		result	= PyRun_FileFlags (file, fileName.c_str ( ), Py_file_input, _globalDict, _localDict, &flags);
	}
	catch (...)
	{
	}
	fclose (file);	file	= NULL;

	// Afficher les sorties python :
	processPythonOutputs ( );	// v 2.15.0

	_running		= wasRunning;		// v 2.9.1
	_executingFile	= false;			// v 2.10.0

	if (NULL == result)
	{
		UTF8String	error (charset);
		error << "Impossibilité d'exécuter le script python " << fileName << " :" << "\n" << get_python_error ( );
		PyErr_Clear ( );
		throw Exception (error);
	}	// if (NULL == result)

	Py_DECREF (result);
}	// QtPythonConsole::execFile


vector<string> QtPythonConsole::getRunnableInstructions (size_t first) const
{
	vector<string>	instructions;
	QString			text	= document ( )->toPlainText ( );
	QStringList		lines	= QString (text).split ("\n", QString::KeepEmptyParts);
	// On élimine les lignes blanches finales :
	size_t	last	= lines.size ( );
	for (size_t i = last; i != 0; i--)
	{
		UTF8String	line	= UTF8String (lines [i - 1].toStdString ( )).trim (true);
		if (true == line.empty ( ))
			last--;
		else
			break;
	}	// for (size_t i = last; i != 0; i--)
	for (size_t i = 1; i <= last; i++)
	{
		if (i >= first)
		{
			const string	line (lines [i - 1].toStdString ( ));
			instructions.push_back (line);
		}	// if (i >= first)
	}	// for (size_t i = 1; i <= last; i++)

	return instructions;
}	// QtPythonConsole::getRunnableInstructions


void QtPythonConsole::where (string& filename, size_t& line)
{
//	PyErr_Clear ( );
	PyObject*	whereFileFunct		= PyObject_GetAttrString (_dbgSessionModule, "fileName");
	PyObject*	strResult			= PyObject_CallObject (whereFileFunct, NULL);
	filename.clear ( );
	char*	name	= NULL;
	CHECK_PYTHON_CALL (name, PyString_AsString (strResult), "PyString_AsString")
	filename	= name;
	PyObject*	whereLineFunct		= PyObject_GetAttrString (_dbgSessionModule, "lineNo");
	PyObject*	longResult			= PyObject_CallObject (whereLineFunct, NULL);
	line		= PyInt_AsLong (longResult);
	if ((-1 == line) && (0 != PyErr_Occurred ( )))
		PyErr_Clear ( );
	Py_DECREF (longResult);			longResult		= NULL;
	Py_DECREF (whereLineFunct);		whereLineFunct	= NULL;
	Py_DECREF (strResult);			strResult		= NULL;
	Py_DECREF (whereFileFunct);		whereFileFunct	= NULL;
}	// QtPythonConsole::where


void QtPythonConsole::processPythonOutputs ( )
{
	try
	{
/*		QString	trimmed	= outputsString.trimmed ( );
		if (false == trimmed.isEmpty ( ))
		{
			ProcessLog	log ("", trimmed.toStdString ( ));
			getLogDispatcher ( ).log (log);
			outputsString.clear ( ); 
		}	// if (false == trimmed.isEmpty ( )) */
		if (false == _checkingCompletion)	// v 5.3.0
		{
			if (false == outputsString.empty ( ))	// v 3.3.0
			{
				ProcessLog	log ("", outputsString);
				getLogDispatcher ( ).log (log);
				outputsString.clear ( );
			}	// if (false == outputsString.empty ( ))
		}
		else
			outputsString.clear ( );
	}
	catch (...)
	{
	}
}	// QtPythonConsole::processPythonOutputs


void QtPythonConsole::runningModeCallback (bool debug)
{
	try
	{
		if ((false == debug) && (0 != document ( )))
		{	// On suggère de ne pas s'arrêter au milieu d'un bloc conditionnel
			// car la reprise risque d'être laborieuse ...
			const QTextBlock	block	= document ( )->findBlockByNumber (currentInstruction ( ) - 1);
			const QtPythonConsole::Instruction	instruction (block.text ( ).toStdString ( ));
			if (true == instruction.partOfBlock ( ))
			{
				UTF8String	question (charset);
				question << "Vous souhaitez quitter le mode debug à la ligne " << currentInstruction ( ) << " :\n" << instruction.command ( )
				         << "\nCette instruction est dans un bloc (par exemple boucle for, while, do ... while, bloc "
				         << "conditionnel if/then/else ou try/catch), cet outil ne sera pas en mesure de reprendre en ce "
				         << "point l'exécution du script, que ce soit en mode normal ou debug.\n"
				         << "Confirmez-vous la sortie du mode debug ?";
				switch (QMessageBox::warning (this, getAppName ( ).c_str ( ), UTF8TOQSTRING (question), "Oui", "Annuler", QString ( ), 0, -1))
				{
					case	1	:
					{
						QtActionAutoLock	lock (&runningModeAction ( ));
						runningModeAction ( ).setChecked (true);
						return;
					}
				}	// switch (QMessageBox::warning (...
			}	// if (true == instruction.partOfBlock ( ))
		}	// if ((false == debug) && (0 != document ( )))
		const QtPythonConsole::RUNNING_MODE	mode	=
			true==debug ? QtPythonConsole::RM_DEBUG : QtPythonConsole::RM_CONTINUOUS;
		setRunningMode (mode);
	}
	catch (const Exception& exc)
	{
		UTF8String	message (charset);
		message << "Erreur lors du changement de mode d'exécution des instructions python :\n" << exc.getFullMessage ( );
		QtMessageBox::displayErrorMessage (this, getAppName ( ), message);
	}
	catch (...)
	{
		UTF8String	message (charset);
		message << "Erreur non documentée lors du changement de mode d'exécution des instructions python.";
		QtMessageBox::displayErrorMessage (this, getAppName ( ), message);
	}

	updateActions ( );
}	// QtPythonConsole::runningModeCallback


void QtPythonConsole::runCallback ( )
{
	try
	{
		run ( );
	}
	catch (const Exception& exc)
	{
		UTF8String	message (charset);
		message << "Erreur lors de l'exécution des instructions python :\n" << exc.getFullMessage ( );
		log (ErrorLog (message));
		QtMessageBox::displayErrorMessage (this, getAppName ( ), message);
	}
	catch (...)
	{
		UTF8String	message (charset);
		message << "Erreur non documentée lors de l'exécution des instructions python.";
		log (ErrorLog (message));
		QtMessageBox::displayErrorMessage (this, getAppName ( ), message);
	}
}	// QtPythonConsole::runCallback


void QtPythonConsole::stopCallback ( )
{
	try
	{
		stop ( );
	}
	catch (const Exception& exc)
	{
		UTF8String	message (charset);
		message << "Erreur lors de l'arrêt de l'exécution des instructions python :\n" << exc.getFullMessage ( );
		QtMessageBox::displayErrorMessage (this, getAppName ( ), message);
	}
	catch (...)
	{
		UTF8String	message (charset);
		message << "Erreur non documentée lors de l'arrêt de l'exécution des instructions python.";
		QtMessageBox::displayErrorMessage (this, getAppName ( ), message);
	}

	updateActions ( );
}	// QtPythonConsole::stopCallback


void QtPythonConsole::nextCallback ( )
{
	try
	{
		_halted	= false;
		next ( );
	}
	catch (const Exception& exc)
	{
		UTF8String	message (charset);
		message << "Erreur lors de l'exécution de l'instruction suivante : \n" << exc.getFullMessage ( );
		QtMessageBox::displayErrorMessage (this, getAppName ( ), message);
	}
	catch (...)
	{
		UTF8String	message (charset);
		message << "Erreur non documentée lors de l'exécution de l'instruction suivante.";
		QtMessageBox::displayErrorMessage (this, getAppName ( ), message);
	}

	_halted	= true;
}	// QtPythonConsole::nextCallback


void QtPythonConsole::addBreakPointCallback ( )
{
	try
	{
		const size_t	line	= textCursor ( ).blockNumber ( ) + 1;
		addBreakpoint (line);
	}
	catch (const Exception& exc)
	{
		UTF8String	message (charset);
		message << "Erreur lors de l'ajout d'un point d'arrêt :\n" << exc.getFullMessage ( );
		QtMessageBox::displayErrorMessage (this, getAppName ( ), message);
	}
	catch (...)
	{
		UTF8String	message (charset);
		message << "Erreur non documentée lors de l'ajout d'un point d'arrêt.";
		QtMessageBox::displayErrorMessage (this, getAppName ( ), message);
	}
}	// QtPythonConsole::addBreakPointCallback


void QtPythonConsole::removeBreakPointCallback ( )
{
	try
	{
		const size_t	line	= textCursor ( ).blockNumber ( ) + 1;
		removeBreakpoint (line);
	}
	catch (const Exception& exc)
	{
		UTF8String	message (charset);
		message << "Erreur lors de la suppression du point d'arrêt :\n" << exc.getFullMessage ( );
		QtMessageBox::displayErrorMessage (this, getAppName ( ), message);
	}
	catch (...)
	{
		UTF8String	message (charset);
		message << "Erreur non documentée lors de la suppression du point d'arrêt.";
		QtMessageBox::displayErrorMessage (this, getAppName ( ), message);
	}
}	// QtPythonConsole::removeBreakPointCallback


void QtPythonConsole::clearBreakPointsCallback ( )
{
	try
	{
		clearBreakpoints ( );
	}
	catch (const Exception& exc)
	{
		UTF8String	message (charset);
		message << "Erreur lors de la suppression des points d'arrêt :\n" << exc.getFullMessage ( );
		QtMessageBox::displayErrorMessage (this, getAppName ( ), message);
	}
	catch (...)
	{
		UTF8String	message (charset);
		message << "Erreur non documentée lors de la suppression des points d'arrêt.";
		QtMessageBox::displayErrorMessage (this, getAppName ( ), message);
	}
}	// QtPythonConsole::clearBreakPointsCallback


void QtPythonConsole::insertScriptCallback ( )
{
	try
	{
		UTF8String	message (charset);
		message << "Sélectionnez un script python.";

		QString	fileName	 = QFileDialog::getOpenFileName (this, UTF8TOQSTRING (message), getAppName ( ).c_str ( ), "scripts python (*.py)", 0, QFileDialog::DontUseNativeDialog);
		if (true == fileName.isEmpty ( ))
			return;

		string	warnings;
		insert (fileName.toStdString ( ), warnings);
		if (0 != warnings.size ( ))
			QtMessageBox::displayWarningMessage (this, getAppName ( ).c_str ( ), warnings.c_str ( ));
	}
	catch (const Exception& exc)
	{
		UTF8String	message (charset);
		message << "Erreur lors de l'insertion d'un script :\n" << exc.getFullMessage ( );
		QtMessageBox::displayErrorMessage (this, getAppName ( ), message);
	}
	catch (...)
	{
		UTF8String	message (charset);
		message << "Erreur non documentée lors de l'insertion d'un script.";
		QtMessageBox::displayErrorMessage (this, getAppName ( ), message);
	}
}	// QtPythonConsole::insertScriptCallback


void QtPythonConsole::completionCallback (int index)
{
	try
	{
		CHECK_NULL_PTR_ERROR (_completionComboBox)
		_completionComboBox->hide ( );
		QTextCursor	cursor		= textCursor ( );
		cursor.movePosition (QTextCursor::StartOfBlock,QTextCursor::MoveAnchor);
		cursor.movePosition (QTextCursor::EndOfBlock,QTextCursor::KeepAnchor);
		cursor.removeSelectedText ( );
		cursor.insertText (_completionComboBox->currentText ( ));
	}
	catch (const Exception& exc)
	{
		UTF8String	message (charset);
		message << "Erreur lors de la saisie d'une ligne script par complétion :\n" << exc.getFullMessage ( );
		QtMessageBox::displayErrorMessage (this, getAppName ( ), message);
	}
	catch (...)
	{
		UTF8String	message (charset);
		message << "Erreur non documentée lors de la saisie d'une ligne script par complétion.";
		QtMessageBox::displayErrorMessage (this, getAppName ( ), message);
	}
}	// QtPythonConsole::completionCallback


void QtPythonConsole::cursorPositionCallback ( )
{
	try
	{
		QtTextEditor::cursorPositionCallback ( );	// presumed no throw
		updateActions ( );
	}
	catch (...)
	{
		UTF8String	message (charset);
		message << "Erreur non documentée lors de l'actualisation des actions de la console python.";
		QtMessageBox::displayErrorMessage (this, getAppName ( ), message);
	}
}	// QtPythonConsole::cursorPositionCallback


// =============================================================================
//                      LA CLASSE QtDecoratedPythonConsole
// =============================================================================

QtDecoratedPythonConsole::QtDecoratedPythonConsole (QWidget* parent, const string& appName, Qt::ToolBarArea area)
	: QMainWindow (parent), _pythonConsole (0)
{
	createGui (*new QtPythonConsole (this, appName), area);
}	// QtDecoratedPythonConsole::QtDecoratedPythonConsole


QtDecoratedPythonConsole::QtDecoratedPythonConsole (QWidget* parent, QtPythonConsole& console, Qt::ToolBarArea area)
	: QMainWindow (parent), _pythonConsole (0)
{
	createGui (console, area);
}	// QtDecoratedPythonConsole::QtDecoratedPythonConsole


QtDecoratedPythonConsole::QtDecoratedPythonConsole (const QtDecoratedPythonConsole&)
	:  QMainWindow (0), _pythonConsole (0)
{
	assert (0 && "QtDecoratedPythonConsole copy constructor is not allowed.");
}	// QtDecoratedPythonConsole::QtDecoratedPythonConsole


QtDecoratedPythonConsole& QtDecoratedPythonConsole::operator = (const QtDecoratedPythonConsole&)
{
	assert (0 &&"QtDecoratedPythonConsole assignment operator is not allowed.");
	return *this;
}	// QtDecoratedPythonConsole::QtDecoratedPythonConsole


QtDecoratedPythonConsole::~QtDecoratedPythonConsole ( )
{
}	// QtDecoratedPythonConsole::~QtDecoratedPythonConsole


const QtPythonConsole& QtDecoratedPythonConsole::getPythonConsole ( ) const
{
	CHECK_NULL_PTR_ERROR (_pythonConsole)
	return *_pythonConsole;
}	// QtDecoratedPythonConsole::getPythonConsole


QtPythonConsole& QtDecoratedPythonConsole::getPythonConsole ( )
{
	CHECK_NULL_PTR_ERROR (_pythonConsole)
	return *_pythonConsole;
}	// QtDecoratedPythonConsole::getPythonConsole


const QtPythonConsole* QtDecoratedPythonConsole::operator -> ( ) const
{
	CHECK_NULL_PTR_ERROR (_pythonConsole)
	return _pythonConsole;
}	// QtDecoratedPythonConsole::operator ->


QtPythonConsole* QtDecoratedPythonConsole::operator -> ( )
{
	CHECK_NULL_PTR_ERROR (_pythonConsole)
	return _pythonConsole;
}	// QtDecoratedPythonConsole::operator ->


void QtDecoratedPythonConsole::createGui (QtPythonConsole& console, Qt::ToolBarArea area)
{
	assert (0 == _pythonConsole);
	_pythonConsole	= &console;
	setCentralWidget (_pythonConsole);
	addToolBar (area, &_pythonConsole->getToolBar ( ));
}	// QtDecoratedPythonConsole::createGui


