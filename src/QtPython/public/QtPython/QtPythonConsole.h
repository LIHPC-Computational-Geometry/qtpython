/**
 * @file		QtPythonConsole.h
 * @author		Charles PIGNEROL, CEA/DAM/DSSI, sur une bonne idée
 * 				de Jean-Denis LESAGE, CEA/DAM/DSSI
 * @date		24/09/2015
 */
#ifndef QT_PYTHON_CONSOLE_H
#define QT_PYTHON_CONSOLE_H

#include <TkUtil/util_config.h>
#include <TkUtil/LogDispatcher.h>
#include <TkUtil/ScriptingLog.h>
#include <TkUtil/Mutex.h>
#include <TkUtil/TemporaryFile.h>
#include <QtUtil/QtTextEditor.h>

#include <QSettings>
#include <QIcon>
#ifndef QT_5
#include <QtGui/QComboBox>
#include <QtGui/QMainWindow>
#include <QtGui/QToolBar>
#else	// QT_5
#include <QComboBox>
#include <QMainWindow>
#include <QToolBar>
#endif	// QT_5

#include <Python.h>

#include <fstream>
#include <memory>
#include <string>
#include <set>
#include <vector>


/**
 * <P>
 * Classe de <I>console Qt</I> permettant d'exécuter séquentiellement du code <I>python</I>.
 * </P>
 *
 * <P>
 * Le code python est :
 * <UL>
 * <LI>ou saisi manuellement, façon éditeur de texte, 
 * <LI>ou importé depuis un fichier,
 * <LI>ou issu de l'historique des commandes jouées, et éventuellement modifé
 * (utilisation via MAJ + touche haut ou bas)
 * </UL>
 * </P>
 *
 * <P>
 * L'exécution du code est effectuée séquentiellement (pas de retour en arrière
 * dans le code), au fur et à mesure ou en mode <I>debug</I>, où les
 * instructions sont alors exécutées à la demande, en mode <I>continu</I> ou 
 * <I>pas à pas</I>, avec possibilité de positionner des <I>points d'arrêt</I>.
 * </P>
 *
 * <P>Cette classe est en mesure d'afficher les sorties standard et erreur des
 * commandes exécutées via une instance de la classe <I>LogOutputStream</I>.
 * Elle peut également insérer des commandes scriptables dans la console
 * transmises via la méthode <I>log</I>.
 * </P>
 *
 * <P>Cette classe propose une complétion d'instruction reposant sur
 * <I>readline</I>. Cependant cette complétion est limitée (arguments non
 * renseignés) lorsqu'il s'agit d'un <I>binding Swig</I> car il repose sur une
 * transmission d'arguments de type <I>varargs</I>. Cette classe propose
 * néanmoins de faire un effort de recherche de signature, activable via
 * <I>enableSwigCompletion</I>. La complétion est appelable via la
 * combinaison de touches <I>CTRL + Tab</I>. Elle requiert d'être un peu
 * aidée (début de nom de fonction déjà saisi).<BR>
 * Depuis la version 2.6.0 de cette bibliothèque, la complétion ne prend pas
 * en compte les méthodes dont la signature comporte
 * <I>UnusedStructForSwigCompletion</I>. Ce dispositif permet de renseigner de
 * manière exacte la signature d'une méthode n'ayant qu'une seule signature. En
 * effet, dans un tel cas, il n'est pas possible avec SWIG d'obtenir de type
 * des arguments, alors qu'on y arrive si plusieurs choix sont possibles. Une
 * idée est alors par exemple de créer une macro créant une fonction de
 * signature bidon pour les cas où seule une signature est possible. Ex :<BR>
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
 * <P>La classe <I>QtDecoratedPythonConsole</I> offre cet éditeur accolé à sa
 * barre d'icône. L'opérateur <I>-></I> permet d'invoquer directement les
 * méthodes de l'instance de la classe <I>QtPythonConsole</I> de l'ensemble.
 * </P>
 *
 * @warning	<B>Ne pas utiliser la méthode setEnabled, mais lui préférer la
 * 			méthode <I>setUsabled</I> qui est <I>thread safe</I>.</B>
 */
class QtPythonConsole : public QtTextEditor
{
	Q_OBJECT

	public :

	/**
	 * Le mode de fonctionnement de la console : <I>Continu</I>, valeur par
	 * défaut, ou <I>debug</I>.
	 */
	enum RUNNING_MODE { RM_CONTINUOUS, RM_DEBUG };

	/**
	 * La console doit elle supporter les scripts avec le jeu de caractères
	 * iso-8859-15 (<I>true</I> par défaut) ?
	 */
	static bool				enableCodingIso8859_15;

	/**
	 * La console doit elle faire des efforts de completions en cas d'appels via
	 * un binding <I>Swig</I> (<I>false</I> par défaut) ? 
	 */
	static bool				enableSwigCompletion;

	/**
	 * La console doit elle s'arrêter lorsque en mode debug une erreur est
	 * rencontrée ?
	 */
	static bool				stopOnError;

