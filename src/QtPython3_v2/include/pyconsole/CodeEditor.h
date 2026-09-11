#pragma once

#include <QPlainTextEdit>
#include <QSet>
#include <QListWidget>

class QWidget;
class QPaintEvent;
class QResizeEvent;

/*
 * CodeEditor
 * ----------
 * QPlainTextEdit spécialisé pour la console Python :
 *  - une gouttière à gauche affiche les numéros de ligne, les points
 *    d'arrêt (cercle rouge) et la flèche orange indiquant la prochaine
 *    instruction à exécuter,
 *  - les lignes dont le numéro est < executedLineCount() sont "déjà
 *    exécutées" : fond gris clair, texte bleu marine, non éditables,
 *  - les lignes suivantes sont "non exécutées" : fond blanc, texte noir,
 *    éditables (sauf pendant l'exécution : setReadOnly(true) global),
 *  - Ctrl+Tab ouvre un menu de complétion à la position du curseur,
 *  - Shift+Up insère la dernière instruction exécutée avec succès à la
 *    position du curseur ; tant que Shift reste enfoncée, Up/Down fait
 *    remonter/redescendre dans l'historique (jusqu'à ne rien insérer).
 *
 * La classe ne connaît pas Python : elle notifie via des signaux
 * (breakpointToggled, completionRequested) et se contente d'un rendu ;
 * c'est PyConsoleWidget qui fait le lien avec PyInterpreterBridge.
 */
class CodeEditor : public QPlainTextEdit
{
    Q_OBJECT

public:
    explicit CodeEditor(QWidget* parent = nullptr);

    void lineNumberAreaPaintEvent(QPaintEvent* event);
    int lineNumberAreaWidth() const;

    // Nombre de lignes (1-based, à partir du haut) considérées comme
    // "déjà exécutées". 0 = aucune ligne exécutée.
    void setExecutedLineCount(int count);
    int executedLineCount() const { return m_executedLineCount; }

    // Ligne (1-based) contenant la flèche "prochaine instruction". 0 = aucune.
    void setCurrentExecLine(int line);
    int currentExecLine() const { return m_currentExecLine; }

    // Ligne (1-based) à surligner en rouge suite à une exception. 0 = aucune.
    void setErrorLine(int line);

    // Ligne de DÉBUT (1-based) du commentaire d'exception inséré, à
    // afficher en texte rouge sur `count` lignes (un message d'exception
    // peut tenir sur plusieurs lignes, ex. SyntaxError) -- mais sans le
    // surlignage de fond réservé à la ligne fautive elle-même. line=0
    // désactive. Nécessaire car applyHighlighting() repeint la totalité
    // de chaque ligne (fond + texte) selon son statut exécuté/non-exécuté :
    // sans cette exception explicite, cette repeinture écraserait
    // systématiquement tout format de caractère posé "à la main" sur le
    // texte du commentaire.
    void setErrorCommentLines(int line, int count);

    QSet<int> breakpoints() const { return m_breakpoints; }
    void setBreakpointMarks(const QSet<int>& lines);

    // Texte du code non encore exécuté (à partir de executedLineCount()+1).
    QString remainingText() const;

    // Insère `text` (considéré comme déjà exécuté) à l'emplacement de la
    // prochaine instruction (juste après la zone déjà exécutée), et
    // augmente executedLineCount() du nombre de lignes insérées.
    // Retourne le nombre de lignes insérées.
    int insertAlreadyExecutedCode(const QString& text);

    // Garantit qu'il existe toujours au moins une ligne NON exécutée
    // (donc éditable) après la zone déjà exécutée, même si tout le
    // document actuel a été exécuté. Sans ça, il n'existerait plus aucune
    // position de curseur non protégée où taper du nouveau code -- à
    // appeler une fois l'exécution terminée, quand tout a été exécuté.
    void ensureEditableTail();

    // Positionne le curseur d'édition sur `line` (1-based) et fait défiler
    // la vue de façon minimale pour la rendre visible (via
    // QPlainTextEdit::ensureCursorVisible(), qui ne défile que le
    // nécessaire) -- utilisé en fin d'exécution pour garder la ligne
    // pertinente (instruction courante, ligne fautive...) visible, et, le
    // défilement étant minimal, garder autant que possible ce qui
    // l'entoure (notamment la dernière instruction exécutée, juste
    // au-dessus) également visible. Ne fait rien si line <= 0.
    void ensureLineVisible(int line);

    // Le programme est en cours d'exécution : édition interdite globalement,
    // même sur les lignes non exécutées.
    void setExecuting(bool executing);
    bool isExecuting() const { return m_executing; }

    // Appelé par la gouttière (LineNumberArea) lors d'un clic ; public pour
    // rester un simple composant interne sans dépendance friend.
    void handleGutterClick(int y);

    // Affiche le popup de complétion avec les éléments fournis par
    // PyConsoleWidget (résultat de PyInterpreterBridge::complete()).
    // `prefix` est le texte déjà tapé qui a servi de base à la complétion
    // (ex: "os.pa") : chaque candidat rlcompleter commence TOUJOURS par ce
    // préfixe (ex: "os.path") -- seule la partie qui le dépasse (ex: "th")
    // est réellement insérée lors du choix d'un candidat.
    void showCompletionPopup(const QStringList& items, const QString& prefix);

