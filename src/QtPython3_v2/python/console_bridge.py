"""
console_bridge.py
------------------
Ce script est chargé par le C++ (PyInterpreterBridge) au démarrage, dans
l'interpréteur __main__. Il expose un ensemble de fonctions appelées depuis
le C++ via l'API C Python, et appelle en retour un petit module natif
"_pyconsole_native" (fourni par le C++) pour notifier :
  - notify_line(lineno)          : la ligne "lineno" va être exécutée
  - notify_output(kind, text)    : texte envoyé sur stdout/stderr
  - notify_exception(lineno, text) : une exception a été levée à la ligne lineno
  - notify_finished()            : l'exécution en cours (run/step/continue) est finie

Le moteur d'exécution s'appuie sur bdb.Bdb (classe mère de pdb.Pdb) : c'est
donc le véritable compilateur/interpréteur CPython qui décide de ce qui est
exécuté (indentation, blocs, boucles, exceptions, fonctions, commentaires...).
On ne réinterprète jamais le code nous-mêmes.
"""

import sys
import os
import io
import bdb
import linecache
import threading
import traceback
import rlcompleter

CONSOLE_FILENAME = "<console>"

# Rempli par init_native() avec le module natif fourni par le C++
_native = None


# ---------------------------------------------------------------------------
# Redirection de sys.stdout / sys.stderr vers le C++
# ---------------------------------------------------------------------------
class _StreamRedirector(io.TextIOBase):
    def __init__(self, kind):
        super().__init__()
        self.kind = kind  # "stdout" ou "stderr"

    def writable(self):
        return True

    def write(self, s):
        if s and _native is not None:
            _native.notify_output(self.kind, s)
        return len(s)

    def flush(self):
        pass


# ---------------------------------------------------------------------------
# Capture au niveau des descripteurs de fichier (fd 1 = stdout, fd 2 =
# stderr) du PROCESSUS. Du code C/C++ wrappé (ex: un module pybind11 qui
# fait std::cout << ... ou printf(...)) écrit directement sur le vrai fd
# 1/2 du processus -- sans capture au niveau fd, ce texte n'apparaîtrait
# jamais dans la console.
# On duplique donc les fds réels vers des tubes (pipes), lus en continu par
# des threads dédiés qui relaient chaque fragment via notify_output. Cette
# capture est activée UNIQUEMENT pendant la durée d'une exécution
# déclenchée par la console (start()/exec_already_executed()) et désactivée
# (fds restaurés) dès qu'elle se termine -- ainsi, seules les sorties
# produites par le code exécuté PAR LA CONSOLE sont récupérées, pas celles
# d'une autre partie de l'application pendant le reste du temps.
# Pendant la capture, sys.stdout/sys.stderr Python restent les objets
# standard (liés au fd 1/2, temporairement redirigés vers nos tubes) -- on
# se garde bien de les remplacer par un objet Python personnalisé séparé
# (cf. init_native ci-dessous) : TOUT (print() Python ET C/C++) passe alors
# par le MÊME tube, lu par le MÊME thread, dans l'ordre chronologique réel
# des écritures. Avoir deux chemins distincts (un appel Python synchrone
# d'un côté, un tube lu de façon asynchrone par un thread dédié de l'autre)
# faisait que les sorties C++ et Python pouvaient apparaître dans un ordre
# différent de celui où elles avaient réellement eu lieu.
# Limite connue : le stdout/stderr C++ est généralement bufferisé en mode
# "pleine mémoire tampon" (non ligne-par-ligne) dès lors qu'il ne pointe
# plus sur un terminal ; le texte peut donc n'apparaître qu'après un flush
# explicite (std::endl, std::flush, ou la fin du programme) côté code
# wrappé -- ce n'est pas quelque chose que l'on puisse forcer depuis ici.
_saved_stdout_fd = None
_saved_stderr_fd = None
_capture_depth = 0  # compteur de réentrance (imbrication par précaution)


