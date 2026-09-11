#include "pyconsole/PyConsoleWidget.h"
#include "pyconsole/CodeEditor.h"
#include "pyconsole/OutputView.h"
#include "pyconsole/PyInterpreterBridge.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QSplitter>
#include <QAction>
#include <QToolBar>
#include <QLabel>
#include <QCoreApplication>
#include <QDir>
#include <QTextCursor>
#include <QTextBlock>
#include <QTextCharFormat>
#include <QTextDocument>
#include <QColor>
#include <algorithm>

PyConsoleWidget::PyConsoleWidget(QWidget* parent)
    : QWidget(parent)
{
    buildUi();

    auto& bridge = PyInterpreterBridge::instance();
    if (!bridge.isInitialized()) {
#ifdef PYTHON_CONSOLE_BRIDGE_PATH
        // Chemin fourni par CMakeLists.txt via une directive de
        // compilation (-DPYTHON_CONSOLE_BRIDGE_PATH), résolu une fois
        // pour toutes à la compilation plutôt que reconstruit à chaque
        // lancement à partir du répertoire de l'exécutable.
        const QString scriptPath = QString::fromUtf8(PYTHON_CONSOLE_BRIDGE_PATH);
#else
        // Repli si le projet est compilé sans cette macro (ex: intégré
        // tel quel dans un autre système de build) : suppose que le
        // script est copié à côté de l'exécutable.
        const QString scriptPath = QDir(QCoreApplication::applicationDirPath())
                                        .filePath("console_bridge.py");
#endif
        bridge.initialize(scriptPath);
    }

    connect(&bridge, &PyInterpreterBridge::lineReached, this, &PyConsoleWidget::onLineReached);
    connect(&bridge, &PyInterpreterBridge::paused, this, &PyConsoleWidget::onPaused);
    connect(&bridge, &PyInterpreterBridge::outputReceived, this, &PyConsoleWidget::onOutputReceived);
    connect(&bridge, &PyInterpreterBridge::exceptionRaised, this, &PyConsoleWidget::onExceptionRaised);
    connect(&bridge, &PyInterpreterBridge::executionFinished, this, &PyConsoleWidget::onExecutionFinished);

    updateButtonsEnabled();
}

PyConsoleWidget::~PyConsoleWidget() = default;

void PyConsoleWidget::buildUi()
{
    m_editor = new CodeEditor(this);
    m_output = new OutputView(this);

    m_modeAction = new QAction(tr("Mode debug"), this);
    m_modeAction->setCheckable(true);
    m_runAction = new QAction(tr("Run"), this);
    m_stepAction = new QAction(tr("Pas à pas"), this);
    m_stopAction = new QAction(tr("Arrêter"), this);
    m_clearBreakpointsAction = new QAction(tr("Suppr. tous les points d'arrêt"), this);
    m_stateLabel = new QLabel(tr("Prêt"), this);

    m_toolBar = new QToolBar(this);
    // Barre verticale : les actions s'empilent de haut en bas, à gauche
    // de l'éditeur, plutôt qu'alignées horizontalement au-dessus.
    m_toolBar->setOrientation(Qt::Vertical);
    // Nos actions n'ont pas d'icône : forcer l'affichage du texte plutôt
    // que de dépendre du style courant pour deviner qu'il faut le montrer.
    m_toolBar->setToolButtonStyle(Qt::ToolButtonTextOnly);
    m_toolBar->addAction(m_modeAction);
    m_toolBar->addAction(m_runAction);
    m_toolBar->addAction(m_stepAction);
    m_toolBar->addAction(m_stopAction);
    m_toolBar->addAction(m_clearBreakpointsAction);

    // Texte potentiellement long ("Exécution en cours...") dans une
    // colonne étroite : autoriser le retour à la ligne.
    m_stateLabel->setWordWrap(true);

    auto* sidebarLayout = new QVBoxLayout;
    sidebarLayout->addWidget(m_toolBar);
    sidebarLayout->addStretch();
    sidebarLayout->addWidget(m_stateLabel);

    auto* splitter = new QSplitter(Qt::Horizontal, this);
    splitter->addWidget(m_editor);
    splitter->addWidget(m_output);
    splitter->setStretchFactor(0, 3);
    splitter->setStretchFactor(1, 2);

    auto* mainLayout = new QHBoxLayout(this);
    mainLayout->addLayout(sidebarLayout);
    mainLayout->addWidget(splitter);

    connect(m_modeAction, &QAction::toggled, this, &PyConsoleWidget::onModeToggled);
    connect(m_runAction, &QAction::triggered, this, &PyConsoleWidget::onRunClicked);
    connect(m_stepAction, &QAction::triggered, this, &PyConsoleWidget::onStepClicked);
    connect(m_stopAction, &QAction::triggered, this, &PyConsoleWidget::onStopClicked);
    connect(m_clearBreakpointsAction, &QAction::triggered, this, &PyConsoleWidget::onClearAllBreakpointsClicked);

    connect(m_editor, &CodeEditor::breakpointToggleRequested, this, &PyConsoleWidget::onBreakpointToggleRequested);
    connect(m_editor, &CodeEditor::completionRequested, this, &PyConsoleWidget::onCompletionRequested);
    connect(m_editor, &CodeEditor::historyRequested, this, &PyConsoleWidget::onHistoryRequested);
}

