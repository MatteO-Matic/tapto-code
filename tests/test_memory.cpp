// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Centlake Software AB
//
// The project's persistent memory file: where it lives, who may reach it,
// and what the system prompt carries of it.
//
// The file sits in ~/.tapto/projects/<encoded-cwd>/memory/memory.md, outside
// the working directory the file tools are pinned to. The editor tool alone
// reaches it, by its exact path (or a spelling that resolves to it); the
// other tools and the allow-listed commands may not. What the prompt carries
// is only the first 50 lines, the index.

#include "tapto/memory.h"

#include "tapto/context.h"
#include "tapto/paths.h"
#include "tapto/tool_registry.h"
#include "tapto/tools.h"

#include <nlohmann/json.hpp>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

using json = nlohmann::json;
namespace fs = std::filesystem;

namespace {

int g_checks = 0;
int g_failures = 0;

void check_eq(int line, const char* what, const std::string& actual, const std::string& expected) {
    ++g_checks;
    if (actual == expected) return;
    ++g_failures;
    std::cout << "FAIL (line " << line << ") " << what << "\n"
              << "  expected:\n" << expected << "\n"
              << "  actual:\n" << actual << "\n";
}

void check_true(int line, const char* what, bool cond) {
    ++g_checks;
    if (cond) return;
    ++g_failures;
    std::cout << "FAIL (line " << line << ") " << what << "\n";
}

#define CHECK_EQ(actual, expected) check_eq(__LINE__, #actual, (actual), (expected))
#define CHECK_TRUE(cond) check_true(__LINE__, #cond, (cond))

// The tools and the store resolve the memory file against the home directory,
// so a fake home (set here, restored on exit) is the test's sandbox for the
// file. It must sit OUTSIDE the working directory, where the editor is
// pinned: inside it, the ordinary sandbox rules would reach the file and the
// test would say nothing about the exception.
const char* kHomeVar =
#ifdef _WIN32
    "USERPROFILE";
#else
    "HOME";
#endif

std::string get_env(const std::string& name) {
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4996) // std::getenv is the portable, intended call here
#endif
    const char* v = std::getenv(name.c_str());
#ifdef _MSC_VER
#pragma warning(pop)
#endif
    return v ? std::string(v) : std::string();
}

void set_env(const std::string& name, const std::string& value) {
#ifdef _WIN32
    _putenv_s(name.c_str(), value.c_str());
#else
    if (value.empty()) unsetenv(name.c_str());
    else setenv(name.c_str(), value.c_str(), 1);
#endif
}

void write_raw(const fs::path& p, const std::string& bytes) {
    std::error_code ec;
    fs::create_directories(p.parent_path(), ec);
    std::ofstream out(p, std::ios::binary | std::ios::trunc);
    out << bytes;
}

std::string read_raw(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

ToolExecutorFn find_tool(const Context& ctx, const std::string& name) {
    for (const auto& t : ctx.tools) {
        if (t.name == name) return t.executor;
    }
    return nullptr;
}

} // namespace

