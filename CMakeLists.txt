cmake_minimum_required(VERSION 3.13)
project(semindex C CXX)

if(NOT CMAKE_CONFIGURATION_TYPES AND NOT CMAKE_BUILD_TYPE)
	set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type" FORCE)
endif()

include(CTest)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(SEMINDEX_FORMAT_SOURCES
	include/semindex.h
	include/semindex_database.h
	src/index_ast.cpp
	src/index_context.cpp
	src/lsp_indexer.cpp
	src/lsp_indexer.h
	src/lsp_source.cpp
	src/lsp_source.h
	src/lsp_server.cpp
	src/lsp_server.h
	src/lsp_transport.cpp
	src/lsp_transport.h
	src/index_pp.cpp
	src/index_pp.h
	src/semindex.cpp
	src/semindex_internal.h
	tests/include/test14.h
	tests/scope/api.h
	tests/scope/use.c
	tests/compiler-target.c
	tests/callgraph-a.c
	tests/callgraph-b.c
	tests/database-api.c
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
	tests/test13.c
	tests/test14.c
	tests/test15.c
	utils/cmd_compiler.c
	utils/cmd_compile_commands.c
	utils/cmd_callgraph.c
	utils/cmd_index.c
	utils/cmd_search.c
	utils/command_db.c
	utils/command_db.h
	utils/index_db.c
	utils/index_db.h
	utils/index_db_reader.c
	utils/output.h
	utils/output_common.c
	utils/output_default.c
	utils/output_dissect.c
	utils/output_search.c
	utils/perf_trace.c
	utils/perf_trace.h
	utils/semindex.c
	utils/semindex_cli.h
	utils/semindex_lsp.cpp
)

find_program(CLANG_FORMAT clang-format)
if(CLANG_FORMAT)
	add_custom_target(format
		COMMAND ${CLANG_FORMAT} -i ${SEMINDEX_FORMAT_SOURCES}
		WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
		COMMENT "Formatting source files"
	)
	add_custom_target(format-check
		COMMAND ${CLANG_FORMAT} --dry-run --Werror ${SEMINDEX_FORMAT_SOURCES}
		WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
		COMMENT "Checking source formatting"
	)
endif()

find_package(SQLite3 3.35 REQUIRED)
if(NOT TARGET SQLite3::SQLite3 AND TARGET SQLite::SQLite3)
	add_library(SQLite3::SQLite3 ALIAS SQLite::SQLite3)
endif()

add_library(semindex_database STATIC
	utils/index_db_reader.c
)
target_include_directories(semindex_database PUBLIC
	${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_link_libraries(semindex_database PUBLIC SQLite3::SQLite3)

add_library(semindex_storage STATIC
	utils/command_db.c
	utils/index_db.c
	utils/output_common.c
	utils/output_search.c
	utils/perf_trace.c
)
target_include_directories(semindex_storage PUBLIC
	${CMAKE_CURRENT_SOURCE_DIR}/include
	${CMAKE_CURRENT_SOURCE_DIR}/utils
)
target_link_libraries(semindex_storage PUBLIC semindex_database SQLite3::SQLite3)

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
	src/index_ast.cpp
	src/index_context.cpp
	src/index_pp.cpp
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

add_executable(semindex_lsp
	src/lsp_indexer.cpp
	src/lsp_source.cpp
	src/lsp_server.cpp
	src/lsp_transport.cpp
	utils/semindex_lsp.cpp
)
set_target_properties(semindex_lsp PROPERTIES OUTPUT_NAME semindex-lsp)
target_include_directories(semindex_lsp PRIVATE
	${CMAKE_CURRENT_SOURCE_DIR}/src
	${LLVM_INCLUDEDIR}
)
target_compile_options(semindex_lsp PRIVATE ${LLVM_CXXFLAGS})
target_link_directories(semindex_lsp PRIVATE ${LLVM_LIBDIR})
target_link_libraries(semindex_lsp PRIVATE semindex_parser semindex_storage ${LLVM_LIBS})

add_executable(semindex
	utils/cmd_compiler.c
	utils/cmd_compile_commands.c
	utils/cmd_callgraph.c
	utils/cmd_index.c
	utils/cmd_search.c
	utils/output_default.c
	utils/output_dissect.c
	utils/semindex.c
)

target_include_directories(semindex PRIVATE include)
target_link_libraries(semindex PRIVATE semindex_parser semindex_storage)

if(BUILD_TESTING)
	find_package(Python3 COMPONENTS Interpreter REQUIRED)

	add_executable(semindex_database_api_test
		tests/database-api.c
	)
	target_link_libraries(semindex_database_api_test PRIVATE semindex_database)

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
		tests/test13.c
		tests/test14.c
		tests/test15.c
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
	add_test(
		NAME semindex-compiler
		COMMAND
			${CMAKE_COMMAND} -E env
			SEMINDEX=$<TARGET_FILE:semindex>
			COMPILE_COMMANDS=${CMAKE_CURRENT_BINARY_DIR}/compile_commands.json
			SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
			sh ${CMAKE_CURRENT_SOURCE_DIR}/tests/run-compiler-tests.sh
	)
	add_test(
		NAME semindex-search
		COMMAND
			${CMAKE_COMMAND} -E env
			SEMINDEX=$<TARGET_FILE:semindex>
			COMPILE_COMMANDS=${CMAKE_CURRENT_BINARY_DIR}/compile_commands.json
			SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
			sh ${CMAKE_CURRENT_SOURCE_DIR}/tests/run-search-tests.sh
	)
	add_test(
		NAME semindex-database
		COMMAND
			${CMAKE_COMMAND} -E env
			SEMINDEX=$<TARGET_FILE:semindex>
			sh ${CMAKE_CURRENT_SOURCE_DIR}/tests/run-database-tests.sh
	)
	add_test(
		NAME semindex-database-api
		COMMAND
			${CMAKE_COMMAND} -E env
			SEMINDEX=$<TARGET_FILE:semindex>
			DATABASE_API_TEST=$<TARGET_FILE:semindex_database_api_test>
			COMPILE_COMMANDS=${CMAKE_CURRENT_BINARY_DIR}/compile_commands.json
			SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
			sh ${CMAKE_CURRENT_SOURCE_DIR}/tests/run-database-api-tests.sh
	)
	add_test(
		NAME semindex-command-database
		COMMAND
			${CMAKE_COMMAND} -E env
			SEMINDEX=$<TARGET_FILE:semindex>
			SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
			sh ${CMAKE_CURRENT_SOURCE_DIR}/tests/run-command-db-tests.sh
	)
	add_test(
		NAME semindex-callgraph
		COMMAND
			${CMAKE_COMMAND} -E env
			SEMINDEX=$<TARGET_FILE:semindex>
			SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
			sh ${CMAKE_CURRENT_SOURCE_DIR}/tests/run-callgraph-tests.sh
	)
	add_test(
		NAME semindex-trace
		COMMAND
			${CMAKE_COMMAND} -E env
			SEMINDEX=$<TARGET_FILE:semindex>
			COMPILE_COMMANDS=${CMAKE_CURRENT_BINARY_DIR}/compile_commands.json
			SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
			sh ${CMAKE_CURRENT_SOURCE_DIR}/tests/run-trace-tests.sh
	)
	add_test(
		NAME semindex-lsp
		COMMAND
			${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/run-lsp-tests.py
				$<TARGET_FILE:semindex_lsp>
				$<TARGET_FILE:semindex>
	)
endif()
