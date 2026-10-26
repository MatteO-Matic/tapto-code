cmake_minimum_required(VERSION 3.16)
project(tapto-code VERSION 1.2.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

# --- libtapto ---------------------------------------------------------------
# The config store, the secret resolver, provider resolution and the three
# provider clients with the agent loop inside are libtapto's, shared with
# tapto-word and tapto-vnc. It is fetched from its own repository at a tag --
# it used to be a copy vendored here, one of five kept byte-identical by hand
# -- and it brings its own dependency pins (nlohmann_json, cpp-httplib,
# OpenSSL), exported on the `tapto::tapto` target, so nothing is repeated here.
#
# Bump LIBTAPTO_TAG to take a library change. -DLIBTAPTO_TAG=main builds
# against an unreleased library, and -DFETCHCONTENT_SOURCE_DIR_LIBTAPTO=<path>
# against a working tree, which is how a change spanning both is developed.
#
# What is NOT in the library, and is this program's own: the tool table
# (src/tools.cpp), the allow-listed commands (src/commands.cpp), the CLI and
# chat loop (src/main.cpp), and src/ui.cpp, which supplies the bodies for the
# tapto::ui functions the library declares -- a terminal status line here.
#
# The library's unit tests run under ctest with ours: this is its only build
# on MSVC, so a warning it trips here is fixed in the library and released.
set(LIBTAPTO_BUILD_TESTS ON CACHE BOOL "" FORCE)
include(FetchContent)
set(LIBTAPTO_TAG "v0.4.0" CACHE STRING "libtapto version to build against")
FetchContent_Declare(libtapto
  GIT_REPOSITORY https://github.com/centlakestefan/libtapto.git
  GIT_TAG        ${LIBTAPTO_TAG}
  GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(libtapto)

# stb: public-domain, header-only image decoding (stb_image) and PNG encoding
# (stb_image_write), which the image plugin (plugins/image) uses to turn an
# image file into the PNG it attaches for the model. The implementations are
# compiled into the plugin by defining the STB_*_IMPLEMENTATION macros there;
# the core does not link it.
#
# The stb repository has no tags (it releases by branch), so the version is
# taken from `master`, the stable release line (`dev` holds work in progress).
# It is pinned to a branch rather than a commit for now; if stb's master
# advances in a way that breaks the build, pin a specific commit here instead.
# The repo has no CMakeLists.txt, so it is populated rather than made
# available -- the CMP0169 policy (CMake 3.31+) keeps the direct Populate call
# from warning about itself.
if(POLICY CMP0169)
  cmake_policy(SET CMP0169 OLD)
endif()
FetchContent_Declare(stb
  GIT_REPOSITORY https://github.com/nothings/stb.git
  GIT_TAG        master
)
FetchContent_GetProperties(stb)
if(NOT stb_POPULATED)
  FetchContent_Populate(stb)
endif()
add_library(stb_headers INTERFACE)
target_include_directories(stb_headers INTERFACE "${stb_SOURCE_DIR}")

# --- Version stamp ----------------------------------------------------------
# Puts the version and the originating commit into the binary, so `version` and
# the first line of every trace name the build that produced them. Regenerated
# on each build, not at configure time; see cmake/GitVersion.cmake.
set(TAPTO_GENERATED_DIR "${CMAKE_BINARY_DIR}/generated")
set(TAPTO_VERSION_HEADER "${TAPTO_GENERATED_DIR}/tapto/version.h")

set(TAPTO_VERSION_ARGS
  -DSRC_DIR=${CMAKE_CURRENT_SOURCE_DIR}
  -DIN_FILE=${CMAKE_CURRENT_SOURCE_DIR}/cmake/version.h.in
  -DOUT_FILE=${TAPTO_VERSION_HEADER}
  -DPROJECT_VERSION=${PROJECT_VERSION}
  -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/GitVersion.cmake
)

# Once at configure time as well, so the header exists before anything asks an
# IDE or a generator to parse the sources that include it.
execute_process(COMMAND ${CMAKE_COMMAND} ${TAPTO_VERSION_ARGS})

add_custom_target(tapto_version ALL
  COMMAND ${CMAKE_COMMAND} ${TAPTO_VERSION_ARGS}
  BYPRODUCTS ${TAPTO_VERSION_HEADER}
  COMMENT "Stamping version ${PROJECT_VERSION}"
  VERBATIM
)

function(tapto_code_warnings target)
  if(MSVC)
    target_compile_options(${target} PRIVATE /W4 /EHsc /utf-8)
  else()
    target_compile_options(${target} PRIVATE -Wall -Wextra)
  endif()
endfunction()

# --- Target -----------------------------------------------------------------
add_executable(tapto-code
  src/main.cpp
  src/commands.cpp
  src/memory.cpp
  src/overflow.cpp
  src/plugins.cpp
  src/prompt.cpp
  src/session.cpp
  src/tools.cpp
  src/ui.cpp
)
target_include_directories(tapto-code PRIVATE include "${TAPTO_GENERATED_DIR}")
add_dependencies(tapto-code tapto_version)
# The library exports nlohmann_json, httplib, OpenSSL, Threads and, on Windows,
# advapi32/crypt32, together with CPPHTTPLIB_OPENSSL_SUPPORT.
target_link_libraries(tapto-code PRIVATE tapto::tapto)
# The plugin loader checks a module's Authenticode signature (WinVerifyTrust)
# on Windows, and loads it with dlopen elsewhere.
if(WIN32)
  target_link_libraries(tapto-code PRIVATE wintrust)
else()
  target_link_libraries(tapto-code PRIVATE ${CMAKE_DL_LIBS})
endif()
tapto_code_warnings(tapto-code)

install(TARGETS tapto-code RUNTIME DESTINATION bin)

# --- Plugins ----------------------------------------------------------------
# Modules beside the program, in a `plugins` folder next to tapto-code.exe,
# which is the only place the loader (src/plugins.cpp) looks. They are built
# there in the build tree too, so a Debug build in build/ finds them. A plugin
# links only libtapto's headers (tapto/plugin.h, tapto/scom.h), never the
# library, and whatever it needs of its own.
function(tapto_code_plugin target name)
  add_library(${target} MODULE ${ARGN})
  target_include_directories(${target} PRIVATE "${libtapto_SOURCE_DIR}/include")
  set_target_properties(${target} PROPERTIES
    PREFIX "" OUTPUT_NAME ${name} CXX_VISIBILITY_PRESET hidden
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins")
  # Multi-config generators would otherwise add a per-config subfolder.
  foreach(_cfg IN ITEMS DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
    set_target_properties(${target} PROPERTIES
      LIBRARY_OUTPUT_DIRECTORY_${_cfg} "${CMAKE_BINARY_DIR}/plugins")
  endforeach()
  tapto_code_warnings(${target})
  install(TARGETS ${target} LIBRARY DESTINATION bin/plugins)
  add_dependencies(tapto-code ${target})
endfunction()

# image: image_read, for a model that can see.
tapto_code_plugin(tapto-plugin-image image plugins/image/image.cpp)
target_link_libraries(tapto-plugin-image PRIVATE stb_headers nlohmann_json::nlohmann_json)

# --- Tests ------------------------------------------------------------------
# Plain executables with assertions of their own, so `ctest` needs no test
# framework fetched or installed. Run with: ctest --test-dir build --output-on-failure
#
# The library's own test (test-libtapto, covering the config store, secrets,
# the /compact trimmer and more) is registered by libtapto's own CMakeLists.
enable_testing()

# The text editor tool's line-ending behaviour and the status-line labels.
# Links the library for config/paths and the library's do-nothing ui: a static
# library only pulls the objects a test references, and the day one of them
# reaches provider.cpp it would otherwise fail to link on tapto::ui::print_error.
add_executable(test-edits
  tests/test_edits.cpp
  src/tools.cpp
  src/commands.cpp
  src/memory.cpp
  src/overflow.cpp
  "${libtapto_SOURCE_DIR}/test/ui_null.cpp"
)
target_include_directories(test-edits PRIVATE include)
target_link_libraries(test-edits PRIVATE tapto::tapto)
tapto_code_warnings(test-edits)

add_test(NAME edits COMMAND test-edits)
# Config store: set/unset/save must preserve comments, blank lines, and ordering
# a user added by hand, rather than rebuilding the file from its key/value table.
# Config now lives in libtapto (src/config.cpp moved there in the re-org), so
# this links the library instead of compiling the source in; the /compact
# trimmer test was folded into libtapto's test-libtapto.
add_executable(test-config
  tests/test_config.cpp
)
target_include_directories(test-config PRIVATE include)
target_link_libraries(test-config PRIVATE tapto::tapto)
tapto_code_warnings(test-config)

add_test(NAME config COMMAND test-config)

# System prompt composition: system-prompt(-file) and the -append keys, and
# how a prompt file is found, read and, when unusable, skipped.
add_executable(test-prompt
  tests/test_prompt.cpp
  src/prompt.cpp
)
target_include_directories(test-prompt PRIVATE include)
target_link_libraries(test-prompt PRIVATE tapto::tapto)
tapto_code_warnings(test-prompt)

add_test(NAME prompt COMMAND test-prompt)

# The saved conversation behind --resume: what a save writes reads back the
# same, and a damaged or foreign file is refused rather than half-loaded.
add_executable(test-session
  tests/test_session.cpp
  src/session.cpp
)
target_include_directories(test-session PRIVATE include)
target_link_libraries(test-session PRIVATE tapto::tapto)
tapto_code_warnings(test-session)

add_test(NAME session COMMAND test-session)

# The project's memory file: where it lives (beside the local config, under the
# same encoded working directory), who may reach it (the editor tool alone, by
# a spelling that resolves to it -- no other tool, no command cwd or %p), and
# what the system prompt carries of it (the first 50 lines, the index).
add_executable(test-memory
  tests/test_memory.cpp
  src/tools.cpp
  src/commands.cpp
  src/memory.cpp
  src/overflow.cpp
  "${libtapto_SOURCE_DIR}/test/ui_null.cpp"
)
target_include_directories(test-memory PRIVATE include)
target_link_libraries(test-memory PRIVATE tapto::tapto)
tapto_code_warnings(test-memory)

add_test(NAME memory COMMAND test-memory)

# Tool output too long for the conversation: the head and tail inline, the
# whole of it in the session's @tmp folder, which the model can read and
# nothing can write.
add_executable(test-overflow
  tests/test_overflow.cpp
  src/tools.cpp
  src/commands.cpp
  src/memory.cpp
  src/overflow.cpp
  "${libtapto_SOURCE_DIR}/test/ui_null.cpp"
)
target_include_directories(test-overflow PRIVATE include)
target_link_libraries(test-overflow PRIVATE tapto::tapto)
tapto_code_warnings(test-overflow)

add_test(NAME overflow COMMAND test-overflow)

# The plugin loader (src/plugins.cpp) with a real module: tests/plugin/probe.cpp,
# built as it is and with a misnamed tool, and then the image plugin. A plugin
# links only libtapto's headers (tapto/plugin.h, tapto/scom.h), never the
# library. The test loads modules built moments ago, so it compiles the loader
# without the signature check a Release build otherwise makes. stb builds the
# test images and decodes what the image plugin attached.
foreach(_probe IN ITEMS probe probe-misnamed)
  add_library(tapto-${_probe} MODULE tests/plugin/probe.cpp)
  target_include_directories(tapto-${_probe} PRIVATE "${libtapto_SOURCE_DIR}/include")
  set_target_properties(tapto-${_probe} PROPERTIES PREFIX "" CXX_VISIBILITY_PRESET hidden)
  tapto_code_warnings(tapto-${_probe})
endforeach()
target_compile_definitions(tapto-probe-misnamed PRIVATE PROBE_BAD_NAME)

add_executable(test-plugins
  tests/test_plugins.cpp
  src/plugins.cpp
  src/tools.cpp
  src/commands.cpp
  src/memory.cpp
  src/overflow.cpp
  "${libtapto_SOURCE_DIR}/test/ui_null.cpp"
)
target_include_directories(test-plugins PRIVATE include)
target_compile_definitions(test-plugins PRIVATE TAPTO_PLUGINS_ALLOW_UNSIGNED)
target_link_libraries(test-plugins PRIVATE tapto::tapto stb_headers)
if(WIN32)
  target_link_libraries(test-plugins PRIVATE wintrust)
else()
  target_link_libraries(test-plugins PRIVATE ${CMAKE_DL_LIBS})
endif()
add_dependencies(test-plugins tapto-probe tapto-probe-misnamed tapto-plugin-image)
tapto_code_warnings(test-plugins)

add_test(NAME plugins COMMAND test-plugins
  $<TARGET_FILE:tapto-probe> $<TARGET_FILE:tapto-probe-misnamed>
  $<TARGET_FILE:tapto-plugin-image>)

# Markdown table rendering: wrapping (not clipping), alignment, inline styling
# and CJK width are exercised at explicit widths via render_markdown (pure,
# no stdout).
add_executable(test-ui
  tests/test_ui.cpp
  src/ui.cpp
)
target_include_directories(test-ui PRIVATE include)
target_link_libraries(test-ui PRIVATE tapto::tapto)
tapto_code_warnings(test-ui)

add_test(NAME ui COMMAND test-ui)
