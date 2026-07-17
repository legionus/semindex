cmake_minimum_required(VERSION 3.13)
project(semindex C CXX)

include(CTest)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

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

# libsemindex (C++)
add_library(semindex_parser SHARED
	src/semindex.cpp
)
set_target_properties(semindex_parser PROPERTIES OUTPUT_NAME semindex)

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
	utils/output_default.c
	utils/output_dissect.c
	utils/semindex.c
)

target_include_directories(semindex PRIVATE include)
target_link_libraries(semindex PRIVATE semindex_parser)

if(BUILD_TESTING)
	foreach(test_source IN ITEMS
		tests/test.c
		tests/test1.c
		tests/test2.c
		tests/test3.c
		tests/test4.c
		tests/test5.c
		tests/test6.c
		tests/test7.c
		tests/test8.c
		tests/test9.c
		tests/test10.c
		tests/test11.c
		tests/test12.c
	)
		get_filename_component(test_name ${test_source} NAME)
		add_test(
			NAME semindex-${test_name}
			COMMAND
				${CMAKE_COMMAND} -E env
				SEMINDEX=$<TARGET_FILE:semindex>
				COMPILE_COMMANDS=${CMAKE_CURRENT_BINARY_DIR}/compile_commands.json
				SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
				sh ${CMAKE_CURRENT_SOURCE_DIR}/tests/run-output-tests.sh
					${test_source}
					${test_source}.expect
		)
		add_test(
			NAME semindex-dissect-${test_name}
			COMMAND
				${CMAKE_COMMAND} -E env
				SEMINDEX=$<TARGET_FILE:semindex>
				COMPILE_COMMANDS=${CMAKE_CURRENT_BINARY_DIR}/compile_commands.json
				SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
				sh ${CMAKE_CURRENT_SOURCE_DIR}/tests/run-output-tests.sh
					${test_source}
					${test_source}.dissect.expect
					dissect
		)
	endforeach()
	add_test(
		NAME semindex-scope
		COMMAND
			${CMAKE_COMMAND} -E env
			SEMINDEX=$<TARGET_FILE:semindex>
			COMPILE_COMMANDS=${CMAKE_CURRENT_BINARY_DIR}/compile_commands.json
			SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
			sh ${CMAKE_CURRENT_SOURCE_DIR}/tests/run-scope-tests.sh
	)
endif()
