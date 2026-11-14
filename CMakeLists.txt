cmake_minimum_required(VERSION 3.15)
project(latte-c VERSION 1.7.0
        DESCRIPTION "C/C++ SDK for LicenseLatte license activation and offline verification"
        HOMEPAGE_URL "https://licenselatte.com/docs/sdks/c"
        LANGUAGES C CXX)

option(LATTE_BUILD_SHARED "Build the shared library (latte.dll, liblatte.so, liblatte.dylib)" ON)
option(LATTE_BUILD_STATIC "Build the static library" ON)
if(NOT LATTE_BUILD_SHARED AND NOT LATTE_BUILD_STATIC)
    message(FATAL_ERROR "Enable LATTE_BUILD_SHARED, LATTE_BUILD_STATIC or both")
endif()

# The version the SDK reports to the API in every request body.
add_compile_definitions(LATTE_VERSION="${PROJECT_VERSION}")

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(MSVC)
    # fopen, getenv, strncpy and friends are standard C; silence the CRT's
    # deprecation of them.
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
    set(LATTE_WARNINGS /W4)
    set(LATTE_NO_WARNINGS /w)
    # Renames every internal global symbol to latte__* (see src/symbols.h).
    set(LATTE_RENAME "/FI${CMAKE_CURRENT_SOURCE_DIR}/src/symbols.h")
else()
    set(LATTE_WARNINGS -Wall -Wextra -Wpedantic)
    set(LATTE_NO_WARNINGS -w)
    set(LATTE_RENAME "SHELL:-include ${CMAKE_CURRENT_SOURCE_DIR}/src/symbols.h")
endif()

# ---------------------------------------------------------------------------
# HTTP transport: WinHTTP on Windows, libcurl elsewhere
# ---------------------------------------------------------------------------
if(WIN32)
    set(LATTE_TRANSPORT src/http/transport_winhttp.c)
    set(LATTE_SYSTEM_LIBS winhttp advapi32)
else()
    set(LATTE_TRANSPORT src/http/transport_curl.c)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(LIBCURL REQUIRED IMPORTED_TARGET libcurl)
    set(LATTE_SYSTEM_LIBS PkgConfig::LIBCURL)
endif()

# ---------------------------------------------------------------------------
# Vendored cJSON and Monocypher. The library compiles them in (latte_vendor);
# the tests link these plain copies.
# ---------------------------------------------------------------------------
add_library(cjson STATIC vendor/cjson/cJSON.c)
target_include_directories(cjson PUBLIC vendor/cjson)
target_compile_options(cjson PRIVATE ${LATTE_NO_WARNINGS})
# cJSON otherwise marks its functions __declspec(dllexport) on Windows.
target_compile_definitions(cjson PUBLIC CJSON_HIDE_SYMBOLS)

add_library(monocypher STATIC
    vendor/monocypher/monocypher.c
    vendor/monocypher/monocypher-ed25519.c)
target_include_directories(monocypher PUBLIC vendor/monocypher)
target_compile_options(monocypher PRIVATE ${LATTE_NO_WARNINGS})

add_library(latte_vendor OBJECT
    vendor/cjson/cJSON.c
    vendor/monocypher/monocypher.c
    vendor/monocypher/monocypher-ed25519.c)
target_compile_definitions(latte_vendor PRIVATE CJSON_HIDE_SYMBOLS)
target_compile_options(latte_vendor PRIVATE ${LATTE_NO_WARNINGS} ${LATTE_RENAME})
set_target_properties(latte_vendor PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    C_VISIBILITY_PRESET hidden)

# ---------------------------------------------------------------------------
# Library
# ---------------------------------------------------------------------------
set(LATTE_SOURCES
    src/domain.c
    src/entitlements.c
    src/errors.c
    src/appid.c
    src/machineid.c
    src/thread.c
    src/validate.c
    src/verify.c
    src/crypto/key.c
    src/crypto/base64url.c
    src/crypto/ed25519.c
    src/crypto/hmac_sha256.c
    src/crypto/jwt.c
    src/crypto/cert.c
    src/http/client.c
    ${LATTE_TRANSPORT}
    src/storage/file.c
    src/storage/mem.c
    src/latte.c
)

function(latte_library target type)
    add_library(${target} ${type} ${LATTE_SOURCES} $<TARGET_OBJECTS:latte_vendor>)
    target_include_directories(${target}
        PUBLIC  include
        PRIVATE src vendor/cjson vendor/monocypher)
    target_compile_definitions(${target} PRIVATE CJSON_HIDE_SYMBOLS)
    target_compile_options(${target} PRIVATE ${LATTE_WARNINGS} ${LATTE_RENAME})
    target_link_libraries(${target} PRIVATE ${LATTE_SYSTEM_LIBS})
    set_target_properties(${target} PROPERTIES
        OUTPUT_NAME latte
        C_VISIBILITY_PRESET hidden)
