project(audiodecoder.dumb)

cmake_minimum_required(VERSION 2.6)

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} ${PROJECT_SOURCE_DIR})

enable_language(CXX)

set(USE_KODI_API_LEVEL 2)

find_package(Kodi REQUIRED)
find_package(Dumb REQUIRED)

if(NOT DUMB_FOUND)
  add_subdirectory(lib/dumb/cmake)
  set(DUMB_LIBRARIES dumb)
  set(DUMB_INCLUDE_DIRS ${PROJECT_SOURCE_DIR}/lib/dumb/include)
endif()
include_directories(${KODI_INCLUDE_DIR}
                    ${DUMB_INCLUDE_DIRS})

set(DUMB_SOURCES src/DumbCodec.cpp)

set(DEPLIBS ${kodi-addon-sharedlibrary_LIBRARIES}
            ${DUMB_LIBRARIES})

build_addon(audiodecoder.dumb DUMB DEPLIBS)

include(CPack)
