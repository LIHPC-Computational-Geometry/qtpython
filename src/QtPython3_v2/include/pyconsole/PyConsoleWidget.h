#pragma once

#include <QWidget>
#include <QSet>
#include <string>
#include "pyconsole/CodeEditor.h"

class OutputView;
class QAction;
class QToolBar;
class QLabel;

/*
 * PyConsoleWidget
 * ---------------
 * Widget composite dérivé de QWidget offrant une console Python 3.10+
 * interactive, avec exécution pas-à-pas, points d'arrêt, et affichage
 * séparé du code déjà exécuté / non exécuté et de la sortie standard/erreur.
 *
 * Usage minimal :
 *
 *     PyConsoleWidget* console = new PyConsoleWidget(this);
 *     console->setCode("for i in range(3):\n    print(i)\n");
 *
 * NOTE : un seul PyConsoleWidget par processus est pris en charge par
 * cette implémentation de référence (l'interpréteur CPython embarqué est
 * un singleton global, cf. PyInterpreterBridge).
 *
 * Toutes les méthodes et tous les slots sont virtuels, et les méthodes
 * autrefois privées sont désormais protégées : une classe dérivée peut
 * ainsi redéfinir tout comportement (les membres de données, eux, restent
 * privés -- seul le comportement, exposé via les méthodes, est ouvert à
 * la personnalisation).
 */
class PyConsoleWidget : public QWidget
{
    Q_OBJECT

public:
    explicit PyConsoleWidget(QWidget* parent = nullptr);
    ~PyConsoleWidget() override;

    // Remplace tout le contenu par du code "non exécuté" (édition libre).
    virtual void setCode(const QString& code);
    virtual QString code() const;

    virtual bool isDebugMode() const { return m_debugMode; }
    virtual bool isRunning() const { return m_running; }

    // Accès à l'éditeur interne, pour un code appelant qui aurait besoin
    // d'aller au-delà de l'API de PyConsoleWidget (ex: personnalisation
    // fine, inspection directe du document...).
    virtual CodeEditor& getCodeEditor() { return *m_editor; }
    virtual const CodeEditor& getCodeEditor() const { return *m_editor; }

    // Exécute `instruction` immédiatement, dans le même thread que
    // l'appelant. Si `comment` est non vide, il est inséré juste
    // au-dessus, sous forme de commentaire Python ("# ..."). Instruction
    // ET commentaire sont ajoutés au point courant (juste après la zone
    // déjà exécutée) et marqués comme exécutés (grisés, non modifiables)
    // -- rien n'est inséré si l'exécution échoue (le message d'erreur est
    // alors affiché dans le panneau de sortie). Ne fait rien si la
    // console est en cours d'exécution.
    virtual void execInstruction(const std::string& instruction, const std::string& comment = std::string());

    // Insère `lines` au point courant (juste après la zone déjà exécutée)
    // et les marque comme déjà exécutées (grisées, non modifiables), SANS
    // les exécuter : contrairement à execInstruction(), ces instructions
    // ont déjà été exécutées PAR AILLEURS (ex: par un appelant C++ qui a
    // lui-même déjà fait tourner ce code via un autre mécanisme) -- ce
    // n'est ici qu'une mise à jour de l'affichage. `lines` peut librement
    // contenir ses propres lignes de commentaire ("# ..."), mélangées aux
    // instructions : tout est inséré tel quel, sur autant de lignes que
    // nécessaire. Ne fait rien si la console est en cours d'exécution ou
    // si `lines` est vide.
    virtual void insertExecutedInstructions(const std::string& lines);

    // Accès à la barre d'outils (verticale, à gauche de l'éditeur) et aux
    // actions qui la composent, pour un code appelant qui aurait besoin
    // d'aller au-delà de l'API de PyConsoleWidget (ex: ajouter ses propres
    // actions à la même barre, changer une icône ou un raccourci...).
    virtual QToolBar& getToolBar() { return *m_toolBar; }
    virtual const QToolBar& getToolBar() const { return *m_toolBar; }

    // NOTE : pas d'action "Continuer" distincte -- "Run" (getRunAction())
    // en tient lieu, déclenché en pleine pause de débogage (cf.
    // onRunClicked() / resumeFromPause()). Pas d'action "Insérer code déjà
    // exécuté" non plus : cette fonctionnalité est désormais purement
    // programmatique, via insertExecutedInstructions().
    virtual QAction& getModeAction() { return *m_modeAction; }
    virtual const QAction& getModeAction() const { return *m_modeAction; }
    virtual QAction& getRunAction() { return *m_runAction; }
    virtual const QAction& getRunAction() const { return *m_runAction; }
    virtual QAction& getStepAction() { return *m_stepAction; }
    virtual const QAction& getStepAction() const { return *m_stepAction; }
    virtual QAction& getStopAction() { return *m_stopAction; }
    virtual const QAction& getStopAction() const { return *m_stopAction; }
    virtual QAction& getClearBreakpointsAction() { return *m_clearBreakpointsAction; }
    virtual const QAction& getClearBreakpointsAction() const { return *m_clearBreakpointsAction; }

public slots:
    virtual void onModeToggled(bool debugChecked);
    virtual void onRunClicked();
    virtual void onStepClicked();
    virtual void onStopClicked();
    virtual void onClearAllBreakpointsClicked();

protected slots:
    virtual void onLineReached(int line);
    virtual void onPaused(int line);
    virtual void onOutputReceived(int stream, QString text);
    virtual void onExceptionRaised(int line, QString text);
    virtual void onExecutionFinished(bool stoppedEarly);
    virtual void onBreakpointToggleRequested(int line);
    virtual void onCompletionRequested(const QString& prefix);
    virtual void onHistoryRequested();

protected:
    virtual void buildUi();
    virtual void updateButtonsEnabled();
    virtual void setRunningState(bool running);
    virtual QString remainingCode() const; // code non exécuté, tel qu'affiché

