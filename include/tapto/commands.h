// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Centlake Software AB

#pragma once

#include <filesystem>
#include <map>
#include <string>
#include <vector>

#include "tapto/paths.h"

namespace tapto {

// A user-defined, allow-listed command: a short name mapped to a command line,
// plus the scope it came from.
struct CommandEntry {
    std::string name;
    std::string command;
    Level origin;
};

// All commands merged across scopes (policy overrides local overrides global
// overrides system), keyed by name. Policy commands are the organization's
// (tapto/policy.h); when its `allow-user-commands` is off they are the only
// ones here.
std::map<std::string, std::string> merged_commands();

// Effective commands with their originating scope, first-seen order preserved.
std::vector<CommandEntry> effective_commands();

// Commands defined in a single scope.
std::vector<CommandEntry> commands_in_scope(Level level);

// Why the user may not define a command named `name`, or "" when they may:
// policy defines that name, or policy allows no user commands at all.
std::string command_policy_refusal(const std::string& name);

// Why `cmdline` cannot be a command, or "" when it can. Commands are run as a
// program and its arguments, never through a shell, so a command line that
// relies on one (a pipe, a redirection, `&&`) is refused here rather than run
// with the operator handed to the program as an argument.
std::string command_line_refusal(const std::string& cmdline);

// A command line that starts with "[ro] " reads only: it is the user's (or,
// in policy, the administrator's) word that the command changes nothing, so
// the model may run it in, and hand it %p paths from, a folder granted
// read-only. Other commands may do anything, so they get only folders granted
// read-write. The mark is not part of what runs; tapto-code cannot check it,
// so a command that does write, marked [ro], can write in read-only folders.
// .git and tapto-code's own folders stay refused either way.
extern const char* const kReadOnlyMark; // "[ro]"
bool command_reads_only(const std::string& cmdline);
// The command line without its [ro] mark: what is run.
std::string command_template(const std::string& cmdline);

// The folders searched for a command's program before PATH, and the only
// place a command line that needs a shell can live: as a script (.cmd/.bat on
// Windows, a shell script elsewhere) that the command names.
//
// The machine's folder is the organization's: policy `script-folder`, else
// %ProgramFiles%\Centlake\tapto\scripts (Windows) or /etc/tapto/scripts.
// The user's is ~/.tapto/scripts, and is searched only while policy allows
// the user commands of their own (policy_allows_user_commands()).
std::filesystem::path machine_script_folder();
std::filesystem::path user_script_folder();

// Add or update a command in the given scope. Throws on I/O failure, and for
// the read-only policy scope.
void add_command(Level level, const std::string& name, const std::string& command);

// Remove a command from the given scope; returns true if it existed. Never
// true for the policy scope.
bool remove_command(Level level, const std::string& name);

} // namespace tapto
