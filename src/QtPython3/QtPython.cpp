#include "QtPython3/QtPython.h"
#include <TkUtil/Exception.h>
#include <TkUtil/Locale.h>

#include <qglobal.h>

#include <Python.h>
#include <pystate.h>	// PyTrace_LINE ...
#include <frameobject.h>// PyFrameObject

#include <assert.h>


using namespace TkUtil;
using namespace std;


const Version	QtPython::_version (QT_PYTHON_VERSION);

Charset	QtPython::_consoleCharset (Locale::detectCharset (""));	// v 3.3.0


UTF8String	QtPython::outputsString (Charset::UTF_8);

typedef struct { PyObject_HEAD } redirector_RedirectorObject;
static PyObject* Redirector_check (redirector_RedirectorObject* r);	// forward declaration
static PyObject* Redirector_write (PyObject*, PyObject* args);		// forward declaration
static PyObject* Redirector_flush (redirector_RedirectorObject* r);	// forward declaration
static PyMethodDef redirector_RedirectorMethods [] =
{
	{"check", (PyCFunction)Redirector_check, METH_NOARGS,"print a message"},
	{"write", (PyCFunction)Redirector_write, METH_VARARGS,	"implement the write method to redirect stdout/err"},
	{"flush", (PyCFunction)Redirector_flush, METH_NOARGS,"flush the buffer"},
	NULL	// Sentinel
};	// redirector_RedirectorMethods

static PyObject* Redirector_check (redirector_RedirectorObject* r)
{
	return PyUnicode_FromString ("My name is toto");
}	// Redirector_check

static PyObject* Redirector_write (PyObject* th, PyObject* args)
{
	char* 		output	= 0;
	PyObject*	selfi	= 0;
//cout << "================== TH = " << PyUnicode_AsUTF8 (PyObject_Str(th)) << " ARG = " << PyUnicode_AsUTF8 (PyObject_Str(args)) << endl;
	if (!PyArg_ParseTuple (args, "s", &output))
	{
		return NULL;
	}

	try
	{
		UTF8String	lines (QtPython::getConsoleCharset ( ));
		lines << output;
		QtPython::outputsString << lines;
	}
	catch (...)
	{	// Remarque : si QtPython::getConsoleCharset ( ) vaut ASCII ou UNKNOWN
		// ça risque de mal se passer => on assure ici un service minimum :
		UTF8String	lines (Charset::UTF_8);
		lines << output;
		QtPython::outputsString << lines;
	}

	Py_INCREF(Py_None);
	return Py_None;
}	// Redirector_write

static PyObject* Redirector_flush (redirector_RedirectorObject* r)
{
	Py_INCREF(Py_None);
	return Py_None;
}	// Redirector_check

static PyTypeObject redirector_RedirectorType =
{
	PyVarObject_HEAD_INIT(NULL, 0)
	"redirector.Redirector",					// tp_name
	sizeof (redirector_RedirectorObject),		// tp_basicsize
	0,											// tp_itemsize
	0,											// tp_dealloc
	0,											// tp_print
	0,											// tp_getattr
	0,											// tp_setattr
	0,											// tp_reserved
	0,											// tp_repr
	0,											// tp_as_number
	nullptr,											// tp_as_sequence
	0,											// tp_as_mapping
	0,											// tp_hash
	0,											// tp_call
	0,											// tp_str
	0,											// tp_getattro
	0,											// tp_setattro
	0,											// tp_as_buffer
	Py_TPFLAGS_DEFAULT,							// tp_flags
	"Redirector objects",						// tp_doc
	0,											// tp_traverse
	0,											// tp_clear
	0,											// tp_richcompare
	0,											// tp_weaklistoffset
	0,											// tp_iter
	0,											// tp_iternext
	redirector_RedirectorMethods,				// tp_methods
	0,											// tp_members
	0,											// tp_getset
	0,											// tp_base
	0,											// tp_dict
	0,											// tp_descr_get
	0,											// tp_descr_set
	0,											// tp_dictoffset
	0,											// tp_init
	0,											// tp_alloc
	0,											// tp_new
};	// redirector_RedirectorType

static PyMethodDef ModuleMethods[] = { {NULL,NULL,0,NULL} };


