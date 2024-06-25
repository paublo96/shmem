project(main LANGUAGES CXX)

set(CMAKE_CXX_FLAGS "-g -O2")

find_package(Threads REQUIRED)
find_package(umpire REQUIRED PATHS ./Umpire/install/lib64/cmake/umpire)

add_executable(main main.cpp)
target_link_libraries(main PRIVATE umpire)