endfunction()

if(LATTE_BUILD_STATIC)
    latte_library(latte_static STATIC)
    if(MSVC)
        # The import library of latte.dll is already latte.lib; the static
        # library is named after the C runtime it was compiled against.
        if(CMAKE_MSVC_RUNTIME_LIBRARY MATCHES "DLL" OR NOT CMAKE_MSVC_RUNTIME_LIBRARY)
            set_target_properties(latte_static PROPERTIES OUTPUT_NAME latte_md)
        else()
            set_target_properties(latte_static PROPERTIES OUTPUT_NAME latte_mt)
        endif()
        set(LATTE_STATIC_DESTINATION lib/static)
    else()
        set(LATTE_STATIC_DESTINATION lib)
    endif()
    install(TARGETS latte_static ARCHIVE DESTINATION ${LATTE_STATIC_DESTINATION})
endif()

if(LATTE_BUILD_SHARED)
    latte_library(latte_shared SHARED)
    target_compile_definitions(latte_shared PRIVATE LATTE_BUILDING_SHARED)
    if(MINGW)
        # No libgcc_s or libwinpthread DLL next to latte.dll.
        target_link_options(latte_shared PRIVATE -static -static-libgcc)
    endif()
    if(NOT MSVC AND NOT APPLE)
        # Symbols of any static archive linked in stay out of the export table.
        target_link_options(latte_shared PRIVATE -Wl,--exclude-libs,ALL)
    endif()
    install(TARGETS latte_shared
        RUNTIME DESTINATION bin
        LIBRARY DESTINATION lib
        ARCHIVE DESTINATION lib)
endif()

install(FILES include/latte/latte.h include/latte/latte.hpp DESTINATION include/latte)

if(LATTE_BUILD_STATIC)
    set(LATTE_LINK latte_static)
else()
    set(LATTE_LINK latte_shared)
endif()
add_library(latte::latte ALIAS ${LATTE_LINK})

# ---------------------------------------------------------------------------
# Examples
# ---------------------------------------------------------------------------
add_executable(example_simple examples/simple.c)
target_link_libraries(example_simple PRIVATE ${LATTE_LINK})

add_executable(example_simple_cpp examples/simple.cpp)
target_link_libraries(example_simple_cpp PRIVATE ${LATTE_LINK})

# ---------------------------------------------------------------------------
# CLI tools
# ---------------------------------------------------------------------------
add_executable(sdktest tools/sdktest.c)
target_link_libraries(sdktest PRIVATE ${LATTE_LINK})

# These call internal functions, so they need the static library and its
# renamed symbols. https_check is what CI runs against real TLS endpoints.
if(LATTE_BUILD_STATIC)
    add_executable(validator tools/validator.c)
    add_executable(machineid_test tools/machineid_test.c)
    add_executable(https_check tools/https_check.c)
    foreach(tool validator machineid_test https_check)
        target_include_directories(${tool} PRIVATE src vendor/cjson)
        target_compile_definitions(${tool} PRIVATE CJSON_HIDE_SYMBOLS)
        target_compile_options(${tool} PRIVATE ${LATTE_RENAME})
        target_link_libraries(${tool} PRIVATE latte_static)
    endforeach()
endif()

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
enable_testing()

# test_key
add_executable(test_key tests/test_key.c src/crypto/key.c)
target_include_directories(test_key PRIVATE include src)
add_test(NAME key COMMAND test_key)

# test_storage
add_executable(test_storage
    tests/test_storage.c
    src/domain.c
    src/storage/file.c
)
target_include_directories(test_storage PRIVATE include src)
target_link_libraries(test_storage PRIVATE cjson)
add_test(NAME storage COMMAND test_storage)

# test_storage_atomic: concurrent saves and loads of the cache file
add_executable(test_storage_atomic
    tests/test_storage_atomic.c
    src/domain.c
    src/thread.c
    src/storage/file.c
)
target_include_directories(test_storage_atomic PRIVATE include src)
target_link_libraries(test_storage_atomic PRIVATE cjson)
add_test(NAME storage_atomic COMMAND test_storage_atomic)

# test_validate
add_executable(test_validate
    tests/test_validate.c
    src/validate.c
    src/domain.c
)
target_include_directories(test_validate PRIVATE include src)
add_test(NAME validate COMMAND test_validate)

# test_entitlements
add_executable(test_entitlements
    tests/test_entitlements.c
    src/entitlements.c
)
target_include_directories(test_entitlements PRIVATE include src vendor/cjson)
target_link_libraries(test_entitlements PRIVATE cjson)
add_test(NAME entitlements COMMAND test_entitlements)

