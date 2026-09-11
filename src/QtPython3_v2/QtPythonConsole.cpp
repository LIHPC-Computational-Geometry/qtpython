#include <Python.h>	// En 1er car contient (dans object.h) le mot clé "slots" en Python 3 et ça interfère avec Qt ...
#include "QtPython3_v2/QtPythonConsole.h"
#include "QtPython3_v2/QtPythonSyntaxHighlighter.h"
#include "pyconsole/PyConsoleWidget.h"

//#include <QtUtil/QtActionAutoLock.h>
#include <QtUtil/QtMessageBox.h>
#include <QtUtil/QtUnicodeHelper.h>
#include <PythonUtil/PythonLogOutputStream.h>
#include <TkUtil/ErrorLog.h>
#include <TkUtil/Exception.h>
#include <TkUtil/InternalError.h>
#include <TkUtil/MemoryError.h>

#include <QHBoxLayout>
#include <QFileDialog>

#include <sstream>
#include <stdio.h>
#include <algorithm>


using namespace TkUtil;
using namespace std;

static const TkUtil::Charset	charset ("àéèùô");

USE_ENCODING_AUTODETECTION





// ============================================================================
//                             FONCTIONS STATIQUES
// ============================================================================


// ============================================================================
//                     LA CLASSE QtInternalPyConsoleWidget
// ============================================================================

/**
 * Cette classe personnalise le widget PyConsoleWidget co-développé à l'extérieur.
 */
class QtInternalPyConsoleWidget : public PyConsoleWidget
{
	public :
	
	/**
	 * Constructeur.
	 * @param	Console python associée.
	 */
	explicit QtInternalPyConsoleWidget (QtPythonConsole& pythonConsole);

	/**
	 * Destructeur. RAS.
	 */
	 virtual ~QtInternalPyConsoleWidget ( );

	/**
	 * Spécialisation des traitements dans le contexte Lem (actualisation de l'IHM, ...).
	 */
	virtual void onModeToggled (bool debugChecked);
	virtual void onRunClicked ( );
	virtual void onStepClicked ( );
	virtual void onStopClicked ( );
	virtual void onClearAllBreakpointsClicked ( );
	virtual void onInsertAsExecutedClicked ( );
	virtual void execInstruction (const string& instruction, const string& comment = string ( ));


	protected :

	/**
	 * Spécialisation des traitements dans le contexte Lem (actualisation de l'IHM, ...).
	 */
	virtual void onPaused (int line);
	virtual void onExceptionRaised (int line, QString text);
	virtual void onExecutionFinished (bool stoppedEarly);


	private :

	/**
	 * Constructeur de copie / opérateur = : interdits.
	 */
	QtInternalPyConsoleWidget (const QtInternalPyConsoleWidget&);
	QtInternalPyConsoleWidget& operator = (const QtInternalPyConsoleWidget&);

	// La console python associée :
	QtPythonConsole*		_pythonConsole;
};	// class QtInternalPyConsoleWidget


QtInternalPyConsoleWidget::QtInternalPyConsoleWidget (QtPythonConsole& pythonConsole)
	: PyConsoleWidget (&pythonConsole), _pythonConsole (&pythonConsole)
{
}	// QtInternalPyConsoleWidget::QtInternalPyConsoleWidget


QtInternalPyConsoleWidget::QtInternalPyConsoleWidget (const QtInternalPyConsoleWidget&)
	: PyConsoleWidget (0), _pythonConsole (0)
{
	assert (0 && "PyConsoleWidget copy constructor is not allowed.");
}	// QtInternalPyConsoleWidget::QtInternalPyConsoleWidget


QtInternalPyConsoleWidget& QtInternalPyConsoleWidget::operator = (const QtInternalPyConsoleWidget&)
{
	assert (0 && "PyConsoleWidget assignment operator is not allowed.");
	return *this;
}	// QtInternalPyConsoleWidget::operator =


QtInternalPyConsoleWidget::~QtInternalPyConsoleWidget ( )
{
}	// QtInternalPyConsoleWidget::~QtInternalPyConsoleWidget 


void QtInternalPyConsoleWidget::onModeToggled (bool debugChecked)
{
	assert (0 != _pythonConsole);
	PyConsoleWidget::onModeToggled (debugChecked);
}	// QtInternalPyConsoleWidget::onModeToggled


