include(FetchContent)

FetchContent_Declare(ogdf
  SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/ogdf")
FetchContent_GetProperties(ogdf)
if(NOT ogdf_POPULATED)
  FetchContent_Populate(ogdf)
  add_subdirectory(${ogdf_SOURCE_DIR} ${ogdf_BINARY_DIR} EXCLUDE_FROM_ALL)
endif()
