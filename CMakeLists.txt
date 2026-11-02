cmake_minimum_required(VERSION 3.19)
project(hypr-alt-tab VERSION "0.1.0")

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_compile_definitions(WLR_USE_UNSTABLE)
add_compile_options(
  -g
  -Wall
  -Wextra
  -Wno-unused-parameter
  -Wno-unused-value
  -Wno-missing-field-initializers
  -Wno-narrowing
  --no-gnu-unique
)

if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
  add_compile_options(-fno-gnu-unique)
endif()

if(CMAKE_EXPORT_COMPILE_COMMANDS)
  set(CMAKE_CXX_STANDARD_INCLUDE_DIRECTORIES ${CMAKE_CXX_IMPLICIT_INCLUDE_DIRECTORIES})
endif()

find_package(PkgConfig REQUIRED)
pkg_check_modules(HYPR_DEPS REQUIRED hyprland pixman-1 libdrm pangocairo xkbcommon lua5.5)

add_library(hypr-alt-tab SHARED
  src/main.cpp
  src/mru.cpp
  src/session.cpp
  src/layout.cpp
  src/preview.cpp
  src/overlay.cpp
  src/config.cpp
)

target_include_directories(hypr-alt-tab PRIVATE ${HYPR_DEPS_INCLUDE_DIRS} src)

foreach(inc ${HYPR_DEPS_INCLUDE_DIRS})
  if(EXISTS "${inc}/hyprland/src")
    target_include_directories(hypr-alt-tab PRIVATE "${inc}/hyprland/src")
  endif()
endforeach()

target_link_directories(hypr-alt-tab PRIVATE ${HYPR_DEPS_LIBRARY_DIRS})
target_link_libraries(hypr-alt-tab PRIVATE ${HYPR_DEPS_LIBRARIES})

set_target_properties(hypr-alt-tab PROPERTIES
  PREFIX "lib"
  OUTPUT_NAME "hypr-alt-tab"
)

include(CTest)
if(BUILD_TESTING)
  add_executable(layout_test src/layout_test.cpp src/layout.cpp)
  target_include_directories(layout_test PRIVATE src)
  add_test(NAME layout_test COMMAND layout_test)
endif()

install(TARGETS hypr-alt-tab LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR})