void QtInternalPyConsoleWidget::onRunClicked ( )
{
	assert (0 != _pythonConsole);
	_pythonConsole->initConsoleCommandExecution ( );
	PyConsoleWidget::onRunClicked ( );
}	// QtInternalPyConsoleWidget::onRunClicked


void QtInternalPyConsoleWidget::onStepClicked ( )
{
	assert (0 != _pythonConsole);
	_pythonConsole->initConsoleCommandExecution ( );
	PyConsoleWidget::onStepClicked ( );
}	// QtInternalPyConsoleWidget::onStepClicked


void QtInternalPyConsoleWidget::onStopClicked ( )
{
	assert (0 != _pythonConsole);
	_pythonConsole->initConsoleCommandExecution ( );
	PyConsoleWidget::onStopClicked ( );
}	// QtInternalPyConsoleWidget::onStopClicked


void QtInternalPyConsoleWidget::onClearAllBreakpointsClicked ( )
{
	assert (0 != _pythonConsole);
}	// QtInternalPyConsoleWidget::onClearAllBreakpointsClicked


void QtInternalPyConsoleWidget::onInsertAsExecutedClicked ( )
{
	assert (0 != _pythonConsole);
}	// QtInternalPyConsoleWidget::onInsertAsExecutedClicked


void QtInternalPyConsoleWidget::onPaused (int line)
{
	assert (0 != _pythonConsole);
	PyConsoleWidget::onPaused (line);
	_pythonConsole->completeConsoleCommandExecution ( );
}	// QtInternalPyConsoleWidget::onPaused


void QtInternalPyConsoleWidget::onExceptionRaised (int line, QString text)
{
	assert (0 != _pythonConsole);
	PyConsoleWidget::onExceptionRaised (line, text);
	_pythonConsole->completeConsoleCommandExecution ( );
}	// QtInternalPyConsoleWidget::onExceptionRaised


void QtInternalPyConsoleWidget::onExecutionFinished (bool stoppedEarly)
{
	assert (0 != _pythonConsole);
	PyConsoleWidget::onExecutionFinished (stoppedEarly);
	_pythonConsole->completeConsoleCommandExecution ( );
}	// QtInternalPyConsoleWidget::onExecutionFinished


void QtInternalPyConsoleWidget::execInstruction (const string& instruction, const string& comment)
{
	assert (0 != _pythonConsole);
	_pythonConsole->initConsoleCommandExecution ( );
	PyConsoleWidget::execInstruction (instruction, comment);
	_pythonConsole->completeConsoleCommandExecution ( );
}	// QtInternalPyConsoleWidget::execInstruction


// ============================================================================
//                            LA CLASSE QtPythonConsole
// ============================================================================

QSize	QtPythonConsole::iconSize (32, 32);
QIcon*	QtPythonConsole::breakPointIcon	= 0;


static bool isUtf16 (const string& path)
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
		if ((NULL != strcasestr (str.c_str ( ), "utf16")) || (NULL != strcasestr (str.c_str ( ), "utf-16")))
			isUtf16	=true;
	}	// if (NULL != strcasestr (str.c_str ( ), "coding"))

	fclose (f);

	return isUtf16;
}	// isUtf16


Charset::CHARSET QtPythonConsole::getFileCharset (const string& path)
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
	: QWidget (parent), _appName (appName), _logDispatcher ( ), _consoleWidget (0)
#ifdef MULTITHREADED_APPLICATION
	  , _mutex (new Mutex ( ))