void PyConsoleWidget::setCode(const QString& code)
{
    m_editor->setPlainText(code);
    m_editor->setExecutedLineCount(0);
    m_editor->setCurrentExecLine(0);
    m_editor->setErrorLine(0);
    m_editor->setErrorCommentLines(0, 1);
    m_editor->setBreakpointMarks({});
    m_output->clearOutput();
    m_lastExecLine = 0;
    m_executedBoundaryAtRunStart = 0;
    m_executedHistory.clear();
    m_lastRecordedExecutedLine = 0;
    setRunningState(false);
}

QString PyConsoleWidget::code() const
{
    return m_editor->toPlainText();
}

QString PyConsoleWidget::remainingCode() const
{
    return m_editor->remainingText();
}

void PyConsoleWidget::setRunningState(bool running)
{
    m_running = running;
    m_paused = false;
    m_editor->setExecuting(running);
    updateButtonsEnabled();
    m_stateLabel->setText(running ? tr("Exécution en cours...") : tr("Prêt"));
}

void PyConsoleWidget::updateButtonsEnabled()
{
    m_modeAction->setEnabled(!m_running);
    // "Run" est actif hors exécution comme avant, mais aussi pendant une
    // pause en mode debug : dans ce cas, il agit comme "Continuer" (cf.
    // onRunClicked()) plutôt que de rester désactivé sans rien pouvoir
    // faire -- il n'y a pas d'action "Continuer" séparée.
    m_runAction->setEnabled(!m_running || (m_debugMode && m_paused));
    m_clearBreakpointsAction->setEnabled(!m_running && m_debugMode);

    // "Pas à pas" est actif soit pendant une vraie pause (pour avancer),
    // soit hors exécution en mode debug (pour démarrer directement en
    // pas-à-pas, sans attendre un premier "Run").
    m_stepAction->setEnabled((m_running && m_debugMode && m_paused) || (!m_running && m_debugMode));
    m_stopAction->setEnabled(m_running);
}

void PyConsoleWidget::onStopClicked()
{
    if (!m_running)
        return;
    // Débloque immédiatement une éventuelle pause en cours (le thread
    // Python attend alors sur l'event de reprise) et demande l'arrêt.
    // N.B. : en mode normal sans point d'arrêt, bdb désactive le traçage
    // dès le premier "continue" ; l'arrêt ne prend alors effet qu'à la fin
    // naturelle de l'exécution (limitation connue).
    m_paused = false;
    updateButtonsEnabled();
    PyInterpreterBridge::instance().stopExec();
}

