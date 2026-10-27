cmake_minimum_required(VERSION 3.22)
project(rankedtools_https_smoke LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 20)
set(RANKEDTOOLS_ROOT "${CMAKE_CURRENT_LIST_DIR}/../..")
set(COMPILE_ID rankedtools_https_smoke)
add_executable(${COMPILE_ID} ../https_smoke.cpp)
target_compile_options(${COMPILE_ID} PRIVATE -ffunction-sections -fdata-sections)
target_link_options(${COMPILE_ID} PRIVATE -static -Wl,--gc-sections)
target_include_directories(${COMPILE_ID} PRIVATE
    "${RANKEDTOOLS_ROOT}/include" "${RANKEDTOOLS_ROOT}/extern/includes"
    "${RANKEDTOOLS_ROOT}/extern/includes/beatsaber-hook/shared/rapidjson/include")
include("${RANKEDTOOLS_ROOT}/cmake/https.cmake")
