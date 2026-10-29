// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Centlake Software AB
//
// Tool output too long for the conversation (tapto/overflow.h): the model
// gets the head and the tail with a note naming the lines left out, and the
// whole output is kept in this session's @tmp folder, which the model can
// read (the editor, the built-ins, a [ro] command) and nothing can write.

#include "tapto/overflow.h"
#include "tapto/program.h"

#include "tapto/commands.h"
#include "tapto/context.h"
#include "tapto/fstools.h"
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

namespace fs = std::filesystem;
using nlohmann::json;

namespace {

int g_checks = 0;
int g_failures = 0;

void check_true(int line, const char* what, bool cond) {
    ++g_checks;
    if (cond) return;
    ++g_failures;
    std::cout << "FAIL (line " << line << ") " << what << "\n";
}

void check_eq(int line, const char* what, const std::string& actual, const std::string& expected) {
    ++g_checks;
    if (actual == expected) return;
    ++g_failures;
    std::cout << "FAIL (line " << line << ") " << what << "\n"
              << "  expected:\n" << expected << "\n"
              << "  actual:\n" << actual << "\n";
}

#define CHECK_TRUE(cond) check_true(__LINE__, #cond, (cond))
#define CHECK_EQ(actual, expected) check_eq(__LINE__, #actual, (actual), (expected))

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

// "line 1\nline 2\n..." up to `n`, each line `width` bytes with its newline.
std::string numbered(int n, std::size_t width = 40) {
    std::string out;
    for (int i = 1; i <= n; ++i) {
        std::string l = "line " + std::to_string(i) + " ";
        l.resize(width - 1, '.');
        out += l + "\n";
    }
    return out;
}

// The "@tmp/<file>" a note names, or "".
std::string named_file(const std::string& result) {
    const std::size_t at = result.find("@tmp/");
    if (at == std::string::npos) return "";
    const std::size_t end = result.find(',', at);
    return result.substr(at, end - at);
}

} // namespace

