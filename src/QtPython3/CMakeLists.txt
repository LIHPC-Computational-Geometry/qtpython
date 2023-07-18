set (CURRENT_PACKAGE_NAME "QtPython3")
set (CURRENT_PACKAGE_MACRO_NAME "QT_PYTHON_3")

find_package (GUIToolkitsVariables)

include (${CMAKE_SOURCE_DIR}/cmake/version.cmake)
include (${GUIToolkitsVariables_CMAKE_DIR}/common.cmake)
include (${GUIToolkitsVariables_CMAKE_DIR}/python_binding.cmake)
include (${GUIToolkitsVariables_CMAKE_DIR}/workarounds.cmake)

# Le répertoire des scripts python :
set (QT_PYTHON_SCRIPTS_DIR "${CMAKE_INSTALL_PREFIX}/${PYTHON_BINDING_DIR}")

find_package (QtUtil 6 REQUIRED)
find_package (PythonUtil 6 REQUIRED)
find_package(Qt5Core NO_CMAKE_SYSTEM_PATH)	# In order to enable moc ...

file (GLOB HEADERS public/${CURRENT_PACKAGE_NAME}/*.h)
file (GLOB CPP_SOURCES *.cpp *.qrc)

# On ajoute les en-têtes aux sources. C'est utile pour cmake dans certains cas,
# par exemple lorsqu'ils doivent être pré-processés (moc, ...).
add_library (QtPython3 ${CPP_SOURCES} ${HEADERS})
set_property (TARGET QtPython3 PROPERTY AUTOMOC ON)
set_property (TARGET QtPython3 PROPERTY AUTORCC ON)
set (ALL_TARGETS QtPython3)
set_property (TARGET QtPython3 PROPERTY VERSION ${QT_PYTHON_3_VERSION})
set_property (TARGET QtPython3 PROPERTY SOVERSION ${QT_PYTHON_3_MAJOR_VERSION})
set (QT_LIB_DEPENDENCIES Qt5::Widgets Qt5::Gui Qt5::Core)
set (QT_INC_DEPENDENCIES ${Qt5Gui_INCLUDE_DIRS} ${Qt5Core_INCLUDE_DIRS})
# Rem : en Qt v 4.* on utilise -DUSE_QT_WEBKIT
set (QT_PYTHON_3_PUBLIC_FLAGS -DQT_5)
set (QT_PYTHON_3_PRIVATE_FLAGS -DQT_PYTHON_VERSION="${QT_PYTHON_3_VERSION}")

# REM : on avait historiquement l'instruction suivante, pour contourner un bogue icpc sur plateforme Bull :
# SET_SOURCE_FILES_PROPERTIES(${GSCC_CMPNTDIR}/QtNetworkFileDialog.cpp PROPERTIES COMPILE_FLAGS -O0)

target_include_directories (QtPython3 PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/public>$<INSTALL_INTERFACE:${CMAKE_INSTALL_PREFIX}/include>)
target_compile_definitions (QtPython3 PUBLIC ${QT_PYTHON_3_PUBLIC_FLAGS})
target_compile_definitions (QtPython3 PRIVATE ${QT_PYTHON_3_PRIVATE_FLAGS})
target_compile_options (QtPython3 PRIVATE ${SHARED_CFLAGS})	# Requested by Qt ...
target_include_directories (QtPython3 PUBLIC ${QT_INC_DEPENDENCIES})
target_link_libraries (QtPython3 PUBLIC QtUtil::QtUtil PythonUtil::PythonUtil)
target_include_directories (QtPython3 PUBLIC ${QT_INC_DEPENDENCIES} ${Python3_INCLUDE_DIRS})
# Etre capable une fois installée de retrouver QtUtil, TkUtil, Qt5*, ... :
# (Rem : en son absence on a Set runtime path of "/tmp/pignerol/install/lib/libQtPython3.so.5.0.0" to "") ...
set_target_properties (QtPython3 PROPERTIES INSTALL_RPATH_USE_LINK_PATH 1)

# INSTALLATION :
include(CMakePackageConfigHelpers)
# Génération du fichier contenant les informations sur la distribution :
configure_file (cmake/QtPython3Distribution.h.in  ${CMAKE_CURRENT_BINARY_DIR}/QtPython3Distribution.tmp)
configure_file (${CMAKE_CURRENT_BINARY_DIR}/QtPython3Distribution.tmp  ${CMAKE_CURRENT_BINARY_DIR}/QtPython3Distribution.h)

# ConfigPackageLocation : c'est plus ou moins standardisé, le défaut étant lib/cmake.
# On nos recommande très vivement d'utiliser ce répertoire afin de limiter les soucis par la suite,
# notamment au niveau des vues.
set (ConfigPackageLocation ${CMAKE_CMAKE_DIR})

# La génération des règles d'installation :
install (TARGETS ${ALL_TARGETS} EXPORT ${CMAKE_PACKAGE_TARGETS_NAME}
         ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
         LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
         RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
         INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

# Recopie des en-têtes dans le répertoire d'installation :
install(FILES ${HEADERS} DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/${CURRENT_PACKAGE_NAME})
# Dont QtPython3Distribution.h généré plus haut :
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/QtPython3Distribution.h DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/${CURRENT_PACKAGE_NAME})

# Recopie des fichiers python indispensable au bon fonctionnement de la console :
file (GLOB QT_PYTHON_SCRIPTS "${CMAKE_SOURCE_DIR}/src/${CMAKE_ETC_DIR_NAME}/python3/*.py")
# Recopie dans le build tree :
foreach (qt_python_script ${QT_PYTHON_SCRIPTS})
	get_filename_component (SCRIPT_NAME ${qt_python_script} NAME)
	configure_file (${qt_python_script} "${CMAKE_BINARY_DIR}/src/${CMAKE_ETC_DIR_NAME}/python3/${SCRIPT_NAME}" COPYONLY)
endforeach (qt_python_script)
# A l'installation recopie dans le répertoire d'installation :
install (FILES ${QT_PYTHON_SCRIPTS} DESTINATION "${QT_PYTHON_SCRIPTS_DIR}")

# Le fichier contenant les informations de version de ce module :
write_basic_package_version_file(${CMAKE_PACKAGE_VERSION_FILE} VERSION ${QT_PYTHON_3_VERSION} COMPATIBILITY SameMajorVersion)
export (EXPORT ${CMAKE_PACKAGE_TARGETS_NAME} FILE ${CMAKE_PACKAGE_TARGETS_FILE} NAMESPACE ${CMAKE_MODULE_NAMESPACE})

# Utilisation par d'autres modules de QtPython3 :
set (INCLUDE_INSTALL_DIR include/)		# QtPython3_INCLUDE_DIR avec QtPython3Config.cmake.in
set (LIB_INSTALL_DIR ${CMAKE_INSTALL_LIBDIR})	# QtPython3_LIB_DIR avec QtPython3Config.cmake.in
set (RUNTIME_INSTALL_DIR ${PYTHON_BINDING_DIR})	# QtPython3_RUNTIME_DIR avec QtPython3Config.cmake.in
# Création du fichier QtPython3Config.cmake qui définira QtPython3_INCLUDE_DIR, QtPython3_LIB_DIR et QtPython3_BIN_DIR :
configure_package_config_file (cmake/${CMAKE_PACKAGE_CONFIG_NAME}.cmake.in ${CMAKE_PACKAGE_CONFIG_FILE}
	INSTALL_DESTINATION ${CMAKE_CMAKE_DIR} PATH_VARS INCLUDE_INSTALL_DIR LIB_INSTALL_DIR RUNTIME_INSTALL_DIR)

# Copie du fichier QtPython3Targets.cmake dans prefix/lib/QtPython3/cmake :
install (EXPORT ${CMAKE_PACKAGE_TARGETS_NAME}
	FILE ${CMAKE_PACKAGE_TARGETS_BASENAME} NAMESPACE ${CMAKE_MODULE_NAMESPACE}
	DESTINATION ${ConfigPackageLocation})
# Copie des fichiers QtPython3Config.cmake et QtPython3ConfigVersion.cmake dans prefix/lib/QtPython3/cmake :
install (FILES ${CMAKE_PACKAGE_CONFIG_FILE} ${CMAKE_PACKAGE_VERSION_FILE} DESTINATION ${ConfigPackageLocation})