void PyConsoleWidget::onModeToggled(bool debugChecked)
{
    if (m_running) {
        // Ne devrait pas arriver (action désactivée pendant l'exécution),
        // mais on protège quand même contre un changement de mode en vol.
        m_modeAction->blockSignals(true);
        m_modeAction->setChecked(m_debugMode);
        m_modeAction->blockSignals(false);
        return;
    }
    m_debugMode = debugChecked;
    updateButtonsEnabled();
}

void PyConsoleWidget::onRunClicked()
{
    if (m_paused) {
        // En pause en cours de débogage : "Run" agit comme "Continuer"
        // (reprend l'exécution jusqu'au prochain point d'arrêt ou la fin),
        // plutôt que de ne rien faire -- ce qui était le comportement
        // précédent, startExecution() refusant de démarrer une nouvelle
        // exécution tant qu'une autre est déjà en cours.
        resumeFromPause();
        return;
    }
    startExecution(/*stepFirst=*/false);
}

void PyConsoleWidget::resumeFromPause()
{
    m_paused = false;
    updateButtonsEnabled();
    PyInterpreterBridge::instance().continueExec();
}

void PyConsoleWidget::startExecution(bool stepFirst)
{
    if (m_running)
        return;

    auto& bridge = PyInterpreterBridge::instance();

    m_executedBoundaryAtRunStart = m_editor->executedLineCount();
    m_editor->setErrorLine(0);
    m_editor->setErrorCommentLines(0, 1);

    const QString src = remainingCode();
    if (src.trimmed().isEmpty()) {
        // Rien à exécuter : tout le document a déjà été exécuté.
        return;
    }

    // 1) Compilation SEULE (sans démarrer). Doit précéder la pose des
    //    points d'arrêt : c'est ce qui calcule les lignes exécutables
    //    valides côté Python.
    const QString compileErr = bridge.loadCode(src);
    if (!compileErr.isEmpty()) {
        m_output->appendStderr(compileErr + QLatin1Char('\n'));
        return;
    }

    // Nombre total de lignes du document à cet instant : si l'exécution se
    // termine sans exception, tout cet intervalle sera marqué "exécuté".
    m_totalLinesAtRunStart = m_editor->document()->blockCount();
    m_exceptionThisRun = false;

    setRunningState(true);

    if (m_debugMode) {
        // 2) Pose des points d'arrêt actuels (traduits en numéros de ligne
        //    relatifs au code qui vient d'être compilé), maintenant que
        //    valid_lines est prêt côté Python.
        bridge.clearAllBreakpoints();
        QSet<int> stillValid;
        const QSet<int> current = m_editor->breakpoints();
        for (int docLine : current) {
            const int relative = docLine - m_executedBoundaryAtRunStart;
            if (relative < 1)
                continue; // ligne déjà exécutée entre-temps : on l'ignore
            if (bridge.setBreakpoint(relative)) {
                stillValid.insert(docLine);
            } else {
                m_output->appendStderr(tr("# Point d'arrêt ligne %1 invalide (pas une instruction)\n").arg(docLine));
            }
        }
        m_editor->setBreakpointMarks(stillValid);
    }

    // 3) Démarrage effectif. stepFirst (uniquement significatif en mode
    //    debug) démarre directement en pas-à-pas : la toute première ligne
    //    provoque une vraie pause, comme si "Pas à pas" avait été cliqué
    //    juste après un Run classique.
    bridge.start(m_debugMode, stepFirst);
}

void PyConsoleWidget::onStepClicked()
{
    if (m_paused) {
        m_paused = false;
        updateButtonsEnabled();
        PyInterpreterBridge::instance().step();
        return;
    }
    // Pas encore en cours d'exécution : "Pas à pas" agit comme un
    // démarrage (comme "Run"), mais en demandant une pause dès la toute
    // première ligne plutôt que de continuer jusqu'au premier point
    // d'arrêt. Uniquement pertinent en mode debug (bouton désactivé sinon,
    // cf. updateButtonsEnabled()).
    if (m_running || !m_debugMode)
        return;
    startExecution(/*stepFirst=*/true);
}

