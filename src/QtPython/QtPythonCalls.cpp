#include "QtPython/QtPythonCalls.h"

#ifndef QT_5
#include <QtGui/QApplication>
#else	// QT_5
#include <QApplication>
#endif	// QT_5

#include <iostream>
#include <set>

using namespace std;


static PyObject* QtPythonCalls_processEvents (PyObject*, PyObject*);
static PyObject* QtPythonCalls_lineIsBeingProcessed (PyObject*, PyObject*);
static PyObject* QtPythonCalls_lineProcessed (PyObject*, PyObject*);
static PyObject* QtPythonCalls_atBreakPoint (PyObject*, PyObject*);
static set<QtPythonConsole*>	consoles;


static PyMethodDef QtPythonCallsMethods [ ] =
{
    {"processEvents",  QtPythonCalls_processEvents, METH_VARARGS,
     "Allows main program to process pending events."},
	 {"lineIsBeingProcessed", QtPythonCalls_lineIsBeingProcessed, METH_VARARGS,
	 "Notify main program that a new line will be interpreted."},
	 {"lineProcessed", QtPythonCalls_lineProcessed, METH_VARARGS,
	 "Notify main program that a new line has been interpreted."},
	 {"atBreakPoint", QtPythonCalls_atBreakPoint, METH_VARARGS,
	 "Notify main program that a breakpoint has been reached."},
    {NULL, NULL, 0, NULL}	// Sentinel
};	// QtPythonCallsMethods


PyMODINIT_FUNC initQtPythonCalls ( )
{
	PyObject*	module	= Py_InitModule ("QtPythonCalls", QtPythonCallsMethods);

	if (NULL == module)
	{
cerr << __FILE__ << ' ' << __LINE__ << " initQtPythonCalls has failed." << endl;
		return;
	}
}	// initQtPythonCalls


static PyObject* QtPythonCalls_processEvents (PyObject* self, PyObject* args)
{
	static QApplication*	application	=
						dynamic_cast<QApplication*>(QApplication::instance ( ));
	const char*	fileName	= 0;
	long		line		= -1;
	int 		ok			= PyArg_ParseTuple (args, "sl", &fileName, &line);

	// Python 2.7.x et plus, il est très fréquent que line ne soit pas transmis
	// => PyArg_ParseTuple est en erreur.
	// => Restaurer l'état de l'interpréteur Python car sinon le bon
	// déroulement des évènements suivants est fortement compromis.
	if (0 == ok)
	{
		PyErr_Clear ( );
		// On passe la main à l'IHM. Ne pas faire de traitement qui dépende du
		// numéro de ligne courant. => pas de Py_RETURN_NONE.
//		Py_RETURN_NONE;
	}

	if (0 != application)
	{
		if (true == application->hasPendingEvents ( ))
		{
			application->processEvents ( );
		}
	}	// if (0 != application)

	Py_RETURN_NONE;
}	// QtPythonCalls_processEvents


void registerConsole (QtPythonConsole& console)
{
	consoles.insert (&console);
}	// registerConsole


void unregisterConsole (QtPythonConsole& console)
{
	consoles.erase (&console);
}	// unregisterConsole


static PyObject* QtPythonCalls_lineIsBeingProcessed (
											PyObject* self, PyObject* args)
{
	const char*	fileName	= 0;
	long		line		= -1;
	int 		ok			= PyArg_ParseTuple (args, "sl", &fileName, &line);

	if (0 == ok)
		PyErr_Clear ( );

	if (0 == fileName)
		Py_RETURN_NONE;

	try
	{
		for (set<QtPythonConsole*>::const_iterator it = consoles.begin ( );
		     consoles.end ( ) != it; it++)
		{
			if ((*it)->getPythonScript ( ) == fileName)
				(*it)->lineIsBeingProcessedCallback (fileName, line);
		}	// for (set<QtPythonConsole*>::const_iterator it = ...
	}
	catch (...)
	{
	}

	Py_RETURN_NONE;
}	// QtPythonCalls_lineIsBeingProcessed


static PyObject* QtPythonCalls_lineProcessed (PyObject* self, PyObject* args)
{
	const char*	fileName	= 0;
	long		line		= -1;
	int 		ok			= PyArg_ParseTuple (args, "sl", &fileName, &line);

	if (0 == ok)
		PyErr_Clear ( );

	if (0 == fileName)
		Py_RETURN_NONE;

	try
	{
		for (set<QtPythonConsole*>::const_iterator it = consoles.begin ( );
		     consoles.end ( ) != it; it++)
		{
			if ((*it)->getPythonScript ( ) == fileName)
				(*it)->lineProcessedCallback (fileName, line, true);
		}	// for (set<QtPythonConsole*>::const_iterator it = ...
	}
	catch (...)
	{
	}

	Py_RETURN_NONE;
}	// QtPythonCalls_lineProcessed


static PyObject* QtPythonCalls_atBreakPoint (PyObject* self, PyObject* args)
{
	const char*	fileName	= 0;
	long		line		= -1;
	int		 	ok			= PyArg_ParseTuple (args, "sl", &fileName, &line);

	if (0 == ok)
	{
		PyErr_Clear ( );
	}	// if (0 == ok)

	if (0 == fileName)
		Py_RETURN_NONE;

	try
	{
		for (set<QtPythonConsole*>::const_iterator it = consoles.begin ( );
		     consoles.end ( ) != it; it++)
		{
			if ((*it)->getPythonScript ( ) == fileName)
				(*it)->atBreakPointCallback (fileName, line);
		}	// for (set<QtPythonConsole*>::const_iterator it = ...
	}
	catch (...)
	{
	}

	Py_RETURN_NONE;
}	// QtPythonCalls_atBreakPoint


