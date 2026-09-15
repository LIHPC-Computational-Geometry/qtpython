/**
 * @file		QtPythonConsole.h
 * @author		Charles PIGNEROL, CEA/DAM/DSSI
 * @date		04/09/2026
 */
#ifndef QT_PYTHON_CONSOLE_H
#define QT_PYTHON_CONSOLE_H

struct _object;	// object.h de python contient le mot clé "slots" en Python 3 et ça interfère avec Qt ...
typedef struct _object PyObject;
//#include <Python.h>	// En 1er car contient (dans object.h) le mot clé "slots" en Python 3 et ça interfère avec Qt ...

#include <TkUtil/util_config.h>
#include <TkUtil/LogDispatcher.h>
#include <TkUtil/Mutex.h>
#include <TkUtil/TemporaryFile.h>
#include <QtUtil/QtTextEditor.h>

#include <QSettings>
#include <QIcon>
#include <QComboBox>
#include <QMainWindow>
#include <QPlainTextEdit>
#include <QToolBar>

#include <fstream>
#include <memory>
#include <string>
#include <set>
#include <vector>


class PyConsoleWidget;

/**
 * <P>
 * Classe de <I>console Qt</I> permettant d'exécuter séquentiellement du code <I>python 3</I>.
 * </P>
 *
 * <P>
 * Le code python est :
 * <UL>
 * <LI>ou saisi manuellement, façon éditeur de texte, 
 * <LI>ou importé depuis un fichier,
 * <LI>ou issu de l'historique des commandes jouées, et éventuellement modifié (utilisation via MAJ + touche haut ou bas)
 * </UL>
 * </P>
 *
 * <P>
 * L'exécution du code est effectuée séquentiellement (pas de retour en arrière dans le code), au fur et à mesure ou en mode <I>debug</I>, où les
 * instructions sont alors exécutées à la demande, en mode <I>continu</I> ou <I>pas à pas</I>, avec possibilité de positionner des <I>points d'arrêt</I>.
 * </P>
 *
 * <P>Cette classe est en mesure d'afficher les sorties standard et erreur des commandes exécutées via une instance de la classe <I>LogOutputStream</I>.
 * Elle peut également insérer des commandes scriptables dans la console transmises via la méthode <I>log</I>.
 * </P>
 *
 * <P>Cette classe propose une complétion d'instruction reposant sur <I>readline</I>. Cependant cette complétion est limitée (arguments non
 * renseignés) lorsqu'il s'agit d'un <I>binding Swig</I> car il repose sur une transmission d'arguments de type <I>varargs</I>. Cette classe propose
 * néanmoins de faire un effort de recherche de signature, activable via <I>enableSwigCompletion</I>. La complétion est appelable via la
 * combinaison de touches <I>CTRL + Tab</I>. Elle requiert d'être un peu aidée (début de nom de fonction déjà saisi).<BR>
 * Depuis la version 2.6.0 de cette bibliothèque, la complétion ne prend pas en compte les méthodes dont la signature comporte
 * <I>UnusedStructForSwigCompletion</I>. Ce dispositif permet de renseigner de manière exacte la signature d'une méthode n'ayant qu'une seule signature. En
 * effet, dans un tel cas, il n'est pas possible avec SWIG d'obtenir de type des arguments, alors qu'on y arrive si plusieurs choix sont possibles. Une
 * idée est alors par exemple de créer une macro créant une fonction de signature bidon pour les cas où seule une signature est possible. Ex :<BR>
 * <PRE>
 * struct UnusedStructForSwigCompletion { };
 * #define SET_SWIG_COMPLETABLE_METHOD(method) \
 *     void method(UnusedStructForSwigCompletion){std::cerr<<"#method (UnusedStructForSwigCompletion) should not be called."<<std::endl; }
 * ...
 * void foo (int, double);	// Méthode à unique signature.
 * SET_SWIG_COMPLETABLE_METHOD(foo)	// On la double pour le binding swig et avoir la complétion exacte. Cette surcharge ne sera pas proposée lors de la completion.
 * </PRE>
 * </P>
 *
 * <P>La classe <I>QtDecoratedPythonConsole</I> offre cet éditeur accolé à sa barre d'icône. L'opérateur <I>-></I> permet d'invoquer directement les
 * méthodes de l'instance de la classe <I>QtPythonConsole</I> de l'ensemble.
 * </P>
 *
 * @warning	<B>Ne pas utiliser la méthode setEnabled, mais lui préférer la méthode <I>setUsabled</I> qui est <I>thread safe</I>.</B>
 * @since	7.0.0
 */