#endif	// MULTITHREADED_APPLICATION
	  , _insertScriptAction (0)
{
	QHBoxLayout*	mainLayout	= new QHBoxLayout ( );
	mainLayout->setContentsMargins (0, 0, 0, 0);
	_consoleWidget	= new QtInternalPyConsoleWidget (*this);
	mainLayout->addWidget (_consoleWidget);

	// Quelques icones :
	breakPointIcon	= 0 == breakPointIcon ? new QIcon (":/images/breakpoint.png") : breakPointIcon;
	QIcon	runningModeIcon (":/images/dbg_mode.png");
	QIcon	runIcon (":/images/run.png");
	QIcon	stopIcon (":/images/stop.png");
	QIcon	nextIcon (":/images/next.png");
	QIcon	addBreakpointIcon (":/images/add_breakpoint.png");
	QIcon	removeBreakpointIcon (":/images/remove_breakpoint.png");
	QIcon	clearBreakpointsIcon (":/images/clear_breakpoints.png");
	QIcon	loadScriptIcon (":/images/load_script.png");
	getConsoleWidget ( ).getModeAction ( ).setIcon (runningModeIcon);
	getConsoleWidget ( ).getRunAction ( ).setIcon (runIcon);
	getConsoleWidget ( ).getStopAction ( ).setIcon (stopIcon);
	getConsoleWidget ( ).getStepAction ( ).setIcon (nextIcon);
	getConsoleWidget ( ).getClearBreakpointsAction ( ).setIcon (clearBreakpointsIcon);
	_insertScriptAction	= new QAction (QIcon (":/images/load_script.png"), QSTR ("Insérer un script ..."), this);
	connect (_insertScriptAction, SIGNAL (triggered ( )), this, SLOT (insertScriptCallback ( )));
	getToolBar ( ).addAction (_insertScriptAction);
	getToolBar ( ).setToolButtonStyle (Qt::ToolButtonIconOnly);

	// Mise en évidence des mots clés :
	new QtPythonSyntaxHighlighter (_consoleWidget->getCodeEditor ( ).document ( ));

	setLayout (mainLayout);
}	// QtPythonConsole::QtPythonConsole


QtPythonConsole::QtPythonConsole (const QtPythonConsole& console)
	: QWidget (0), _appName (console._appName), _logDispatcher ( ), _consoleWidget (0)
#ifdef MULTITHREADED_APPLICATION
	  , _mutex (new Mutex ( ))
#endif	// MULTITHREADED_APPLICATION
	  , _insertScriptAction (0)
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
}	// QtPythonConsole::~QtPythonConsole


UTF8String	QtPythonConsole::getPythonCode ( ) const
{
	UTF8String	code (getConsoleWidget ( ).getCodeEditor ( ).toPlainText ( ).toUtf8 ( ), Charset::UTF_8);
	
	return code;
}	// QtPythonConsole::getPythonCode


const PyConsoleWidget& QtPythonConsole::getConsoleWidget ( ) const
{
	CHECK_NULL_PTR_ERROR (_consoleWidget)
	return *_consoleWidget;
}	// QtPythonConsole::getConsoleWidget


PyConsoleWidget& QtPythonConsole::getConsoleWidget ( )
{
	CHECK_NULL_PTR_ERROR (_consoleWidget)
	return *_consoleWidget;
}	// QtPythonConsole::getConsoleWidget
	

bool QtPythonConsole::isRunning ( ) const
{
	return getConsoleWidget ( ).isRunning ( );
}	// QtPythonConsole::isRunning


string QtPythonConsole::getAppName ( ) const
{
	return _appName;
}	// QtPythonConsole::getAppName


void QtPythonConsole::insert (const string& fileName, string& warnings /* not used*/)
{	// Version simplifiée : on ne vérifie pas ici que le texte du script est interprétable par python
	Charset::CHARSET	streamCharset	= getFileCharset (fileName);
	streamCharset						= Charset::UNKNOWN == streamCharset ? Charset::UTF_8 : streamCharset;

	// UTF-16 : les sauts de ligne ne sont pas des \n => réécrire différemment la lecture du fichier.
	if (Charset::UTF_16 == streamCharset)
		throw Exception ("Encodage UTF-16 non supporté dans cette version.");

	ifstream	stream (fileName.c_str ( ));
	if ((false == stream.good ( )) && (false == stream.eof ( )))
		throw Exception ("Fichier invalide.");
	char	buffer [10001];
	while ((true == stream.good ( )) && (false == stream.eof ( )))
	{
		memset (buffer, '\0', 10001);
		stream.getline (buffer, 10000, '\n');
		UTF8String	script (buffer, streamCharset);
		getConsoleWidget ( ).getCodeEditor ( ).insertPlainText (QString::fromUtf8 (script.utf8 ( ).c_str ( )));
		getConsoleWidget ( ).getCodeEditor ( ).insertPlainText (QString ('\n'));
	}	// while ((true == stream.good ( )) && (false == stream.eof ( )))
}	// QtPythonConsole::insert


