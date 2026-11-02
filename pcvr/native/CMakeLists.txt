cmake_minimum_required(VERSION 3.22)
project(RankedToolsPCVR VERSION 0.1.24 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
set(ROOT "${CMAKE_CURRENT_LIST_DIR}/../..")
set(COMPILE_ID rankedtools_core)

set(MODEL_SOURCES
    "${ROOT}/src/recommendation.cpp" "${ROOT}/src/beatleader_pp.cpp"
    "${ROOT}/src/clan_recommendation.cpp" "${ROOT}/src/attempt_history.cpp"
    "${ROOT}/src/ability_chart.cpp" "${ROOT}/src/playlist_image.cpp" "${ROOT}/src/account.cpp")
add_library(${COMPILE_ID} SHARED bridge.cpp ${MODEL_SOURCES}
    "${ROOT}/src/recommendation_api.cpp" "${ROOT}/src/playlist_writer.cpp"
    "${ROOT}/src/refresh_engine.cpp")
set_target_properties(${COMPILE_ID} PROPERTIES OUTPUT_NAME "RankedTools.Core" PREFIX "")
file(GENERATE OUTPUT "${CMAKE_BINARY_DIR}/native-build.json" CONTENT
    "{\n  \"compiler\": \"${CMAKE_CXX_COMPILER_ID}\",\n  \"compilerVersion\": \"${CMAKE_CXX_COMPILER_VERSION}\",\n  \"architecture\": \"x64\",\n  \"curl\": \"8.23.0\",\n  \"mbedTLS\": \"3.6.7\",\n  \"abi\": 1\n}\n")

include(FetchContent)
if(NOT RAPIDJSON_INCLUDE_DIR)
    if(EXISTS "${ROOT}/extern/includes/beatsaber-hook/shared/rapidjson/include/rapidjson/document.h")
        set(RAPIDJSON_INCLUDE_DIR "${ROOT}/extern/includes/beatsaber-hook/shared/rapidjson/include")
    else()
        FetchContent_Declare(rankedtools_rapidjson
            GIT_REPOSITORY https://github.com/Tencent/rapidjson.git
            GIT_TAG f54b0e47a08782a6131cc3d60f94d038fa6e0a51)
        FetchContent_GetProperties(rankedtools_rapidjson)
        if(NOT rankedtools_rapidjson_POPULATED)
            FetchContent_Populate(rankedtools_rapidjson)
        endif()
        set(RAPIDJSON_INCLUDE_DIR "${rankedtools_rapidjson_SOURCE_DIR}/include")
    endif()
endif()
include("${ROOT}/cmake/https.cmake")

function(rankedtools_compile target)
    target_include_directories(${target} PRIVATE "${ROOT}/include" "${CMAKE_CURRENT_LIST_DIR}"
        "${CMAKE_CURRENT_LIST_DIR}/compat" "${RAPIDJSON_INCLUDE_DIR}" "${rankedtools_curl_SOURCE_DIR}/include")
    target_compile_definitions(${target} PRIVATE CURL_STATICLIB NOMINMAX WIN32_LEAN_AND_MEAN)
    if(MSVC)
        target_compile_options(${target} PRIVATE /EHsc /utf-8 /Gy)
        target_link_options(${target} PRIVATE /OPT:REF)
    else()
        target_compile_options(${target} PRIVATE -ffunction-sections -fdata-sections)
        target_link_options(${target} PRIVATE -static -Wl,--gc-sections)
    endif()
endfunction()
rankedtools_compile(${COMPILE_ID})
target_compile_definitions(${COMPILE_ID} PRIVATE RANKEDTOOLS_PCVR)

option(RANKEDTOOLS_TESTS "Build offline production regression checks" ON)
if(RANKEDTOOLS_TESTS)
    enable_testing()
    add_executable(recommendation_tests "${ROOT}/tests/recommendation_tests.cpp")
    target_link_libraries(recommendation_tests PRIVATE CURL::libcurl)
    add_executable(playlist_writer_tests "${ROOT}/tests/playlist_writer_tests.cpp"
        "${ROOT}/src/recommendation.cpp" "${ROOT}/src/beatleader_pp.cpp" "${ROOT}/src/attempt_history.cpp")
    add_executable(http_client_tests "${ROOT}/tests/http_client_tests.cpp" ${MODEL_SOURCES})
    add_executable(refresh_engine_tests "${ROOT}/tests/refresh_engine_tests.cpp" "${ROOT}/src/refresh_engine.cpp")
    add_executable(platform_tests "${ROOT}/tests/platform_tests.cpp" "${ROOT}/src/playlist_writer.cpp" ${MODEL_SOURCES})
    add_executable(pcvr_bridge_tests "${ROOT}/tests/pcvr_bridge_tests.cpp" bridge.cpp ${MODEL_SOURCES} "${ROOT}/src/recommendation_api.cpp")
    target_link_libraries(pcvr_bridge_tests PRIVATE CURL::libcurl)
    target_compile_definitions(pcvr_bridge_tests PRIVATE RANKEDTOOLS_PCVR)
    foreach(target recommendation_tests playlist_writer_tests http_client_tests refresh_engine_tests platform_tests pcvr_bridge_tests)
        rankedtools_compile(${target})
        add_test(NAME ${target} COMMAND ${target})
        set_tests_properties(${target} PROPERTIES WORKING_DIRECTORY "${ROOT}")
    endforeach()
endif()
