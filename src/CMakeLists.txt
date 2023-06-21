if (USE_PYTHON_3)
    add_subdirectory (QtPython3)
    if (BUILD_PY_CONSOLE)
		add_subdirectory (QtPyConsole3)	# Bloque sur la partie moc en de rares circonstances
	endif (BUILD_PY_CONSOLE)
elseif (USE_PYTHON_2)
    add_subdirectory (QtPython)
    if (BUILD_PY_CONSOLE)
		add_subdirectory (QtPyConsole)	# Bloque sur la partie moc en de rares circonstances
    endif (BUILD_PY_CONSOLE)
else ( )
	message (STATUS "========================================= VERSION DE PYTHON NON DEFINIE =========================================")
	message (FATAL_ERROR "==> UTILISEZ -DUSE_PYTHON_2 OU -DUSE_PYTHON_3 A LA LIGNE DE COMMANDE")
endif (USE_PYTHON_3)