void QtPythonConsole::addToHistoric (const UTF8String& command, const UTF8String& comments, const UTF8String& commandOutput, bool statusErr, bool fromKernel)
{
#ifdef MULTITHREADED_APPLICATION
	AutoMutex	mutex (_mutex.get ( ));
#endif	// MULTITHREADED_APPLICATION
	if (false == comments.empty ( ))
	{
		const UTF8String	commentsText (PythonLogOutputStream::toComment (comments), Charset::UTF_8);
		getConsoleWidget ( ).insertExecutedInstructions (commentsText.utf8 ( ));
	}	// if (false == comments.empty ( ))
	getConsoleWidget ( ).insertExecutedInstructions (command.utf8 ( ));
}	// QtPythonConsole::addToHistoric


void QtPythonConsole::initConsoleCommandExecution ( )
{	// A surcharger
}	// QtPythonConsole::initConsoleCommandExecution


void QtPythonConsole::completeConsoleCommandExecution ( )
{	// A surcharger
}	// QtPythonConsole::completeConsoleCommandExecution


QToolBar& QtPythonConsole::getToolBar ( )
{
	return getConsoleWidget ( ).getToolBar ( );
}	// QtPythonConsole::getToolBar


const QToolBar& QtPythonConsole::getToolBar ( ) const
{
	return getConsoleWidget ( ).getToolBar ( );
}	// QtPythonConsole::getToolBar


void QtPythonConsole::writeSettings (QSettings& settings)
{
	settings.beginGroup ("PythonConsole_v2");
	settings.setValue ("size", size ( ));
	settings.endGroup ( );
}	// QtPythonConsole::writeSettings


void QtPythonConsole::readSettings (QSettings& settings)
{
	settings.beginGroup ("PythonConsole_v2");
	resize (settings.value ("size", size ( )).toSize ( ));
	settings.endGroup ( );
}	// QtPythonConsole::readSettings


void QtPythonConsole::setUsabled (bool enable)
{
#ifdef MULTITHREADED_APPLICATION
        AutoMutex       mutex (_mutex.get ( ));
#endif  // MULTITHREADED_APPLICATION
        emit setUsabledCalled (enable);
}       // QtPythonConsole::setUsabled


#ifdef MULTITHREADED_APPLICATION
Mutex& QtPythonConsole::getMutex ( )
{
	assert (0 != _mutex.get ( ));
	return *(_mutex.get ( ));
}	// QtPythonConsole::getMutex
#endif	// MULTITHREADED_APPLICATION


void QtPythonConsole::execInstruction (const string& instruction, const string& comment)
{
#ifdef MULTITHREADED_APPLICATION
        AutoMutex       mutex (_mutex.get ( ));
#endif  // MULTITHREADED_APPLICATION
	getConsoleWidget ( ).execInstruction (instruction, comment);
}	// QtPythonConsole::execInstruction


void QtPythonConsole::insertScriptCallback ( )
{
	try
	{
		static QString	directory;
		UTF8String	message (charset);
		message << "Sélectionnez un script python.";

		QString	fileName	 = QFileDialog::getOpenFileName (this, UTF8TOQSTRING (message), directory, "scripts python (*.py)", 0, QFileDialog::DontUseNativeDialog);
		if (true == fileName.isEmpty ( ))
			return;
		directory	= TkUtil::File (fileName.toStdString ( )).getPath ( ).getFullFileName ( ).c_str ( );
		string	warnings;
		insert (fileName.toStdString ( ), warnings);
		if (0 != warnings.size ( ))
			QtMessageBox::displayWarningMessage (this, getAppName ( ).c_str ( ), warnings.c_str ( ));
	}
	catch (const Exception& exc)
	{
		UTF8String	message (charset);
		message << "Erreur lors de l'insertion d'un script :" << "\n" << exc.getFullMessage ( );
		QtMessageBox::displayErrorMessage (this, getAppName ( ), message);
	}
	catch (...)
	{
		UTF8String	message (charset);
		message << "Erreur non documentée lors de l'insertion d'un script.";
		QtMessageBox::displayErrorMessage (this, getAppName ( ), message);
	}
}	// QtPythonConsole::insertScriptCallback


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
