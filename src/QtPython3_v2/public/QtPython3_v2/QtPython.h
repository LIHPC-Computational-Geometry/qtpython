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
	 * Initialisation du module.
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
	 * @return		Le numéro de version de ce composant logiciel suffixé de la version de la console.
	 */
	static TkUtil::UTF8String getNamedVersion ( );


	private :

	QtPython ( );
	QtPython (const QtPython&);
	QtPython& operator = (const QtPython&);
	~QtPython ( );

	/** La version de ce composant logiciel. */
	static const TkUtil::Version	_version;
};	// class QtPython

#endif	// QT_PYTHON_H
