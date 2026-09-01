# corvid-c — the canonical C consumer of the corvid database's FFI.
#
# Offline-first by design (docs/PLAN.md): NO FetchContent, no network at
# configure/build time. Everything links the RELEASE artifacts that
# fetch.sh / fetch.ps1 downloaded, sha256-verified, and extracted into
# deps/ — the cdylib, corvid.h, and the golden fixtures that ship inside
# the pinned tag's archive.

cmake_minimum_required(VERSION 3.16)
project(corvid-c VERSION 0.1.0 LANGUAGES C)

# ---- locate the fetched engine artifacts (deps/) ------------------------
if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/deps/version.txt")
    message(FATAL_ERROR
        "deps/version.txt is missing: the engine artifacts have not been "
        "fetched. Run ./fetch.sh (macOS/Linux) or ./fetch.ps1 (Windows) "
        "first — the build consumes deps/ only, by design.")
endif()
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/deps/version.txt" CORVID_VERSION)
string(STRIP "${CORVID_VERSION}" CORVID_VERSION)

# The stamp carries the version; the extracted dir adds the host triple.
file(GLOB CORVID_ROOT_LIST
     "${CMAKE_CURRENT_SOURCE_DIR}/deps/corvid-ffi-${CORVID_VERSION}-*")
list(LENGTH CORVID_ROOT_LIST CORVID_ROOT_COUNT)
if(NOT CORVID_ROOT_COUNT EQUAL 1)
    message(FATAL_ERROR
        "expected exactly one deps/corvid-ffi-${CORVID_VERSION}-<triple> "
        "directory, found ${CORVID_ROOT_COUNT}: re-run fetch.sh / fetch.ps1 "
        "(it discards stale versions and keeps exactly one)")
endif()
list(GET CORVID_ROOT_LIST 0 CORVID_ROOT)
message(STATUS "corvid-c: engine artifacts ${CORVID_VERSION} at ${CORVID_ROOT}")

if(WIN32)
    set(CORVID_LIB "${CORVID_ROOT}/corvid.dll")
    set(CORVID_IMPLIB "${CORVID_ROOT}/corvid.dll.lib")
elseif(APPLE)
    set(CORVID_LIB "${CORVID_ROOT}/libcorvid.dylib")
else()
    set(CORVID_LIB "${CORVID_ROOT}/libcorvid.so")
endif()

foreach(artifact "${CORVID_ROOT}/corvid.h" "${CORVID_LIB}")
    if(NOT EXISTS "${artifact}")
        message(FATAL_ERROR "engine artifact missing: ${artifact}")
    endif()
endforeach()
if(WIN32 AND NOT EXISTS "${CORVID_IMPLIB}")
    message(FATAL_ERROR "engine import library missing: ${CORVID_IMPLIB}")
endif()

# The downloaded cdylib as an imported target: MSVC links the shipped
# import library (corvid.dll.lib); everyone else links the cdylib itself.
add_library(corvid SHARED IMPORTED GLOBAL)
set_target_properties(corvid PROPERTIES
    IMPORTED_LOCATION "${CORVID_LIB}"
    INTERFACE_INCLUDE_DIRECTORIES "${CORVID_ROOT}") # corvid.h at the root
if(WIN32)
    set_target_properties(corvid PROPERTIES IMPORTED_IMPLIB "${CORVID_IMPLIB}")
endif()

# ---- consumer plumbing ---------------------------------------------------
include(GNUInstallDirs)

# Wire one executable against the downloaded library: link, C11, an rpath
# to deps/ for the build tree (macOS @rpath install name; Linux), an
# install rpath for system installs, and — on Windows — the DLL copied
# next to the binary so the loader always finds it.
function(corvid_consumer target)
    target_link_libraries(${target} PRIVATE corvid)
    set_target_properties(${target} PROPERTIES
        C_STANDARD 11
        C_STANDARD_REQUIRED YES
        BUILD_RPATH "${CORVID_ROOT}"
        INSTALL_RPATH "${CMAKE_INSTALL_FULL_LIBDIR}")
    if(WIN32)
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                    "${CORVID_LIB}" "$<TARGET_FILE_DIR:${target}>"
            COMMENT "copying corvid.dll for ${target}")
    endif()
endfunction()

if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-Wall -Wextra)
elseif(MSVC)
    add_compile_options(/W3)
endif()

# One predictable home for the executables (multi-config generators
# still append their config dir: build/bin/Release on MSVC).
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/bin")

# ---- the demo (examples/demo.c) ------------------------------------------
add_executable(demo examples/demo.c)
corvid_consumer(demo)

# ---- the golden-suite port (test/golden.c) --------------------------------
# The core deliverable: the engine harness, verbatim, driving the
# DOWNLOADED cdylib over the release's golden/ fixtures. One ctest per
# fixture file (each with its own scratch workdir for file-db scenarios).
enable_testing()

add_executable(golden test/golden.c)
corvid_consumer(golden)
if(UNIX AND NOT APPLE)
    target_link_libraries(golden PRIVATE m) # libm for the harness's math
endif()

file(GLOB CORVID_FIXTURES "${CORVID_ROOT}/golden/*.txt")
list(SORT CORVID_FIXTURES)
if(NOT CORVID_FIXTURES)
    message(FATAL_ERROR "no golden fixtures under ${CORVID_ROOT}/golden")
endif()
foreach(fixture ${CORVID_FIXTURES})
    get_filename_component(stem "${fixture}" NAME_WE)
    set(workdir "${CMAKE_CURRENT_BINARY_DIR}/scratch/${stem}")
    file(MAKE_DIRECTORY "${workdir}")
    add_test(NAME golden_${stem} COMMAND golden "${workdir}" "${fixture}")
endforeach()
message(STATUS "corvid-c: golden suite = ${CORVID_FIXTURES}")

add_test(NAME demo COMMAND demo)

# ---- install (system use) + pkg-config ------------------------------------
string(REGEX REPLACE "^v" "" CORVID_PC_VERSION "${CORVID_VERSION}")
if(APPLE)
    # The dylib's install name is @rpath/…; bake the install libdir so
    # `pkg-config --libs corvid` links AND runs without extra flags.
    set(CORVID_PC_RPATH " -Wl,-rpath,${CMAKE_INSTALL_FULL_LIBDIR}")
else()
    set(CORVID_PC_RPATH "")
endif()
configure_file(corvid.pc.in corvid.pc @ONLY)

install(FILES "${CORVID_ROOT}/corvid.h"
        DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")
if(WIN32)
    install(FILES "${CORVID_LIB}" DESTINATION "${CMAKE_INSTALL_BINDIR}")
    install(FILES "${CORVID_IMPLIB}" DESTINATION "${CMAKE_INSTALL_LIBDIR}")
else()
    install(FILES "${CORVID_LIB}" DESTINATION "${CMAKE_INSTALL_LIBDIR}")
endif()
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/corvid.pc"
        DESTINATION "${CMAKE_INSTALL_LIBDIR}/pkgconfig")
