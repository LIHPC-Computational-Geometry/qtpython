#ifndef QT_PYTHON_INTERPRETOR_MAIN_WINDOW_H
#define QT_PYTHON_INTERPRETOR_MAIN_WINDOW_H

#include <Python.h>	// En 1er car contient (dans object.h) le mot clé "slots" en Python 3 et ça interfère avec Qt ...

#include <QtUtil/QtLogsView.h>

#include <QDialog>
#include <QMainWindow>
#include "QtPython/QtPythonConsole.h"


class QtPythonInterpretorMainWindow : public QMainWindow
{
	Q_OBJECT

	public :

	QtPythonInterpretorMainWindow (QWidget* parent, bool logOutputs);
	void setLogStream (TkUtil::LogOutputStream* stream);
	void setScript (const std::string& filename);


	protected slots :

	void exitCallback ( );



	private :

	QtPythonConsole*		_pythonConsole;
	QtLogsView*				_logsView;
};	// class QtPythonInterpretorMainWindow


#endif	// QT_PYTHON_INTERPRETOR_MAIN_WINDOW_H
