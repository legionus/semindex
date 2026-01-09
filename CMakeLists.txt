cmake_minimum_required(VERSION 3.13)
project(semind C CXX)

# LLVM paths from llvm-config
execute_process(
	COMMAND llvm-config --includedir
	OUTPUT_VARIABLE LLVM_INCLUDEDIR
	OUTPUT_STRIP_TRAILING_WHITESPACE
)

execute_process(
	COMMAND llvm-config --libdir
	OUTPUT_VARIABLE LLVM_LIBDIR
	OUTPUT_STRIP_TRAILING_WHITESPACE
)

execute_process(
	COMMAND llvm-config --cxxflags
	OUTPUT_VARIABLE LLVM_CXXFLAGS
	OUTPUT_STRIP_TRAILING_WHITESPACE
)

execute_process(
	COMMAND llvm-config --ldflags
	OUTPUT_VARIABLE LLVM_LDFLAGS
	OUTPUT_STRIP_TRAILING_WHITESPACE
)

execute_process(
	COMMAND llvm-config --libs --system-libs
	OUTPUT_VARIABLE LLVM_LIBS
	OUTPUT_STRIP_TRAILING_WHITESPACE
)

# libsemind_parser (C++)
add_library(semind_parser SHARED
	src/semind.cpp
)

target_include_directories(semind_parser PUBLIC
	${CMAKE_CURRENT_SOURCE_DIR}/include
	${LLVM_INCLUDEDIR}
)

# Compile flags
separate_arguments(LLVM_CXXFLAGS)
target_compile_options(semind_parser PRIVATE ${LLVM_CXXFLAGS})
target_link_directories(semind_parser PRIVATE ${LLVM_LIBDIR})
target_link_libraries(semind_parser PRIVATE clang-cpp ${LLVM_LIBS})

add_executable(semind
	utils/semind.c
)

target_include_directories(semind PRIVATE include)
target_link_libraries(semind PRIVATE semind_parser)
