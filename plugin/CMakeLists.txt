cmake_minimum_required(VERSION 3.21)
project(PhotoModeRayReconstructionFix LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(PhotoModeRayReconstructionFix SHARED src/Main.cpp)

# RED4ext.SDK is header-only and lives in the read-only reference mirror. It is included, never
# built from there: nothing may write into _source.
target_include_directories(PhotoModeRayReconstructionFix PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../../../_source/RED4ext.SDK/include")

target_compile_definitions(PhotoModeRayReconstructionFix PRIVATE
    WIN32_LEAN_AND_MEAN NOMINMAX _CRT_SECURE_NO_WARNINGS)

set_target_properties(PhotoModeRayReconstructionFix PROPERTIES
    OUTPUT_NAME "PhotoModeRayReconstructionFix"
    SUFFIX ".dll")
