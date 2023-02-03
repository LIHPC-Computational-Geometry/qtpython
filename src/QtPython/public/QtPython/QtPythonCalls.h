#ifndef QT_PYTHON_CALLS_H
#define QT_PYTHON_CALLS_H

#include "QtPython/QtPythonConsole.h"

/**
 * <P>
 * Module appelé par python lors de l'utilisation de la console
 * python <I>QtPythonConsole</I> en mode <I>debug</I>.
 * </P>
 * <P>
 * Ce module contient des fonctions permettant :
 * <UL>
 * <LI>Le traitement des évènements <I>Qt</I> en parallèle
 * des instructions <I>python</I>,
 * <LI>De transmettre à la console les lignes de code python exécutées
 * </UL>
 */


PyMODINIT_FUNC initQtPythonCalls ( );

void registerConsole (QtPythonConsole& console);
void unregisterConsole (QtPythonConsole& console);


#endif	// QT_PYTHON_CALLS_H