int main() {
    // A fake home, outside the working directory, for the store the memory
    // file lives in. The working directory is this process's, as the
    // editor's sandbox sees it.
    const fs::path fake_home = fs::temp_directory_path() / "tapto-memory-tests-home";
    const std::string saved_home = get_env(kHomeVar);
    set_env(kHomeVar, fake_home.string());

    std::error_code ec;
    fs::remove_all(fake_home, ec);

    Context ctx;
    ctx.tools = tapto::builtin_tools();
    ToolExecutorFn edit = find_tool(ctx, "str_replace_based_edit_tool");
    ToolExecutorFn run = find_tool(ctx, "run_command");
    if (!edit || !run) {
        std::cout << "FAIL: expected tools are not registered\n";
        return 1;
    }

    const fs::path memory = tapto::memory_path();
    CHECK_TRUE(!memory.empty());

    // --- where the file lives ------------------------------------------------
    // Beside the project's other per-folder state, under the same encoded
    // working directory: a cloned repository cannot ship it.
    {
        CHECK_EQ(memory.filename().string(), "memory.md");
        CHECK_EQ(memory.parent_path().filename().string(), "memory");
        CHECK_EQ(memory.parent_path().parent_path().string(),
                 tapto::config_path(tapto::Level::Local).parent_path().string());
        CHECK_TRUE(!tapto::is_memory_file(fs::path()));
    }

    // --- no memory yet: the prompt says so, and create makes the file --------
    {
        CHECK_TRUE(!fs::exists(memory, ec));
        CHECK_EQ(tapto::memory_head(), "");
        const std::string prompt = tapto::memory_prompt();
        CHECK_TRUE(prompt.find("memory file") != std::string::npos);
        CHECK_TRUE(prompt.find("first 50 lines") != std::string::npos);
        CHECK_TRUE(prompt.find(memory.generic_string()) != std::string::npos);
        CHECK_TRUE(prompt.find("empty") != std::string::npos);
    }
    {
        const std::string r = edit(ctx, json{{"command", "create"},
                                             {"path", memory.string()},
                                             {"file_text", "# Memory\n\n- one\n"}});
        CHECK_EQ(r, "OK");
        CHECK_EQ(read_raw(memory), "# Memory\n\n- one\n");
        // The second creation is refused, as an existing file is anywhere.
        const std::string again = edit(ctx, json{{"command", "create"},
                                                 {"path", memory.string()},
                                                 {"file_text", "x\n"}});
        CHECK_TRUE(again.rfind("ERROR: File already exists", 0) == 0);
    }

    // --- the editor reads and edits it ----------------------------------------
    {
        const std::string r = edit(ctx, json{{"command", "view"}, {"path", memory.string()}});
        CHECK_EQ(r, "1|# Memory\n2|\n3|- one\n");

        const std::string s = edit(ctx, json{{"command", "str_replace"},
                                             {"path", memory.string()},
                                             {"old_str", "- one"},
                                             {"new_str", "- one\n- two"}});
        CHECK_EQ(s, "OK");
        CHECK_EQ(read_raw(memory), "# Memory\n\n- one\n- two\n");

        const std::string w = edit(ctx, json{{"command", "write"},
                                             {"path", memory.string()},
                                             {"file_text", "# Memory\n\n- one\n- two\n- three\n"}});
        CHECK_EQ(w, "OK: wrote " + memory.string());
        CHECK_EQ(read_raw(memory), "# Memory\n\n- one\n- two\n- three\n");

        const std::string i = edit(ctx, json{{"command", "insert"},
                                             {"path", memory.string()},
                                             {"insert_line", 0},
                                             {"new_str", "fresh"}});
        CHECK_TRUE(i.rfind("Insertion successful", 0) == 0);
        CHECK_EQ(read_raw(memory), "fresh\n# Memory\n\n- one\n- two\n- three\n");

        // A spelling that resolves to the file is the file.
        const std::string v = edit(ctx, json{{"command", "view"},
                                             {"path", (memory.parent_path() / ".." /
                                                      memory.parent_path().filename() /
                                                      memory.filename()).string()}});
        CHECK_TRUE(v.find("1|fresh") != std::string::npos);
    }

    // --- nothing else outside the file is reachable --------------------------
    // A sibling of the file, a step out of the memory directory, and a bare
    // relative name are all refused: the memory resolver refuses them with its
    // own error, or the ordinary sandbox does (a bare relative name resolves
    // against the working directory, as it always does). A marker file planted
    // in the memory directory is the canary: it must never be readable, so the
    // directory is unreachable whether the write or the read is refused.
    {
        const std::string marker = "MEMORY-DIR-MARKER-DO-NOT-LEAK";
        write_raw(memory.parent_path() / "marker.txt", marker + "\n");
        // A decoy with the same name in the working directory, so a bare
        // relative name (which resolves there) cannot be mistaken for the
        // marker.
        const std::string local = "local decoy\n";
        write_raw("marker.txt", local);

        auto no_leak = [&](const std::string& r) {
            CHECK_TRUE(r.find(marker) == std::string::npos);
            if (r.rfind("ERROR:", 0) == 0)
                CHECK_TRUE(r.find("outside the working directory") != std::string::npos ||
                           r.find("not the project's memory file") != std::string::npos);
        };

        const std::string s1 = edit(ctx, json{{"command", "create"},
                                              {"path", (memory.parent_path() / "other.md").string()},
                                              {"file_text", "no\n"}});
        CHECK_TRUE(!fs::exists(memory.parent_path() / "other.md", ec)); // not planted
        no_leak(s1);

        // A bare relative name stays in the working directory (the safe
        // default), never in the memory directory.
        const std::string s2 = edit(ctx, json{{"command", "view"}, {"path", "marker.txt"}});
        CHECK_TRUE(s2.find(local) != std::string::npos);
        CHECK_TRUE(s2.find(marker) == std::string::npos);

        const std::string s3 = edit(ctx, json{{"command", "view"},
                                              {"path", (memory.parent_path() / "marker.txt").string()}});
        no_leak(s3);

        const std::string s4 = edit(ctx, json{{"command", "view"},
                                              {"path", (memory.parent_path() / ".." / "config").string()}});
        no_leak(s4);

        fs::remove("marker.txt", ec);
        fs::remove_all(memory.parent_path(), ec);
    }

    // --- a directory with the file's name is a refusal, not a write ----------
    // The editor's own checks (a file, a single file) must say so, and a
    // delete must not take the directory with it.
    {
        fs::remove(memory, ec);
        fs::create_directories(memory, ec);
        const std::string d = edit(ctx, json{{"command", "delete"}, {"path", memory.string()}});
        CHECK_TRUE(d.rfind("ERROR:", 0) == 0 && d.find("directory") != std::string::npos);
        CHECK_TRUE(fs::exists(memory, ec)); // the directory is still there
        const std::string c = edit(ctx, json{{"command", "create"},
                                             {"path", memory.string()},
                                             {"file_text", "x\n"}});
        CHECK_TRUE(c.rfind("ERROR:", 0) == 0 && c.find("already exists") != std::string::npos);
        fs::remove(memory, ec);
        CHECK_TRUE(!fs::exists(memory, ec));
    }

    // --- delete removes the file; editing a missing file names create --------
    {
        // The directory test above left the file absent, so make a fresh one.
        const std::string mk = edit(ctx, json{{"command", "create"},
                                              {"path", memory.string()},
                                              {"file_text", "to delete\n"}});
        CHECK_EQ(mk, "OK");
        const std::string d = edit(ctx, json{{"command", "delete"}, {"path", memory.string()}});
        CHECK_EQ(d, "OK: deleted " + memory.string());
        CHECK_TRUE(!fs::exists(memory, ec));

        const std::string s = edit(ctx, json{{"command", "str_replace"},
                                             {"path", memory.string()},
                                             {"old_str", "x"},
                                             {"new_str", "y"}});
        CHECK_TRUE(s.rfind("ERROR:", 0) == 0 && s.find("create") != std::string::npos);
    }

    // --- no other tool, and no command, may reach the file -------------------
    // The file lives in ~/.tapto, outside the working directory: the other
    // tools and the commands' cwd and %p paths are refused, and only the
    // editor's resolver makes the memory exception.
    {
        write_raw(memory, "# Memory\n\n- one\n");

        const std::string r = run(ctx, json{{"name", "cat"}, {"args", {memory.string()}}});
        CHECK_TRUE(r.rfind("ERROR:", 0) == 0 && r.find("outside the working directory") != std::string::npos);

        const std::string r2 = run(ctx, json{{"name", "cat"}, {"cwd", memory.parent_path().string()}});
        CHECK_TRUE(r2.rfind("ERROR:", 0) == 0 && r2.find("outside the working directory") != std::string::npos);

        // A user command's %p path is held to the sandbox too.
#ifdef _WIN32
        write_raw(fake_home / ".tapto" / "commands", "tapto-memory = cmd /c type %p1\n");
#else
        write_raw(fake_home / ".tapto" / "commands", "tapto-memory = echo %p1\n");
#endif
        const std::string r3 = run(ctx, json{{"name", "tapto-memory"}, {"args", {memory.string()}}});
        CHECK_TRUE(r3.rfind("ERROR:", 0) == 0 && r3.find("outside the working directory") != std::string::npos);

        // The editor still reads it while all of that is refused.
        const std::string v = edit(ctx, json{{"command", "view"}, {"path", memory.string()}});
        CHECK_TRUE(v.find("1|# Memory") != std::string::npos);
        fs::remove_all(fake_home / ".tapto" / "commands", ec);
    }

    // --- the index: the first 50 lines, and the prompt carries them ----------
    {
        // 121 lines: the head is the first 50 (the heading and lines 1-49).
        std::string content = "# Memory\n";
        for (int i = 1; i <= 120; ++i) content += "line " + std::to_string(i) + "\n";
        write_raw(memory, content);

        std::string head_expected = "# Memory";
        for (int i = 1; i <= 49; ++i) head_expected += "\nline " + std::to_string(i);
        CHECK_EQ(tapto::memory_head(), head_expected);

        // A file shorter than the limit comes through whole, without a
        // phantom trailing line for its final newline.
        write_raw(memory, "# Memory\n\n- one\n");
        CHECK_EQ(tapto::memory_head(), "# Memory\n\n- one");

        // The prompt ends with the index of what is in the file. Line 49 is
        // the last index line (the heading plus lines 1-49), so it appears with
        // no trailing newline; line 50 and beyond are not carried.
        std::string body = "# Memory\n";
        for (int i = 1; i <= 60; ++i) body += "line " + std::to_string(i) + "\n";
        write_raw(memory, body);
        const std::string prompt = tapto::memory_prompt();
        CHECK_TRUE(prompt.find("Memory index") != std::string::npos);
        CHECK_TRUE(prompt.find("\nline 49") != std::string::npos);
        CHECK_TRUE(prompt.find("line 50") == std::string::npos);
        CHECK_TRUE(prompt.find("line 51") == std::string::npos);
    }

    // --- is_memory_file: the exact path, a resolving spelling, nothing else --
    {
        CHECK_TRUE(!tapto::is_memory_file(memory.parent_path()));
        CHECK_TRUE(!tapto::is_memory_file(fake_home / ".tapto" / "projects" / "other" /
                                          "memory" / "memory.md"));
        CHECK_TRUE(tapto::is_memory_file(memory));
        CHECK_TRUE(tapto::is_memory_file(memory.parent_path() / ".." /
                                         memory.parent_path().filename() /
                                         memory.filename()));
    }

    fs::remove_all(fake_home, ec);
    set_env(kHomeVar, saved_home);

    std::cout << (g_failures ? "FAILED " : "ok ") << (g_checks - g_failures) << "/" << g_checks
              << " checks\n";
    return g_failures ? 1 : 0;
}
