// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Centlake Software AB

#pragma once

#include <cstddef>
#include <filesystem>
#include <string>

namespace tapto {

class FolderSet;

// ---------------------------------------------------------------------------
// The project's persistent memory: a folder tapto-code keeps beside the
// project's other per-folder state, with one Markdown file as its index:
//
//   ~/.tapto/projects/<encoded-cwd>/memory/memory.md
//
// The same <encoded-cwd> as the local config scope, so memory, config and the
// saved conversation of one project sit together and a cloned repository
// never ships it. It is the agent's own scratch space for the project:
// decisions, conventions, gotchas, where things live.
//
// The model reaches the folder as @memory, one of tapto-code's own folders
// (FolderSet::add_system), read and write, through the same path rules as a
// grant: so nothing outside it, and no symlink out of it, is reachable. It is
// in ~/.tapto, which no write may touch; the editor tool alone is let through
// for @memory, so no command's cwd or %p path ever reaches it.
//
// What the system prompt carries is only the first kMemoryIndexLines lines of
// memory.md, the index: the model reads the rest, and any other file it keeps
// in the folder, with its editor.
// ---------------------------------------------------------------------------

// The label the model knows the folder by.
constexpr const char* kMemoryLabel = "@memory";

// The first kMemoryIndexLines lines are the index the system prompt carries:
// the model reads the rest of the file by name with its editor tool.
constexpr std::size_t kMemoryIndexLines = 50;

// And at most this many bytes of them: the index rides along with every
// request, so a few very long lines must not make every request huge. An
// index cut short by this ends in a note that says so.
constexpr std::size_t kMemoryIndexBytes = 8192;

// The index file, memory.md. The project directory the working directory
// encodes, so each project holds its own memory, the way it holds its local
// config and its saved conversation. Empty when the working directory cannot
// be read.
std::filesystem::path memory_path();

// The folder that holds it: what @memory is.
std::filesystem::path memory_dir();

// Make the memory folder if it is not there and add it to `folders` as
// @memory, read and write. Returns an empty string on success, otherwise
// what went wrong; the chat then runs without a memory.
std::string add_memory_folder(FolderSet& folders);

// The first `max_lines` lines of the memory file, joined with '\n' (a CR at a
// line's end is dropped). An absent or unreadable file gives an empty string;
// a file shorter than `max_lines` gives what it has. Lines past the first
// kMemoryIndexBytes bytes are left out, and a note says so. The index, when the
// file is still empty, is what the prompt tells the model to keep in place.
std::string memory_head(std::size_t max_lines = kMemoryIndexLines);

// The system-prompt paragraph about the memory: where it lives (as @memory and
// as its path), the index rule, what to keep in it and how to treat it, and
// the index itself.
std::string memory_prompt();

} // namespace tapto