	/**
	 * @return		Retourne - si possible - le jeu de caractères d'encodage
	 * 				du fichier dont le chemin d'accès est transmis en argument.
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
	 * @return		La largeur souhaitée pour l'affichage des numéros de ligne
	 * 				et breakpoints.
	 */
	virtual int lineNumberAreaWidth ( ) const;

	/**
	 * Dessine les numéros de ligne et icônes dans le rectangle transmis en
	 * argument.
	 */
	virtual void drawLinesNumbers (const QRect& rect);

	/**
	 * @return		Le nom de l'application.
	 */
	virtual std::string getAppName ( ) const;

	/**
	 * @return		Le mode de fonctionnement courant.
	 * @see			setRunningMode
	 */
	virtual RUNNING_MODE getRunningMode ( ) const;

	/**
	 * @param		Le nouveau mode de fonctionnement.
	 * @see			getRunningMode
	 */
	virtual void setRunningMode (RUNNING_MODE mode);

	/**
	 * Insère le fichier transmis en argument à la position courante du curseur.
	 * @param	Chemin d'accès complet du fichier à insérer.
	 * @param	En retour, éventuels avertissements sur le contenu inséré.
	 * @warning	L'insertion se fait sur une nouvelle ligne
	 */
	virtual void insert (const std::string& fileName, std::string& warnings);

	/**
	 * Ajoute un point d'arrêt à la ligne dont le numéro est transmis en
	 * argument (premier numéro : 1).
	 */
	virtual void addBreakpoint (size_t line);

	/**
	 * Enlève le point d'arrêt à la ligne dont le numéro est transmis en
	 * argument (premier numéro : 1).
	 */
	virtual void removeBreakpoint (size_t line);

	/**
	 * Supprime tous les points d'arrêts.
	 */
	virtual void clearBreakpoints ( );

	/**
	 * Exécute le script à partir de la ligne d'exécution courante. Prend en
	 * compte le mode d'exécution (continu/debug).
	 * @see		isRunning;
	 * @see		stop
	 */
	virtual void run ( );

	/**
	 * Arrête l'exécution en mode debug dès que possible.
	 * @see		run
	 * @see		cont
	 * @see		next
	 */
	virtual void stop ( );

	/**
	 * @return	<I>true</I> si l'instance est en train d'exécuter des
	 * 			instructions (même si en mode pas à pas, ou exécution d'un
	 * 			fichier via <I>execFile</I>), <I>false</I> dans le cas
	 * 			contraire. En cas d'exécution d'un fichier
	 *			<I>isExecutingFile</I> retournera également <I>true</I>.
	 * @see		isHalted
	 * @see		run
	 * @see		execFile
	 * @see		isExecutingFile
	 */
	virtual bool isRunning ( ) const;

	/**
	 * @return	<I>true</I> si l'instance est en train d'exécuter un fichier.
	 * @see		isRunning
	 * @see		execFile
	 */
	virtual bool isExecutingFile ( ) const;	// v 2.10.0

	/**
	 * @return	<I>true</I> si l'instance est actuellement arrêtée et dans
	 * 			l'attente d'un ordre de reprise (suivant, continuer).
	 * @see		isRunning
	 */
	virtual bool isHalted ( ) const;

	/**
	 * @return	<I>true</I> si l'instance est actuellement dans l'attente d'un
	 *			ordre de reprise (next/cont/run).
	 * @see		isRunning
	 * @warning	N'a de sens qu'en mode debug
	 */
	virtual bool isWaitingForRunning ( ) const;

	/**
 	 * Reprend l'exécution à partir de la ligne courante (mode debug).
 	 */
	virtual void cont ( );

	/**
 	 * Exécute l'instruction suivante (mode debug).
 	 */
	virtual void next ( );

	/**
 	 * Quitte le mode debug (uniquement les relations avec le debogueur).
 	 * @warning	Ne change pas le mode (cf. <I>setRunningMode</I>) de
 	 * 			fonctionnement.
 	 */
	virtual void quitDbg ( );

	/**
	 * @return		Le nombre total de lignes.
	 */
	virtual size_t lineCount ( ) const;

	/**
	 * Classe représentant une instruction <I>Python</I>.
	 * Une instruction peut être exécutable (ou non t.q. commentaire),
	 * mono ou multiligne.
	 */
	class Instruction
	{
		public :

		/**
		 * Constructeur.
		 * @param	Texte de l'instruction (ou commentaire)
		 */
		Instruction (const std::string& command);

		/**
		 * Constructeur de copie, opérateur = : RAS.
		 */
		Instruction (const Instruction&);
		Instruction& operator = (const Instruction&);

		/**
		 * Destructeur. RAS.
		 */
		~Instruction ( );

		/**
		 * @return	Le texte de l'instruction.
		 */
		const std::string& command ( ) const;

		/**
		 * @return	Le nombre de lignes de l'instruction.
		 */
		size_t lineCount ( ) const;

		/**
		 * @return	<I>true</I> si l'instruction est exécutable, <I>false</I>
		 * 			dans le cas contraire.
		 */
		bool runnable ( ) const;

