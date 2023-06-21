set (CURRENT_PACKAGE_NAME "QtPyConsole")

find_package (GUIToolkitsVariables)

include (${CMAKE_SOURCE_DIR}/cmake/version.cmake)
include (${GUIToolkitsVariables_CMAKE_DIR}/common.cmake)
include (${GUIToolkitsVariables_CMAKE_DIR}/workarounds.cmake)
find_package(Qt5Core NO_CMAKE_SYSTEM_PATH)

add_executable (PyConsole QtPythonInterpretorMainWindow.cpp QtPythonInterpretorMainWindow.h python_console.cpp)
set_property (TARGET PyConsole PROPERTY AUTOMOC ON)
target_link_libraries (PyConsole PUBLIC QtPython)

# INSTALL_RPATH modifie le rpath pour les libs internes au projet :
set_target_properties (PyConsole PROPERTIES INSTALL_RPATH_USE_LINK_PATH 1 INSTALL_RPATH ${CMAKE_PACKAGE_RPATH_DIR})

