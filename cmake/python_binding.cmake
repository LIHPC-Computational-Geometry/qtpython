# Version 0.2 (support Python 2/Python 3)

include (GNUInstallDirs)
find_package (SWIG 3 REQUIRED)
# Par défaut on utilise Python 2

# CMake v 3.* : est on en mesure de spécifier le minor de version de python demandé ???
if (USE_PYTHON_3)
	find_package (Python3 REQUIRED COMPONENTS Interpreter Development)
	set (PYTHON_BINDING_DIR ${CMAKE_INSTALL_LIBDIR}/python${Python3_VERSION_MAJOR}.${Python3_VERSION_MINOR}/site-packages/)
else (USE_PYTHON_3)
	find_package (Python2 REQUIRED COMPONENTS Interpreter Development)
	set (PYTHON_BINDING_DIR ${CMAKE_INSTALL_LIBDIR}/python${Python2_VERSION_MAJOR}.${Python2_VERSION_MINOR}/site-packages/)
endif (USE_PYTHON_3)
include (${SWIG_USE_FILE})

# Répertoire d'installation des modules (pour le RPATH) :
set (CMAKE_PYTHON_RPATH_DIR ${CMAKE_INSTALL_PREFIX}/${PYTHON_BINDING_DIR})


