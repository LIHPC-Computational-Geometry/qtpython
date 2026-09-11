// IMPORTANT : Python.h doit être inclus AVANT tout header Qt (y compris
// notre propre header, qui inclut <QObject>). Python.h et les macros Qt
// (slots/signals/emit) entrent en conflit si Qt est inclus en premier.
#include <Python.h>

#include "pyconsole/PyInterpreterBridge.h"

#include <QFile>
#include <QDebug>
#include <QThread>
#include <QMutexLocker>
#include <QList>

namespace {

// Pointeur vers l'unique instance, utilisé par les callbacks natifs appelés
// depuis Python (potentiellement depuis le thread d'exécution Python).
PyInterpreterBridge* g_bridge = nullptr;

PyObject* native_notify_line(PyObject*, PyObject* args)
{
    int line = 0;
    if (!PyArg_ParseTuple(args, "i", &line)) {
        return nullptr;
    }
    if (g_bridge) g_bridge->enqueueLine(line);
    Py_RETURN_NONE;
}

PyObject* native_notify_paused(PyObject*, PyObject* args)
{
    int line = 0;
    if (!PyArg_ParseTuple(args, "i", &line)) {
        return nullptr;
    }
    if (g_bridge) g_bridge->enqueuePaused(line);
    Py_RETURN_NONE;
}

PyObject* native_notify_output(PyObject*, PyObject* args)
{
    const char* kind = nullptr;
    const char* text = nullptr;
    if (!PyArg_ParseTuple(args, "ss", &kind, &text)) {
        return nullptr;
    }
    const int stream = (kind && QString::fromUtf8(kind) == QLatin1String("stderr")) ? 1 : 0;
    if (g_bridge) g_bridge->enqueueOutput(stream, QString::fromUtf8(text));
    Py_RETURN_NONE;
}

PyObject* native_notify_exception(PyObject*, PyObject* args)
{
    int line = 0;
    const char* text = nullptr;
    if (!PyArg_ParseTuple(args, "is", &line, &text)) {
        return nullptr;
    }
    if (g_bridge) g_bridge->enqueueException(line, QString::fromUtf8(text));
    Py_RETURN_NONE;
}

PyObject* native_notify_finished(PyObject*, PyObject* args)
{
    int stoppedEarly = 0;
    if (!PyArg_ParseTuple(args, "p", &stoppedEarly)) {
        return nullptr;
    }
    if (g_bridge) g_bridge->enqueueFinished(stoppedEarly != 0);
    Py_RETURN_NONE;
}

PyMethodDef g_nativeMethods[] = {
    {"notify_line", native_notify_line, METH_VARARGS, "Notifie la ligne sur le point d'être exécutée"},
    {"notify_paused", native_notify_paused, METH_VARARGS, "Notifie une vraie pause en attente d'action utilisateur"},
    {"notify_output", native_notify_output, METH_VARARGS, "Notifie une sortie stdout/stderr"},
    {"notify_exception", native_notify_exception, METH_VARARGS, "Notifie une exception"},
    {"notify_finished", native_notify_finished, METH_VARARGS, "Notifie la fin de l'exécution en cours"},
    {nullptr, nullptr, 0, nullptr}
};

PyModuleDef g_nativeModuleDef = {
    PyModuleDef_HEAD_INIT,
    "_pyconsole_native",
    "Module natif de callbacks pour PyConsoleWidget",
    -1,
    g_nativeMethods,
    nullptr, nullptr, nullptr, nullptr
};

PyObject* PyInit__pyconsole_native()
{
    return PyModule_Create(&g_nativeModuleDef);
}

// Récupère une fonction du dict __main__ (emprunt de référence, ne pas DECREF)
PyObject* borrowMainFunc(PyObject* mainDict, const char* name)
{
    PyObject* func = PyDict_GetItemString(mainDict, name);
    if (!func || !PyCallable_Check(func)) {
        qWarning() << "Fonction bridge introuvable :" << name;
        return nullptr;
    }
    return func;
}

} // namespace

PyInterpreterBridge& PyInterpreterBridge::instance()
{
    static PyInterpreterBridge s_instance;
    return s_instance;
}

PyInterpreterBridge::~PyInterpreterBridge()
{
    shutdown();
}

