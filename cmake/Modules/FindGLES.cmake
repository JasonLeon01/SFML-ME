#
# Try to find GLES library and include path.
# Once done this will define
#
# GLES_FOUND
# GLES_INCLUDE_DIR
# GLES_LIBRARY
#

find_path(GLES_INCLUDE_DIR GLES2/gl2.h PATHS ${FIND_SFML_PATHS} PATH_SUFFIXES include)
find_library(GLES_LIBRARY NAMES GLESv2 libGLESv2 PATHS ${FIND_SFML_PATHS} PATH_SUFFIXES lib)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(GLES DEFAULT_MSG GLES_LIBRARY GLES_INCLUDE_DIR)

add_library(GLES::GLES IMPORTED UNKNOWN)
set_target_properties(GLES::GLES PROPERTIES
    INTERFACE_INCLUDE_DIRECTORIES ${GLES_INCLUDE_DIR}
    IMPORTED_LOCATION ${GLES_LIBRARY})
