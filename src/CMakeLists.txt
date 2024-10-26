# The Flutter tooling requires that developers have CMake 3.10 or later
# installed. You should not increase this version, as doing so will cause
# the plugin to fail to compile for some customers of the plugin.
cmake_minimum_required(VERSION 3.10)

project(libmoc_library VERSION 0.0.1 LANGUAGES C)

add_library(libmoc SHARED
  "libmoc.c"
  "pocket.c"
  "domme.c"
  "mnet.c"
  "mfile.c"
  "packet.c"
  "callback.c"
  "client.c"
  "binary.c"
  "server.c"
  "timer.c"
  "omusic.c"
)

set(MOC_PUBLIC_HEADERS
    "libmoc.h"
    "doome.h"
    "mnet.h"
    "mfile.h"
    "packet.h"
    "callback.h"
    "client.h"
    "binary.h"
    "server.h"
    "timer.h"
    "omusic.h"
)

set_target_properties(libmoc PROPERTIES
  PUBLIC_HEADER "${MOC_PUBLIC_HEADERS}"
  OUTPUT_NAME "libmoc"
)

target_compile_definitions(libmoc PUBLIC DART_SHARED_LIB)

set(REEF_SOURCE_DIR "C:\\Users\\DELL\\Desktop\\avm\\libreef\\src\\reef")
target_include_directories(libmoc PUBLIC ${REEF_SOURCE_DIR}\\include)

find_library( # Defines the name of the path variable that stores the
              # location of the NDK library.
              log-lib

              # Specifies the name of the NDK library that
              # CMake needs to locate.
              log )

find_library(
  reef-lib
  libreef
)
# Links your native library against one or more other native libraries.
target_link_libraries( # Specifies the target library.
                       libmoc

                      ${reef-lib}
                      ${log-lib} )