void PyConsoleWidget::onClearAllBreakpointsClicked()
{
    PyInterpreterBridge::instance().clearAllBreakpoints();
    m_editor->setBreakpointMarks({});
}

void PyConsoleWidget::insertExecutedInstructions(const std::string& lines)
{
    if (m_running)
        return;

    const QString text = QString::fromStdString(lines);
    if (text.trimmed().isEmpty())
        return;

    // Contrairement à execInstruction(), rien n'est exécuté ici : `lines`
    // représente des instructions déjà exécutées PAR AILLEURS (par
    // exemple par le code C++ appelant, via un autre mécanisme) -- il ne
    // s'agit que d'une mise à jour de l'affichage, insérant le texte tel
    // quel (commentaires éventuellement déjà inclus dedans) au point
    // courant, marqué comme déjà exécuté.
    m_editor->insertAlreadyExecutedCode(text);
}

void PyConsoleWidget::execInstruction(const std::string& instruction, const std::string& comment)
{
    if (m_running)
        return;

    const QString qInstruction = QString::fromStdString(instruction);
    if (qInstruction.trimmed().isEmpty())
        return;

    const QString err = PyInterpreterBridge::instance().execAsAlreadyExecuted(qInstruction);
    if (!err.isEmpty()) {
        m_output->appendStderr(err + QLatin1Char('\n'));
        return;
    }

    // Le commentaire (s'il y en a un) est inséré juste au-dessus de
    // l'instruction, sous forme de commentaire Python ("# ..."), et fait
    // partie du même bloc marqué "déjà exécuté" -- comme le reste de ce
    // bloc, il devient donc grisé et non modifiable.
    QString toInsert;
    const QString qComment = QString::fromStdString(comment);
    if (!qComment.trimmed().isEmpty()) {
        QString commentText = qComment;
        commentText.replace(QLatin1Char('\n'), QLatin1String("\n# "));
        toInsert += QLatin1String("# ") + commentText + QLatin1Char('\n');
    }
    toInsert += qInstruction;

    m_editor->insertAlreadyExecutedCode(toInsert);
}

void PyConsoleWidget::onBreakpointToggleRequested(int line)
{
    QSet<int> bps = m_editor->breakpoints();
    if (bps.contains(line)) {
        bps.remove(line);
    } else {
        bps.insert(line);
    }
    m_editor->setBreakpointMarks(bps);
    // La validité réelle (ligne exécutable) n'est vérifiée qu'au lancement
    // (onRunClicked), une fois le code compilé.
}

void PyConsoleWidget::onCompletionRequested(const QString& prefix)
{
    // Le préfixe est la ligne courante jusqu'au curseur ; rlcompleter attend
    // le dernier "mot" (identifiant / attribut) en cours de saisie.
    QString word = prefix;
    int i = word.length() - 1;
    while (i >= 0) {
        const QChar c = word.at(i);
        if (c.isLetterOrNumber() || c == QLatin1Char('_') || c == QLatin1Char('.')) {
            --i;
        } else {
            break;
        }
    }
    word = word.mid(i + 1);

    const QStringList items = PyInterpreterBridge::instance().complete(word);
    m_editor->showCompletionPopup(items, word);
}

void PyConsoleWidget::onLineReached(int line)
{
    const int docLine = m_executedBoundaryAtRunStart + line;
    m_lastExecLine = docLine;

    // Tout ce qui précède docLine est désormais exécuté ; docLine est la
    // prochaine instruction sur le point d'être exécutée (flèche orange).
    // NOTE : ce signal ne signifie pas que l'exécution est en pause (elle
    // peut s'enchaîner silencieusement si docLine n'est pas un point
    // d'arrêt actif) -- c'est onPaused() qui gère l'état d'attente.
    m_editor->setExecutedLineCount(docLine - 1);
    m_editor->setCurrentExecLine(docLine);
    recordExecutedLines(docLine - 1);
}

