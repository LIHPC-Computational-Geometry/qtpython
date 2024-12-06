#add_subdirectory (QtPython)	# Obsolète, python 2
#add_subdirectory (QtPyConsole)	# Obsolète, python 2. Bloque sur la partie moc en de rares circonstances
add_subdirectory (QtPython3)
if (BUILD_PY_CONSOLE)
	add_subdirectory (QtPyConsole3)	# Bloque sur la partie moc en de rares circonstances
endif (BUILD_PY_CONSOLE)