void PyInterpreterBridge::enqueueEvent(NativeEvent ev)
{
    {
        QMutexLocker locker(&m_queueMutex);
        m_queue.enqueue(std::move(ev));
    }
    if (QThread::currentThread() == this->thread()) {
        // Déjà sur le thread GUI (cas de execAsAlreadyExecuted, qui exécute
        // du Python synchrone sur ce thread) : on traite immédiatement,
        // inutile d'attendre le prochain tick du timer.
        drainQueue();
    }
    // Sinon : appelé depuis le thread Python d'exécution. On ne fait RIEN
    // de plus ici -- en particulier, on n'essaie plus de réveiller la
    // boucle d'événements du thread GUI (ni via QMetaObject::invokeMethod,
    // ni via une connexion bloquante). m_pollTimer, qui tourne sur le
    // thread GUI, viendra vider la file à son prochain déclenchement.
}

void PyInterpreterBridge::enqueueLine(int line)
{
    NativeEvent ev; ev.kind = NativeEvent::Line; ev.line = line;
    enqueueEvent(std::move(ev));
}

void PyInterpreterBridge::enqueuePaused(int line)
{
    NativeEvent ev; ev.kind = NativeEvent::Paused; ev.line = line;
    enqueueEvent(std::move(ev));
}

void PyInterpreterBridge::enqueueOutput(int stream, const QString& text)
{
    NativeEvent ev; ev.kind = NativeEvent::Output; ev.stream = stream; ev.text = text;
    enqueueEvent(std::move(ev));
}

void PyInterpreterBridge::enqueueException(int line, const QString& text)
{
    NativeEvent ev; ev.kind = NativeEvent::Exception; ev.line = line; ev.text = text;
    enqueueEvent(std::move(ev));
}

void PyInterpreterBridge::enqueueFinished(bool stoppedEarly)
{
    NativeEvent ev; ev.kind = NativeEvent::Finished; ev.flag = stoppedEarly;
    enqueueEvent(std::move(ev));
}

void PyInterpreterBridge::drainQueue()
{
    QList<NativeEvent> batch;
    {
        QMutexLocker locker(&m_queueMutex);
        while (!m_queue.isEmpty())
            batch.append(m_queue.dequeue());
    }
    for (const NativeEvent& ev : batch) {
        switch (ev.kind) {
        case NativeEvent::Line:
            emit lineReached(ev.line);
            break;
        case NativeEvent::Paused:
            emit paused(ev.line);
            break;
        case NativeEvent::Output:
            emit outputReceived(ev.stream, ev.text);
            break;
        case NativeEvent::Exception:
            emit exceptionRaised(ev.line, ev.text);
            break;
        case NativeEvent::Finished:
            emit executionFinished(ev.flag);
            break;
        }
    }
}

void PyInterpreterBridge::initialize(const QString& bridgeScriptPath)
{
    if (m_initialized) {
        return;
    }
    g_bridge = this;

    PyImport_AppendInittab("_pyconsole_native", &PyInit__pyconsole_native);
    Py_Initialize();
    // A ce stade, le thread appelant (thread GUI) détient DÉJÀ le GIL --
    // c'est le comportement documenté de Py_Initialize(). Inutile (et
    // trompeur) d'appeler PyGILState_Ensure() ici : on l'utilise
    // directement pour charger le script bridge.

    QFile file(bridgeScriptPath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qCritical() << "Impossible d'ouvrir le script bridge :" << bridgeScriptPath;
        PyEval_SaveThread(); // relâche quand même le GIL avant de sortir
        return;
    }
    const QByteArray source = file.readAll();
    file.close();

    PyObject* mainModule = PyImport_AddModule("__main__"); // référence empruntée
    PyObject* mainDict = PyModule_GetDict(mainModule);     // référence empruntée
    Py_INCREF(mainDict);
    m_mainDict = mainDict;

    PyObject* execResult = PyRun_String(source.constData(), Py_file_input, mainDict, mainDict);
    if (!execResult) {
        PyErr_Print();
    } else {
        Py_DECREF(execResult);
    }

    PyObject* nativeModule = PyImport_ImportModule("_pyconsole_native");
    if (nativeModule) {
        if (PyObject* initFunc = borrowMainFunc(mainDict, "init_native")) {
            PyObject* r = PyObject_CallFunctionObjArgs(initFunc, nativeModule, nullptr);
            if (!r) {
                PyErr_Print();
            }
            Py_XDECREF(r);
        }
        Py_DECREF(nativeModule);
    } else {
        PyErr_Print();
    }

    // IMPORTANT -- correctif clé : Py_Initialize() a laissé le thread GUI
    // détenir implicitement le GIL, mais CETTE prise n'est pas "comptée"
    // de la même façon qu'une prise via PyGILState_Ensure(). Tant qu'on ne
    // la relâche pas explicitement ici, TOUTE la synchronisation
    // ultérieure entre le thread GUI (PyGILState_Ensure/Release répétés à
    // chaque appel : loadCode, setBreakpoint, step, continueExec...) et un
    // thread Python natif (threading.Thread, notre thread d'exécution) part
    // d'un état incohérent. C'est le patron documenté par CPython pour
    // l'embarquement multi-thread : "if you wish to use Python's threading
    // services from a thread not created by Python, release the GIL first
    // via PyEval_SaveThread()". Sans cet appel, le symptôme observé est
    // exactement celui-ci : le thread d'exécution se bloque durablement
    // dans threading.Event.wait() bien qu'un .set() ait été effectué avec
    // succès depuis le thread GUI (la toute première pause fonctionne,
    // mais aucune reprise -- Continuer/Pas à pas -- ne débloque le thread).
    PyEval_SaveThread();

    m_initialized = true;

    // Timer de vidage de la file, sur le thread GUI. Intervalle court
    // (imperceptible pour un humain qui clique) mais surtout indépendant
    // de tout mécanisme de réveil inter-thread : il tourne tant que la
    // boucle d'événements du thread GUI tourne, point final.
    m_pollTimer = new QTimer(this);
    m_pollTimer->setInterval(15);
    connect(m_pollTimer, &QTimer::timeout, this, &PyInterpreterBridge::drainQueue);
    m_pollTimer->start();
}

