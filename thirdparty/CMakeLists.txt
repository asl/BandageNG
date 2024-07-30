include(FetchContent)

FetchContent_Declare(ogdf
  SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/ogdf"
  EXCLUDE_FROM_ALL)
FetchContent_MakeAvailable(ogdf)

set(SKIP_INSTALL_ALL ON)
set(ZLIB_COMPAT ON)
set(WITH_GTEST OFF)
set(ZLIB_ENABLE_TESTS OFF)
set(ZLIBNG_ENABLE_TESTS OFF)
FetchContent_Declare(zlib_ng
  SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/zlib-ng"
  EXCLUDE_FROM_ALL)
FetchContent_MakeAvailable(zlib_ng)
