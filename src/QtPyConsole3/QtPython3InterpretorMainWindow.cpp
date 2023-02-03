#include "QtPython3InterpretorMainWindow.h"

#include <TkUtil/Exception.h>
#include <QtUtil/QtMessageBox.h>

#include <QApplication>
#include <QDockWidget>
#include <QMenu>
#include <QMenuBar>


using namespace TkUtil;
using namespace std;


// ===========================================================================
//                  LA CLASSE QtPython3InterpretorMainWindow
// ===========================================================================

QtPython3InterpretorMainWindow::QtPython3InterpretorMainWindow (
		QWidget* parent, bool logOutputs)
	: QMainWindow (parent),
	  _pythonConsole (0), _logsView (0)
{
	_pythonConsole	= new QtPythonConsole (this, "Console Python");
	setCentralWidget (_pythonConsole);

	if (true == logOutputs)
	{
		QDockWidget*	dock	=
					new QDockWidget ("Sorties console python", this);
		dock->setSizePolicy (QSizePolicy::Preferred, QSizePolicy::Preferred);
		_logsView	= new QtLogsView (this, Log::PROCESS);
		_logsView->enableDate (false, false);
		_logsView->enableThreadID (false);
		dock->setWidget (_logsView);
		addDockWidget (Qt::BottomDockWidgetArea, dock);
		_pythonConsole->getLogDispatcher ( ).addStream (_logsView);
	}	// if (true == logOutputs)

	QMenu*	menu	= new QMenu ("Application", this);
	menuBar ( )->addMenu (menu);
	menu->addAction (&_pythonConsole->runningModeAction ( ));
	menu->addSeparator ( );
	menu->addAction (&_pythonConsole->runAction ( ));
//	menu->addAction (&_pythonConsole->stopAction ( ));
	menu->addAction (&_pythonConsole->nextAction ( ));
	menu->addSeparator ( );
	menu->addAction (&_pythonConsole->addBreakPointAction ( ));
	menu->addAction (&_pythonConsole->removeBreakPointAction ( ));
	menu->addSeparator ( );
	menu->addAction ("Quitter", this, SLOT (exitCallback ( )));

	addToolBar (Qt::TopToolBarArea, &_pythonConsole->getToolBar ( ));
}	// QtPython3InterpretorMainWindow::QtPython3InterpretorMainWindow


void QtPython3InterpretorMainWindow::setLogStream (LogOutputStream* stream)
{
	if (0 != _pythonConsole)
		_pythonConsole->setLogStream (stream);
}	// QtPython3InterpretorMainWindow::setLogStream


void QtPython3InterpretorMainWindow::setScript (const std::string& filename)
{
	if (0 != _pythonConsole)
	{
		string	warnings;
		_pythonConsole->insert (filename, warnings);

		if (0 != warnings.size ( ))
			QtMessageBox::displayWarningMessage (
				this, _pythonConsole->getAppName ( ).c_str ( ),
				warnings.c_str ( ));
	}	// QtPython3InterpretorMainWindow::setScript
}	// QtPython3InterpretorMainWindow::setScript


void QtPython3InterpretorMainWindow::exitCallback ( )
{
	switch (QtMessageBox::displayQuestionMessage (
			this, "QUITTER",
			"Souhaitez-vous réellement quitter l'application ?", 100,
			"OK", "Annuler", 0, 0))
	{
		case	0	:
			_pythonConsole->quitDbg ( );
			QApplication::exit (0);
			break;
	}
}	// QtPython3InterpretorMainWindow::exitCallback