		/**
		 * @return	<I>true</I> si l'instruction appartient à un bloc (boucle
		 *			<I>for</I>, <I>while</I>, ..., <I>if</I>, ..., <I>try</I>,
		 *			..., sinon <I>false</I>.
		 */
		bool partOfBlock ( ) const;

		/**
		 * @return	<I>true</I> si l'instruction transmise en argument est
		 * 			de nature multilignes (début d'instruction for, if, while,
		 * 			try), <I>false</I> dans le cas contraire.
		 */
		static bool isMultiline (const std::string& instruction);

		/**
		 * @return	<I>true</I> si l'instruction transmise en argument est
		 * 			exécutable, <I>false</I> dans le cas contraire.
		 */
		static bool isRunnable (const std::string& instruction);


		private :

		std::string		_command;
		size_t			_line;
		bool			_runnable;
	};	// class Instruction


	class QtScriptTextFormat : public QTextCharFormat
	{
		public :


		enum LINE_TYPE { BLANK, COMMENT, INSTRUCTION,
		                 RAN_INSTRUCTION, FAILED_INSTRUCTION, TRY };

		QtScriptTextFormat (LINE_TYPE type);
		LINE_TYPE type ( ) const
		{ return _type; }

		// Marche moyennement avec le syntax highlighting !
		static const QtScriptTextFormat	commentFormat, emptyLineFormat,
										instructionFormat,
										ranInstructionFormat,
										failedInstructionFormat, tryFormat;

		static const QtScriptTextFormat& textFormat (const Instruction& ins);


		private :

		LINE_TYPE		_type;
	};	// class QtScriptTextFormat

	/**
	 * @return		L'action "changer de mode" associée à l'instance.
	 */
	virtual QAction& runningModeAction ( );

	/**
	 * @return		L'action "exécuter" associée à l'instance.
	 */
	virtual QAction& runAction ( );

	/**
	 * @return		L'action "stopper" associée à l'instance.
	 */
	virtual QAction& stopAction ( );

	/**
	 * @return		L'action "instruction suivante" associée à l'instance.
	 */
	virtual QAction& nextAction ( );

	/**
	 * @return		L'action "ajouter un point d'arrêt" associée à l'instance.
	 */
	virtual QAction& addBreakPointAction ( );

	/**
	 * @return		L'action "enlever le point d'arrêt" associée à l'instance.
	 */
	virtual QAction& removeBreakPointAction ( );

	/**
	 * @return		L'action "enlever tous les points d'arrêt" associée à
	 * l			instance.
	 */
	virtual QAction& removeAllBreakPointsAction ( );

	/**
	 * @return		L'action "insérer un script" associée à l'instance.
	 */
	virtual QAction& insertScriptAction ( );

	/**
	 * @return		Une barre d'icônes associée à l'instance.
	 */
	virtual QToolBar& getToolBar ( );

	/**
	 * @return		Le fichier soumis à python.
	 */
	virtual std::string getPythonScript ( ) const;

	/**
	 * Fonction callback appelée lorsqu'une ligne du fichier python va
	 * être exécutée. Actualise la console.
	 */
	virtual void lineIsBeingProcessedCallback (const std::string& fileName, size_t scriptLine);

	/**
	 * Fonction callback appelée lorsqu'une ligne du fichier python vient
	 * d'être exécutée. Actualise la console.
	 * @param		fichier considéré
	 * @param		numéro de ligne venant d'être exécutée
	 * @param		<I>true</I> si la ligne s'est correctement exécutée,
	 * 				<I>false</I> en cas d'erreur.
	 */
	virtual void lineProcessedCallback (const std::string& fileName, size_t scriptLine, bool ok);

	/**
	 * Fonction callback appelée lorsqu'une ligne de la console vient
	 * d'être exécutée. Actualise la console.
	 * @param		numéro de ligne venant d'être exécutée
	 * @param		<I>true</I> si la ligne s'est correctement exécutée,
	 * 				<I>false</I> en cas d'erreur.
	 */
	virtual void lineProcessedCallback (size_t consoleLine, bool ok);

	/**
	 * Fonction callback appelée lorsqu'un un point d'arrêt du fichier python
	 * vient d'être atteint. Actualise la console.
	 */
	virtual void atBreakPointCallback (const std::string& fileName, size_t line);

	/**
	 * Gestion de <I>logs</I> : commandes scripts et/ou sorties de commandes
	 * scriptées.
	 */
	//@{

	/**
	 * @return	L'éventuel flux sortant de messages utilisé pour écrire les
	 *			instructions exécutées dans des scripts.
	 * @see		dispatchLog
	 */
	virtual TkUtil::LogDispatcher& getLogDispatcher ( );

	/**
	 * @return	L'éventuel flux sortant de messages utilisé pour afficher
	 * 			des messages sur le déroulement des commandes exécutées.
	 * @see		setLogStream
	 * @see		log
	 * @warning	Ce flux n'est pas adopté, et sa destruction reste de ce fait à
	 * 			la charge de l'appelant.
	 */
	virtual TkUtil::LogOutputStream* getLogStream ( );

