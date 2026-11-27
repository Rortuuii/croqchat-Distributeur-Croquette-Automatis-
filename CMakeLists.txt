# Déclare le projet ESP-IDF et charge sa configuration de compilation.
cmake_minimum_required(VERSION 3.16)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
# Compile seulement l'application et les dependances necessaires.
idf_build_set_property(MINIMAL_BUILD ON)
project(croqchat)
