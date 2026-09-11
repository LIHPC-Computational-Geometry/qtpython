#include "QtPython3_v2/QtPython.h"
#include <TkUtil/Exception.h>
#include <TkUtil/Locale.h>

#include <qglobal.h>

#include <Python.h>

#include <assert.h>


using namespace TkUtil;
using namespace std;


const Version	QtPython::_version (QT_PYTHON_VERSION);

static Charset	charset (Locale::detectCharset (""));


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


void QtPython::initialize (const Charset& charset)
{
	Q_INIT_RESOURCE (QtPython);
}	// QtPython::initialize


void QtPython::finalize ( )
{
}	// QtPython::finalize


const Version& QtPython::getVersion ( )
{
	return _version;
}	// QtPython::getVersion


UTF8String QtPython::getNamedVersion ( )
{
	return UTF8String (_version.getVersion ( ) + " (console v2)", charset);
}	// QtPython::getNamedVersion


