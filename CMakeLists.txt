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
	include/semindex_export.h
	lib/common/semindex_paths.h
	lib/parser/index_ast.cpp
	lib/parser/index_asm.cpp
	lib/parser/index_asm.h
	lib/parser/index_context.cpp
	lib/index-update/index_command_resolver.cpp
	lib/index-update/index_command_resolver.h
	lib/index-update/index_updater.cpp
	lib/index-update/index_updater.h
	tools/lsp/lsp_call_hierarchy.cpp
	tools/lsp/lsp_call_hierarchy.h
	tools/lsp/lsp_indexer.cpp
	tools/lsp/lsp_indexer.h
	tools/lsp/lsp_overlay.cpp
	tools/lsp/lsp_overlay.h
	tools/lsp/lsp_source.cpp
	tools/lsp/lsp_source.h
	tools/lsp/lsp_server.cpp
	tools/lsp/lsp_server.h
	tools/lsp/lsp_transport.cpp
	tools/lsp/lsp_transport.h
	tools/mcp/mcp_callgraph_tools.cpp
	tools/mcp/mcp_server.cpp
	tools/mcp/mcp_server.h
	tools/mcp/mcp_source_tools.cpp
	tools/mcp/mcp_symbol_tools.cpp
	tools/mcp/mcp_tools.cpp
	tools/mcp/mcp_tools.h
	tools/mcp/mcp_tools_internal.h
	tools/mcp/mcp_transport.cpp
	tools/mcp/mcp_transport.h
	lib/protocol/protocol_logger.cpp
	lib/protocol/protocol_logger.h
	lib/semantic/semantic_query.cpp
	lib/semantic/semantic_query.h
	lib/parser/index_pp.cpp
	lib/parser/index_pp.h
	lib/parser/semindex.cpp
	lib/parser/semindex_internal.h
	lib/source/source_resolver.cpp
	lib/source/source_resolver.h
	tests/include/test14.h
	tests/scope/api.h
	tests/scope/use.c
	tests/compiler-target.c
	tests/callgraph-a.c
	tests/callgraph-b.c
	tests/database-api.c
	tests/partial-index.cpp
	tests/semantic-query.cpp
	tests/source-resolver.cpp
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
	tests/test20.c
	lib/common/array_size.h
	tools/callgraph/cmd.c
	tools/cc/cmd.c
	tools/compile-commands/cmd.c
	tools/compiler/cmd.c
	tools/index/cmd.c
	tools/search/cmd.c
	tools/common/cli_common.c
	tools/common/command_main.c
	tools/common/compiler_command.c
	tools/common/compiler_command.h
	lib/query/command_db.c
	lib/query/command_db_export.c
	lib/query/command_db.h
	lib/query/command_db_internal.h
	lib/query/command_db_reader.c
	lib/storage/command_db_writer.c
	lib/storage/filesystem.c
	lib/storage/filesystem.h
	lib/git/git_provenance.c
	lib/git/git_provenance.h
	lib/git/git_provenance_loader.c
	lib/git/git_provenance_stub.c
	tools/common/index_command.c
	tools/common/index_command.h
	lib/query/index_db_callgraph.c
	lib/query/index_db.h
	lib/database/index_db_reader.c
	lib/query/index_db_search.c
	lib/storage/index_db_writer.c
	lib/pipeline/index_pipeline.c
	lib/pipeline/index_pipeline.h
	lib/common/output.h
	lib/query/output_common.c
	tools/common/output_dissect.c
	tools/common/output_json.c
	lib/query/output_search.c
	lib/query/repository.c
	lib/storage/perf_trace.c
	lib/common/perf_trace.h
	lib/sqlite/sqlite.c
	lib/sqlite/sqlite.h
	tools/semindex/main.c
	tools/common/semindex_cli.h
	lib/names/semantic_names.c
	lib/names/semantic_names.h
	tools/lsp/main.cpp
	tools/mcp/main.cpp
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

option(SEMINDEX_GIT "Enable Git provenance when libgit2 is available" ON)