def _enable_fd_capture():
    """Redirige fd 1/2 vers des tubes pour la durée d'une exécution.
    Retourne True si la capture est active (déjà activée par un appel
    englobant, ou tout juste démarrée), False si les vrais descripteurs de
    fichier standards ne sont pas disponibles (environnement rare)."""
    global _saved_stdout_fd, _saved_stderr_fd, _capture_depth
    if _capture_depth > 0:
        _capture_depth += 1
        return True

    try:
        _saved_stdout_fd = os.dup(1)
        _saved_stderr_fd = os.dup(2)
        stdout_r, stdout_w = os.pipe()
        stderr_r, stderr_w = os.pipe()
        os.dup2(stdout_w, 1)
        os.dup2(stderr_w, 2)
        os.close(stdout_w)
        os.close(stderr_w)
    except OSError:
        return False

    def reader(fd, kind):
        while True:
            try:
                chunk = os.read(fd, 4096)
            except OSError:
                break
            if not chunk:
                break
            if _native is not None:
                _native.notify_output(kind, chunk.decode("utf-8", errors="replace"))

    threading.Thread(target=reader, args=(stdout_r, "stdout"), daemon=True).start()
    threading.Thread(target=reader, args=(stderr_r, "stderr"), daemon=True).start()
    _capture_depth = 1
    return True


def _disable_fd_capture():
    """Restaure fd 1/2 à leur état d'avant _enable_fd_capture() (annule le
    dernier niveau d'imbrication uniquement)."""
    global _saved_stdout_fd, _saved_stderr_fd, _capture_depth
    if _capture_depth <= 0:
        return
    _capture_depth -= 1
    if _capture_depth > 0:
        return

    try:
        sys.stdout.flush()
        sys.stderr.flush()
    except Exception:
        pass
    try:
        os.dup2(_saved_stdout_fd, 1)
        os.dup2(_saved_stderr_fd, 2)
        os.close(_saved_stdout_fd)
        os.close(_saved_stderr_fd)
    except OSError:
        pass
    _saved_stdout_fd = None
    _saved_stderr_fd = None


# ---------------------------------------------------------------------------
# Validation des lignes pouvant recevoir un point d'arrêt : uniquement les
# lignes qui correspondent réellement à une instruction (bytecode), jamais
# un commentaire, une ligne vide ou la suite d'une instruction multi-lignes.
# ---------------------------------------------------------------------------
def _executable_lines(code_obj, acc=None):
    if acc is None:
        acc = set()
    try:
        for _start, _end, lineno in code_obj.co_lines():
            if lineno is not None:
                acc.add(lineno)
    except AttributeError:
        # Python < 3.10 fallback (non attendu ici, mais defensif)
        for lineno in dict(_co_lnotab_lines(code_obj)):
            acc.add(lineno)
    for const in code_obj.co_consts:
        if hasattr(const, "co_lines"):
            _executable_lines(const, acc)
    return acc


def _co_lnotab_lines(code_obj):
    import dis
    for inst in dis.get_instructions(code_obj):
        if inst.starts_line:
            yield inst.starts_line, True


