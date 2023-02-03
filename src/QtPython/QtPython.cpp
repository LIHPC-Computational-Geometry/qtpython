#include "QtPython/QtPython.h"
#include <TkUtil/Locale.h>

#include <qglobal.h>

#include <assert.h>


using namespace TkUtil;
using namespace std;


const Version	QtPython::_version (QT_PYTHON_VERSION);

Charset	QtPython::_consoleCharset (Locale::detectCharset (""));	// v 3.3.0


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


void QtPython::preInitialize ( )	// CP V 5.1.0
{
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