	/**
	 * @param	Le flux sortant de messages à utiliser pour afficher des
	 * 			messages sur le déroulement des commandes exécutées.
	 * @see		getLogStream
	 * @see		log
	 */
	virtual void setLogStream (TkUtil::LogOutputStream* stream);

	/**
	 * Affiche le message transmis en arguments dans le flux sortant de messages
	 * associé à l'instance.
	 * @see		getLogStream
	 */
	virtual void log (const TkUtil::Log& log);

	/**
	 * @param	Les résultats contenant la chaine transmise en argument doivent
	 * 			être masqués.
	 */
	virtual void hideResult (const std::string& str);

	/**
	 * Ajoute la commande transmise en argument, si il (le panneau) n'en est pas
	 * à l'origine, à l'historique des commandes exécutées, mais ne l'exécute
	 * pas. Présente l'intérêt d'intercaller des commandes effectuées par
	 * ailleurs.
	 * @param		Commande à ajouter
	 * @param		Commentaires associés à la commande.
	 * @param		Sortie de la commande à ajouter.
	 * @param       Status en erreur ou non de la commande
	 * @param		<I>true</I> si la commande vient du noyau, <I>false</I>
	 * 				si elle vient d'ailleurs (par exemple de la console python).
	 * @see			setPythonOutputStream
	 */
	virtual void addToHistoric (
		const IN_UTIL UTF8String& command, const IN_UTIL UTF8String& comments,
		const IN_UTIL UTF8String& commandOutput, bool statusErr,
		bool fromKernel = false);

	/**
	 * @return		Nom de l'interpréteur python (ex : nom de l'application),
	 *				pour les messages d'erreur.
	 * @see			setInterpreterName
	 */
	virtual const IN_STD string& getInterpreterName ( ) const;

	/**
	 * @param		Nom de l'interpréteur python (ex : nom de l'application),
	 *				pour les messages d'erreur.
	 * @see			getInterpreterName
	 */
	virtual void setInterpreterName (const IN_STD string& name);


	/**
	 * <P>Flux standards (<I>stdout</I> et <I>stderr</I>) récupérés
	 * (<I>true</I>) ou non (<I>false</I>) par la session <I>python</I> lors de
	 * l'exécution de commandes ou fichiers scripts.<P>
	 * <P>L'intérêt de le faire est que les sorties figurent dans les
	 * <I>logs</I>. L'intérêt de ne pas le faire est en cas de plantage où ces
	 * traces, éventuellement utiles à la mise au point, sont perdues.
	 * </P>
	 */
	static bool				_catchStdOutputs;

	//@}	// Gestion de <I>logs</I>

	/**
	 * Divers IHM.
	 */
	//@{

	/**
	 * Enregistre les paramètres d'affichage (taille, position, ...) de cette
	 * fenêtre.
	 * @see		readSettings
	 */
	virtual void writeSettings (QSettings& settings);

	/**
	 * Lit et s'applique les paramètres d'affichage (taille, position, ...) de
	 * cette fenêtre.
	 * @see		writeSettings
	 */
	virtual void readSettings (QSettings& settings);
	
	//@}	// Divers IHM.

	/**
	 * Méthode <I>thread safe</I> contrairement à <I>setEnabled</I>. Emet le
	 * signal <I>setUsabledCalled (bool)</I>.
	 * @param	Si <I>true</I> rend fonctionnel le panneau, l'inactive dans
	 *			le cas contraire.
	 * @see		setUsabledCalled
	 * @see		setEnabled
	 */
	virtual void setUsabled (bool enable);

	/**
	 * Méthode non <I>thread safe</I>  à appeler depuis le thread de l'instance.
	 * @param	Si <I>true</I> rend fonctionnel le panneau, l'inactive dans
	 *			le cas contraire.
	 * @see		setUsabled
	 */
	virtual void setEnabled (bool enable);


	protected :

	/**
	 * <P>Classe représentant une séquence d'instructions Python issue de la
	 * console et mises dans un fichier temporaire qui sera exécuté, en mode
	 * debug ou non.
	 * </P>
	 * <P>Par convention les numéros de ligne commencent à 1.
	 * </P>
	 */
	class InstructionsFile
	{
		public :

		/**
		 * Créé le fichier temporaire utilisé.
		 * @param		Prefix utilisé pour le nom du fichier
		 * @param		<I>true</I> si le jeu de caractères du fichier est
		 * 				<I>ISO-8859-15</I>, sinon <I>false</I>.
		 * @param		<I>true</I. si le fichier doit être détruit lors de la
		 * 				destruction de cette instance.
		 */
		InstructionsFile (
				const std::string& filePrefix, bool iso8859, bool autoDelete);

		/**
		 * Détruit le fichier temporaire utilisé.
		 */
		virtual ~InstructionsFile ( );

		/**
		 * Détruit le fichier, libère la mémoire et réinitialise les
		 * différentes variables.
		 */
		virtual void reset ( );