int main() {
    tapto::set_program_name("tapto-code"); // in what the library says
    const fs::path fake_home = fs::temp_directory_path() / "tapto-overflow-tests-home";
    const std::string saved_home = get_env(kHomeVar);
    std::error_code ec;
    fs::remove_all(fake_home, ec);
    fs::create_directories(fake_home, ec);
    set_env(kHomeVar, fake_home.string());

    // --- without a folder: short text is untouched; long text is cut --------
    {
        tapto::set_overflow_folder(fs::path(), "");
        CHECK_EQ(tapto::overflow("short\n", "x"), "short\n");
        const std::string fits(tapto::kOverflowBytes, 'a');
        CHECK_EQ(tapto::overflow(fits, "x"), fits);

        // 1000 lines of 40 bytes: 150 fit in each 6000-byte half.
        const std::string text = numbered(1000);
        const std::string r = tapto::overflow(text, "x");
        CHECK_TRUE(r.size() < 13000);
        CHECK_TRUE(r.rfind("line 1 ", 0) == 0);
        CHECK_TRUE(r.find("line 150 ") != std::string::npos);
        CHECK_TRUE(r.find("line 151 ") == std::string::npos);
        CHECK_TRUE(r.find("lines 151-850 of 1000 are not shown here (40000 bytes in all).") !=
                   std::string::npos);
        CHECK_TRUE(r.find("could not be kept") != std::string::npos);
        CHECK_TRUE(r.find("line 850 ") == std::string::npos);
        CHECK_TRUE(r.find("line 851 ") != std::string::npos);
        CHECK_TRUE(r.size() >= 40 && r.compare(r.size() - 40, 9, "line 1000") == 0);
    }

    // --- one long line: the start of it, never half a character ------------
    {
        std::string text;
        while (text.size() < 20000) text += "\xc3\xa5"; // a run of 'å', two bytes each
        const std::string r = tapto::overflow(text, "x");
        const std::size_t note = r.find("[... the output was cut: line 1 of 1 is not shown in full");
        CHECK_TRUE(note != std::string::npos);
        CHECK_TRUE(note <= tapto::kOverflowHeadBytes + 1);
        CHECK_TRUE(r.compare(0, 2, "\xc3\xa5") == 0);
        CHECK_TRUE(r[note - 1] == '\n' && (static_cast<unsigned char>(r[note - 2]) == 0xa5));
    }

    // --- the session's folder: made, stale ones removed, live ones kept -----
    fs::path tmp;
    {
        const fs::path root = tapto::config_path(tapto::Level::Local).parent_path() / "tmp";
        // A pid that is not running, and this process's own pid from an
        // earlier run: both stale. A folder not named "<pid>-..." is not ours.
        fs::create_directories(root / "4000000000-1", ec);
        fs::create_directories(root / "keep-me", ec);
        std::string err;
        tmp = tapto::open_session_tmp(err);
        CHECK_EQ(err, "");
        CHECK_TRUE(fs::is_directory(tmp));
        CHECK_TRUE(tmp.parent_path() == root);
        CHECK_TRUE(!fs::exists(root / "4000000000-1"));
        CHECK_TRUE(fs::exists(root / "keep-me"));
    }

    // --- through the tools: run_command keeps it, the model reads it --------
    {
        tapto::FolderSet folders;
        CHECK_EQ(folders.set_home(fs::current_path().string(), /*writable=*/true), "");
        CHECK_EQ(folders.add_system("@tmp", tmp.string(), false, "long output"), "");
        tapto::set_granted_folders(&folders);
        tapto::set_overflow_folder(tmp, "@tmp");

        Context ctx;
        ctx.tools = tapto::builtin_tools();
        ToolExecutorFn run = find_tool(ctx, "run_command");
        ToolExecutorFn edit = find_tool(ctx, "str_replace_based_edit_tool");
        CHECK_TRUE(run && edit);

        // A built-in's long output: cat of a 1000-line file.
        const fs::path big = fs::path("tapto-overflow-tests-big.txt");
        write_raw(big, numbered(1000));
        const std::string r = run(ctx, json{{"name", "cat"}, {"args", {big.string()}}});
        const std::string file = named_file(r);
        CHECK_TRUE(file.rfind("@tmp/cat-", 0) == 0);
        CHECK_TRUE(r.find("lines 151-850 of 1000") != std::string::npos);

        // The file holds all of it, and the editor reads it by its label.
        const fs::path on_disk = tmp / file.substr(5);
        CHECK_EQ(read_raw(on_disk), numbered(1000));
        const std::string v = edit(ctx, json{{"command", "view"}, {"path", file},
                                             {"view_range", {500, 501}}});
        CHECK_TRUE(v.find("500|line 500 ") != std::string::npos);
        CHECK_TRUE(v.find("501|line 501 ") != std::string::npos);
        // ...and a built-in reads it too.
        const std::string t = run(ctx, json{{"name", "tail"}, {"args", {"-n", "1", file}}});
        CHECK_TRUE(t.find("line 1000 ") != std::string::npos);

        // Nothing writes there: not the editor, not a command.
        std::string w = edit(ctx, json{{"command", "str_replace"}, {"path", file},
                                       {"old_str", "line 500 "}, {"new_str", "X"}});
        CHECK_TRUE(w.rfind("ERROR:", 0) == 0 && w.find("tapto-code's own folder") != std::string::npos);
        w = edit(ctx, json{{"command", "create"}, {"path", "@tmp/new.txt"}, {"file_text", "x"}});
        CHECK_TRUE(w.rfind("ERROR:", 0) == 0);
        CHECK_TRUE(!fs::exists(tmp / "new.txt"));
        w = edit(ctx, json{{"command", "delete"}, {"path", file}});
        CHECK_TRUE(w.rfind("ERROR:", 0) == 0);
        CHECK_TRUE(fs::exists(on_disk));

        // A command that says it only reads may be pointed at it; another
        // may not, although it is in ~/.tapto either way.
        write_raw(fake_home / ".tapto" / "commands",
#ifdef _WIN32
                  "look = [ro] cmd /c type %p1\n"
                  "touch = cmd /c type %p1\n"
#else
                  "look = [ro] cat %p1\n"
                  "touch = cat %p1\n"
#endif
        );
        std::string c = run(ctx, json{{"name", "look"}, {"args", {file}}});
        CHECK_TRUE(c.rfind("ERROR:", 0) != 0);
        CHECK_TRUE(c.find("line 1 ") != std::string::npos);
        CHECK_TRUE(c.find("[... the output was cut") != std::string::npos); // overflowed again
        c = run(ctx, json{{"name", "look"}, {"args", {"x.txt"}}, {"cwd", "@tmp"}});
        CHECK_TRUE(c.find("read-only") == std::string::npos && c.find("own folders") == std::string::npos);
        c = run(ctx, json{{"name", "touch"}, {"args", {file}}});
        CHECK_TRUE(c.rfind("ERROR:", 0) == 0);

        fs::remove(big, ec);
        tapto::set_granted_folders(nullptr);
    }

    // --- closing removes the folder ------------------------------------------
    tapto::close_session_tmp();
    CHECK_TRUE(!fs::exists(tmp));
    tapto::close_session_tmp(); // twice is fine

    fs::remove_all(fake_home, ec);
    set_env(kHomeVar, saved_home);

    std::cout << (g_failures ? "FAILED " : "ok ") << (g_checks - g_failures) << "/" << g_checks
              << " checks\n";
    return g_failures ? 1 : 0;
}
