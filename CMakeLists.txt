cmake_minimum_required(VERSION 3.13)
project(semindex C CXX)

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
add_library(semindex_parser SHARED
	src/semindex.cpp
)

target_include_directories(semindex_parser PUBLIC
	${CMAKE_CURRENT_SOURCE_DIR}/include
	${LLVM_INCLUDEDIR}
)

# Compile flags
separate_arguments(LLVM_CXXFLAGS)
target_compile_options(semindex_parser PRIVATE ${LLVM_CXXFLAGS})
target_link_directories(semindex_parser PRIVATE ${LLVM_LIBDIR})
target_link_libraries(semindex_parser PRIVATE clang-cpp ${LLVM_LIBS})

add_executable(semindex
	utils/semindex.c
)

target_include_directories(semindex PRIVATE include)
target_link_libraries(semindex PRIVATE semindex_parser)