		/**
		 * Ajoute les lignes transmises en argument dans le fichier.
		 * @param		Ensemble de lignes à ajouter
		 * @param		Numéro dans la console de la première ligne ajoutée
		 * 				 au fichier
		 */
		virtual void addLines (
				const std::vector<std::string>& lines, size_t firstLineNum);

		/**
		 * @return		Le nom (chemin complet) du fichier temporaire
		 */	
		virtual std::string getFileName ( ) const;

		/**
		 * @return		Le nombre de lignes du fichier (hors lignes rajoutées
		 *				par l'instance, type encodage des caractères).
		 * @see			getLineOffset
		 * @see			getRealLineCount
		 */	
		virtual size_t getLineCount ( ) const;

		/**
		 * @return		Le nombre de lignes réel du fichier (incluant les lignes
		 *				rajoutées par l'instance, type encodage des caractères).
		 * @see			getLineOffset
		 * @see			getLineCount
		 */	
		virtual size_t getRealLineCount ( ) const;

		/**
		 * @return		Le numéro de la première ligne de la console 
		 * 				ajoutée dans ces script.
		 */	
		virtual size_t getFirstLineNum ( ) const;

		/**
		 * @return		Le numéro de ligne dans le script correspondant au
		 * 				numéro de ligne dans la console donné en argument.
		 * 				Des lignes sont éventuellements rajoutées à celles de
		 * 				la console (support charset ISO-8859, ...).
		 * @see			getLineOffset
		 */
		virtual size_t getScriptLineNum (size_t consoleLineNum) const;

		/*
		 * @return		Le numéro de ligne dans la console correspondant au
		 * 				numéro de ligne dans le script donné en argument.
		 * @see			getLineOffset
		 */
		virtual size_t getConsoleLineNum (size_t scriptLineNum) const;

		/**
		 * @return		La ligne de numéro dans la console donné en argument.
		 */
		virtual std::string getConsoleLine (size_t num) const;

		/**
		 * @return		Le nombre de lignes ajoutées (type d'encodage ISO, ...).
		 */
		virtual size_t getLineOffset ( ) const;

		/**
		 * @return		Le numéro le plus élevé de ligne traité.
		 * @see			setMaxProcessedLine
		 */
		virtual size_t maxProcessedLine ( ) const;

		/**
		 * Enregistre le numéro le plus élevé de ligne traité.
		 * @see			setMaxProcessedLine
		 */
		virtual void setMaxProcessedLine (size_t num);

		/**
		 * @return		Le numéro de ligne du "point d'arrêt nécessaire". C'est
		 *				l'éventuel point d'arrêt posé automatiquement en fin de
		 *				script afin de s'y arrêter pour s'assurer que le script
		 *				a bien été exécuté jusqu'au bout, car sinon Pdb
		 *				n'appelle pas lineProcessed en fin de script ... Vaut 0
		 *				par défaut (absence de point d'arrêt nécessaire).
		 * @see			setRequestedBreakPoint
		 */
		virtual size_t getRequestedBreakPoint ( ) const;

		/**
		 * @param		Numéro de ligne du "point d'arrêt nécessaire".
		 * @see			getRequestedBreakPoint
		 */
		virtual void setRequestedBreakPoint (size_t num);

		/**
		 * @param		<I>true</I> si le numéro de ligne donné est avec
		 *				<I>offset</I>, <I>false</I> dans le cas contraire.
		 * @return		Le numéro de la première ligne exécutable à partir de
		 * 				celle transmise en argument, hors <I>offset</I>, ou 0
		 * 				si aucune ligne n'est exécutable. La numérotation
		 * 				commence à 1.
		 * @see getLineOffset
		 */
		virtual size_t nextRunnableLine (
								size_t after, bool withLineOffset) const;


		private :

		/**
		 * Constructeur de copie, opérateur = : interdits.
		 */
		InstructionsFile (const InstructionsFile&);
		InstructionsFile& operator = (const InstructionsFile&);

		/** Le fichier temporaire utilisé. */
		std::unique_ptr<TkUtil::TemporaryFile>		_tmpFile;

		/** Le flux utilisé pour écrire dans le fichier temporire. */
		std::unique_ptr<std::ofstream>				_tmpStream;

		/** Les lignes conservées. */
		std::vector<std::string>					_lines;

		/** Le nombre de lignes du fichier. */
		size_t										_lineCount;

		/** Le numéro de la première ligne de la console écrite dans le
		    fichier. */
		size_t										_firstLineNum;

		/** <I>true</I> si le jeu de caractères du fichier est
		 * 	<I>ISO-8859-15</I>, sinon <I>false</I>. */
		bool										_iso8859;

		/** Le plus grand numéro de ligne traité. */
		size_t										_maxProcessedLine;

		/** Le numéro de ligne du "point d'arrêt nécessaire". */
		size_t										_requestedBreakPoint;
	};	// class InstructionsFile

	/**
	 * Classe dont la vocation est de positionner le curseur en position
	 * éditable en fin de document lors de sa destruction.
	 */
	class EndOfDocCursor
	{
		public :

