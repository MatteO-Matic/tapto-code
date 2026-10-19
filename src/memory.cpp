// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Centlake Software AB

#include "tapto/memory.h"

#include <fstream>
#include <sstream>
#include <system_error>
#include <vector>

#include "tapto/fstools.h"
#include "tapto/paths.h"

namespace fs = std::filesystem;

namespace tapto {

namespace {

// The path a read of the memory file is canonicalized against. `weakly_canonical`
// also canonicalizes the part that exists, so a file created after this runs
// compares equal to the path it was written at.
fs::path memory_path_canonical() {
    fs::path p = memory_path();
    if (p.empty()) return p;
    std::error_code ec;
    fs::path c = fs::weakly_canonical(p, ec);
    return ec ? p : c;
}

} // namespace

fs::path memory_path() {
    // The local scope's directory is the project's: the same <encoded-cwd>
    // it uses for its config and its saved conversation. Without a readable
    // working directory there is no project, and so no memory.
    const fs::path dir = config_path(Level::Local).parent_path();
    return dir.empty() ? fs::path() : dir / "memory" / "memory.md";
}

bool is_memory_file(const fs::path& path) {
    const fs::path target = memory_path();
    if (target.empty() || path.empty()) return false;
    std::error_code ec;
    const fs::path resolved = fs::weakly_canonical(path, ec);
    if (ec || resolved.empty()) return false;
    if (resolved == memory_path_canonical()) return true;
    // The editor refuses to write a directory, but its message compares the
    // target by spelling as well as by resolution: a directory named memory.md
    // is refused by name here, which is all the guard needs it to be.
    return path == target && fs::is_directory(path, ec);
}

std::string memory_head(std::size_t max_lines) {
    std::ifstream in(memory_path(), std::ios::binary);
    if (!in) return "";
    std::ostringstream ss;
    ss << in.rdbuf();

    // content_lines, not split_lines: a trailing newline must not count as a
    // line, or the prompt's "first 50 lines" and the editor's view [1, 50]
    // would disagree about what the file's 50th line is.
    std::vector<std::string> lines = content_lines(ss.str());
    if (lines.empty()) return "";
    if (max_lines < lines.size()) lines.resize(max_lines);

    std::string out;
    for (std::size_t i = 0; i < lines.size(); ++i) {
        if (i) out += '\n';
        out += lines[i];
    }
    return out;
}

std::string memory_prompt() {
    const fs::path file = memory_path();
    if (file.empty()) return "";

    std::string prompt =
        "You have a persistent memory file for this project, at:\n" +
        file.generic_string() +
        "\nIt is yours to read and write across sessions with your file editor: "
        "decisions the user made, project conventions, where things live, and the "
        "state of ongoing work, written so a fresh session can pick up where this "
        "one stopped.";
    prompt += "\nThe first " + std::to_string(kMemoryIndexLines) +
              " lines of the file are an index of what is in it, and the index is "
              "shown to you at the start of every session, below this paragraph. "
              "Keep that index in the first " + std::to_string(kMemoryIndexLines) +
              " lines: when you add to the memory, file the one line that points at "
              "it under the index, and keep the index under the limit.";
    prompt += "\nRead the rest of the file with your editor when the index says it is "
              "worth it. You can create the file the first time you have something "
              "to remember (its folder is created for you), and the file is yours "
              "to reorganize as it grows.";

    std::string index = memory_head();
    if (index.empty()) {
        prompt += "\n\nThe memory file is empty or has no index yet:";
    } else {
        prompt += "\n\nMemory index (first " + std::to_string(kMemoryIndexLines) +
                  " lines of " + file.filename().string() + "):\n" + index;
    }
    return prompt;
}

} // namespace tapto
