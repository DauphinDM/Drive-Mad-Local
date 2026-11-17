get_filename_component(_dm_prefix "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)
if(NOT TARGET DriveMad::SDK)
  add_library(DriveMad::SDK INTERFACE IMPORTED)
  set_target_properties(DriveMad::SDK PROPERTIES
    INTERFACE_INCLUDE_DIRECTORIES "${_dm_prefix}/include")
endif()
unset(_dm_prefix)