		EndOfDocCursor (QtPythonConsole& pc);
		virtual ~EndOfDocCursor ( );
		virtual void setEnabled (bool enabled);			// v 6.2.0
		virtual bool isEnabled ( ) const;				// v 6.2.0
		

		private :

		EndOfDocCursor (const EndOfDocCursor&);
		EndOfDocCursor& operator = (const EndOfDocCursor&);

		QtPythonConsole&	_console;
		QTextCursor			_cursor;
		bool				_enabled;					// v 6.2.0
		QTextCursor			_initialCursorPosition;		// v 6.2.0
	};	// class EndOfDocCursor

	/**
	 * Appelé lorsque le curseur change de position. Actualise les actions.
	 * L'action d'insertion d'un script dépend notamment de la position du
	 * curseur.
	 */
	virtual void cursorPositionCallback ( );

	/**
	 * Certains évènements sont interdits sur les zones de script déjà
	 * exécutées. Les surcharges ci-dessous visent à metter en place cette
	 * interdiction.
	 */
	virtual bool event (QEvent*);
	virtual void dragEnterEvent (QDragEnterEvent*);
	virtual void dragLeaveEvent (QDragLeaveEvent*);
	virtual void dragMoveEvent (QDragMoveEvent*);
	virtual void dropEvent (QDropEvent*);

	/** Gestion des évènements : surchages pour spécificité (historique des
	 * instructions jouées, ...).
	 * @see	handleDownKeyPress
	 * @see	handleUpKeyPress
	 */
	virtual void keyPressEvent (QKeyEvent* event);

	/**
	 * Appelé  lorsque l'utilisateur presse la touche <I>down</I>
	 * (resp. <I>up</I>). Si les modificateurs adéquats sont également pressés 
	 * propose alors une instruction présente dans l'historique.
	 * @see		addToHistoric
	 */
	virtual bool handleDownKeyPress (QKeyEvent& event);
	virtual bool handleUpKeyPress (QKeyEvent& event);
	virtual bool handleComplete (QKeyEvent& event);
	virtual void addToHistoric (const std::string& instruction);

	/**
	 * @return	Un menu contextuel.
	 * @see		updateActions
	 */
	virtual QMenu* createPopupMenu ( );

	/**
	 * Actualise les actions (actives/inactives) selon le contexte actuel.
	 */
	virtual void updateActions ( );

	/**
	 * @return	La ligne courrante, à savoir la prochaine ligne à devoir être
	 * 			exécutée.
	 * @see		editedInstruction
	 * @see		previousInstruction
	 * @see		maxExecLine
	 */
	virtual size_t currentInstruction ( ) const;

	/**
	 * @return	La ligne max atteinte. Peut différer de
	 * 			currentInstruction ( ) - 1 lorsqu'on est dans une boucle.
	 * @see		currentInstruction
	 */
	virtual size_t maxExecLine ( ) const;

	/**
	 * @return	La ligne en cours d'édition
	 * @see		currentInstruction
	 */
	virtual std::string editedInstruction ( ) const;

	/**
	 * @return	La ligne d'instruction exécutable suivant la ligne transmise 
	 * 			dont le numéro est transmis en argument (saute les lignes
	 * 			blanches, les lignes de commentaires). S'il n'y a que des lignes
	 * 			blanches après retourne alors le numéro de la première ligne
	 * 			blanche.
	 * @warning	<B>ATTENTION :</B> ne garanti pas que ce sera la prochaine
	 * 			instruction exécutée, qui peut dépendre d'un branchement
	 * 			conditionnel, d'une exception.
	 * @see		previousInstruction
	 */
	virtual size_t followingInstruction (size_t line) const;

	/**
	 * @return	La ligne d'instruction exécutable précédant la ligne courrante,
	 *			ou 0 s'il n'y en a pas.
	 * @see		currentInstruction
	 */
	virtual size_t previousInstruction ( ) const;

	/**
	 * Ajoute l'instruction transmise en argument à la console, au point
	 * courant d'édition. Vérifie que cette instruction est valable.
	 * Gère les instructions multilignes, à savoir que le cas échéant l'instance
	 * attend de disposer de tous le bloc multiligne avant de l'ajouter.
	 * @warning	L'instruction est supposée être encodée en UTF8.
	 */
	virtual void addInstruction (const std::string& instruction);

	/**
	 * Exécuter en mode non debug les instructions.
	 * @see		getRunnableInstructions
	 */
	virtual void execInstructions ( );

	/**
	 * Exécuter en mode debug les instructions.
	 * @param	Si <I>true</I> s'arrête après la première instruction (permet
	 *			de débuter avec la commande "suivant").
	 */
	virtual void execDbgInstructions (bool stopImmediatly);

	/**
	 * Envoie la commande transmise en argument au débogueur python.
	 * @exception	Une exception est levée si on n'est pas en mode debug, ou
	 * 				pour tout autre erreur rencontrée.
	 */
	virtual void sendDbgCommand (const std::string& cmd);

	/**
	 * Exécute l'instruction transmise en argument hors débogueur, via
	 * <I>PyRun_StringFlags</I>.
	 * @param	instruction à exécuter
	 * @param	insère dans la console au point courant d'exécution
	 * 			l'instruction si <I>insert</I> vaut <I>true</I>.
	 */
	virtual void execInstruction (const std::string& instruction, bool insert);

