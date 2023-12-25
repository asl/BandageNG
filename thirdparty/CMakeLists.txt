include(FetchContent)

FetchContent_Declare(ogdf
  SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/ogdf")
FetchContent_GetProperties(ogdf)
if(NOT ogdf_POPULATED)
  FetchContent_Populate(ogdf)
  add_subdirectory(${ogdf_SOURCE_DIR} ${ogdf_BINARY_DIR} EXCLUDE_FROM_ALL)
endif()

FetchContent_Declare(zlib_ng
  SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/zlib-ng")
FetchContent_GetProperties(zlib_ng)
if (NOT zlib_ng_POPULATED)
  set(SKIP_INSTALL_ALL ON)
  set(ZLIB_COMPAT ON)
  set(WITH_GTEST OFF)
  FetchContent_Populate(zlib_ng)
  add_subdirectory(${zlib_ng_SOURCE_DIR} ${zlib_ng_BINARY_DIR} EXCLUDE_FROM_ALL)
endif()
