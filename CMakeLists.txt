cmake_minimum_required(VERSION 3.13)
project(semindex C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

if(NOT CMAKE_CONFIGURATION_TYPES AND NOT CMAKE_BUILD_TYPE)
	set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type" FORCE)
endif()

include(GNUInstallDirs)
include(CTest)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(SEMINDEX_FORMAT_SOURCES
	include/semindex.h
	include/semindex_database.h
	src/index_ast.cpp
	src/index_context.cpp
	src/lsp_call_hierarchy.cpp
	src/lsp_call_hierarchy.h
	src/lsp_indexer.cpp
	src/lsp_indexer.h
	src/lsp_overlay.cpp
	src/lsp_overlay.h
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
	tests/partial-index.cpp
	tests/partial/broken-delimiter.c
	tests/partial/clean.c
	tests/partial/local-syntax.c
	tests/partial/missing-header.c
	tests/partial/semantic.c
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
	tests/test16.c
	tests/test17.c
	tests/test18.c
	tests/test19.c
	utils/cmd_compiler.c
	utils/cmd_compile_commands.c
	utils/cmd_callgraph.c
	utils/cmd_index.c
	utils/cmd_search.c
	utils/cli_common.c
	utils/command_main.c
	utils/command_db.c
	utils/command_db.h
	utils/index_db_callgraph.c
	utils/index_db.h
	utils/index_db_reader.c
	utils/index_db_search.c
	utils/index_db_writer.c
	utils/index_pipeline.c
	utils/index_pipeline.h
	utils/output.h
	utils/output_common.c
	utils/output_dissect.c
	utils/output_json.c
	utils/output_search.c
	utils/repository.c
	utils/perf_trace.c
	utils/perf_trace.h
	utils/sqlite.h
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

find_package(LLVM REQUIRED CONFIG)
find_package(Clang REQUIRED CONFIG)

# libsemindex_database
add_library(libsemindex_database SHARED
	utils/index_db_reader.c
)
target_include_directories(libsemindex_database PUBLIC
	$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
	$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/utils>
)
target_link_libraries(libsemindex_database PRIVATE SQLite3::SQLite3)

# libsemindex_query
add_library(libsemindex_query SHARED
	utils/command_db.c
	utils/index_db_callgraph.c
	utils/index_db_search.c
	utils/output_common.c
	utils/output_search.c
	utils/repository.c
)
target_include_directories(libsemindex_query PUBLIC
	$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
	$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/utils>
)
target_link_libraries(libsemindex_query
	PUBLIC libsemindex_database
	PRIVATE SQLite3::SQLite3
)

# libsemindex_storage
add_library(libsemindex_storage SHARED
	utils/index_db_writer.c
	utils/perf_trace.c
)
target_include_directories(libsemindex_storage PUBLIC
	$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
	$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/utils>
)
target_link_libraries(libsemindex_storage
	PUBLIC libsemindex_query
	PRIVATE SQLite3::SQLite3
)

# libsemindex (C++)
add_library(libsemindex SHARED
	src/index_ast.cpp
	src/index_context.cpp
	src/index_pp.cpp
	src/semindex.cpp
)
set_target_properties(libsemindex PROPERTIES OUTPUT_NAME semindex)

target_include_directories(libsemindex PUBLIC
	$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

# Compile flags
target_include_directories(libsemindex PRIVATE
	${LLVM_INCLUDE_DIRS}
	${CLANG_INCLUDE_DIRS}
)
target_compile_definitions(libsemindex PRIVATE ${LLVM_DEFINITIONS})
target_link_libraries(libsemindex PRIVATE
	clang-cpp
	LLVM
)

add_library(libsemindex_pipeline SHARED
	utils/index_pipeline.c
)
target_include_directories(libsemindex_pipeline PUBLIC
	$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
	$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/utils>
)
target_link_libraries(libsemindex_pipeline PUBLIC
	libsemindex
	libsemindex_storage
)

add_executable(semindex_lsp
	src/lsp_call_hierarchy.cpp
	src/lsp_indexer.cpp
	src/lsp_overlay.cpp
	src/lsp_source.cpp
	src/lsp_server.cpp
	src/lsp_transport.cpp
	utils/semindex_lsp.cpp
)
set_target_properties(semindex_lsp PROPERTIES OUTPUT_NAME semindex-lsp)

target_include_directories(semindex_lsp PRIVATE
	${CMAKE_CURRENT_SOURCE_DIR}/src
	${LLVM_INCLUDE_DIRS}
	${CLANG_INCLUDE_DIRS}
)
target_compile_definitions(semindex_lsp PRIVATE ${LLVM_DEFINITIONS})
target_link_libraries(semindex_lsp PRIVATE
	libsemindex_pipeline
	LLVM
)

add_library(semindex_cli_common STATIC
	utils/cli_common.c
	utils/output_dissect.c
	utils/output_json.c
)
target_include_directories(semindex_cli_common PRIVATE include)
target_link_libraries(semindex_cli_common PUBLIC libsemindex)

function(add_semindex_command target command source)
	add_executable(${target}
		${source}
		utils/command_main.c
	)
	target_compile_definitions(${target} PRIVATE SEMINDEX_COMMAND=cmd_${command})
	set_target_properties(${target} PROPERTIES OUTPUT_NAME semindex-${command})
endfunction()

add_executable(semindex_dispatcher utils/semindex.c)
set_target_properties(semindex_dispatcher PROPERTIES OUTPUT_NAME semindex)

add_semindex_command(semindex_compiler compiler utils/cmd_compiler.c)
target_link_libraries(semindex_compiler PRIVATE semindex_cli_common libsemindex_pipeline)

add_semindex_command(semindex_index index utils/cmd_index.c)
target_link_libraries(semindex_index PRIVATE semindex_cli_common libsemindex_pipeline)

add_semindex_command(semindex_search search utils/cmd_search.c)
target_link_libraries(semindex_search PRIVATE libsemindex_query)

add_semindex_command(semindex_callgraph callgraph utils/cmd_callgraph.c)
target_link_libraries(semindex_callgraph PRIVATE libsemindex_query)

add_semindex_command(semindex_compile_commands compile_commands utils/cmd_compile_commands.c)
set_target_properties(semindex_compile_commands PROPERTIES OUTPUT_NAME semindex-compile-commands)
target_link_libraries(semindex_compile_commands PRIVATE libsemindex_query)

install(TARGETS semindex_dispatcher semindex_compiler semindex_index semindex_search semindex_callgraph
	semindex_compile_commands semindex_lsp libsemindex libsemindex_database libsemindex_query libsemindex_storage
	libsemindex_pipeline
	RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
	LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
	ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

if(BUILD_TESTING)
	find_package(Python3 COMPONENTS Interpreter REQUIRED)

	# Keep fixture indexing independent of distribution Clang configuration.
	file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/tests)
	configure_file(
		tests/compile_commands.json.in
		${CMAKE_CURRENT_BINARY_DIR}/tests/compile_commands.json
		@ONLY
	)
	set(SEMINDEX_TEST_COMPILE_COMMANDS
		${CMAKE_CURRENT_BINARY_DIR}/tests/compile_commands.json
	)

	add_executable(semindex_database_api_test
		tests/database-api.c
	)
	target_link_libraries(semindex_database_api_test PRIVATE libsemindex_database)

	add_executable(semindex_partial_index_test
		tests/partial-index.cpp
	)
	target_link_libraries(semindex_partial_index_test PRIVATE libsemindex)

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
		tests/test16.c
		tests/test17.c
		tests/test18.c
		tests/test19.c
	)
		get_filename_component(test_name ${test_source} NAME)
		add_test(
			NAME semindex-json-${test_name}
			COMMAND
				${CMAKE_COMMAND} -E env
				SEMINDEX=$<TARGET_FILE:semindex_dispatcher>
				SEMINDEX_INDEX=$<TARGET_FILE:semindex_index>
				COMPILE_COMMANDS=${SEMINDEX_TEST_COMPILE_COMMANDS}
				SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
				PYTHON=${Python3_EXECUTABLE}
				sh ${CMAKE_CURRENT_SOURCE_DIR}/tests/run-output-tests.sh
					${test_source}
					${test_source}.json.expect
					json
		)
		add_test(
			NAME semindex-dissect-${test_name}
			COMMAND
				${CMAKE_COMMAND} -E env
				SEMINDEX=$<TARGET_FILE:semindex_dispatcher>
				SEMINDEX_INDEX=$<TARGET_FILE:semindex_index>
				COMPILE_COMMANDS=${SEMINDEX_TEST_COMPILE_COMMANDS}
				SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
				PYTHON=${Python3_EXECUTABLE}
				sh ${CMAKE_CURRENT_SOURCE_DIR}/tests/run-output-tests.sh
					${test_source}
					${test_source}.dissect.expect
		)
	endforeach()
	add_test(
		NAME semindex-dispatcher
		COMMAND
			sh ${CMAKE_CURRENT_SOURCE_DIR}/tests/run-dispatcher-tests.sh
				$<TARGET_FILE:semindex_dispatcher>
				$<TARGET_FILE:semindex_compiler>
				$<TARGET_FILE:semindex_index>
				$<TARGET_FILE:semindex_search>
				$<TARGET_FILE:semindex_callgraph>
				$<TARGET_FILE:semindex_compile_commands>
				$<TARGET_FILE:semindex_lsp>
	)
	add_test(
		NAME semindex-scope
		COMMAND
			${CMAKE_COMMAND} -E env
			SEMINDEX=$<TARGET_FILE:semindex_dispatcher>
			COMPILE_COMMANDS=${SEMINDEX_TEST_COMPILE_COMMANDS}
			SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
			sh ${CMAKE_CURRENT_SOURCE_DIR}/tests/run-scope-tests.sh
	)
	add_test(
		NAME semindex-compiler
		COMMAND
			${CMAKE_COMMAND} -E env
			SEMINDEX=$<TARGET_FILE:semindex_dispatcher>
			COMPILE_COMMANDS=${SEMINDEX_TEST_COMPILE_COMMANDS}
			SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
			PYTHON=${Python3_EXECUTABLE}
			sh ${CMAKE_CURRENT_SOURCE_DIR}/tests/run-compiler-tests.sh
	)
	add_test(
		NAME semindex-search
		COMMAND
			${CMAKE_COMMAND} -E env
			SEMINDEX=$<TARGET_FILE:semindex_dispatcher>
			COMPILE_COMMANDS=${SEMINDEX_TEST_COMPILE_COMMANDS}
			SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
			sh ${CMAKE_CURRENT_SOURCE_DIR}/tests/run-search-tests.sh
	)
	add_test(
		NAME semindex-database
		COMMAND
			${CMAKE_COMMAND} -E env
			SEMINDEX=$<TARGET_FILE:semindex_dispatcher>
			sh ${CMAKE_CURRENT_SOURCE_DIR}/tests/run-database-tests.sh
	)
	add_test(
		NAME semindex-database-api
		COMMAND
			${CMAKE_COMMAND} -E env
			SEMINDEX=$<TARGET_FILE:semindex_dispatcher>
			DATABASE_API_TEST=$<TARGET_FILE:semindex_database_api_test>
			COMPILE_COMMANDS=${SEMINDEX_TEST_COMPILE_COMMANDS}
			SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
			sh ${CMAKE_CURRENT_SOURCE_DIR}/tests/run-database-api-tests.sh
	)
	add_test(
		NAME semindex-partial-index
		COMMAND
			$<TARGET_FILE:semindex_partial_index_test>
			${CMAKE_C_COMPILER}
			${CMAKE_CURRENT_SOURCE_DIR}/tests/partial
	)
	add_test(
		NAME semindex-partial-cli
		COMMAND
			${CMAKE_COMMAND} -E env
			SEMINDEX=$<TARGET_FILE:semindex_dispatcher>
			SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
			sh ${CMAKE_CURRENT_SOURCE_DIR}/tests/run-partial-cli-tests.sh
	)
	add_test(
		NAME semindex-command-database
		COMMAND
			${CMAKE_COMMAND} -E env
			SEMINDEX=$<TARGET_FILE:semindex_dispatcher>
			SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
			sh ${CMAKE_CURRENT_SOURCE_DIR}/tests/run-command-db-tests.sh
	)
	add_test(
		NAME semindex-callgraph
		COMMAND
			${CMAKE_COMMAND} -E env
			SEMINDEX=$<TARGET_FILE:semindex_dispatcher>
			SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
			sh ${CMAKE_CURRENT_SOURCE_DIR}/tests/run-callgraph-tests.sh
	)
	add_test(
		NAME semindex-trace
		COMMAND
			${CMAKE_COMMAND} -E env
			SEMINDEX=$<TARGET_FILE:semindex_dispatcher>
			COMPILE_COMMANDS=${SEMINDEX_TEST_COMPILE_COMMANDS}
			SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
			sh ${CMAKE_CURRENT_SOURCE_DIR}/tests/run-trace-tests.sh
	)
	add_test(
		NAME semindex-lsp
		COMMAND
			${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/run-lsp-tests.py
				$<TARGET_FILE:semindex_lsp>
				$<TARGET_FILE:semindex_dispatcher>
	)
endif()