	/**
	 * Exécute le fichier transmis en argument hors débogueur, via
	 * <I>PyRun_FileFlags</I>. Le fichier n'est pas inséré dans la console.
	 * <I>isRunning</I> retourne <I>true</I> durant l'exécution du fichier.
	 * @param	fichier à exécuter
	 * @warning	<B>ATTENTION : Non compatible en l'état avec une seconde
	 * 			session en mode debug.</B>
	 */
	virtual void execFile (const std::string& file);

	/**
	 * @return	Les instructions exécutables (depuis la ligne donnée en argument
	 * 			à la dernière ligne).
	 */
	virtual std::vector<std::string> getRunnableInstructions (
															size_t first) const;

	/**
	 * En retour, fichier et ligne courants dans le debugger python.
	 */
	virtual void where (std::string& filename, size_t& line);

	/**
	 * Récupère les sorties python et les exploite conformément au contexte
	 * en cours.
	 */
	virtual void processPythonOutputs ( );

	/**
	 * @return	La hauteur à utiliser pour les icônes
	 */	
	virtual int getIconSize ( ) const;

	/**
	 * @return	<I>true</I> si la modification du script est autorisée à
	 * 			l'emplacement du curseur, <I>false</I> dans le cas contraire.
	 */
	virtual bool allowEditionAtCursorPos ( ) const;

	/**
	 * @return	<I>true</I> si la modification du script est autorisée à
	 * 			la ligne dont le numéro est tranmis en argument (numéro première
	 * 			ligne : 1).
	 */
	virtual bool allowEditionAtLine (size_t line) const;

	/**
	 * Positionne le curseur à une "bonne position pour l'utilisateur". Ajoute
	 * une nouvelle ligne si nécessaire.
	 */
	virtual void validateCursorPosition ( );

	/**
	 * Ajoute à <I>completions</I> les complétions acceptées par <I>Swig</I>.
	 * Cette fonction part du principe que l'instruction <I>instruction</I> est
	 * un appel <I>Swig</I>, et les signatures des complétions qui seront
	 * proposées (et ajoutées à <I>completions</I>) sont extraites des messages
	 * d'erreur fournis par <I>Swig</I> ...
	 * @param	ensemble de complétions auxquelles seront ajoutées celle de cet
	 * 			appel.
	 * @param	instruction dont on recherche les complétion <I>Swig</I>.
	 * @see		getSwigCompletion
	 */
	virtual void addSwigCompletions (
		std::vector<std::string>& completions, const std::string& instruction);

	/**
	 * Trouve la seule complétion acceptable par <I>Swig</I> possible pour
	 * l'instruction transmise en argument, et la retourne. Cette instruction
	 * est de type nom de fonction, sans parenthèes, sans arguments.
	 * La complétion retournée est reconstituée à partir des messages
	 * d'erreur fournis par <I>Swig</I> ...
	 * @return	Complétion acceptable pour <I>Swig</I>
	 * @param	instruction dont on recherche la seule complétion <I>Swig</I>.
	 */
	virtual std::string getSwigCompletion (const std::string& instruction);

	/**
	 * @return	Une conversion <I>C++</I> vers <I>Python</I> de l'instruction
	 * 			transmise en argument.
	 */
	virtual std::string cppToPython (const std::string& instruction) const;

#ifdef MULTITHREADED_APPLICATION

	/**
	 * @return	Un mutex, pour fonctionnement en environnement multithread.
	 */
	virtual TkUtil::Mutex& getMutex ( );

#endif	// MULTITHREADED_APPLICATION


	protected slots :

	/**
	 * Appelé lorsque l'utilisateur active l'action "mode d'exécution".
	 * Change le mode d'exécution.
	 */
	virtual void runningModeCallback (bool);

	/**
	 * Appelé lorsque l'utilisateur active l'action "exécuter".
	 * Lance l'exécution dans le mode d'exécution courant.
	 */
	virtual void runCallback ( );

	/**
	 * Appelé lorsque l'utilisateur active l'action "arrêter".
	 * Arrête l'exécution.
	 */
	virtual void stopCallback ( );

	/**
	 * Appelé lorsque l'utilisateur active l'action "suivant".
	 * Exécute l'instruction suivante.
	 */
	virtual void nextCallback ( );

	/**
	 * Appelé lorsque l'utilisateur active l'action "ajouter un point d'arrêt".
	 * Ajoute un point d'arrêt à la ligne où est positionné le curseur.
	 */
	virtual void addBreakPointCallback ( );

	/**
	 * Appelé lorsque l'utilisateur active l'action "enlever le point d'arrêt".
	 * Enlève le point d'arrêt à la ligne où est positionné le curseur.
	 */
	virtual void removeBreakPointCallback ( );

	/**
	 * Appelé lorsque l'utilisateur active l'action "enlever tous les points
	 * d'arrêt".
	 * Enlève tous les points d'arrêt.
	 */
	virtual void clearBreakPointsCallback ( );

