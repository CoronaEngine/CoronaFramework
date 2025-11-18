cmake_minimum_required(VERSION 4.0)
project(CoronaFramework VERSION 0.1.0 LANGUAGES CXX)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

include(CoronaInit)
include(CoronaTbbConfig)
include(CoronaTargets)
include(CoronaFeatures)
include(CoronaPackaging)
include(CoronaRuntime)
