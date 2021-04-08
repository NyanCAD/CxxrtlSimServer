project("CxxrtlSimulationServer" CXX)
cmake_minimum_required(VERSION 3.1)
set(CMAKE_CXX_STANDARD 17 CACHE STRING "C++ standard to be used")

find_package(CapnProto CONFIG REQUIRED)

capnp_generate_cpp(SimulatorSources SimulatorHeaders api/Simulator.capnp)
add_executable(CxxrtlSimServer CxxrtlSimServer.cpp ${SimulatorSources})
target_link_libraries(CxxrtlSimServer PRIVATE CapnProto::capnp-rpc ${CMAKE_DL_LIBS})
target_include_directories(CxxrtlSimServer PRIVATE ${CMAKE_CURRENT_BINARY_DIR})

set_target_properties(CxxrtlSimServer PROPERTIES
    PUBLIC_HEADER ${CMAKE_CURRENT_SOURCE_DIR}/include/cxxrtl_stream.hpp
)

install(TARGETS CxxrtlSimServer)
