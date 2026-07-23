// SPDX-License-Identifier: GPL-2.0-or-later
#include "semindex_internal.h"

#include <clang/Basic/Diagnostic.h>
#include <clang/Basic/DiagnosticOptions.h>
#include <clang/Frontend/TextDiagnosticPrinter.h>
#include <clang/Tooling/CompilationDatabase.h>
#include <clang/Tooling/Tooling.h>

#include <llvm/ADT/SmallString.h>
#include <llvm/Support/Path.h>
#include <llvm/Support/raw_ostream.h>

#include <memory>
#include <string>
#include <utility>
#include <vector>

using namespace clang::tooling;

class SemindexDiagnosticConsumer : public clang::TextDiagnosticPrinter
{
public:
	SemindexDiagnosticConsumer(semindex *out, clang::DiagnosticOptions &options)
	    : clang::TextDiagnosticPrinter(llvm::errs(), options), out(out)
	{
	}

	void HandleDiagnostic(clang::DiagnosticsEngine::Level level, const clang::Diagnostic &info) override
	{
		clang::TextDiagnosticPrinter::HandleDiagnostic(level, info);

		if (level == clang::DiagnosticsEngine::Fatal)
			fatal = true;

		if (level < clang::DiagnosticsEngine::Note)
			return;

		SemindexDiagnostic diagnostic;
		llvm::SmallString<128> message;

		if (level >= clang::DiagnosticsEngine::Error)
			diagnostic.severity = SEMINDEX_DIAGNOSTIC_ERROR;

		else if (level == clang::DiagnosticsEngine::Warning)
			diagnostic.severity = SEMINDEX_DIAGNOSTIC_WARNING;
		else
			diagnostic.severity = SEMINDEX_DIAGNOSTIC_NOTE;
		info.FormatDiagnostic(message);
		diagnostic.message = message.str().str();
		diagnostic.line = 0;
		diagnostic.column = 0;

		if (info.hasSourceManager() && info.getLocation().isValid()) {
			clang::PresumedLoc location = info.getSourceManager().getPresumedLoc(info.getLocation());

			if (location.isValid()) {
				diagnostic.file = location.getFilename();
				diagnostic.line = location.getLine();
				diagnostic.column = location.getColumn();
			}
		}
		out->diagnostics.push_back(std::move(diagnostic));
	}

	bool fatalOccurred() const
	{
		return fatal;
	}

private:
	semindex *out;
	bool fatal = false;
};

static void rebuildDiagnostics(semindex *s)
{
	s->diagnostic_records.clear();
	s->diagnostic_records.reserve(s->diagnostics.size());

	for (const auto &diagnostic : s->diagnostics) {
		s->diagnostic_records.push_back({
			.severity = diagnostic.severity,
			.message = diagnostic.message.c_str(),
			.file = diagnostic.file.c_str(),
			.line = diagnostic.line,
			.column = diagnostic.column,
		});
	}
}

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
	s->file_fingerprints[0].clear();
	s->file_fingerprints[1].clear();
	s->files.clear();
	s->command_directory.clear();
	s->command_file.clear();
	s->command_arguments.clear();
	s->command_argv.clear();
	s->command_record = {};
	s->index_result = { SEMINDEX_INDEX_FAILED, 0, 0, 0 };
	s->has_index_data = false;
	s->diagnostics.clear();
	s->diagnostic_records.clear();
}

static void saveCompileCommand(semindex_t *s, const semindex_compile_command_t *cmd)
{
	s->command_directory = cmd->directory ? cmd->directory : ".";
	s->command_file = cmd->file;
	s->command_arguments.reserve(cmd->argc);

	for (size_t i = 0; i < cmd->argc; i++)
		s->command_arguments.emplace_back(cmd->argv[i]);

	s->command_argv.reserve(s->command_arguments.size());

	for (const auto &arg : s->command_arguments)
		s->command_argv.push_back(arg.c_str());

	s->command_record.directory = s->command_directory.c_str();
	s->command_record.file = s->command_file.c_str();
	s->command_record.argc = s->command_argv.size();
	s->command_record.argv = s->command_argv.data();
}

static bool isUnsupportedJoinedArg(llvm::StringRef arg)
{
	return arg.starts_with("--arch=") || arg.starts_with("-mpreferred-stack-boundary=") ||
		arg.starts_with("-mindirect-branch=") || arg == "-mindirect-branch-register" ||
		arg == "-fno-allow-store-data-races" || arg.starts_with("-fzero-init-padding-bits=") ||
		arg.starts_with("-fdiagnostics-show-context=") || arg.starts_with("-fmin-function-alignment=") ||
		arg == "-fconserve-stack" || arg.starts_with("-falign-jumps=");
}

static bool unsupportedArgTakesValue(llvm::StringRef arg)
{
	return arg == "--arch";
}

static bool isWarningErrorArg(llvm::StringRef arg)
{
	return arg == "-Werror" || arg.starts_with("-Werror=");
}

static bool isPreprocessedAssembly(llvm::StringRef file)
{
	return file.ends_with(".S");
}

static bool isX86_64BuiltinDefinition(llvm::StringRef value)
{
	llvm::StringRef name = value.split('=').first;

	return name == "__x86_64__" || name == "__x86_64" || name == "__amd64__" || name == "__amd64";
}

