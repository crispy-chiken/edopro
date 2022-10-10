#----------------------------------------------------------------
# Generated CMake target import file for configuration "Debug".
#----------------------------------------------------------------

# Commands may need to know the format version.
set(CMAKE_IMPORT_FILE_VERSION 1)

# Import target "unofficial::git2::git2" for configuration "Debug"
set_property(TARGET unofficial::git2::git2 APPEND PROPERTY IMPORTED_CONFIGURATIONS DEBUG)
set_target_properties(unofficial::git2::git2 PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_DEBUG "C;RC"
  IMPORTED_LOCATION_DEBUG "${_IMPORT_PREFIX}/debug/lib/git2.lib"
  )

list(APPEND _cmake_import_check_targets unofficial::git2::git2 )
list(APPEND _cmake_import_check_files_for_unofficial::git2::git2 "${_IMPORT_PREFIX}/debug/lib/git2.lib" )

# Commands beyond this point should not need to know the version.
set(CMAKE_IMPORT_FILE_VERSION)