class QtPythonConsole : public QWidget
{
	Q_OBJECT

	public :

	/**
	 * @return		Retourne - si possible - le jeu de caractères d'encodage du fichier dont le chemin d'accès est transmis en argument.
	 */
	static TkUtil::Charset::CHARSET getFileCharset (const std::string& path);

	/**
	 * Constructeur. RAS.
	 * @param		widget parent
	 * @param		Nom de l'application (pour les différents messages).
	 */
	QtPythonConsole (QWidget* parent, const std::string& appName);

	/**
	 * Destructeur. RAS.
	 */
	virtual ~QtPythonConsole ( );

	/**
	 * @return		true si la console est en cours d'exécution d'instructions.
	 */
	virtual bool isRunning ( ) const;

	/**
	 * @return	Le contenu de l'éditeur.
	 */
	virtual TkUtil::UTF8String	getPythonCode ( ) const;

	/**
	 * @return		Le nom de l'application.
	 */
	virtual std::string getAppName ( ) const;

	/**
	 * @return		
	/**
	 * Insère le fichier transmis en argument à la position courante du curseur.
	 * @param	Chemin d'accès complet du fichier à insérer.
	 * @param	En retour, éventuels avertissements sur le contenu inséré.
	 * @warning	L'insertion se fait sur une nouvelle ligne
	 */
	virtual void insert (const std::string& fileName, std::string& warnings);


	/**
	 * Ajoute la commande transmise en argument, si il (le panneau) n'en est pas à l'origine, à l'historique des commandes exécutées, mais ne l'exécute
	 * pas. Présente l'intérêt d'intercaller des commandes effectuées par ailleurs.
	 * @param		Commande à ajouter
	 * @param		Commentaires associés à la commande.
	 * @param		Sortie de la commande à ajouter.
	 * @param       Status en erreur ou non de la commande
	 * @param		<I>true</I> si la commande vient du noyau, <I>false</I> si elle vient d'ailleurs (par exemple de la console python).
	 * @see			setPythonOutputStream
	 */
	virtual void addToHistoric (const IN_UTIL UTF8String& command, const IN_UTIL UTF8String& comments, const IN_UTIL UTF8String& commandOutput, bool statusErr, bool fromKernel = false);

	/**
	 * A surcharger, ne fait rien par défaut. Cette méthode a pour objectif de se préparer à enregistrer des modifications qui ont lieu
	 * durant l'exécution de commandes python.
	 * @see			completeConsoleCommandExecution
	 */
	virtual void initConsoleCommandExecution ( );

	/**
	 * A surcharger, actualise l'IHM suite à l'exécution de commandes python.
	 * @see			initConsoleCommandExecution
	 */
	virtual void completeConsoleCommandExecution ( );

	/**
	 * Divers IHM.
	 */
	//@{

	virtual QAction& getInsertScriptAction ( ) { return *_insertScriptAction; }
	virtual const QAction& getInsertScriptAction ( ) const { return *_insertScriptAction; }
	virtual QToolBar& getToolBar ( );
	virtual const QToolBar& getToolBar ( ) const;

	/**
	 * Enregistre les paramètres d'affichage (taille, position, ...) de cette fenêtre.
	 * @see		readSettings
	 */
	virtual void writeSettings (QSettings& settings);

	/**
	 * Lit et s'applique les paramètres d'affichage (taille, position, ...) de cette fenêtre.
	 * @see		writeSettings
	 */
	virtual void readSettings (QSettings& settings);
	
	//@}	// Divers IHM.

	/**
	 * Méthode <I>thread safe</I> contrairement à <I>setEnabled</I>. Emet le signal <I>setUsabledCalled (bool)</I>.
	 * @param	Si <I>true</I> rend fonctionnel le panneau, l'inactive dans le cas contraire.
	 * @see		setUsabledCalled
	 * @see		setEnabled
	 */
	virtual void setUsabled (bool enable);


	protected :

#ifdef MULTITHREADED_APPLICATION
	/**
	 * @return	Un mutex, pour fonctionnement en environnement multithread.
	 */
	virtual TkUtil::Mutex& getMutex ( );
#endif	// MULTITHREADED_APPLICATION