void PyConsoleWidget::onPaused(int line)
{
    // Emis uniquement quand l'exécution est réellement bloquée à cette
    // ligne, en attente d'un clic "Pas à pas" ou "Continuer".
    Q_UNUSED(line);
    if (m_debugMode) {
        m_paused = true;
        updateButtonsEnabled();
    }
}

void PyConsoleWidget::onOutputReceived(int stream, QString text)
{
    if (stream == 0)
        m_output->appendStdout(text);
    else
        m_output->appendStderr(text);
}

void PyConsoleWidget::onExceptionRaised(int line, QString text)
{
    m_exceptionThisRun = true;
    const int docLine = m_executedBoundaryAtRunStart + line;

    // Marque explicitement tout ce qui précède la ligne fautive comme
    // exécuté (grisé, non modifiable). NE PAS se contenter de l'état déjà
    // en place via un précédent onLineReached() : en mode normal (ou en
    // mode debug sans point d'arrêt), bdb désactive son traçage après la
    // toute première ligne pour ne pas ralentir l'exécution -- aucune
    // notification de ligne n'arrive alors plus pour les lignes qui
    // s'exécutent silencieusement ensuite, et la limite resterait figée
    // sur la première ligne bien que beaucoup plus de code ait, en
    // réalité, été exécuté avec succès avant l'erreur.
    m_editor->setExecutedLineCount(docLine - 1);
    m_lastExecLine = docLine;
    recordExecutedLines(docLine - 1);

    // Insère le texte de l'exception, sous forme de commentaire, juste
    // au-dessus de la ligne fautive. Le rendu en rouge est délégué à
    // CodeEditor::setErrorCommentLines() (via applyHighlighting()) : poser
    // le format directement sur le texte inséré ici n'aurait aucun effet
    // visible, applyHighlighting() repeignant intégralement chaque ligne
    // (fond + texte) selon son statut exécuté/non-exécuté juste après.
    QString comment = text;
    comment.replace(QLatin1Char('\n'), QLatin1String("\n# "));
    comment = QLatin1String("# ") + comment;
    const int commentLineCount = comment.count(QLatin1Char('\n')) + 1;

    QTextCursor cursor(m_editor->document());
    QTextBlock block = m_editor->document()->findBlockByNumber(docLine - 1);
    if (block.isValid()) {
        cursor.setPosition(block.position());
    } else {
        cursor.movePosition(QTextCursor::End);
    }
    cursor.insertText(comment + QLatin1Char('\n'));

    // La ligne fautive a été décalée d'autant de lignes que le commentaire
    // inséré au-dessus.
    m_editor->setErrorCommentLines(docLine, commentLineCount);
    m_editor->setErrorLine(docLine + commentLineCount);
    m_editor->setCurrentExecLine(0);
    m_lastErrorLine = docLine + commentLineCount;

    m_output->appendStderr(text + QLatin1Char('\n'));
}

