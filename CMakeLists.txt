cmake_minimum_required(VERSION 3.12)

project (QtPython CXX)

include (GNUInstallDirs)
include (cmake/version.cmake)
include (cmake/common.cmake)

set (QtPYTHON_VERSION ${QT_PYTHON_VERSION})

# Ici et pas ailleurs : CMAKE_VERBOSE_MAKEFILE
set (CMAKE_VERBOSE_MAKEFILE ON)
option (BUILD_SHARED_LIBS "Creation de bibliotheques dynamiques (defaut : ON)" ON)

add_subdirectory (src)
