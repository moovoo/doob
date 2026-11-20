cmake_minimum_required(VERSION 3.16)
project(exampleplugin LANGUAGES CXX)

# The doob plugin API — point at an installed doob (the main build's
# install rules ship libdoob_lib, the headers under include/doob and the
# find_package config):
#
#   cmake -S . -B build -DCMAKE_PREFIX_PATH=<prefix>
#
# (equivalently: -Ddoob_DIR=<prefix>/lib/cmake/doob)
find_package(doob REQUIRED)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

if(Qt6_FOUND)
    qt_add_plugin(exampleplugin exampleplugin.cpp exampleplugin.h)
else()
    add_library(exampleplugin MODULE exampleplugin.cpp exampleplugin.h)
endif()
target_link_libraries(exampleplugin PRIVATE doob::doob_lib)
