include(FetchContent)

FetchContent_Declare(ogdf
  SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/ogdf"
  EXCLUDE_FROM_ALL)
FetchContent_MakeAvailable(ogdf)

# zlib-ng does not support universal builds,
# resort to system one
if (CMAKE_OSX_ARCHITECTURES)
  find_package(ZLIB REQUIRED)
  set(bandage_zlib ${ZLIB_LIBRARIES} PARENT_SCOPE)
else()
  set(SKIP_INSTALL_ALL ON)
  set(ZLIB_COMPAT ON)
  set(WITH_GTEST OFF)
  set(ZLIB_ENABLE_TESTS OFF)
  set(ZLIBNG_ENABLE_TESTS OFF)
  FetchContent_Declare(zlib_ng
    SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/zlib-ng"
    EXCLUDE_FROM_ALL)
  FetchContent_MakeAvailable(zlib_ng)
  set(bandage_zlib zlibstatic PARENT_SCOPE)
endif()

include_directories(.)
add_subdirectory(llvm)
