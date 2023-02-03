#ifndef QT_3

#include "QtPython/QtPython.h"
#include "QtPythonInterpretorMainWindow.h"
#include <TkUtil/Exception.h>
#include <TkUtil/Locale.h>
#include <TkUtil/FileLogOutputStream.h>
#include <TkUtil/AnsiEscapeCodes.h>

#include <QApplication>


USING_UTIL
USING_STD


static string	logFile, script;
static bool		needHelp	= false;

static void parseArgs (int argc, char* argv []);
static int syntax (const string& name);


static Charset detectCharset (int argc, char* argv [])
{
	for (int i = 0; i < argc - 1; i++)
	{
		if (0 == strcmp (argv [i], "-console"))
		{
			Charset::CHARSET	charset	= Charset::str2charset (argv [i + 1]);
			if (Charset::UNKNOWN != charset)
				return Charset (charset);
		}	// if (0 == strcmp (argv [i], "-console"))
	}	// for (int i = 0; i < argc - 1; i++)

	return Locale::detectCharset ("");
}	// detectCharset


int main (int argc, char* argv[])
{
	try
	{
//QtPythonConsole::enableCodingIso8859_15	= false;
		parseArgs (argc, argv);
		{
			TermAutoStyle	termAutoStyle (cout, AnsiEscapeCodes::blueFg);
			cout << argv [0] << " : console d'exécution de scripts python avec possibilité de mode pas à pas." << endl
			     << "Pour fonctionner les variables d'environnement QT_PYTHON_SCRIPTS_DIR et PYTHONPATH doivent être positionnées :" << endl
			     << "- QT_PYTHON_SCRIPTS_DIR : répertoire contenant le script python NECPdb.py" << endl
			     << "- PYTHONPATH : idem (!) + les répertoires contenant les fichiers python et modules.so des APIs utilisées." << endl; 
		}
		if (true == needHelp)
			return syntax (argv [0]);

		Charset	consoleCharset	= detectCharset (argc, argv);
		ConsoleOutput::cout ( ).setCharset (consoleCharset);
		ConsoleOutput::cerr ( ).setCharset (consoleCharset);
		ConsoleOutput::cout ( ) << "Jeu de caractères de la console de lancement : "
		     << consoleCharset.name ( ) << co_endl;
		QtPython::preInitialize ( );	// CP v 5.1.0
		QtPython::initialize (consoleCharset);

		ConsoleOutput::cout ( ) << "Version QtPython : " << QtPython::getVersion ( ).getVersion ( ) << co_endl;

		QApplication		app (argc, argv);
		QtPythonInterpretorMainWindow*	window	= new QtPythonInterpretorMainWindow (0, true);
		if (false == logFile.empty ( ))
			window->setLogStream (new FileLogOutputStream (logFile, true));
		if (false == script.empty ( ))	
			window->setScript (script);

		window->resize (800, 1200);
		window->show ( );

		return app.exec ( );
	}
	catch (const Exception& exc)
	{
		ConsoleOutput::cerr ( ) << "Erreur : " << exc.getFullMessage ( ) << co_endl;
		return -1;
	}
	catch (...)
	{
		ConsoleOutput::cerr ( ) << "Erreur non documentée." << co_endl;
		return -1;
	}

	return 0;
}	// main


static void parseArgs (int argc, char* argv [])
{
	for (int i = 1; i < argc; i++)
	{
		bool	kept	= false;

		// Analyse des options sans argument (ex : -debug)
		if (string ("-help") == argv [i])
			needHelp	= true;
		else if (string ("-swigCompletion") == argv [i])
			QtPythonConsole::enableSwigCompletion	= true;

		if (true == kept)
			continue;

		// Analyse des options avec argument :
		if (i < argc - 1)
		{
			if (string ("-script") == argv [i])
			{
				script	= argv [i + 1];
				kept	= true;
			}	// if (string ("-script") == argv [i])
			else if (string ("-logFile") == argv [i])
			{
				logFile	= argv [i + 1];
				kept	= true;
			}	// if (string ("-logFile") == argv [i])

			if (true == kept)
				i	+= 1;
		}	// if (i < argc - 1)
	}	// for (int i = 1; i < argc; i++)
}	// parseArgs


static int syntax (const string& name)
{
	cout << name << "[-help][-swigCompletion][-script fileName]"
	     << "[-logFile fileName]" << endl
	     << "-help .......................... : affiche ce message" << endl
	     << "-swigCompletion ................ : augmente l'effort de "
			<< "complétion en cas de binding SWIG" << endl
	     << "-script fileName ............... : charge le fichier fileName"
	     << endl
	     << "-logFile fileName .............. : "
			<< "écrit les sorties dans le fichier fileName" << endl
	     << endl;

	return 0;
}	// syntax


#endif	// QT_3
