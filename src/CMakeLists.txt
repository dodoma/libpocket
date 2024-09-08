# The Flutter tooling requires that developers have CMake 3.10 or later
# installed. You should not increase this version, as doing so will cause
# the plugin to fail to compile for some customers of the plugin.
cmake_minimum_required(VERSION 3.10)

project(libmoc_library VERSION 0.0.1 LANGUAGES C)

add_library(libmoc SHARED
  "libmoc.c"
  "pocket.c"
  "mnet.c"
  "mfile.c"
)

set(MOC_PUBLIC_HEADERS
    "libmoc.h"
    "mnet.h"
    "mfile.h"
)

set_target_properties(libmoc PROPERTIES
  PUBLIC_HEADER "${MOC_PUBLIC_HEADERS}"
  OUTPUT_NAME "libmoc"
)

target_compile_definitions(libmoc PUBLIC DART_SHARED_LIB)

find_library( # Defines the name of the path variable that stores the
              # location of the NDK library.
              log-lib

              # Specifies the name of the NDK library that
              # CMake needs to locate.
              log )

# Links your native library against one or more other native libraries.
target_link_libraries( # Specifies the target library.
                       libmoc

                       # Links the log library to the target library.
                       ${log-lib} )
