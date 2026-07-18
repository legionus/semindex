// SPDX-License-Identifier: GPL-2.0-or-later
#include "semindex_internal.h"

#include <clang/Tooling/CompilationDatabase.h>
#include <clang/Tooling/Tooling.h>

#include <llvm/Support/Path.h>
#include <llvm/Support/raw_ostream.h>

#include <memory>
#include <string>
#include <utility>
#include <vector>

using namespace clang::tooling;

static std::unique_ptr<CompilationDatabase> loadCompileCommands(const char *compile_commands_json, std::string &error)
{
	if (!compile_commands_json || !compile_commands_json[0]) {
		error = "missing compile_commands.json path";
		return nullptr;
	}

	llvm::StringRef path(compile_commands_json);
	std::string directory;

	if (llvm::sys::path::filename(path) == "compile_commands.json")
		directory = llvm::sys::path::parent_path(path).str();
	else
		directory = path.str();

	if (directory.empty())
		directory = ".";

	return CompilationDatabase::loadFromDirectory(directory, error);
}

class SingleCompilationDatabase : public CompilationDatabase
{
public:
	explicit SingleCompilationDatabase(CompileCommand command) : command(std::move(command))
	{
	}

	std::vector<CompileCommand> getCompileCommands(llvm::StringRef) const override
	{
		return { command };
	}

private:
	CompileCommand command;
};

static void clearIndex(semindex_t *s)
{
	s->next_order = 0;
	s->symbols.clear();
	s->uses.clear();
	s->symbol_records.clear();
	s->use_records.clear();
	s->files.clear();
}

extern "C" {

semindex_t *semindex_create(void)
{
	return new semindex{};
}

void semindex_destroy(semindex_t *s)
{
	delete s;
}

void semindex_set_scope(semindex_t *s, semindex_scope_t scope)
{
	if (s)
		s->scope = scope;
}

int semindex_index_command(semindex_t *s, const semindex_compile_command_t *cmd)
{
	if (!s || !cmd || !cmd->file || !cmd->argv || !cmd->argc)
		return -1;

	clearIndex(s);

	std::vector<std::string> args;
	args.reserve(cmd->argc);
	for (size_t i = 0; i < cmd->argc; i++)
		args.emplace_back(cmd->argv[i]);

	CompileCommand compile(cmd->directory ? cmd->directory : ".", cmd->file, args, "");
	SingleCompilationDatabase db(compile);
	ClangTool tool(db, { cmd->file });
	std::unique_ptr<FrontendActionFactory> factory = createSemindexActionFactory(s);
	int ret = tool.run(factory.get());

	if (ret == 0)
		rebuildRecords(s);

	return ret;
}

int semindex_index_file(semindex_t *s, const char *compile_commands_json, const char *source_file)
{
	if (!s || !source_file)
		return -1;

	std::string error;
	std::unique_ptr<CompilationDatabase> db = loadCompileCommands(compile_commands_json, error);

	if (!db) {
		llvm::errs() << "semindex: failed to load compilation database";
		if (compile_commands_json)
			llvm::errs() << " from '" << compile_commands_json << "'";
		if (!error.empty())
			llvm::errs() << ": " << error;
		llvm::errs() << "\n";
		return -1;
	}

	std::vector<CompileCommand> commands = db->getCompileCommands(source_file);
	if (commands.empty()) {
		llvm::errs() << "semindex: no compile command for '" << source_file << "'\n";
		return -1;
	}

	std::vector<const char *> argv;
	const CompileCommand &compile = commands[0];
	argv.reserve(compile.CommandLine.size());
	for (const auto &arg : compile.CommandLine)
		argv.push_back(arg.c_str());

	semindex_compile_command_t cmd;

	cmd.directory = compile.Directory.c_str();
	cmd.file = source_file;
	cmd.argc = argv.size();
	cmd.argv = argv.data();

	return semindex_index_command(s, &cmd);
}

size_t semindex_symbol_count(const semindex_t *s)
{
	return s ? s->symbols.size() : 0;
}

const semindex_symbol_t *semindex_get_symbol(const semindex_t *s, size_t idx)
{
	if (!s || idx >= s->symbol_records.size())
		return nullptr;

	return &s->symbol_records[idx];
}

size_t semindex_use_count(const semindex_t *s)
{
	return s ? s->uses.size() : 0;
}

const semindex_use_t *semindex_get_use(const semindex_t *s, size_t idx)
{
	if (!s || idx >= s->use_records.size())
		return nullptr;

	return &s->use_records[idx];
}

} /* extern "C" */
