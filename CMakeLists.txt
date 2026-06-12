cmake_minimum_required(VERSION 3.16)
project(ManajemenMahasiswa CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

include(FetchContent)

# -----------------------------------------------------------------------------
# 1) asio (standalone, header-only) — Crow's networking backend.
#    We download a pinned version and expose ASIO_INCLUDE_DIR so Crow's
#    bundled Findasio.cmake picks it up automatically (no system install needed).
# -----------------------------------------------------------------------------
FetchContent_Declare(
    asio
    GIT_REPOSITORY https://github.com/chriskohlhoff/asio.git
    GIT_TAG        asio-1-30-2
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(asio)
set(ASIO_INCLUDE_DIR "${asio_SOURCE_DIR}/asio/include" CACHE PATH "asio headers" FORCE)

# -----------------------------------------------------------------------------
# 2) Crow (header-only web microframework).
# -----------------------------------------------------------------------------
set(CROW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(CROW_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    crow
    GIT_REPOSITORY https://github.com/CrowCpp/Crow.git
    GIT_TAG        v1.3.2
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(crow)

# -----------------------------------------------------------------------------
# 3) libcurl (OPTIONAL) — enables real SMTP email for OTP.
#    If not present the app still builds and prints OTP to the console.
# -----------------------------------------------------------------------------
find_package(CURL QUIET)

# -----------------------------------------------------------------------------
# Target
# -----------------------------------------------------------------------------
add_executable(manajemen_mahasiswa
    backend/src/main.cpp
    backend/src/Student.cpp
    backend/src/StudentManager.cpp
    backend/src/Algorithms.cpp
    backend/src/Sha256.cpp
    backend/src/Utils.cpp
    backend/src/Auth.cpp
    backend/src/Mailer.cpp
)

target_include_directories(manajemen_mahasiswa PRIVATE backend/include)
target_link_libraries(manajemen_mahasiswa PRIVATE Crow::Crow)

if(CURL_FOUND)
    message(STATUS "libcurl ditemukan -> pengiriman email OTP via SMTP AKTIF")
    target_compile_definitions(manajemen_mahasiswa PRIVATE HAVE_CURL)
    target_link_libraries(manajemen_mahasiswa PRIVATE CURL::libcurl)
else()
    message(STATUS "libcurl TIDAK ditemukan -> OTP dicetak ke konsol (mode dev)")
endif()

find_package(Threads REQUIRED)
target_link_libraries(manajemen_mahasiswa PRIVATE Threads::Threads)

# -----------------------------------------------------------------------------
# Copy the frontend next to the binary and create a writable data dir, so the
# server can be launched directly from the build directory.
# -----------------------------------------------------------------------------
add_custom_command(TARGET manajemen_mahasiswa POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_SOURCE_DIR}/frontend" "$<TARGET_FILE_DIR:manajemen_mahasiswa>/frontend"
    COMMAND ${CMAKE_COMMAND} -E make_directory
            "$<TARGET_FILE_DIR:manajemen_mahasiswa>/data"
    COMMENT "Menyalin frontend dan menyiapkan folder data"
)