# ---------------------------------------------------------------------------
# Débogueur
# ---------------------------------------------------------------------------
class ConsoleDebugger(bdb.Bdb):
    """
    Bdb gère nativement : set_break/clear_break/clear_all_breaks, set_step
    (s'arrêter à la prochaine ligne), set_continue (courir jusqu'au prochain
    point d'arrêt ou la fin). On ajoute la synchronisation avec le thread
    C++/Qt via un threading.Event.
    """

    def __init__(self):
        super().__init__()
        self.filename = CONSOLE_FILENAME
        self.mode = "normal"          # "normal" ou "debug"
        self.current_line = None
        self.valid_lines = set()      # lignes où un breakpoint est acceptable
        self._resume_event = threading.Event()
        self._pending_action = None   # "step" | "continue" | "stop"
        self._stop_requested = False
        self._exception_reported = False
        # True juste après un clic "Pas à pas" : la toute prochaine ligne
        # rencontrée, quelle qu'elle soit (point d'arrêt ou non), doit
        # provoquer une vraie pause. Remis à False dès que cette pause a
        # eu lieu, ou lors d'un "Continuer"/nouveau départ.
        self._stepping = False
        # Nombre de "pas" à exécuter silencieusement (sans notifier de
        # vraie pause) au tout début d'une exécution démarrée directement
        # via "Pas à pas" (cf. start(..., step_first=True)). Sans ça, le
        # premier clic ne ferait que "confirmer" une position déjà connue
        # (la flèche pointait déjà sur cette ligne avant le clic, par ex.
        # après un "Arrêter") sans rien exécuter, obligeant à cliquer une
        # seconde fois pour obtenir un effet visible. Avec ce compteur, un
        # seul clic sur "Pas à pas" exécute effectivement une instruction.
        self._auto_steps_remaining = 0

    # -- API appelée depuis le C++ (thread GUI) -----------------------------
    def set_mode(self, mode):
        self.mode = mode

    def request_step(self):
        self._stepping = True
        self._pending_action = "step"
        self._resume_event.set()

    def request_continue(self):
        self._stepping = False
        self._pending_action = "continue"
        self._resume_event.set()

    def request_stop(self):
        self._stop_requested = True
        self._pending_action = "stop"
        self._resume_event.set()

    # -- Points d'arrêt ------------------------------------------------------
    def try_set_break(self, lineno):
        if lineno not in self.valid_lines:
            return False
        err = self.set_break(self.filename, lineno)
        return err is None

    def try_clear_break(self, lineno):
        self.clear_break(self.filename, lineno)
        return True

    def clear_every_break(self):
        self.clear_all_breaks()

    # -- Hooks bdb ------------------------------------------------------------
    def user_line(self, frame):
        if frame.f_code.co_filename != self.filename:
            return
        lineno = frame.f_lineno
        self.current_line = lineno

        if _native is not None:
            _native.notify_line(lineno)

        if self._stop_requested:
            self.set_quit()
            return

        if self.mode == "normal":
            # Mode normal : on exécute tout ce qui peut l'être, sans jamais
            # marquer de pause.
            self.set_continue()
            return

        # Mode debug : IMPORTANT - on ne fait une vraie pause (attente d'une
        # action utilisateur) QUE si :
        #   - un pas-à-pas est en cours (l'utilisateur vient de cliquer
        #     "Pas à pas" et veut s'arrêter à la ligne suivante, quelle
        #     qu'elle soit), ou
        #   - cette ligne porte effectivement un point d'arrêt actif.
        # Dans tous les autres cas (y compris la toute première ligne d'un
        # "Run"), on continue silencieusement : "les instructions doivent
        # s'exécuter tant qu'on ne rencontre pas de point d'arrêt ou une
        # exception". Sans ce garde-fou, bdb (via reset()/set_step())
        # s'arrête par défaut à CHAQUE ligne tracée, ce qui imposait une
        # pause artificielle sur la ligne 1 et exigeait un clic "Continuer"
        # à répétition même sans point d'arrêt.
        if self._auto_steps_remaining > 0:
            # Démarrage via "Pas à pas" (step_first) : on exécute cette
            # ligne SANS pause visible (elle correspond à une position déjà
            # affichée avant même ce démarrage, ex. après un "Arrêter") --
            # la vraie pause n'aura lieu qu'à la ligne SUIVANTE.
            self._auto_steps_remaining -= 1
            self._stepping = True
            self.set_step()
            return

        if not self._stepping and not self.break_here(frame):
            self.set_continue()
            return

        # Mode debug : on informe le C++ qu'une vraie pause démarre (pour
        # activer Pas-à-pas/Continuer côté GUI -- notify_line seul ne
        # suffit pas : il peut être émis pour une ligne qui ne bloque pas,
        # cf. le garde-fou juste au-dessus), puis on attend sa décision.
        if _native is not None:
            _native.notify_paused(lineno)
        self._resume_event.clear()
        self._resume_event.wait()
        action = self._pending_action
        if action == "step":
            self.set_step()
        elif action == "continue":
            self.set_continue()
        else:
            self.set_quit()

    def user_exception(self, frame, exc_info):
        if frame.f_code.co_filename != self.filename:
            return
        etype, evalue, _etb = exc_info
        text = "".join(traceback.format_exception_only(etype, evalue)).strip()
        if _native is not None:
            _native.notify_exception(frame.f_lineno, text)
        self._exception_reported = True
        self.set_quit()

    def user_return(self, frame, return_value):
        pass


# ---------------------------------------------------------------------------
# État global du "process" console (un seul espace de noms persistant)
# ---------------------------------------------------------------------------
_debugger = ConsoleDebugger()
_console_globals = {"__name__": "__console__", "__builtins__": __builtins__}
_run_thread = None
_pending_code = None  # code compilé, prêt à démarrer via start()


