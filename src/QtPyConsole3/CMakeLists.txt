set (CURRENT_PACKAGE_NAME "QtPyConsole3")

include (${CMAKE_SOURCE_DIR}/cmake/version.cmake)
include (${CMAKE_SOURCE_DIR}/cmake/common.cmake)
include (${CMAKE_SOURCE_DIR}/cmake/workarounds.cmake)

find_package(Qt5Core NO_CMAKE_SYSTEM_PATH)

add_executable (PyConsole3 QtPython3InterpretorMainWindow.cpp QtPython3InterpretorMainWindow.h python_console3.cpp)
set_property (TARGET PyConsole3 PROPERTY AUTOMOC ON)
target_link_libraries (PyConsole3 PUBLIC QtPython3)

# INSTALL_RPATH modifie le rpath pour les libs internes au projet :
set_target_properties (PyConsole3 PROPERTIES INSTALL_RPATH_USE_LINK_PATH 1 INSTALL_RPATH ${CMAKE_PACKAGE_RPATH_DIR})