    // Factorise le démarrage d'une exécution (compilation, pose des
    // points d'arrêt, lancement) entre le bouton "Run" (stepFirst=false :
    // continue jusqu'au premier point d'arrêt) et un démarrage direct via
    // "Pas à pas" (stepFirst=true : pause dès la première ligne).
    virtual void startExecution(bool stepFirst);

    // Reprend l'exécution depuis une pause en cours de débogage (appelle
    // PyInterpreterBridge::continueExec()) -- déclenché par "Run" lorsqu'il
    // est cliqué en pleine pause (il n'y a pas de bouton "Continuer"
    // séparé).
    virtual void resumeFromPause();

    // Ajoute à l'historique le texte de chaque ligne entre
    // m_lastRecordedExecutedLine+1 et uptoLineInclusive (bornes incluses),
    // dans l'ordre, en ignorant les lignes blanches et les commentaires
    // purs. Les entrées les plus anciennes sont retirées au-delà de 20.
    // Appelé à chaque progression de la limite "exécuté" (onLineReached,
    // fin normale, exception, arrêt volontaire) pour ne jamais rater de
    // lignes exécutées silencieusement (cf. le mécanisme équivalent pour
    // l'affichage des lignes grisées).
    virtual void recordExecutedLines(int uptoLineInclusive);

private:
    CodeEditor* m_editor = nullptr;
    OutputView* m_output = nullptr;

    QAction* m_modeAction = nullptr;     // "Mode debug" (checkable)
    QAction* m_runAction = nullptr;      // "Run" -- agit aussi comme "Continuer" en pleine pause
    QAction* m_stepAction = nullptr;
    QAction* m_stopAction = nullptr;
    QAction* m_clearBreakpointsAction = nullptr;
    QToolBar* m_toolBar = nullptr;       // verticale, à gauche de l'éditeur
    QLabel* m_stateLabel = nullptr;

    bool m_debugMode = false;
    bool m_running = false;   // exécution en cours (run/step/continue en attente)
    bool m_paused = false;    // en pause sur une ligne, en attente de step/continue

    // Ligne (dans le référentiel du dernier code compilé, 1-based) où
    // l'exécution s'est arrêtée pour la dernière fois.
    int m_lastExecLine = 0;

    // Ligne (document affiché, 1-based) de la ligne fautive suite à la
    // dernière exception rapportée -- utilisée en fin d'exécution pour
    // s'assurer qu'elle reste visible (cf. onExecutionFinished()).
    int m_lastErrorLine = 0;

    // Nombre de lignes considérées comme exécutées au moment où le dernier
    // run/runDebug a démarré (permet de traduire les numéros de ligne
    // renvoyés par bdb -- relatifs au code compilé -- en numéros de ligne
    // absolus dans le document affiché).
    int m_executedBoundaryAtRunStart = 0;

    // Nombre total de lignes du document au moment où le dernier run a
    // démarré. Si l'exécution se termine SANS exception, tout ce qui a été
    // envoyé à l'interpréteur a forcément été exécuté jusqu'au bout : on
    // marque alors tout cet intervalle comme "exécuté" (fond gris/bleu
    // marine, non modifiable), même si aucun lineReached() supplémentaire
    // n'est émis pour la toute dernière ligne exécutée.
    int m_totalLinesAtRunStart = 0;

    // Vrai si une exception a été signalée pendant la passe d'exécution en
    // cours (empêche de marquer la ligne fautive comme "exécutée").
    bool m_exceptionThisRun = false;

    // Historique (Shift+Up/Down) des 20 dernières instructions exécutées, dans
    // l'ordre d'exécution (la plus ancienne en tête). Cumulatif sur toute
    // la durée de vie du widget (jamais réinitialisé entre deux "Run" :
    // le contexte Python, lui, ne l'est pas non plus), remis à zéro
    // uniquement par setCode().
    QStringList m_executedHistory;

    // Dernière ligne (1-based, dans le référentiel du document affiché)
    // déjà prise en compte dans m_executedHistory -- évite d'ajouter deux
    // fois la même ligne si recordExecutedLines() est appelée plusieurs
    // fois avec des bornes qui se recouvrent.
    int m_lastRecordedExecutedLine = 0;
};