find_package(PkgConfig QUIET)
if(SEMINDEX_GIT AND PkgConfig_FOUND)
	pkg_check_modules(LibGit2 QUIET IMPORTED_TARGET libgit2)
endif()
if(TARGET PkgConfig::LibGit2)
	message(STATUS "Git provenance support: enabled")
elseif(NOT SEMINDEX_GIT)
	message(STATUS "Git provenance support: disabled by SEMINDEX_GIT")
else()
	message(STATUS "Git provenance support: disabled (libgit2 not found)")
endif()

find_package(LLVM REQUIRED CONFIG)
find_package(Clang REQUIRED CONFIG)
find_package(Threads REQUIRED)

# Internal SQLite helpers
add_library(semindex_sqlite STATIC
	lib/sqlite/sqlite.c
)
set_target_properties(semindex_sqlite PROPERTIES
	C_VISIBILITY_PRESET hidden
	POSITION_INDEPENDENT_CODE YES
)
target_include_directories(semindex_sqlite PUBLIC
	$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/lib/sqlite>
)
target_link_libraries(semindex_sqlite PUBLIC SQLite3::SQLite3)

# Streaming database reader
add_library(semindex_database SHARED
	lib/database/index_db_reader.c
)
set_target_properties(semindex_database PROPERTIES
	C_VISIBILITY_PRESET hidden
	LINK_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/lib/database/libsemindex_database.map
)
target_link_options(semindex_database PRIVATE
	-Wl,--version-script=${CMAKE_CURRENT_SOURCE_DIR}/lib/database/libsemindex_database.map
)
target_include_directories(semindex_database PUBLIC
	$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_link_libraries(semindex_database PRIVATE semindex_sqlite)

# Optional Git backend loader
add_library(semindex_git STATIC)
if(TARGET PkgConfig::LibGit2)
	add_library(semindex_git_backend MODULE lib/git/git_provenance.c)
	set_target_properties(semindex_git_backend PROPERTIES
		OUTPUT_NAME semindex_git_backend
	)
	target_include_directories(semindex_git_backend PRIVATE lib/git)
	target_link_libraries(semindex_git_backend PRIVATE PkgConfig::LibGit2)

	target_sources(semindex_git PRIVATE lib/git/git_provenance_loader.c)
	target_compile_definitions(semindex_git PUBLIC SEMINDEX_HAVE_LIBGIT2=1)
	target_compile_definitions(semindex_git PRIVATE
		SEMINDEX_GIT_BACKEND_NAME="$<TARGET_FILE_NAME:semindex_git_backend>"
	)
	target_link_libraries(semindex_git PRIVATE ${CMAKE_DL_LIBS})
	add_dependencies(semindex_git semindex_git_backend)
else()
	target_sources(semindex_git PRIVATE lib/git/git_provenance_stub.c)
endif()
target_include_directories(semindex_git PUBLIC
	$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/lib/git>
)

# Source path resolution and working-tree access
add_library(semindex_source STATIC
	lib/source/source_resolver.cpp
)
target_include_directories(semindex_source PUBLIC
	$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
	$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/lib/source>
)
target_link_libraries(semindex_source PUBLIC semindex_database semindex_git)

# Canonical semantic names
add_library(semindex_names STATIC
	lib/names/semantic_names.c
)
target_include_directories(semindex_names PUBLIC
	$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
	$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/lib/common>
	$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/lib/names>
)

# Protocol-neutral semantic queries
add_library(semindex_semantic STATIC
	lib/semantic/semantic_query.cpp
)
target_include_directories(semindex_semantic PUBLIC
	$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
	$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/lib/semantic>
)
target_link_libraries(semindex_semantic PUBLIC semindex_database semindex_source)

# SQLite query adapters
add_library(semindex_query STATIC
	lib/query/command_db.c
	lib/query/command_db_export.c
	lib/query/command_db_reader.c
	lib/query/index_db_callgraph.c
	lib/query/index_db_search.c
	lib/query/output_common.c
	lib/query/output_search.c
	lib/query/repository.c
)
target_include_directories(semindex_query PUBLIC
	$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
	$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/lib/common>
	$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/lib/query>
)
target_link_libraries(semindex_query
	PUBLIC semindex_database
	PRIVATE semindex_sqlite
)

# SQLite storage adapters
add_library(semindex_storage STATIC
	lib/storage/command_db_writer.c
	lib/storage/filesystem.c
	lib/storage/index_db_writer.c
	lib/storage/perf_trace.c
)
target_include_directories(semindex_storage PUBLIC
	$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
	$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/lib/storage>
)
target_link_libraries(semindex_storage
	PUBLIC semindex_query
	PRIVATE semindex_sqlite
)

# Clang parser and index construction
add_library(semindex_parser SHARED
	lib/parser/index_asm.cpp
	lib/parser/index_ast.cpp
	lib/parser/index_context.cpp
	lib/parser/index_pp.cpp
	lib/parser/semindex.cpp
)
set_target_properties(semindex_parser PROPERTIES
	CXX_VISIBILITY_PRESET hidden
	LINK_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/lib/parser/libsemindex.map
	OUTPUT_NAME semindex
	VISIBILITY_INLINES_HIDDEN YES
)
target_link_options(semindex_parser PRIVATE
	-Wl,--version-script=${CMAKE_CURRENT_SOURCE_DIR}/lib/parser/libsemindex.map
)

target_include_directories(semindex_parser PUBLIC
	$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

# Compile flags
target_include_directories(semindex_parser PRIVATE
	${LLVM_INCLUDE_DIRS}
	${CLANG_INCLUDE_DIRS}
)
target_compile_definitions(semindex_parser PRIVATE ${LLVM_DEFINITIONS})
target_link_libraries(semindex_parser PRIVATE
	clang-cpp
	LLVM
)

# Indexing orchestration
add_library(semindex_pipeline STATIC
	lib/pipeline/index_pipeline.c
)
target_include_directories(semindex_pipeline PUBLIC
	$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
	$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/lib/pipeline>
)
target_link_libraries(semindex_pipeline PUBLIC
	semindex_git
	semindex_parser
	semindex_storage
)

add_library(semindex_index_update STATIC
	lib/index-update/index_command_resolver.cpp
	lib/index-update/index_updater.cpp
)
target_include_directories(semindex_index_update PUBLIC
	$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/lib/index-update>
)
target_link_libraries(semindex_index_update PUBLIC
	semindex_pipeline
)

add_library(semindex_protocol STATIC
	lib/protocol/protocol_logger.cpp
)
target_include_directories(semindex_protocol PUBLIC
	$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/lib/protocol>
)

add_executable(semindex_lsp
	tools/lsp/lsp_call_hierarchy.cpp
	tools/lsp/lsp_indexer.cpp
	tools/lsp/lsp_overlay.cpp
	tools/lsp/lsp_source.cpp
	tools/lsp/lsp_server.cpp
	tools/lsp/lsp_transport.cpp
	tools/lsp/main.cpp
)
set_target_properties(semindex_lsp PROPERTIES OUTPUT_NAME semindex-lsp)

target_include_directories(semindex_lsp PRIVATE
	${CMAKE_CURRENT_SOURCE_DIR}/tools/lsp
	${LLVM_INCLUDE_DIRS}
	${CLANG_INCLUDE_DIRS}
)
target_compile_definitions(semindex_lsp PRIVATE ${LLVM_DEFINITIONS})
target_link_libraries(semindex_lsp PRIVATE
	semindex_index_update
	semindex_protocol
	semindex_semantic
	LLVM
)
set_target_properties(semindex_lsp PROPERTIES
	BUILD_RPATH "$ORIGIN"
	INSTALL_RPATH "${CMAKE_INSTALL_FULL_LIBDIR}"
)

add_executable(semindex_mcp
	tools/mcp/mcp_callgraph_tools.cpp
	tools/mcp/mcp_server.cpp
	tools/mcp/mcp_source_tools.cpp
	tools/mcp/mcp_symbol_tools.cpp
	tools/mcp/mcp_tools.cpp
	tools/mcp/mcp_transport.cpp
	tools/mcp/main.cpp
)
set_target_properties(semindex_mcp PROPERTIES OUTPUT_NAME semindex-mcp)
target_include_directories(semindex_mcp PRIVATE
	${CMAKE_CURRENT_SOURCE_DIR}/tools/mcp
	${LLVM_INCLUDE_DIRS}
)
target_compile_definitions(semindex_mcp PRIVATE ${LLVM_DEFINITIONS})
target_link_libraries(semindex_mcp PRIVATE
	semindex_index_update
	semindex_names
	semindex_protocol
	semindex_semantic
	Threads::Threads
	LLVM
)
set_target_properties(semindex_mcp PROPERTIES
	BUILD_RPATH "$ORIGIN"
	INSTALL_RPATH "${CMAKE_INSTALL_FULL_LIBDIR}"
)

add_library(semindex_cli_common STATIC
	tools/common/cli_common.c
	tools/common/index_command.c
	tools/common/output_dissect.c
	tools/common/output_json.c
)
target_include_directories(semindex_cli_common
	PUBLIC
		$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/lib/common>
		$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/lib/pipeline>
		$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/lib/query>
		$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/tools/common>
	PRIVATE
		${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_link_libraries(semindex_cli_common PUBLIC semindex_names semindex_parser)

function(add_semindex_command target command source)
	add_executable(${target}
		${source}
		tools/common/command_main.c
	)
	target_compile_definitions(${target} PRIVATE SEMINDEX_COMMAND=cmd_${command})
	set_target_properties(${target} PROPERTIES OUTPUT_NAME semindex-${command})
endfunction()

add_executable(semindex_dispatcher tools/semindex/main.c)
set_target_properties(semindex_dispatcher PROPERTIES OUTPUT_NAME semindex)

add_semindex_command(semindex_compiler compiler tools/compiler/cmd.c)
target_sources(semindex_compiler PRIVATE tools/common/compiler_command.c)
target_link_libraries(semindex_compiler PRIVATE semindex_cli_common semindex_pipeline)
set_target_properties(semindex_compiler PROPERTIES
	BUILD_RPATH "$ORIGIN"
	INSTALL_RPATH "${CMAKE_INSTALL_FULL_LIBDIR}"
)

add_semindex_command(semindex_cc cc tools/cc/cmd.c)
target_sources(semindex_cc PRIVATE tools/common/compiler_command.c)
target_link_libraries(semindex_cc PRIVATE semindex_cli_common semindex_pipeline)
set_target_properties(semindex_cc PROPERTIES
	BUILD_RPATH "$ORIGIN"
	INSTALL_RPATH "${CMAKE_INSTALL_FULL_LIBDIR}"
)

add_semindex_command(semindex_index index tools/index/cmd.c)
target_link_libraries(semindex_index PRIVATE semindex_cli_common semindex_pipeline)
set_target_properties(semindex_index PROPERTIES
	BUILD_RPATH "$ORIGIN"
	INSTALL_RPATH "${CMAKE_INSTALL_FULL_LIBDIR}"
)

add_semindex_command(semindex_search search tools/search/cmd.c)
target_link_libraries(semindex_search PRIVATE semindex_cli_common semindex_query)

add_semindex_command(semindex_callgraph callgraph tools/callgraph/cmd.c)
target_link_libraries(semindex_callgraph PRIVATE semindex_query)

add_semindex_command(semindex_compile_commands compile_commands tools/compile-commands/cmd.c)
set_target_properties(semindex_compile_commands PROPERTIES OUTPUT_NAME semindex-compile-commands)
target_link_libraries(semindex_compile_commands PRIVATE semindex_query)

install(TARGETS semindex_dispatcher semindex_cc semindex_compiler semindex_index semindex_search semindex_callgraph
	semindex_compile_commands semindex_lsp semindex_mcp semindex_parser semindex_database
	RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
	LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
	ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)
if(TARGET semindex_git_backend)
	install(TARGETS semindex_git_backend LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR})
endif()
install(FILES include/semindex.h include/semindex_database.h include/semindex_export.h
	DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
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
	target_link_libraries(semindex_database_api_test PRIVATE semindex_database)

	add_executable(semindex_partial_index_test
		tests/partial-index.cpp
	)
	target_link_libraries(semindex_partial_index_test PRIVATE semindex_parser)

	add_executable(semindex_source_resolver_test
		tests/source-resolver.cpp
	)
	target_link_libraries(semindex_source_resolver_test PRIVATE semindex_source)

	add_executable(semindex_semantic_query_test
		tests/semantic-query.cpp
	)
	target_link_libraries(semindex_semantic_query_test PRIVATE semindex_semantic)

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
		tests/test20.c
		tests/test21.S
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
				$<TARGET_FILE:semindex_cc>
				$<TARGET_FILE:semindex_compiler>
				$<TARGET_FILE:semindex_index>
				$<TARGET_FILE:semindex_search>
				$<TARGET_FILE:semindex_callgraph>
				$<TARGET_FILE:semindex_compile_commands>
				$<TARGET_FILE:semindex_lsp>
				$<TARGET_FILE:semindex_mcp>
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
		NAME semindex-cc
		COMMAND
			${CMAKE_COMMAND} -E env
			SEMINDEX=$<TARGET_FILE:semindex_dispatcher>
			SEMINDEX_CC=$<TARGET_FILE:semindex_cc>
			CC=${CMAKE_C_COMPILER}
			SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
			sh ${CMAKE_CURRENT_SOURCE_DIR}/tests/run-cc-tests.sh
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
		NAME semindex-parser-exports
		COMMAND
			sh ${CMAKE_CURRENT_SOURCE_DIR}/tests/run-export-tests.sh
				${CMAKE_NM}
				$<TARGET_FILE:semindex_parser>
				${CMAKE_CURRENT_SOURCE_DIR}/tests/libsemindex.exports
	)
	add_test(
		NAME semindex-database-exports
		COMMAND
			sh ${CMAKE_CURRENT_SOURCE_DIR}/tests/run-export-tests.sh
				${CMAKE_NM}
				$<TARGET_FILE:semindex_database>
				${CMAKE_CURRENT_SOURCE_DIR}/tests/libsemindex_database.exports
	)
	add_test(
		NAME semindex-partial-index
		COMMAND
			$<TARGET_FILE:semindex_partial_index_test>
			${CMAKE_C_COMPILER}
			${CMAKE_CURRENT_SOURCE_DIR}/tests/partial
	)
	add_test(
		NAME semindex-source-resolver
		COMMAND
			${CMAKE_COMMAND} -E env
			SEMINDEX=$<TARGET_FILE:semindex_dispatcher>
			SOURCE_RESOLVER_TEST=$<TARGET_FILE:semindex_source_resolver_test>
			SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
			sh ${CMAKE_CURRENT_SOURCE_DIR}/tests/run-source-resolver-tests.sh
	)
	add_test(
		NAME semindex-semantic-query
		COMMAND
			${CMAKE_COMMAND} -E env
			SEMINDEX=$<TARGET_FILE:semindex_dispatcher>
			SEMINDEX_COMPILER=$<TARGET_FILE:semindex_compiler>
			SEMANTIC_QUERY_TEST=$<TARGET_FILE:semindex_semantic_query_test>
			SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
			sh ${CMAKE_CURRENT_SOURCE_DIR}/tests/run-semantic-query-tests.sh
	)
	if(TARGET PkgConfig::LibGit2)
		set_tests_properties(semindex-source-resolver PROPERTIES
			ENVIRONMENT SEMINDEX_TEST_GIT=1
		)
	endif()
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
		NAME semindex-asm
		COMMAND
			sh ${CMAKE_CURRENT_SOURCE_DIR}/tests/run-asm-tests.sh
				$<TARGET_FILE:semindex_dispatcher>
				${CMAKE_CURRENT_SOURCE_DIR}
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
	add_test(
		NAME semindex-mcp
		COMMAND
			${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/run-mcp-tests.py
				$<TARGET_FILE:semindex_mcp>
				$<TARGET_FILE:semindex_dispatcher>
				${CMAKE_CURRENT_SOURCE_DIR}
	)
endif()
