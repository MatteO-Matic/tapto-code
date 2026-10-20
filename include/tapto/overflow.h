// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Centlake Software AB

#pragma once

#include <cstddef>
#include <filesystem>
#include <string>

namespace tapto {

// ---------------------------------------------------------------------------
// Tool output too long for the conversation.
//
// A tool result past kOverflowBytes is not cut and lost: the whole of it is
// written to a file in this session's own folder, and the model gets its first
// and last lines with a note between them naming the lines left out and the
// file that holds them, which it reads (or searches) as it needs.
//
// The folder is the session's, under the project's state beside its config
// and saved conversation:
//
//   ~/.tapto/projects/<encoded-cwd>/tmp/<pid>-<start time>/
//
// and the model reaches it as @tmp, read-only (main adds it to the folder set
// with FolderSet::add_system). One folder per session, so two sessions in one
// project never see each other's files, and clean-up is removing the folder:
// at exit, or at the next start for a session that did not exit.
// ---------------------------------------------------------------------------

// A result longer than this is cut.
constexpr std::size_t kOverflowBytes = 16000;
// What of it the model gets inline: whole lines from the start and from the
// end, up to these many bytes each.
constexpr std::size_t kOverflowHeadBytes = 6000;
constexpr std::size_t kOverflowTailBytes = 6000;

// Make this session's folder, after removing those of sessions that are no
// longer running. Returns it, or an empty path (and `error`) when it cannot be
// made; overflow() then drops what it cuts and says so.
std::filesystem::path open_session_tmp(std::string& error);

// Remove this session's folder and what is in it. Safe to call when none was
// opened, and more than once.
void close_session_tmp();

// The folder overflow() writes to and the label the model knows it by (the
// one open_session_tmp made, under "@tmp"). An empty path turns writing off.
void set_overflow_folder(const std::filesystem::path& dir, const std::string& label);

// `text` as a tool result: itself when it fits in kOverflowBytes; otherwise its
// head and tail lines with a note naming the lines left out and the file in
// the overflow folder that holds all of it. `name_hint` (a command's name)
// starts the file's name. Without a folder, or when the file cannot be
// written, the cut lines are dropped and the note says so.
std::string overflow(const std::string& text, const std::string& name_hint);

} // namespace tapto
