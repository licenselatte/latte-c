cmake_minimum_required(VERSION 3.15)
project(latte-c VERSION 0.1.0 LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Homebrew prefix (Apple Silicon default; Intel uses /usr/local)
if(APPLE)
    execute_process(COMMAND brew --prefix
        OUTPUT_VARIABLE HOMEBREW_PREFIX
        OUTPUT_STRIP_TRAILING_WHITESPACE)
    if(HOMEBREW_PREFIX)
        list(APPEND CMAKE_PREFIX_PATH "${HOMEBREW_PREFIX}")
        link_directories("${HOMEBREW_PREFIX}/lib")
        include_directories("${HOMEBREW_PREFIX}/include")
    endif()
endif()

# ---------------------------------------------------------------------------
# Dependencies
# ---------------------------------------------------------------------------
find_package(PkgConfig REQUIRED)

pkg_check_modules(LIBSODIUM REQUIRED libsodium)
pkg_check_modules(LIBCURL   REQUIRED libcurl)

# ---------------------------------------------------------------------------
# Vendored cJSON
# ---------------------------------------------------------------------------
add_library(cjson STATIC vendor/cjson/cJSON.c)
target_include_directories(cjson PUBLIC vendor/cjson)
target_compile_options(cjson PRIVATE -w)   # suppress cJSON warnings

# ---------------------------------------------------------------------------
# Library sources
# ---------------------------------------------------------------------------
set(LATTE_SOURCES
    src/domain.c
    src/errors.c
    src/appid.c
    src/machineid.c
    src/thread.c
    src/validate.c
    src/verify.c
    src/crypto/key.c
    src/crypto/base64url.c
    src/crypto/jwt.c
    src/crypto/cert.c
    src/http/client.c
    src/storage/file.c
    src/storage/mem.c
    src/latte.c
)

# Static library
add_library(latte_static STATIC ${LATTE_SOURCES})
set_target_properties(latte_static PROPERTIES OUTPUT_NAME latte)

target_include_directories(latte_static
    PUBLIC  include
    PRIVATE src ${LIBSODIUM_INCLUDE_DIRS} ${LIBCURL_INCLUDE_DIRS}
)
target_link_libraries(latte_static
    PRIVATE cjson ${LIBSODIUM_LIBRARIES} ${LIBCURL_LIBRARIES}
)
target_compile_options(latte_static PRIVATE
    ${LIBSODIUM_CFLAGS_OTHER} ${LIBCURL_CFLAGS_OTHER}
    -Wall -Wextra -Wpedantic
)

# Shared library
add_library(latte_shared SHARED ${LATTE_SOURCES})
set_target_properties(latte_shared PROPERTIES OUTPUT_NAME latte)

target_include_directories(latte_shared
    PUBLIC  include
    PRIVATE src ${LIBSODIUM_INCLUDE_DIRS} ${LIBCURL_INCLUDE_DIRS}
)
target_link_libraries(latte_shared
    PRIVATE cjson ${LIBSODIUM_LIBRARIES} ${LIBCURL_LIBRARIES}
)
target_compile_options(latte_shared PRIVATE
    ${LIBSODIUM_CFLAGS_OTHER} ${LIBCURL_CFLAGS_OTHER}
    -Wall -Wextra -Wpedantic
)

# Convenience alias
add_library(latte::latte ALIAS latte_static)

# ---------------------------------------------------------------------------
# Examples
# ---------------------------------------------------------------------------
add_executable(example_simple examples/simple.c)
target_include_directories(example_simple PRIVATE include)
target_link_libraries(example_simple PRIVATE latte_static)

# ---------------------------------------------------------------------------
# CLI tools
# ---------------------------------------------------------------------------
add_executable(sdktest tools/sdktest.c)
target_include_directories(sdktest PRIVATE include src)
target_link_libraries(sdktest PRIVATE latte_static)

add_executable(validator tools/validator.c)
target_include_directories(validator PRIVATE include src)
target_link_libraries(validator PRIVATE latte_static ${LIBSODIUM_LIBRARIES})

add_executable(machineid_test tools/machineid_test.c)
target_include_directories(machineid_test PRIVATE src)
target_link_libraries(machineid_test PRIVATE latte_static)

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
enable_testing()

# test_key
add_executable(test_key tests/test_key.c src/crypto/key.c)
target_include_directories(test_key PRIVATE src)
add_test(NAME key COMMAND test_key)

# test_storage
add_executable(test_storage
    tests/test_storage.c
    src/domain.c
    src/storage/file.c
)
target_include_directories(test_storage PRIVATE src)
target_link_libraries(test_storage PRIVATE cjson)
add_test(NAME storage COMMAND test_storage)

# test_validate
add_executable(test_validate
    tests/test_validate.c
    src/validate.c
    src/domain.c
)
target_include_directories(test_validate PRIVATE src)
add_test(NAME validate COMMAND test_validate)

# test_jwt
add_executable(test_jwt
    tests/test_jwt.c
    src/crypto/base64url.c
    src/crypto/jwt.c
)
target_include_directories(test_jwt PRIVATE src ${LIBSODIUM_INCLUDE_DIRS})
target_link_libraries(test_jwt PRIVATE cjson ${LIBSODIUM_LIBRARIES})
add_test(NAME jwt COMMAND test_jwt)

# ---------------------------------------------------------------------------
# Install
# ---------------------------------------------------------------------------
install(TARGETS latte_static latte_shared DESTINATION lib)
install(FILES include/latte/latte.h DESTINATION include/latte)
