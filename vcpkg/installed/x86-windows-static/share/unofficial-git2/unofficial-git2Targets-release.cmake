#----------------------------------------------------------------
# Generated CMake target import file for configuration "Release".
#----------------------------------------------------------------

# Commands may need to know the format version.
set(CMAKE_IMPORT_FILE_VERSION 1)

# Import target "unofficial::git2::git2" for configuration "Release"
set_property(TARGET unofficial::git2::git2 APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(unofficial::git2::git2 PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "C;RC"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/git2.lib"
  )

list(APPEND _cmake_import_check_targets unofficial::git2::git2 )
list(APPEND _cmake_import_check_files_for_unofficial::git2::git2 "${_IMPORT_PREFIX}/lib/git2.lib" )

# Commands beyond this point should not need to know the version.
set(CMAKE_IMPORT_FILE_VERSION)