	/**
	 * Appelé lorsque l'utilisateur active l'action "charger un script".
	 * Affiche un sélecteur de fichier de chargement d'un script et insère le
	 * contenu du script sélectionné à l'emplacement du curseur.
	 * @warning		L'insertion se fait sur une nouvelle ligne
	 */
	virtual void insertScriptCallback ( );

	/**
	 * Appelé lorsque l'utilisateur choisi une ligne par complétion.
	 */
	virtual void completionCallback (int index);


	signals :

	/**
	 * Signal émis en environnement multithread lorsque la méthode
	 * <I>setUsabled/I> est invoquée depuis un thread autre que le thread de
	 * l'instance.
	 * @warning		Requiert une compilation avec la directive
	 *				<I>-DMULTITHREADED_APPLICATION</I>.
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

	/** Le mode de fonctionnement courant. */
	RUNNING_MODE							_runningMode;

	/** En cours d'exécution ? d'un fichier ? Dans l'attente de reprise ? */
	bool									_running, _executingFile, _halted;

	/** En mode debug, dans l'attente d'un ordre de reprise (next/cont/run) ? */
	bool									_waitingForRunning;

	/** La ligne courante (à exécuter). */
	size_t									_currentExecLine, _previousExecLine;

	/** La ligne max atteinte (a priori _currentExecLine - 1, mais, en cas de 
	 * boucles ... */
	size_t									_maxExecLine;

	/** Le fichier script (temporaire) en cours d'exécution. */
	std::unique_ptr<QtPythonConsole::InstructionsFile>	_currentScript;

	std::string								_pendingString;
	size_t									_currentPendingLine;
	/** Le fichier (nom, contenu) utilisé pour envoyer des ordres àl'interpreteur python. */
// CP, v 4.2.0 : convertir les 2 auto_ptr ci-après en unique_ptr provoque une erreur incompréhensible à g++ 7.5.0
// (error: call of overloaded ‘unique_ptr(int)’ is ambiguous _resultFilter ( ))
// => on les laisse ...
	std::auto_ptr<TkUtil::TemporaryFile>	_commandsFile;
	std::auto_ptr<std::ofstream>			_commandsStream;
	/** Les points d'arrêts. */
	std::set<size_t>						_breakpoints;

	/** Une éventuelle barre d'icônes associée à l'instance, et ses actions. */
	QToolBar*								_toolBar;
	QAction									*_runningModeAction,
											*_runAction, *_stopAction,
											*_nextAction, 
											*_addBreakPointAction,
											*_removeBreakPointAction,
											*_clearBreakPointsAction,
											*_insertScriptAction;

	/** Combo-box pour la complétion. */
	QComboBox*								_completionComboBox;
	
	/** La console est elle en cours d'évaluation de la complétion ? */
	bool									_checkingCompletion;	// v5.3.0

	/**
	 * Quelques icônes (non static car QApplication doit être instanciée avant).
	 */
	const	QIcon							_breakPointIcon, _currentLineIcon;
	static	const	QSize					iconSize;

	/**
	 * Dictionnaires python global et local.
	 */
	PyObject*								_globalDict;
	PyObject*								_localDict;

	/** Les module python utilisés pour l'implémentation de cette classe. */
	PyObject*								_dbgModule;
	PyObject*								_dbgSessionModule;
	PyObject*								_dbgCompletionModule;

	/** L'historique. */
	std::vector<std::string>				_history;
	size_t									_historyIndex;

#ifdef MULTITHREADED_APPLICATION
	/** La protection des opération en environnement multithread. */
	std::unique_ptr<TkUtil::Mutex>			_mutex;
#endif	// MULTITHREADED_APPLICATION

	/** Flux sortant des commandes python exécutées. */
	TkUtil::LogDispatcher					_logDispatcher;

	/** Flux sortant de messages éventuellement utilisé pour afficher des
	 * messages concernant les commandes exécutées. */
	TkUtil::LogOutputStream*				_logStream;

	/** Le nom de l'interpreteur python embarqué, pour les messages d'erreur. */
	std::string								_interpreterName;

	/** Le filtre d'affichage sur les résultats. */
	std::vector<std::string>				_resultFilter;
};	// class QtPythonConsole


/**
 * <P>
 * Classe de widget comprenant une instance de la classe <I>QtPythonConsole</I>
 * accolée à sa barre d'icônes. L'<I>opérateur -></I> permet d'invoquer
 * directement les méthodes de l'instance associée de la classe
 * <I>QtPythonConsole</I>.
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
	QtDecoratedPythonConsole (
			QWidget* parent, const std::string& appName, Qt::ToolBarArea area);


	/**
	 * Constructeur 2.
	 * @param	widget parent
	 * @param	Console python à utiliser.
	 * @param	Emplacement initial de la barre d'icônes.
	 */
	QtDecoratedPythonConsole (
			QWidget* parent, QtPythonConsole& console, Qt::ToolBarArea area);

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
	 * Invoquer directement des méthodes de l'instance de la console python
	 * associée.
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
