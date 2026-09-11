#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QMutex>
#include <QQueue>
#include <QTimer>

/*
 * PyInterpreterBridge
 * --------------------
 * Encapsule un interpréteur CPython 3.10+ embarqué et le script Python
 * "console_bridge.py" qui implémente le moteur d'exécution pas-à-pas
 * basé sur bdb.Bdb.
 *
 * IMPORTANT : CPython ne supporte qu'un seul interpréteur "principal" par
 * processus dans ce mode d'usage. Cette classe est donc un singleton :
 * une seule PyConsoleWidget "active" par processus est prise en charge
 * par cette implémentation de référence.
 *
 * Tous les appels publics acquièrent le GIL (PyGILState_Ensure/Release),
 * ils peuvent donc être appelés depuis le thread GUI sans précaution
 * particulière.
 *
 * Communication thread Python -> thread GUI : les callbacks natifs
 * (notify_line, notify_paused, ...) appelés depuis le thread Python
 * d'exécution NE dépendent PLUS du mécanisme de réveil inter-thread de Qt
 * (QMetaObject::invokeMethod / postEvent), qui s'est révélé peu fiable
 * dans certains environnements (les événements postés depuis un thread
 * natif externe pouvaient rester en attente jusqu'au prochain événement
 * UI natif, voire indéfiniment avec Qt::BlockingQueuedConnection). A la
 * place : les callbacks natifs empilent un événement dans une file
 * protégée par mutex (enqueueEvent()), et un QTimer tournant sur le
 * thread GUI (donc piloté par une boucle d'événements dont on sait
 * qu'elle fonctionne, puisque les clics de boutons fonctionnent) vide
 * cette file à intervalle court et régulier (drainQueue()). Ce polling
 * ne dépend d'aucun mécanisme de réveil cross-thread : il fonctionne
 * même si postEvent()/invokeMethod() ne réveillent pas correctement la
 * boucle d'événements dans l'environnement cible.
 */
class PyInterpreterBridge : public QObject
{
    Q_OBJECT

public:
    static PyInterpreterBridge& instance();

    // A appeler une fois, depuis le thread GUI, avant toute autre méthode.
    // bridgeScriptPath : chemin vers console_bridge.py (copié à côté du
    // binaire par CMake).
    void initialize(const QString& bridgeScriptPath);
    void shutdown();
    bool isInitialized() const { return m_initialized; }

    // Points d'arrêt (lignes 1-based, relatives au code compilé lors du
    // dernier run/runDebug). Retourne false si la ligne ne correspond pas
    // à une instruction exécutable.
    bool setBreakpoint(int line);
    bool clearBreakpoint(int line);
    void clearAllBreakpoints();

    // Compile le code et calcule les lignes exécutables valides, SANS
    // démarrer l'exécution. A appeler avant setBreakpoint() (sinon les
    // breakpoints seraient validés contre un ensemble vide) puis avant
    // start(). Retourne le message d'erreur de compilation, vide si succès.
    QString loadCode(const QString& source);

    // Démarre l'exécution du code précédemment chargé par loadCode().
    // stepFirst (mode debug uniquement) : démarre directement en pas-à-pas
    // (pause dès la première ligne) plutôt que de continuer silencieusement
    // jusqu'au premier point d'arrêt.
    void start(bool debugMode, bool stepFirst = false);

    // Contrôle en mode debug (valables seulement après runDebug, tant que
    // l'exécution est en pause sur une ligne).
    void step();
    void continueExec();
    void stopExec();

    // Exécute immédiatement du code et le considère comme "déjà exécuté".
    // Ne doit être appelé que hors exécution. Retourne le texte d'erreur
    // (vide si succès).
    QString execAsAlreadyExecuted(const QString& source);

    // Complétion façon readline sur le préfixe donné.
    QStringList complete(const QString& text);

    // -- Appelé UNIQUEMENT par les callbacks natifs (namespace anonyme de
    //    PyInterpreterBridge.cpp), depuis n'importe quel thread. Empile un
    //    événement dans la file thread-safe ; si l'appel provient déjà du
    //    thread GUI (cas de execAsAlreadyExecuted, qui exécute du Python
    //    synchrone sur ce thread), la file est aussi vidée immédiatement
    //    pour éviter toute latence inutile.
    void enqueueLine(int line);
    void enqueuePaused(int line);
    void enqueueOutput(int stream, const QString& text);
    void enqueueException(int line, const QString& text);
    void enqueueFinished(bool stoppedEarly);

signals:
    // Emis juste avant l'exécution de la ligne "line" (1-based, relative
    // au code source du dernier run). Sert uniquement à la mise à jour
    // visuelle (surlignage, position de la flèche) : NE signifie PAS que
    // l'exécution est en pause (une ligne peut être notifiée puis
    // s'enchaîner silencieusement si elle ne correspond à aucun point
    // d'arrêt actif).
    void lineReached(int line);

    // Emis uniquement quand l'exécution est réellement mise en pause à la
    // ligne "line", en attente d'une action utilisateur (step/continue).
    // C'est ce signal, et lui seul, qui doit piloter l'activation des
    // boutons "Pas à pas" / "Continuer".
    void paused(int line);

    // stream : 0 = stdout, 1 = stderr
    void outputReceived(int stream, QString text);

    // Une exception a stoppé l'exécution à la ligne "line".
    void exceptionRaised(int line, QString text);

    // L'exécution en cours (run/step/continue) est terminée. stoppedEarly
    // est vrai uniquement si elle a été interrompue par un clic sur
    // "Arrêter" avant d'avoir tout exécuté (permet de ne griser que la
    // portion réellement exécutée, pas tout le code restant). Faux pour
    // une fin normale (tout exécuté) ou une exception (déjà gérée
    // séparément via exceptionRaised).
    void executionFinished(bool stoppedEarly);

private slots:
    // Tourne sur le thread GUI (appelé par m_pollTimer, ou directement
    // par enqueueEvent() quand l'appelant est déjà sur ce thread). Vide la
    // file et ré-émet les signaux Qt correspondants, dans l'ordre.
    void drainQueue();

private:
    PyInterpreterBridge() = default;
    ~PyInterpreterBridge();
    PyInterpreterBridge(const PyInterpreterBridge&) = delete;
    PyInterpreterBridge& operator=(const PyInterpreterBridge&) = delete;

    QString callFunctionReturningStr(const char* name, const QString& arg, bool* ok = nullptr);
    bool callFunctionReturningBool(const char* name, int arg);

    struct NativeEvent {
        enum Kind { Line, Paused, Output, Exception, Finished } kind = Line;
        int line = 0;
        int stream = 0;
        QString text;
        bool flag = false; // Finished : stoppedEarly
    };
    void enqueueEvent(NativeEvent ev);

    void* m_mainDict = nullptr; // PyObject* (dict du module __main__)
    bool m_initialized = false;

    QMutex m_queueMutex;
    QQueue<NativeEvent> m_queue;
    QTimer* m_pollTimer = nullptr;
};