def init_native(native_module):
    global _native
    _native = native_module

    # Reconfigure sys.stdout/sys.stderr en ligne-bufferisé une bonne fois
    # pour toutes (réglage indépendant de ce que fd 1/2 pointe réellement à
    # un instant donné -- il s'applique aussi bien avant qu'après une
    # redirection temporaire via _enable_fd_capture()) : par défaut, un
    # flux non lié à un terminal est bufferisé en pleine mémoire tampon, ce
    # qui retarderait l'affichage jusqu'à ce que le tampon se remplisse.
    try:
        sys.stdout.reconfigure(line_buffering=True)
        sys.stderr.reconfigure(line_buffering=True)
    except (AttributeError, ValueError):
        pass  # interpréteur trop ancien / flux non reconfigurable : tant pis


def set_mode(mode):
    _debugger.set_mode(mode)


def set_breakpoint(lineno):
    return _debugger.try_set_break(lineno)


def clear_breakpoint(lineno):
    return _debugger.try_clear_break(lineno)


def clear_all_breakpoints():
    _debugger.clear_every_break()
    return True


def _finish(stopped_early):
    if _native is not None:
        _native.notify_finished(stopped_early)


def load_code(source):
    """
    Compile `source` et calcule l'ensemble des lignes exécutables
    (valid_lines), SANS démarrer l'exécution. A appeler AVANT de poser des
    points d'arrêt (set_breakpoint) et avant start() : c'est justement
    l'ordre inverse qui provoquait le rejet systématique des points
    d'arrêt (valid_lines était encore vide au moment de leur pose).
    Retourne une chaîne vide en cas de succès, ou le message d'erreur de
    compilation sinon.
    """
    global _pending_code
    try:
        code = compile(source, CONSOLE_FILENAME, "exec")
    except SyntaxError as e:
        lineno = e.lineno or 1
        return f"SyntaxError: {e.msg} (ligne {lineno})"

    # bdb.Bdb.set_break() (et pdb en général) vérifie l'existence de la
    # ligne via linecache.getline(filename, lineno), qui lit normalement un
    # vrai fichier sur disque. Notre code vient de compile()/exec() en
    # mémoire : sans peupler linecache manuellement, TOUTE ligne serait
    # rejetée par set_break() ("Line <console>:N does not exist"), quel
    # que soit N. On enregistre donc le source dans le cache, à la façon
    # de ce que fait l'interpréteur interactif pour le code REPL.
    lines = source.splitlines(keepends=True)
    linecache.cache[CONSOLE_FILENAME] = (len(source), None, lines, CONSOLE_FILENAME)

    _pending_code = code
    _debugger.valid_lines = _executable_lines(code)
    return ""