static std::vector<std::string> sanitizeCommandLine(const std::vector<std::string> &input)
{
	std::vector<std::string> args;

	args.reserve(input.size() + 5);

	for (size_t i = 0; i < input.size(); i++) {
		llvm::StringRef arg(input[i]);

		if (arg == "--")
			continue;

		if (isUnsupportedJoinedArg(arg))
			continue;

		if (isWarningErrorArg(arg))
			continue;

		if (arg == "-D" && i + 1 < input.size() && isX86_64BuiltinDefinition(input[i + 1])) {
			i++;
			continue;
		}
		if (arg.starts_with("-D") && isX86_64BuiltinDefinition(arg.drop_front(2)))
			continue;

		if (unsupportedArgTakesValue(arg)) {
			i++;
			continue;
		}

		args.emplace_back(arg.str());
	}

	args.emplace_back("-w");
	args.emplace_back("-Wno-error");
	args.emplace_back("-Wno-unknown-warning-option");
	args.emplace_back("-Wno-error=ignored-optimization-argument");
	args.emplace_back("-Wno-builtin-macro-redefined");

	return args;
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

void semindex_set_details(semindex_t *s, int enabled)
{
	if (s)
		s->details = enabled;
}

void semindex_set_include_local(semindex_t *s, int enabled)
{
	if (s)
		s->include_local = enabled;
}

int semindex_index_command(semindex_t *s, const semindex_compile_command_t *cmd)
{
	if (!s || !cmd || !cmd->file || !cmd->argv || !cmd->argc)
		return -1;

	clearIndex(s);
	saveCompileCommand(s, cmd);

	std::vector<std::string> args;

	args.reserve(cmd->argc);

	for (size_t i = 0; i < cmd->argc; i++)
		args.emplace_back(cmd->argv[i]);

	CompileCommand compile(cmd->directory ? cmd->directory : ".", cmd->file, args, "");
	SingleCompilationDatabase db(compile);
	ClangTool tool(db, { cmd->file });
	clang::DiagnosticOptions diagnostic_options;
	SemindexDiagnosticConsumer diagnostics(s, diagnostic_options);

	tool.setDiagnosticConsumer(&diagnostics);
	tool.appendArgumentsAdjuster(
		[](const CommandLineArguments &args, llvm::StringRef) { return sanitizeCommandLine(args); });
	std::unique_ptr<FrontendActionFactory> factory = isPreprocessedAssembly(cmd->file)
		? createSemindexPreprocessorActionFactory(s)
		: createSemindexActionFactory(s);
	int ret = tool.run(factory.get());

	s->index_result.warnings = diagnostics.getNumWarnings();
	s->index_result.errors = diagnostics.getNumErrors();
	s->index_result.fatal = diagnostics.fatalOccurred();

	if (ret == 0 && s->has_index_data && !s->index_result.errors)
		s->index_result.status = SEMINDEX_INDEX_CLEAN;

	else if (s->has_index_data)
		s->index_result.status = SEMINDEX_INDEX_PARTIAL;
	else
		s->index_result.status = SEMINDEX_INDEX_FAILED;

	if (s->has_index_data)
		rebuildRecords(s);
	rebuildDiagnostics(s);

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

const semindex_index_result_t *semindex_get_index_result(const semindex_t *s)
{
	return s ? &s->index_result : nullptr;
}

size_t semindex_diagnostic_count(const semindex_t *s)
{
	return s ? s->diagnostic_records.size() : 0;
}

const semindex_diagnostic_t *semindex_get_diagnostic(const semindex_t *s, size_t idx)
{
	if (!s || idx >= s->diagnostic_records.size())
		return nullptr;

	return &s->diagnostic_records[idx];
}

const semindex_compile_command_t *semindex_get_compile_command(const semindex_t *s)
{
	if (!s || !s->command_record.argc)
		return nullptr;

	return &s->command_record;
}

int semindex_build_file_fingerprints(semindex_t *s)
{
	if (!s)
		return -1;

	rebuildFingerprints(s);
	return 0;
}

size_t semindex_symbol_count(const semindex_t *s)
{
	return s ? s->symbol_records.size() : 0;
}

const semindex_symbol_t *semindex_get_symbol(const semindex_t *s, size_t idx)
{
	if (!s || idx >= s->symbol_records.size())
		return nullptr;

	return &s->symbol_records[idx];
}

size_t semindex_use_count(const semindex_t *s)
{
	return s ? s->use_records.size() : 0;
}

const semindex_use_t *semindex_get_use(const semindex_t *s, size_t idx)
{
	if (!s || idx >= s->use_records.size())
		return nullptr;

	return &s->use_records[idx];
}

size_t semindex_file_fingerprint_count(const semindex_t *s)
{
	return s ? s->file_fingerprints[0].size() : 0;
}

const semindex_file_fingerprint_t *semindex_get_file_fingerprint(const semindex_t *s, size_t idx, int include_local)
{
	if (!s || idx >= s->file_fingerprints[include_local != 0].size())
		return nullptr;

	return &s->file_fingerprints[include_local != 0][idx];
}

} /* extern "C" */