static struct PyModuleDef redirectorModule =
{
  PyModuleDef_HEAD_INIT,
	"redirector",			//
	"usage: bla bla bla",	// module documentation, may be NULL
	-1,						// size of per-interpreter state of the module, or -1 if the module keeps state in global variables.
  NULL, NULL, NULL, NULL, NULL//ModuleMethods
};	// redirectorModule


PyMODINIT_FUNC PyInit_redirector ( )
{
	redirector_RedirectorType.tp_new	= PyType_GenericNew;
	if (PyType_Ready (&redirector_RedirectorType) < 0)
		return NULL;
	PyObject*	module		= PyModule_Create (&redirectorModule);
	if (NULL == module)
		return module;
	Py_INCREF (&redirector_RedirectorType);
	PyModule_AddObject (module, "Redirector", (PyObject*)&redirector_RedirectorType);
/*	PyObject*	module		= PyModule_Create (&redirectorModule);
	PyObject*	classDict	= PyDict_New ( );
	PyObject*	className	= PyUnicode_FromString ("redirector");
	Py_DECREF(classDict);
	Py_DECREF(className);

	// add methods to class
	for (PyMethodDef* rm = redirectorMethods; rm->ml_name != NULL; rm++)
	{
		PyObject*	func	= PyCFunction_New (rm, NULL);
		PyObject*	method	= PyInstanceMethod_New (func);
		PyDict_SetItemString (classDict, rm->ml_name, method);
		Py_DECREF(func);
		Py_DECREF(method);
	}	// for (PyMethodDef* rm = redirectorMethods; rm->ml_name != NULL; rm++)
	PyObject*	fooClass	= PyObject_CallFunctionObjArgs ((PyObject*)&PyType_Type, className, PyTuple_New (0), classDict, 0);
	PyModule_AddObject (module, "redirector", fooClass);*/

	return module;
}	// PyInit_redirector

// ===============================================================================================================================
//                                                           LA CLASSE QtPython
// ===============================================================================================================================


QtPython::QtPython ( )
{
	assert (0 && "QtPython is not instanciable.");
}	// QtPython::QtPython


QtPython::QtPython (const QtPython&)
{
	assert (0 && "QtPython is not instanciable.");
}	// QtPython::QtPython


QtPython& QtPython::operator = (const QtPython&)
{
	assert (0 && "QtPython assignment operator is not allowed.");
	return *this;
}	// QtPython::operator =


QtPython::~QtPython ( )
{
}	// QtPython::~QtPython


void QtPython::preInitialize ( )	// CP v 5.1.0
{
	// A faire avant Py_Initialize :
	if (-1 == PyImport_AppendInittab ("redirector", PyInit_redirector))
		throw Exception ("Echec de l'importation du module redirector par la console Python.");
}	// QtPython::preInitialize


void QtPython::initialize (const Charset& charset)
{
	_consoleCharset	= charset;
	Q_INIT_RESOURCE (QtPython);
}	// QtPython::initialize


void QtPython::finalize ( )
{
}	// QtPython::finalize


const Version& QtPython::getVersion ( )
{
	return _version;
}	// QtPython::getVersion


const Charset& QtPython::getConsoleCharset ( )	// v 3.3.0
{
	return _consoleCharset;
}	// QtPython::getConsoleCharset


void QtPython::setConsoleCharset (const Charset& charset)	// v 3.3.0
{
	_consoleCharset	= charset;
}	// QtPython::setConsoleCharset


string QtPython::slashify (const string& s)
{
	string	str (s);

	string::size_type	pos;

	pos = str.find ('"');
	while (string::npos != pos)
	{
		str.replace (pos, 1, "\\\"");
		pos	= str.find ('"', pos + 2);
	}	// while (string::npos != pos)

    pos = str.find ('\t');
    while (string::npos != pos)
    {
        str.replace (pos, 1, "\\t");
        pos = str.find ('\t', pos + 2);
    }   // while (string::npos != pos)

    pos = str.find ('\n');
    while (string::npos != pos)
    {
        str.replace (pos, 1, "\\n");
        pos = str.find ('\n', pos + 2);
    }   // while (string::npos != pos)

	return str;
}	// QtPython::slashify