def start(debug, step_first=False):
    """
    Démarre, dans un thread dédié, l'exécution du code précédemment chargé
    par load_code(). Les points d'arrêt doivent être posés entre
    load_code() et start().

    step_first : si vrai (mode debug uniquement), démarre directement en
    pas-à-pas -- la toute première ligne provoque une vraie pause, comme un
    clic sur "Pas à pas" l'aurait fait après un Run classique. Si faux
    (comportement par défaut, cas du bouton "Run"), l'exécution continue
    silencieusement jusqu'au premier point d'arrêt (ou la fin).
    """
    global _run_thread

    if _pending_code is None:
        return

    set_mode("debug" if debug else "normal")
    _debugger._stop_requested = False
    _debugger._exception_reported = False
    _debugger._stepping = False
    _debugger._auto_steps_remaining = 1 if (debug and step_first) else 0
    code = _pending_code

    # Capture des flux sortants scopée à la durée de CETTE exécution (voir
    # commentaire plus haut) : activée ici, désactivée dans le `finally`
    # de target() ci-dessous, une fois la session terminée (elle couvre
    # donc aussi bien toutes les pauses intermédiaires que la fin réelle).
    fd_capture_active = _enable_fd_capture()
    if not fd_capture_active:
        # Environnement sans vrais descripteurs de fichier standards
        # (rare) : repli sur la redirection Python simple -- elle ne
        # capture que print()/write() Python (pas le C/C++ de bas niveau),
        # mais reste préférable à une absence totale de sortie.
        sys.stdout = _StreamRedirector("stdout")
        sys.stderr = _StreamRedirector("stderr")

    def target():
        # Distingue, pour le C++, un arrêt VOLONTAIRE (bouton "Arrêter",
        # via request_stop()) d'une fin normale (tout le code envoyé a
        # réellement été exécuté jusqu'au bout) -- l'exécuteur C++ en a
        # besoin pour savoir jusqu'où griser le code (tout, ou seulement
        # jusqu'à la ligne où l'on s'est effectivement arrêté).
        stopped_early = False
        try:
            _debugger.reset()
            # NOTE : on n'appelle plus set_step() ici. bdb.Bdb.run() appelle
            # lui-même reset() en interne, qui remet stopframe à None -- ce
            # qui a pour effet secondaire de forcer un arrêt sur la toute
            # première ligne tracée, quel que soit le mode. C'est justement
            # ce comportement que la nouvelle logique de user_line()
            # neutralise (elle ne s'arrête que sur point d'arrêt réel ou
            # pas-à-pas actif), donc plus besoin de le préparer ici.
            _debugger.run(code, _console_globals, _console_globals)
            # IMPORTANT : bdb.Bdb.run() catche BdbQuit EN INTERNE (juste un
            # `pass`, elle ne la laisse jamais remonter jusqu'ici) -- on ne
            # peut donc pas détecter un arrêt volontaire via un `except
            # bdb.BdbQuit` autour de cet appel. On teste à la place le flag
            # positionné par request_stop() une fois run() revenue : si on a
            # explicitement demandé l'arrêt ET qu'aucune exception n'a par
            # ailleurs déjà été notifiée séparément, c'est un arrêt
            # volontaire.
            if _debugger._stop_requested and not _debugger._exception_reported:
                stopped_early = True
        except SystemExit:
            pass
        except BaseException:
            if not _debugger._exception_reported:
                etype, evalue, etb = sys.exc_info()
                text = "".join(traceback.format_exception_only(etype, evalue)).strip()
                # _debugger.current_line n'est fiable QUE si le traçage bdb
                # était encore actif au moment de l'exception (pas-à-pas en
                # cours, ou point d'arrêt présent). En mode normal (ou en
                # mode debug sans le moindre point d'arrêt), bdb désactive
                # le traçage après la première ligne pour ne pas ralentir
                # l'exécution -- current_line reste alors figé sur cette
                # première ligne, quel que soit l'endroit réel où
                # l'exception a été levée. La ligne exacte existe malgré
                # tout dans la traceback Python elle-même : on la retrouve
                # en cherchant la frame la plus profonde appartenant à
                # notre code <console>.
                lineno = _debugger.current_line or 1
                tb = etb
                while tb is not None:
                    frame = tb.tb_frame
                    if frame.f_code.co_filename == CONSOLE_FILENAME:
                        lineno = tb.tb_lineno
                    tb = tb.tb_next
                if _native is not None:
                    _native.notify_exception(lineno, text)
        finally:
            _finish(stopped_early)
            if fd_capture_active:
                _disable_fd_capture()
            else:
                sys.stdout = sys.__stdout__
                sys.stderr = sys.__stderr__

    _run_thread = threading.Thread(target=target, daemon=True)
    _run_thread.start()


def step():
    _debugger.request_step()


def cont():
    _debugger.request_continue()


def stop():
    _debugger.request_stop()


def complete(text):
    """Complétion façon readline/rlcompleter sur l'espace de noms courant."""
    completer = rlcompleter.Completer(_console_globals)
    results = []
    i = 0
    while True:
        r = completer.complete(text, i)
        if r is None:
            break
        results.append(r)
        i += 1
        if i > 200:
            break
    return results


def exec_already_executed(source):
    """
    Exécute immédiatement `source` (hors mécanisme pas-à-pas) et le
    considère comme "déjà exécuté" : utilisé pour l'insertion de code déjà
    exécuté à l'emplacement de la prochaine instruction. Ne doit être
    appelé que lorsque la console n'est pas en cours d'exécution.
    Retourne None en cas de succès, ou le texte de l'exception sinon.
    """
    fd_capture_active = _enable_fd_capture()
    if not fd_capture_active:
        sys.stdout = _StreamRedirector("stdout")
        sys.stderr = _StreamRedirector("stderr")
    try:
        code = compile(source, CONSOLE_FILENAME, "exec")
        exec(code, _console_globals, _console_globals)
        return None
    except BaseException:
        etype, evalue, _etb = sys.exc_info()
        return "".join(traceback.format_exception_only(etype, evalue)).strip()
    finally:
        if fd_capture_active:
            _disable_fd_capture()
        else:
            sys.stdout = sys.__stdout__
            sys.stderr = sys.__stderr__
