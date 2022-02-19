############################################################################
# Copyright (c) 2018 Saint Petersburg State University
# All Rights Reserved
# See file LICENSE for details.
############################################################################

project(gfa1 C)

include_directories(include)

# Create libraries.
find_package(ZLIB REQUIRED)
add_library(gfa1 STATIC src/gfa.c)
target_link_libraries(gfa1 ${ZLIB_LIBRARIES})
