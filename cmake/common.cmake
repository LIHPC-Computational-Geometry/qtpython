#
# common.cmake : instructions de compilation partagées par tous les CMakeLists.txt.
# include (version.cmake) ici ne fonctionnenent pas. Why ??? Ce fichier est déjà un
#fichier déjà inclus ???
# => include version.cmake avant celui-ci.
#

include (${CMAKE_SOURCE_DIR}/cmake/organization.cmake)


# Compilation :
add_definitions (-DQT_PYTHON_VERSION="${QT_PYTHON_VERSION}")
add_definitions (-DQT_PYTHON_3_VERSION="${QT_PYTHON_3_VERSION}")
set (MANDATORY_CXX_OPTIONS -DMULTITHREADED_APPLICATION)

# Edition des liens :
# A l'installation les RPATHS utilisés seront ceux spécifiés ci-dessous (liste
# de répertoires séparés par des ;) :
#set (CMAKE_INSTALL_RPATH ${CMAKE_INSTALL_PREFIX}/${MT_INSTALL_SHLIB_DIR})
#set (CMAKE_BUILD_WITH_INSTALL_RPATH ON)
# CMAKE_SKIP_BUILD_RPATH : avoir le rpath dans l'arborescence de developpement
set (CMAKE_SKIP_BUILD_RPATH OFF)
set (CMAKE_SKIP_RPATH OFF)

set (CMAKE_SKIP_INSTALL_RPATH OFF)
#set (CMAKE_INSTALL_RPATH_USE_LINK_PATH ON)

# ATTENTION : enleve le build tree du rpath :
#set (CMAKE_BUILD_WITH_INSTALL_RPATH ON)