# test_fixtures — the shared cross-language suite from testdata/ (a
# submodule of latte-testvectors). Runs from the source dir so the fixture
# paths resolve without copying them into the build tree.
add_executable(test_fixtures
    tests/test_fixtures.c
    src/storage/file.c
    src/verify.c
    src/validate.c
    src/domain.c
    src/entitlements.c
    src/crypto/base64url.c
    src/crypto/ed25519.c
    src/crypto/jwt.c
    src/crypto/cert.c
)
target_include_directories(test_fixtures PRIVATE include src vendor/cjson)
target_link_libraries(test_fixtures PRIVATE cjson monocypher)
add_test(NAME fixtures COMMAND test_fixtures WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

# test_machine_id: testdata/machine_id.json through the shared hash.
add_executable(test_machine_id tests/test_machine_id.c src/machineid.c src/crypto/hmac_sha256.c)
target_include_directories(test_machine_id PRIVATE include src vendor/cjson)
target_link_libraries(test_machine_id PRIVATE cjson monocypher)
add_test(NAME machine_id COMMAND test_machine_id WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

# test_http_body
add_executable(test_http_body
    tests/test_http_body.c
    src/http/client.c
    ${LATTE_TRANSPORT}
    src/domain.c
)
target_include_directories(test_http_body PRIVATE include src vendor/cjson)
target_link_libraries(test_http_body PRIVATE cjson ${LATTE_SYSTEM_LIBS})
add_test(NAME http_body COMMAND test_http_body)

# test_anchors
add_executable(test_anchors
    tests/test_anchors.c
    src/verify.c
    src/validate.c
    src/domain.c
    src/entitlements.c
    src/crypto/base64url.c
    src/crypto/ed25519.c
    src/crypto/jwt.c
    src/crypto/cert.c
)
target_include_directories(test_anchors PRIVATE include src vendor/cjson)
target_link_libraries(test_anchors PRIVATE cjson monocypher)
add_test(NAME anchors COMMAND test_anchors)

# test_daily_cert
add_executable(test_daily_cert
    tests/test_daily_cert.c
    src/verify.c
    src/validate.c
    src/domain.c
    src/entitlements.c
    src/crypto/base64url.c
    src/crypto/ed25519.c
    src/crypto/jwt.c
    src/crypto/cert.c
)
target_include_directories(test_daily_cert PRIVATE include src vendor/cjson)
target_link_libraries(test_daily_cert PRIVATE cjson monocypher)
add_test(NAME daily_cert COMMAND test_daily_cert)

# test_jwt
add_executable(test_jwt
    tests/test_jwt.c
    src/crypto/base64url.c
    src/crypto/ed25519.c
    src/crypto/jwt.c
)
target_include_directories(test_jwt PRIVATE include src)
target_link_libraries(test_jwt PRIVATE cjson monocypher)
add_test(NAME jwt COMMAND test_jwt)

# test_machines: deactivate / machines / rename against a local HTTP stub.
# The test includes src/latte.c itself, so it builds from every other source.
set(TEST_MACHINES_SOURCES ${LATTE_SOURCES})
list(REMOVE_ITEM TEST_MACHINES_SOURCES src/latte.c)
add_executable(test_machines tests/test_machines.c ${TEST_MACHINES_SOURCES})
target_include_directories(test_machines PRIVATE include src vendor/cjson)
target_link_libraries(test_machines PRIVATE cjson monocypher ${LATTE_SYSTEM_LIBS})
if(WIN32)
    target_link_libraries(test_machines PRIVATE ws2_32)
endif()
add_test(NAME machines COMMAND test_machines)

# test_ed25519: RFC 8032, Wycheproof and ed25519-speccheck vectors (tests/vectors/).
add_executable(test_ed25519 tests/test_ed25519.c src/crypto/ed25519.c)
target_include_directories(test_ed25519 PRIVATE include src vendor/cjson)
target_link_libraries(test_ed25519 PRIVATE cjson monocypher)
add_test(NAME ed25519 COMMAND test_ed25519 WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

# test_hmac_sha256: RFC 4231 test cases.
add_executable(test_hmac_sha256 tests/test_hmac_sha256.c src/crypto/hmac_sha256.c)
target_include_directories(test_hmac_sha256 PRIVATE include src)
add_test(NAME hmac_sha256 COMMAND test_hmac_sha256)

# test_transport_stress: hundreds of requests cancelled by a 200 ms deadline
# in every phase, from several threads.
add_executable(test_transport_stress tests/test_transport_stress.c ${LATTE_TRANSPORT} src/thread.c)
target_include_directories(test_transport_stress PRIVATE include src)
target_compile_definitions(test_transport_stress PRIVATE LATTE_HTTP_TIMEOUT_MS=200)
target_link_libraries(test_transport_stress PRIVATE ${LATTE_SYSTEM_LIBS})
if(WIN32)
    target_link_libraries(test_transport_stress PRIVATE ws2_32)
endif()
add_test(NAME transport_stress COMMAND test_transport_stress)
