// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Centlake Software AB

#pragma once

#include <cstddef>
#include <filesystem>
#include <string>

namespace tapto {

// ---------------------------------------------------------------------------
// The project's persistent memory: one Markdown file tapto-code keeps beside
// the project's other per-folder state, under
//
//   ~/.tapto/projects/<encoded-cwd>/memory/memory.md
//
// The same <encoded-cwd> as the local config scope, so memory, config and the
// saved conversation of one project sit together and a cloned repository
// never ships it. The file is the agent's own scratch space for the project:
// decisions, conventions, gotchas, where things live. It lives outside the
// working directory, so the file tools never see it -- only the editor tool
// reaches it, and only the project directory it belongs to does: a sandboxed
// write to a path that is exactly this file.
//
// What the system prompt carries of it is only the first kMemoryIndexLines
// lines, the index, not the file: the model reads the index from the prompt
// at startup, and the rest of the file by name with its editor.
// ---------------------------------------------------------------------------

// The first kMemoryIndexLines lines are the index the system prompt carries:
// the model reads the rest of the file by name with its editor tool.
constexpr std::size_t kMemoryIndexLines = 50;

// And at most this many bytes of them: the index rides along with every
// request, so a few very long lines must not make every request huge. An
// index cut short by this ends in a note that says so.
constexpr std::size_t kMemoryIndexBytes = 8192;

// The file itself. The project directory the working directory encodes, so
// each project holds its own memory, the way it holds its local config and
// its saved conversation. Empty when the working directory cannot be read.
std::filesystem::path memory_path();

// True when `path` is the memory file of the current project (the editor
// tool's sandboxed exception). The path is compared canonically, so a
// spelling that resolves elsewhere never matches.
bool is_memory_file(const std::filesystem::path& path);

// The first `max_lines` lines of the memory file, joined with '\n' (a CR at a
// line's end is dropped). An absent or unreadable file gives an empty string;
// a file shorter than `max_lines` gives what it has. Lines past the first
// kMemoryIndexBytes bytes are left out, and a note says so. The index, when the
// file is still empty, is what the prompt tells the model to keep in place.
std::string memory_head(std::size_t max_lines = kMemoryIndexLines);

// The system-prompt paragraph about the memory file: where it lives, the
// index rule, and what to keep in it. It names the file's own path, so the
// model can address it from the start.
std::string memory_prompt();

} // namespace tapto