void PyInterpreterBridge::shutdown()
{
    if (!m_initialized) {
        return;
    }
    if (m_pollTimer) {
        m_pollTimer->stop();
        delete m_pollTimer;
        m_pollTimer = nullptr;
    }
    // On ré-acquiert le GIL (proprement cette fois, PyEval_SaveThread()
    // ayant correctement "nettoyé" l'état au moment de initialize()) et on
    // le CONSERVE jusqu'à Py_FinalizeEx(), qui doit être appelé en le
    // tenant -- d'où l'absence volontaire de PyGILState_Release() ici.
    PyGILState_Ensure();
    if (m_mainDict) {
        Py_DECREF(static_cast<PyObject*>(m_mainDict));
        m_mainDict = nullptr;
    }

    Py_FinalizeEx();
    m_initialized = false;
    g_bridge = nullptr;
}

bool PyInterpreterBridge::setBreakpoint(int line)
{
    return callFunctionReturningBool("set_breakpoint", line);
}

bool PyInterpreterBridge::clearBreakpoint(int line)
{
    return callFunctionReturningBool("clear_breakpoint", line);
}

void PyInterpreterBridge::clearAllBreakpoints()
{
    if (!m_initialized) return;
    PyGILState_STATE gstate = PyGILState_Ensure();
    PyObject* mainDict = static_cast<PyObject*>(m_mainDict);
    if (PyObject* func = borrowMainFunc(mainDict, "clear_all_breakpoints")) {
        PyObject* r = PyObject_CallObject(func, nullptr);
        if (!r) PyErr_Print();
        Py_XDECREF(r);
    }
    PyGILState_Release(gstate);
}

QString PyInterpreterBridge::loadCode(const QString& source)
{
    bool ok = false;
    return callFunctionReturningStr("load_code", source, &ok);
}

void PyInterpreterBridge::start(bool debugMode, bool stepFirst)
{
    if (!m_initialized) return;
    PyGILState_STATE gstate = PyGILState_Ensure();
    PyObject* mainDict = static_cast<PyObject*>(m_mainDict);
    if (PyObject* func = borrowMainFunc(mainDict, "start")) {
        PyObject* pyDebug = debugMode ? Py_True : Py_False;
        PyObject* pyStepFirst = stepFirst ? Py_True : Py_False;
        PyObject* r = PyObject_CallFunctionObjArgs(func, pyDebug, pyStepFirst, nullptr);
        if (!r) PyErr_Print();
        Py_XDECREF(r);
    }
    PyGILState_Release(gstate);
}

