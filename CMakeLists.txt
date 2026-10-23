cmake_minimum_required(VERSION 3.21)
# Static MSVC runtime (/MT) to match the vcpkg x64-windows-static triplet:
# the result is a single SeedFlow.exe without DLLs.
if(POLICY CMP0091)
  cmake_policy(SET CMP0091 NEW)
endif()
if(POLICY CMP0167)
  cmake_policy(SET CMP0167 NEW)
endif()

option(SF_STATIC_RUNTIME "Link the static MSVC runtime (static vcpkg triplet)" ON)
if(SF_STATIC_RUNTIME)
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
else()
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
endif()

project(SeedFlow VERSION 0.8.7.4 LANGUAGES CXX RC)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(LibtorrentRasterbar CONFIG REQUIRED)
get_target_property(SF_TORRENT_DEFINITIONS LibtorrentRasterbar::torrent-rasterbar INTERFACE_COMPILE_DEFINITIONS)
if(SF_STATIC_RUNTIME AND SF_TORRENT_DEFINITIONS MATCHES "TORRENT_LINKING_SHARED")
  message(FATAL_ERROR "Static build requested, but libtorrent is shared. Use x64-windows-static or SF_STATIC_RUNTIME=OFF.")
endif()

# The resource script embeds these files: rebuild it when they change
# (MSBuild tracks them itself; Makefile/Ninja need the explicit list).
file(GLOB SF_RC_INPUTS CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/res/*.manifest"
  "${CMAKE_CURRENT_SOURCE_DIR}/res/*.ico" "${CMAKE_CURRENT_SOURCE_DIR}/res/icons/*.png")
set_property(SOURCE res/SeedFlow.rc APPEND PROPERTY OBJECT_DEPENDS ${SF_RC_INPUTS} "${CMAKE_CURRENT_SOURCE_DIR}/src/version.h")

add_executable(SeedFlow WIN32
  src/mainwindow.cpp
  src/dialogs.cpp
  src/engine.cpp
  src/settings.cpp
  src/netcheck.cpp
  src/icons.cpp
  src/speed_graph.cpp
  src/paint.cpp
  src/creator.cpp
  src/util.cpp
  res/SeedFlow.rc
)
target_compile_definitions(SeedFlow PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX _WIN32_WINNT=0x0A00)
target_link_libraries(SeedFlow PRIVATE
  LibtorrentRasterbar::torrent-rasterbar
  comctl32 shlwapi shell32 ole32 uuid uxtheme winhttp iphlpapi ws2_32 psapi)

if(MSVC)
  target_compile_options(SeedFlow PRIVATE "$<$<COMPILE_LANGUAGE:CXX>:/utf-8;/W4;/permissive-;/EHsc;/MP>")
  set_source_files_properties(res/SeedFlow.rc PROPERTIES COMPILE_FLAGS "/c65001")
  # the manifest is embedded by the resource script (res/SeedFlow.manifest)
  target_link_options(SeedFlow PRIVATE /MANIFEST:NO)
else()
  # windres does not reliably honor #pragma code_page: set it explicitly.
  set_source_files_properties(res/SeedFlow.rc PROPERTIES COMPILE_FLAGS "--codepage=65001")
  target_compile_options(SeedFlow PRIVATE -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers)
  target_link_options(SeedFlow PRIVATE -municode -static)
endif()

option(SF_BUILD_ENGINE_TESTS "Build isolated engine flag tests (no external peers)" OFF)
if(SF_BUILD_ENGINE_TESTS)
  enable_testing()
  add_executable(SeedFlowEngineCheck tests/superseed.cpp src/engine.cpp)
  target_compile_definitions(SeedFlowEngineCheck PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX _WIN32_WINNT=0x0A00)
  target_link_libraries(SeedFlowEngineCheck PRIVATE LibtorrentRasterbar::torrent-rasterbar)
  if(MSVC)
    target_compile_options(SeedFlowEngineCheck PRIVATE /utf-8 /W4 /permissive- /EHsc /MP)
  endif()
  add_test(NAME superseed-flags COMMAND SeedFlowEngineCheck "${CMAKE_CURRENT_BINARY_DIR}/superseed-test-data")
endif()

option(SF_BUILD_TESTS "Build offline Windows regression tests" OFF)
if(SF_BUILD_TESTS)
  enable_testing()
  add_executable(SeedFlowRegression tests/regression.cpp
    src/dialogs.cpp src/engine.cpp src/netcheck.cpp src/icons.cpp src/paint.cpp src/speed_graph.cpp src/creator.cpp src/util.cpp)
  target_compile_definitions(SeedFlowRegression PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX _WIN32_WINNT=0x0A00)
  target_link_libraries(SeedFlowRegression PRIVATE LibtorrentRasterbar::torrent-rasterbar
    comctl32 shlwapi shell32 ole32 uuid uxtheme winhttp iphlpapi ws2_32 psapi)
  if(MSVC)
    target_compile_options(SeedFlowRegression PRIVATE /utf-8 /W4 /permissive- /EHsc /MP)
  else()
    target_link_options(SeedFlowRegression PRIVATE -municode -static)
  endif()
  add_dependencies(SeedFlowRegression SeedFlow)
  add_test(NAME win32-regression COMMAND SeedFlowRegression "$<TARGET_FILE:SeedFlow>")
endif()

