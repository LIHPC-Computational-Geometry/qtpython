#
# version.cmake : version du projet
#

# Pour la bibliothèque QtPython3 :
set (QT_PYTHON_3_MAJOR_VERSION "6")
set (QT_PYTHON_3_MINOR_VERSION "5")
set (QT_PYTHON_3_RELEASE_VERSION "0")
set (QT_PYTHON_3_VERSION ${QT_PYTHON_3_MAJOR_VERSION}.${QT_PYTHON_3_MINOR_VERSION}.${QT_PYTHON_3_RELEASE_VERSION})

# Pour la bibliothèque QtPython (obsolète, Python 2) :
# On la met à la fin car elle pertube la CI
#set (QT_PYTHON_MAJOR_VERSION "6")
#set (QT_PYTHON_MINOR_VERSION "4")
#set (QT_PYTHON_RELEASE_VERSION "7")
#set (QT_PYTHON_VERSION ${QT_PYTHON_MAJOR_VERSION}.${QT_PYTHON_MINOR_VERSION}.${QT_PYTHON_RELEASE_VERSION})
