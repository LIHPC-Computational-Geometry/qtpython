if (USE_PYTHON_3)
    add_subdirectory (QtPython3)
    if (BUILD_PY_CONSOLE)
		add_subdirectory (QtPyConsole3)	# Bloque sur la partie moc en de rares circonstances
	endif (BUILD_PY_CONSOLE)
else (USE_PYTHON_3) # => Python 3 and upper
    add_subdirectory (QtPython)
    if (BUILD_PY_CONSOLE)
		add_subdirectory (QtPyConsole)	# Bloque sur la partie moc en de rares circonstances
    endif (BUILD_PY_CONSOLE)
endif (USE_PYTHON_3)

