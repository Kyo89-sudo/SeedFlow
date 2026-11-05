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

project(SeedFlow VERSION 0.8.9.4 LANGUAGES CXX RC)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

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
  src/geoip.cpp
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
  target_compile_options(SeedFlow PRIVATE "$<$<COMPILE_LANGUAGE:CXX>:/utf-8;/W4;/permissive-;/EHsc;/Zc:__cplusplus;/MP>")
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
    target_compile_options(SeedFlowEngineCheck PRIVATE /utf-8 /W4 /permissive- /EHsc /Zc:__cplusplus /MP)
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
    target_compile_options(SeedFlowRegression PRIVATE /utf-8 /W4 /permissive- /EHsc /Zc:__cplusplus /MP)
  else()
    target_link_options(SeedFlowRegression PRIVATE -municode -static)
  endif()
  add_dependencies(SeedFlowRegression SeedFlow)
  add_test(NAME win32-regression COMMAND SeedFlowRegression "$<TARGET_FILE:SeedFlow>")
endif()


# Native per-user installer. Its resources embed the app, license and uninstaller.
option(SF_BUILD_INSTALLER "Build native Windows setup" OFF)
if(SF_BUILD_INSTALLER)
  if(NOT SF_STATIC_RUNTIME)
    message(FATAL_ERROR "The installer requires SF_STATIC_RUNTIME=ON.")
  endif()
  configure_file(installer/Setup.manifest.in setup/SeedFlowSetup.manifest @ONLY)
  file(GLOB notices CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/licenses/*.txt"
    "${CMAKE_CURRENT_SOURCE_DIR}/licenses/README.md")
  set(notice_text "")
  foreach(notice IN LISTS notices)
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${notice}")
    get_filename_component(notice_name "${notice}" NAME)
    file(READ "${notice}" notice_body)
    string(APPEND notice_text "\n--- ${notice_name} ---\n${notice_body}\n")
  endforeach()
  file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/setup/ThirdPartyNotices.txt" "${notice_text}")
  add_executable(SeedFlowUninstall WIN32 installer/main.cpp installer/common.rc)
  target_compile_definitions(SeedFlowUninstall PRIVATE SF_UNINSTALLER)
  file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/setup/$<CONFIG>/payload.rc"
    CONTENT "201 RCDATA \"$<TARGET_FILE:SeedFlow>\"\n202 RCDATA \"$<TARGET_FILE:SeedFlowUninstall>\"\n203 RCDATA \"${CMAKE_CURRENT_SOURCE_DIR}/LICENSE\"\n204 RCDATA \"${CMAKE_CURRENT_BINARY_DIR}/setup/ThirdPartyNotices.txt\"\n")
  add_executable(SeedFlowSetup WIN32 installer/main.cpp installer/common.rc
    "${CMAKE_CURRENT_BINARY_DIR}/setup/$<CONFIG>/payload.rc")
  add_dependencies(SeedFlowSetup SeedFlow SeedFlowUninstall)
  foreach(target IN ITEMS SeedFlowSetup SeedFlowUninstall)
    target_include_directories(${target} PRIVATE src installer "${CMAKE_CURRENT_BINARY_DIR}/setup")
    target_compile_definitions(${target} PRIVATE UNICODE _UNICODE _WIN32_WINNT=0x0A00)
    target_link_libraries(${target} PRIVATE comctl32 shell32 ole32 uuid advapi32)
    if(MSVC)
      target_compile_options(${target} PRIVATE "$<$<COMPILE_LANGUAGE:CXX>:/utf-8;/W4;/permissive-;/EHsc>")
      target_link_options(${target} PRIVATE /MANIFEST:NO)
    else()
      target_link_options(${target} PRIVATE -municode -static)
    endif()
  endforeach()
  set_property(SOURCE installer/common.rc APPEND PROPERTY OBJECT_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/version.h" "${CMAKE_CURRENT_BINARY_DIR}/setup/SeedFlowSetup.manifest")
  if(MSVC)
    set_source_files_properties(installer/common.rc PROPERTIES COMPILE_FLAGS "/c65001")
  else()
    set_source_files_properties(installer/common.rc PROPERTIES COMPILE_FLAGS "--codepage=65001")
  endif()
  if(SF_BUILD_TESTS)
    add_executable(SeedFlowInstallerCheck tests/installer.cpp
      "${CMAKE_CURRENT_BINARY_DIR}/setup/$<CONFIG>/payload.rc")
    target_include_directories(SeedFlowInstallerCheck PRIVATE src)
    target_compile_definitions(SeedFlowInstallerCheck PRIVATE UNICODE _UNICODE _WIN32_WINNT=0x0A00)
    target_link_libraries(SeedFlowInstallerCheck PRIVATE comctl32 shell32 ole32 uuid advapi32)
    add_dependencies(SeedFlowInstallerCheck SeedFlow SeedFlowUninstall)
    if(MSVC)
      target_compile_options(SeedFlowInstallerCheck PRIVATE /utf-8 /W4 /permissive- /EHsc)
    else()
      target_link_options(SeedFlowInstallerCheck PRIVATE -municode -static)
    endif()
    add_test(NAME native-installer COMMAND SeedFlowInstallerCheck)
  endif()
endif()
