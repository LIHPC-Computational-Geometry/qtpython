#ifndef QT_PYTHON_H
#define QT_PYTHON_H

#include <TkUtil/UTF8String.h>
#include <TkUtil/Version.h>
#include <string>


/**
 * Services de ce module <I>QtPython</I>.
 * Classe non instanciable.
 */
class QtPython
{
	public :

	/**
	 * Pré-initialisation du module, <B>à appeler avant Py_Initialize</B>. Les sorties des commandes python sont accessibles
	 * dans la variable de classe <I>outputsString</I>.
	 * @see		initialize
	 * @see		outputString;
	 */
	static void preInitialize ( );				// CP v 5.1.0
	static TkUtil::UTF8String	outputsString;	// CP v 5.1.0
	
	/**
	 * Initialisation du module. Appel requis pour le bon fonctionnement de certains services.
	 * @param	Jeu de caractères utilisé par la console de lancement du programme. Il est utilisé lors de la lecture de sorties
	 * 			python (type scripting). Il est initialisé à <I>Locale::detectCharset ("")</I> par défaut.
	 * @see		finalize
	 * @see		preInitialize
	 */
	static void initialize (const TkUtil::Charset& consoleCharset);

	/**
	 * Finalisation du module. Libère les ressources associées.
	 * @see		initialize
	 */
	static void finalize ( );

	/**
	 * @return		Le numéro de version de ce composant logiciel.
	 */
	static const TkUtil::Version& getVersion ( );

	/**
	 * @return		Le jeu de caractères utilisé par la console de lancement.
	 */
	static const TkUtil::Charset& getConsoleCharset ( );

	/**
	 * @param		Le jeu de caractères utilisé par la console de lancement.
	 */
	static void setConsoleCharset (const TkUtil::Charset& charset);

	/**
	 * Insère des <I>backslashs</I> devant les <I>guillemets</I> de la chaine tranmise en arguement, idem avec \t et \n
	 * retourne la chaîne de caractères résultante.
	 */
	static std::string slashify (const std::string& s);


	private :

	QtPython ( );
	QtPython (const QtPython&);
	QtPython& operator = (const QtPython&);
	~QtPython ( );

	/** La version de ce composant logiciel. */
	static const TkUtil::Version	_version;

	/** Le jeu de caractères utilisé par la console de lancement. */
	static TkUtil::Charset			_consoleCharset;
};	// class QtPython

#endif	// QT_PYTHON_H
