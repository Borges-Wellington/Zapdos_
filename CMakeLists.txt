cmake_minimum_required(VERSION 3.16)
project(Zapdos VERSION 0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Diretórios de include do projeto
include_directories(${CMAKE_SOURCE_DIR}/include)
include_directories(${CMAKE_SOURCE_DIR}/proto_gen)

# Find Protobuf (exporta alvo protobuf::libprotobuf em muitas distros)
find_package(Protobuf REQUIRED)
message(STATUS "Protobuf_FOUND=${Protobuf_FOUND}; PROTOBUF_INCLUDE_DIR=${Protobuf_INCLUDE_DIR}")

# Find ZLIB (necessário para compress/decompress)
find_package(ZLIB REQUIRED)

# FetchContent for dependencies: websocketpp (header-only) and nlohmann_json
include(FetchContent)
FetchContent_Declare(
  websocketpp
  GIT_REPOSITORY https://github.com/zaphoyd/websocketpp.git
  GIT_TAG        0.8.2
)
FetchContent_MakeAvailable(websocketpp)

FetchContent_Declare(
  nlohmann_json
  GIT_REPOSITORY https://github.com/nlohmann/json.git
  GIT_TAG        v3.11.2
)
FetchContent_MakeAvailable(nlohmann_json)

# Boost ASIO (system)
find_package(Boost REQUIRED COMPONENTS system thread)
if (NOT Boost_FOUND)
  message(FATAL_ERROR "Boost not found")
endif()

# Source files
# Coleta todos os .cpp, mas exclui inspector.cpp e gen_sample.cpp
file(GLOB SRC_FILES src/*.cpp src/*.cc)
list(REMOVE_ITEM SRC_FILES
    ${CMAKE_SOURCE_DIR}/src/inspector.cpp
    ${CMAKE_SOURCE_DIR}/tools/gen_sample.cpp
)

add_executable(zapdos ${SRC_FILES})

# Add proto generated sources to zapdos if present
file(GLOB PROTO_SRCS "${CMAKE_SOURCE_DIR}/proto_gen/*.cc" "${CMAKE_SOURCE_DIR}/proto_gen/*.cxx")
if(PROTO_SRCS)
  target_sources(zapdos PRIVATE ${PROTO_SRCS})
endif()

# Utils library
add_library(zapdos_utils src/Utils.cpp)
target_include_directories(zapdos_utils PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(zapdos_utils PRIVATE protobuf::libprotobuf)

# Link utils into main binary
target_link_libraries(zapdos PRIVATE zapdos_utils)

# Link include dirs from FetchContent (websocketpp and nlohmann)
target_include_directories(zapdos PRIVATE ${websocketpp_SOURCE_DIR} ${nlohmann_json_SOURCE_DIR}/include)

# Link libraries for main target
target_link_libraries(zapdos PRIVATE protobuf::libprotobuf Boost::system Boost::thread ZLIB::ZLIB)

# Definitions (if any)
target_compile_definitions(zapdos PRIVATE -DASIO_STANDALONE)

#
# Tools: inspector & gen_sample
#
# inspector: only add once and link zlib + protobuf
if (NOT TARGET inspector)
  add_executable(inspector src/inspector.cpp ${CMAKE_SOURCE_DIR}/proto_gen/rotom.pb.cc)
  target_include_directories(inspector PRIVATE ${CMAKE_SOURCE_DIR}/proto_gen ${CMAKE_SOURCE_DIR}/src)
  target_link_libraries(inspector PRIVATE protobuf::libprotobuf ZLIB::ZLIB)
endif()

# gen_sample tool
if (NOT TARGET gen_sample)
  add_executable(gen_sample tools/gen_sample.cpp ${CMAKE_SOURCE_DIR}/proto_gen/rotom.pb.cc)
  target_include_directories(gen_sample PRIVATE ${CMAKE_SOURCE_DIR}/proto_gen ${CMAKE_SOURCE_DIR})
  target_link_libraries(gen_sample PRIVATE protobuf::libprotobuf)
endif()