    // Démarre un cycle de rappel d'historique (déclenché par Shift+Up) :
    // insère `items[0]` (l'instruction la plus récemment exécutée) à la
    // position du curseur. `items` est une copie de l'historique au
    // moment du déclenchement, la plus récente en tête -- CodeEditor la
    // conserve pour parcourir le cycle localement (Shift+Up/Down suivants,
    // cf. keyPressEvent) sans autre aller-retour avec PyConsoleWidget.
    void beginHistoryCycle(const QStringList& items);

signals:
    // L'utilisateur a cliqué dans la gouttière sur la ligne "line" (1-based),
    // dans la zone du code non exécuté : bascule le point d'arrêt.
    void breakpointToggleRequested(int line);

    // Ctrl+Tab : demande de complétion avec le préfixe de la ligne courante
    // jusqu'au curseur.
    void completionRequested(const QString& prefix);

    // Shift+Up (premier appui, hors cycle en cours) : demande le
    // démarrage d'un cycle de rappel d'historique -- PyConsoleWidget
    // répond en appelant beginHistoryCycle() avec l'historique courant.
    void historyRequested();

protected:
    void resizeEvent(QResizeEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void keyReleaseEvent(QKeyEvent* event) override;
    void insertFromMimeData(const QMimeData* source) override;
    void mousePressEvent(QMouseEvent* event) override;

private slots:
    void updateLineNumberAreaWidth(int newBlockCount);
    void updateLineNumberArea(const QRect& rect, int dy);

    // Connecté à QTextDocument::contentsChange() : quand l'utilisateur
    // insère ou supprime des lignes, décale (ou supprime, si elles
    // tombaient dans une zone supprimée) les points d'arrêt ainsi que la
    // flèche/ligne d'erreur/commentaire d'erreur affichés, pour qu'ils
    // restent en face de l'instruction à laquelle ils étaient associés.
    void onContentsChange(int position, int charsRemoved, int charsAdded);

private:
    void applyHighlighting();
    bool cursorInProtectedZone() const;
    void insertCompletion(const QString& completion);

    // Avance ou recule d'un cran dans le cycle d'historique en cours
    // (direction : +1 = Shift+Up, plus ancien ; -1 = Shift+Down, plus
    // récent, jusqu'à ne rien insérer). Remplace le texte précédemment
    // inséré par ce cycle (m_historyInsertStart/End) par la nouvelle
    // entrée. Ne fait rien si aucun cycle n'est en cours (cf.
    // beginHistoryCycle()) ou si on est déjà à une extrémité.
    void stepHistoryCycle(int direction);

    // Termine le cycle de rappel d'historique en cours (relâchement de
    // Shift, ou toute autre action -- frappe d'une touche non liée au
    // cycle, clic souris...) : le texte actuellement inséré devient du
    // texte normal, plus rien ne sera remplacé par un Shift+Up/Down
    // ultérieur (qui redémarrera alors un nouveau cycle depuis le début).
    void endHistoryCycle();

    // Vérifie, par une simple analyse textuelle (sans compiler le code),
    // qu'une ligne ressemble à une instruction sur laquelle un point
    // d'arrêt aurait un sens : ni vide, ni un commentaire pur, ni une
    // ligne de déclaration "def ...:" / "class ...:" (qui ne s'exécute
    // qu'une fois, à la définition, jamais à chaque appel -- s'y arrêter
    // n'apporte rien en pratique). Ce n'est qu'un filtre client, rapide,
    // pour éviter de laisser croire qu'un point d'arrêt a été posé là où
    // il n'a pas de sens ; la validation faisant autorité reste côté
    // Python (cf. PyInterpreterBridge::setBreakpoint(), basée sur les
    // lignes réellement exécutables du code compilé).
    bool isPlausibleBreakpointLine(int lineNumber) const;

    QWidget* m_lineNumberArea;
    int m_executedLineCount = 0;
    int m_currentExecLine = 0;
    int m_errorLine = 0;
    int m_errorCommentLine = 0;
    int m_errorCommentLineCount = 1;
    bool m_executing = false;
    QSet<int> m_breakpoints;
    int m_lastKnownBlockCount = 1; // pour détecter les insertions/suppressions de lignes

    QListWidget* m_completionPopup = nullptr;
    QString m_completionPrefix; // texte déjà tapé (cf. showCompletionPopup)

    // Etat du cycle de rappel d'historique (Shift+Up/Down).
    bool m_historyCyclingActive = false;
    QStringList m_historyItems;     // copie de l'historique, la plus récente en tête
    int m_historyIndex = -1;        // -1 = "rien inséré" ; 0 = plus récente ; 1 = avant, etc.
    int m_historyInsertStart = 0;   // position (caractère) où le texte du cycle a été inséré
    int m_historyInsertEnd = 0;     // position de fin de ce texte
};
