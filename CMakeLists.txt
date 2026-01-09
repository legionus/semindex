cmake_minimum_required(VERSION 3.13)
project(semind LANGUAGES CXX)

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

add_executable(semind semind.cpp)

# Includes
target_include_directories(semind PRIVATE ${LLVM_INCLUDEDIR})

# Compile flags
separate_arguments(LLVM_CXXFLAGS)
target_compile_options(semind PRIVATE ${LLVM_CXXFLAGS})

# Linker flags
separate_arguments(LLVM_LDFLAGS)
target_link_options(semind PRIVATE ${LLVM_LDFLAGS})

target_link_directories(semind PRIVATE ${LLVM_LIBDIR})

target_link_libraries(semind PRIVATE
	clang-cpp
	${LLVM_LIBS}
)