void PyInterpreterBridge::step()
{
    if (!m_initialized) return;
    PyGILState_STATE gstate = PyGILState_Ensure();
    PyObject* mainDict = static_cast<PyObject*>(m_mainDict);
    if (PyObject* func = borrowMainFunc(mainDict, "step")) {
        PyObject* r = PyObject_CallObject(func, nullptr);
        if (!r) PyErr_Print();
        Py_XDECREF(r);
    }
    PyGILState_Release(gstate);
}

void PyInterpreterBridge::continueExec()
{
    if (!m_initialized) return;
    PyGILState_STATE gstate = PyGILState_Ensure();
    PyObject* mainDict = static_cast<PyObject*>(m_mainDict);
    if (PyObject* func = borrowMainFunc(mainDict, "cont")) {
        PyObject* r = PyObject_CallObject(func, nullptr);
        if (!r) PyErr_Print();
        Py_XDECREF(r);
    }
    PyGILState_Release(gstate);
}

void PyInterpreterBridge::stopExec()
{
    if (!m_initialized) return;
    PyGILState_STATE gstate = PyGILState_Ensure();
    PyObject* mainDict = static_cast<PyObject*>(m_mainDict);
    if (PyObject* func = borrowMainFunc(mainDict, "stop")) {
        PyObject* r = PyObject_CallObject(func, nullptr);
        if (!r) PyErr_Print();
        Py_XDECREF(r);
    }
    PyGILState_Release(gstate);
}

QString PyInterpreterBridge::execAsAlreadyExecuted(const QString& source)
{
    bool ok = false;
    QString err = callFunctionReturningStr("exec_already_executed", source, &ok);
    return err; // vide si succès (None côté Python -> chaîne vide ici)
}

QStringList PyInterpreterBridge::complete(const QString& text)
{
    QStringList result;
    if (!m_initialized) return result;

    PyGILState_STATE gstate = PyGILState_Ensure();
    PyObject* mainDict = static_cast<PyObject*>(m_mainDict);
    if (PyObject* func = borrowMainFunc(mainDict, "complete")) {
        QByteArray utf8 = text.toUtf8();
        PyObject* pyText = PyUnicode_FromStringAndSize(utf8.constData(), utf8.size());
        PyObject* r = PyObject_CallFunctionObjArgs(func, pyText, nullptr);
        Py_XDECREF(pyText);
        if (r && PyList_Check(r)) {
            const Py_ssize_t n = PyList_Size(r);
            for (Py_ssize_t i = 0; i < n; ++i) {
                PyObject* item = PyList_GetItem(r, i); // référence empruntée
                if (item && PyUnicode_Check(item)) {
                    result << QString::fromUtf8(PyUnicode_AsUTF8(item));
                }
            }
        } else if (!r) {
            PyErr_Print();
        }
        Py_XDECREF(r);
    }
    PyGILState_Release(gstate);
    return result;
}

QString PyInterpreterBridge::callFunctionReturningStr(const char* name, const QString& arg, bool* ok)
{
    if (ok) *ok = false;
    if (!m_initialized) return QString();

    QString result;
    PyGILState_STATE gstate = PyGILState_Ensure();
    PyObject* mainDict = static_cast<PyObject*>(m_mainDict);
    if (PyObject* func = borrowMainFunc(mainDict, name)) {
        QByteArray utf8 = arg.toUtf8();
        PyObject* pyArg = PyUnicode_FromStringAndSize(utf8.constData(), utf8.size());
        PyObject* r = PyObject_CallFunctionObjArgs(func, pyArg, nullptr);
        Py_XDECREF(pyArg);
        if (r) {
            if (r != Py_None && PyUnicode_Check(r)) {
                result = QString::fromUtf8(PyUnicode_AsUTF8(r));
            }
            if (ok) *ok = true;
            Py_DECREF(r);
        } else {
            PyErr_Print();
        }
    }
    PyGILState_Release(gstate);
    return result;
}

bool PyInterpreterBridge::callFunctionReturningBool(const char* name, int arg)
{
    if (!m_initialized) return false;
    bool result = false;
    PyGILState_STATE gstate = PyGILState_Ensure();
    PyObject* mainDict = static_cast<PyObject*>(m_mainDict);
    if (PyObject* func = borrowMainFunc(mainDict, name)) {
        PyObject* r = PyObject_CallFunction(func, "i", arg);
        if (r) {
            result = PyObject_IsTrue(r) == 1;
            Py_DECREF(r);
        } else {
            PyErr_Print();
        }
    }
    PyGILState_Release(gstate);
    return result;
}