void PyConsoleWidget::onExecutionFinished(bool stoppedEarly)
{
    if (m_exceptionThisRun) {
        // Déjà entièrement géré par onExceptionRaised() (executedLineCount
        // et errorLine correctement positionnés sur la ligne fautive).
    } else if (stoppedEarly) {
        // Arrêt volontaire (bouton "Arrêter") avant la fin du code : seule
        // la portion RÉELLEMENT exécutée doit être grisée. m_lastExecLine
        // (mis à jour à chaque lineReached()) est la ligne sur laquelle
        // l'exécution s'est arrêtée -- elle n'a PAS été exécutée, donc la
        // flèche reste positionnée dessus, prête pour une reprise ou une
        // modification du code à venir.
        const int boundary = qMax(0, m_lastExecLine - 1);
        m_editor->setExecutedLineCount(boundary);
        m_editor->setCurrentExecLine(m_lastExecLine);
        recordExecutedLines(boundary);
    } else {
        // Fin normale : tout le code envoyé à l'interpréteur pour cette
        // passe (jusqu'à m_totalLinesAtRunStart) a été exécuté jusqu'au
        // bout, y compris sa toute dernière ligne -- qui ne reçoit jamais
        // de lineReached() puisqu'il n'y a pas de "ligne suivante".
        m_editor->setExecutedLineCount(m_totalLinesAtRunStart);
        m_editor->setCurrentExecLine(0);
        recordExecutedLines(m_totalLinesAtRunStart);
        // S'assure qu'il reste toujours une ligne éditable en fin de
        // document pour pouvoir taper de nouvelles instructions, même si
        // absolument tout le code actuel a été exécuté.
        m_editor->ensureEditableTail();
    }

    setRunningState(false);

    // Quelle que soit la façon dont l'exécution s'est terminée, on revient
    // à un état "prêt, mode normal" : le code à venir (existant ou
    // nouvellement tapé) reste éditable, et une relance -- en mode normal
    // ou en réactivant le mode debug -- reprend dans le MÊME contexte
    // Python (l'espace de noms _console_globals n'est jamais réinitialisé
    // entre deux exécutions, pendant toute la durée de vie du widget).
    m_modeAction->setChecked(false);
    updateButtonsEnabled();

    // Quelle que soit la façon dont l'exécution s'est terminée, on
    // s'assure que la ligne pertinente reste visible à l'écran : la ligne
    // fautive en cas d'exception, la prochaine instruction non exécutée
    // (flèche) en cas d'arrêt volontaire, ou la toute dernière ligne
    // exécutée si tout s'est terminé normalement (il n'y a alors plus de
    // "prochaine instruction"). Le défilement étant minimal
    // (CodeEditor::ensureLineVisible()), ce qui entoure cette ligne --
    // notamment la dernière instruction exécutée juste au-dessus -- reste
    // autant que possible également visible.
    if (m_exceptionThisRun) {
        m_editor->ensureLineVisible(m_lastErrorLine);
    } else if (stoppedEarly) {
        m_editor->ensureLineVisible(m_lastExecLine);
    } else {
        m_editor->ensureLineVisible(m_totalLinesAtRunStart);
    }
}

void PyConsoleWidget::recordExecutedLines(int uptoLineInclusive)
{
    for (int ln = m_lastRecordedExecutedLine + 1; ln <= uptoLineInclusive; ++ln) {
        QTextBlock block = m_editor->document()->findBlockByNumber(ln - 1);
        if (!block.isValid())
            continue;
        const QString text = block.text();
        const QString trimmed = text.trimmed();
        if (trimmed.isEmpty())
            continue; // une ligne blanche n'est pas une "instruction"
        if (trimmed.startsWith(QLatin1Char('#')))
            continue; // un commentaire pur n'est pas une "instruction" exécutée
        m_executedHistory.append(text);
        if (m_executedHistory.size() > 20) {
            m_executedHistory.removeFirst();
        }
    }
    m_lastRecordedExecutedLine = qMax(m_lastRecordedExecutedLine, uptoLineInclusive);
}

void PyConsoleWidget::onHistoryRequested()
{
    if (m_executedHistory.isEmpty())
        return;

    // La plus récemment exécutée en tête de liste : c'est celle-ci que
    // beginHistoryCycle() insère immédiatement, Shift+Up/Down suivants
    // reculant/avançant ensuite dans cet ordre (cf. CodeEditor). Ordre
    // inverse de celui du stockage interne (chronologique, la plus
    // ancienne en tête).
    QStringList mostRecentFirst = m_executedHistory;
    std::reverse(mostRecentFirst.begin(), mostRecentFirst.end());
    m_editor->beginHistoryCycle(mostRecentFirst);
}