	/**
	 * @return		L'implémentation de la console python.
	 */
	virtual const PyConsoleWidget& getConsoleWidget ( ) const;
	virtual PyConsoleWidget& getConsoleWidget ( );

	/**
	 * Ajote l'instruction transmise en argument à la cnosole et l'exécute.
	 * @param	instruction à exécuter
	 * @param	commentaire associé.
	 */
	virtual void execInstruction (const std::string& instruction, const std::string& comment);


	protected slots :

	/**
	 * Appelé lorsque l'utilisateur active l'action "charger un script".
	 * Affiche un sélecteur de fichier de chargement d'un script et insère le contenu du script sélectionné à l'emplacement du curseur.
	 * @warning		L'insertion se fait sur une nouvelle ligne
	 */
	virtual void insertScriptCallback ( );


	signals :

	/**
	 * Signal émis en environnement multithread lorsque la méthode <I>setUsabled/I> est invoquée depuis un thread autre que le thread de l'instance.
	 * @warning		Requiert une compilation avec la directive <I>-DMULTITHREADED_APPLICATION</I>.
	 */
	void setUsabledCalled (bool usable);


	private :

	/**
	 * Constructeur de copie, opérateur = : interdits.
	 */
	QtPythonConsole (const QtPythonConsole&);
	QtPythonConsole& operator = (const QtPythonConsole&);

	/** Le nom de l'application. */
	std::string								_appName;

	/** Flux sortant des commandes python exécutées. */
	TkUtil::LogDispatcher					_logDispatcher;
	
	/** L'implémentation de la console python. */
	PyConsoleWidget*						_consoleWidget;

#ifdef MULTITHREADED_APPLICATION
	/** La protection des opération en environnement multithread. */
	std::unique_ptr<TkUtil::Mutex>			_mutex;
#endif	// MULTITHREADED_APPLICATION

	QAction									*_insertScriptAction;

	static	QSize							iconSize;
	static	QIcon*							breakPointIcon;
};	// class QtPythonConsole


/**
 * <P>
 * Classe de widget comprenant une instance de la classe <I>QtPythonConsole</I> accolée à sa barre d'icônes. L'<I>opérateur -></I> permet d'invoquer
 * directement les méthodes de l'instance associée de la classe <I>QtPythonConsole</I>.
 * </P>
 */
class QtDecoratedPythonConsole : public QMainWindow
{
	public :

	/**
	 * Constructeur 1. Instancie la console python.
	 * @param	widget parent
	 * @param	Nom de l'application (pour les différents messages).
	 * @param	Emplacement initial de la barre d'icônes.
	 */
	QtDecoratedPythonConsole (QWidget* parent, const std::string& appName, Qt::ToolBarArea area);


	/**
	 * Constructeur 2.
	 * @param	widget parent
	 * @param	Console python à utiliser.
	 * @param	Emplacement initial de la barre d'icônes.
	 */
	QtDecoratedPythonConsole (QWidget* parent, QtPythonConsole& console, Qt::ToolBarArea area);

	/**
	 * Destructeur. RAS.
	 */
	virtual ~QtDecoratedPythonConsole ( );

	/**
	 * @return	Une référence sur la console python associée.
	 */
	virtual const QtPythonConsole& getPythonConsole ( ) const;
	virtual QtPythonConsole& getPythonConsole ( );

	/**
	 * Invoquer directement des méthodes de l'instance de la console python associée.
	 */
	virtual const QtPythonConsole* operator -> ( ) const;
	virtual QtPythonConsole* operator -> ( );


	protected :

	/**
	 * Création de l'IHM à partir de la console python transmise en argument.
	 * @param	Console à associer
	 * @param	Emplacement de la barre d'icônes
	 */
	virtual void createGui (QtPythonConsole& console, Qt::ToolBarArea area);


	private :

	/**
	 * Constructeur de copie, opérateur = : interdits.
	 */
	QtDecoratedPythonConsole (const QtDecoratedPythonConsole&);
	QtDecoratedPythonConsole& operator = (const QtDecoratedPythonConsole&);

	/** La console python associée. */
	QtPythonConsole*	_pythonConsole;
};	// class QtDecoratedPythonConsole

#endif	// QT_PYTHON_CONSOLE_H
