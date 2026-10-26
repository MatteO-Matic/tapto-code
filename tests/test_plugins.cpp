// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Centlake Software AB
//
// The plugin loader (tapto/plugins.h) with a real module, tests/plugin/
// probe.cpp: what is offered (writing tools only rw, the path mark stripped),
// what a call gets (paths resolved and checked by the host first), what comes
// back through the host (an image, a file in @tmp, a long result cut), the
// prompt, the tool-name rule, policy, and unloading. Then the image plugin
// (plugins/image), the way the model uses it.
//
//   test-plugins <probe module> <probe module with a misnamed tool> <image module>

#include "tapto/plugins.h"

#include "tapto/context.h"
#include "tapto/fstools.h"
#include "tapto/overflow.h"
#include "tapto/tool_image.h"
#include "tapto/tools.h"

#include <nlohmann/json.hpp>

// stb builds the test images and decodes what the image plugin attached.
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4996 4244 4245 4100 4456 4457 4702)
#endif
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"
#ifdef _MSC_VER
#pragma warning(pop)
#endif

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
    setenv(name.c_str(), value.c_str(), 1);
#endif
}

const ToolSpec* find_tool(const std::vector<ToolSpec>& tools, const std::string& name) {
    for (const auto& t : tools) {
        if (t.name == name) return &t;
    }
    return nullptr;
}

std::string names(const std::vector<ToolSpec>& tools) {
    std::string s;
    for (const auto& t : tools) s += (s.empty() ? "" : " ") + t.name;
    return s;
}

std::string call(const std::vector<ToolSpec>& tools, Context& ctx, const std::string& name,
                 const json& args) {
    const ToolSpec* t = find_tool(tools, name);
    if (!t) return "(no tool " + name + ")";
    return t->executor(ctx, args);
}

bool starts_with(const std::string& s, const std::string& prefix) {
    return s.rfind(prefix, 0) == 0;
}

bool contains(const std::string& s, const std::string& part) {
    return s.find(part) != std::string::npos;
}

// A w x h RGBA image as a PNG file at `path`; pixel (x, y) from `color`.
template <class F>
bool write_png(const fs::path& path, int w, int h, F color) {
    std::vector<uint8_t> px(static_cast<std::size_t>(w) * h * 4);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) color(x, y, &px[(static_cast<std::size_t>(y) * w + x) * 4]);
    }
    std::vector<uint8_t> png;
    auto cb = [](void* ctx, void* data, int size) {
        auto* out = static_cast<std::vector<uint8_t>*>(ctx);
        out->insert(out->end(), static_cast<const uint8_t*>(data),
                    static_cast<const uint8_t*>(data) + size);
    };
    if (!stbi_write_png_to_func(cb, &png, w, h, 4, px.data(), w * 4)) return false;
    std::ofstream(path, std::ios::binary).write(reinterpret_cast<const char*>(png.data()),
                                                static_cast<std::streamsize>(png.size()));
    return true;
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 4) {
        std::cout << "usage: test-plugins <probe module> <misnamed probe module> <image module>\n";
        return 2;
    }
    const fs::path probe = argv[1];
    const fs::path misnamed = argv[2];
    const fs::path image_module = argv[3];

    const fs::path fake_home = fs::temp_directory_path() / "tapto-plugins-tests-home";
    const std::string saved_home = get_env(kHomeVar);
    std::error_code ec;
    fs::remove_all(fake_home, ec);
    fs::create_directories(fake_home, ec);
    set_env(kHomeVar, fake_home.string());

    // The working directory is the sandbox; a file in it to read, and @tmp.
    const fs::path cwd = fs::current_path();
    const fs::path tmp = fake_home / "tmp";
    fs::create_directories(tmp, ec);
    { std::ofstream(cwd / "probe-input.txt") << "x"; }
    tapto::FolderSet folders;
    CHECK_EQ(folders.set_home(cwd.string(), /*writable=*/true), "");
    CHECK_EQ(folders.add_system("@tmp", tmp.string(), false, "long output"), "");
    tapto::set_granted_folders(&folders);
    tapto::set_overflow_folder(tmp, "@tmp");

    // --- the config value and policy ---------------------------------------------
    {
        std::string err;
        auto list = tapto::parse_plugin_list("image, jira:rw ,  x:ro", err);
        CHECK_EQ(err, "");
        CHECK_TRUE(list.size() == 3);
        CHECK_TRUE(list.size() == 3 && list[0] == std::make_pair(std::string("image"), false) &&
                   list[1] == std::make_pair(std::string("jira"), true));
        list = tapto::parse_plugin_list("Bad, ok:maybe, fine", err);
        CHECK_TRUE(list.size() == 1);
        CHECK_TRUE(err.find("'Bad'") != std::string::npos);
        CHECK_TRUE(err.find("'ok:maybe'") != std::string::npos);

        using E = std::vector<tapto::Config::Entry>;
        CHECK_EQ(tapto::plugin_policy_refusal("image", false, E{}), "");
        const E only_jira{{"allowed-plugins", "jira, mail"}};
        CHECK_EQ(tapto::plugin_policy_refusal("jira", false, only_jira), "");
        CHECK_TRUE(tapto::plugin_policy_refusal("image", false, only_jira).find("jira, mail") !=
                   std::string::npos);
        const E no_user{{"allow-user-plugins", "0"}};
        CHECK_TRUE(!tapto::plugin_policy_refusal("image", false, no_user).empty());
        CHECK_EQ(tapto::plugin_policy_refusal("image", true, no_user), "");
        // by_policy does not get around allowed-plugins.
        CHECK_TRUE(!tapto::plugin_policy_refusal("image", true, only_jira).empty());
    }

    // --- loaded ro -------------------------------------------------------------------
    {
        tapto::Plugins plugins;
        CHECK_EQ(plugins.add_file(probe, /*writable=*/false), "");
        const auto tools = plugins.tools();
        // probe_write writes through its path, probe_config says it writes.
        CHECK_EQ(names(tools), "probe_read probe_image probe_temp probe_long");
        const auto loaded = plugins.loaded();
        CHECK_TRUE(loaded.size() == 1 && loaded[0].name == "probe" && loaded[0].version == "1.0.0" &&
                   loaded[0].tools == 6 && loaded[0].write_tools == 2 && !loaded[0].writable);
        // The mark is the host's business; the model's schema does not carry it.
        if (const ToolSpec* t = find_tool(tools, "probe_read")) {
            CHECK_TRUE(t->parameters.dump().find("x-tapto-path") == std::string::npos);
            CHECK_EQ(t->parameters["properties"]["path"].value("type", ""), "string");
        }

        Context ctx;
        // A path in the working directory reaches the plugin absolute.
        CHECK_EQ(call(tools, ctx, "probe_read", {{"path", "probe-input.txt"}}),
                 "read:" + (cwd / "probe-input.txt").u8string());
        // Outside every folder: refused by the host, the plugin never called.
        const std::string outside = (fake_home.parent_path() / "elsewhere.txt").u8string();
        const std::string refused = call(tools, ctx, "probe_read", {{"path", outside}});
        CHECK_TRUE(starts_with(refused, "ERROR"));
        CHECK_TRUE(refused.find("read:") == std::string::npos);
        CHECK_TRUE(starts_with(call(tools, ctx, "probe_read", {{"path", 42}}), "ERROR"));

        // An image, named by the path the model wrote.
        CHECK_EQ(call(tools, ctx, "probe_image", {{"path", "probe-input.txt"}}), "attached");
        tapto::ToolImage image;
        CHECK_TRUE(tapto::takeToolImage(ctx, image));
        CHECK_TRUE(image.width == 1 && image.height == 1);
        CHECK_EQ(image.label, "probe-input.txt");

        // A file in @tmp, by its label.
        const std::string label = call(tools, ctx, "probe_temp", json::object());
        CHECK_TRUE(starts_with(label, "@tmp/note-") && label.size() > 4 &&
                   label.substr(label.size() - 4) == ".txt");
        if (starts_with(label, "@tmp/")) {
            std::ifstream in(tmp / label.substr(5), std::ios::binary);
            const std::string body((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
            CHECK_EQ(body, "hello");
        }

        // A long result is cut like a core tool's.
        const std::string long_result = call(tools, ctx, "probe_long", json::object());
        CHECK_TRUE(long_result.size() < tapto::kOverflowBytes);
        CHECK_TRUE(long_result.find("the output was cut") != std::string::npos);

        // The fixed prompt, then the changing one, asked again each rebuild.
        CHECK_EQ(plugins.prompt(), "probe fixed\n\nprobe changing 1");
        CHECK_EQ(plugins.prompt(), "probe fixed\n\nprobe changing 2");

        // Loading it twice is refused.
        CHECK_TRUE(!plugins.add_file(probe, false).empty());

        bool unloaded = false;
        CHECK_TRUE(plugins.remove("probe", &unloaded));
        CHECK_TRUE(unloaded);
        CHECK_TRUE(plugins.tools().empty());
        CHECK_EQ(plugins.prompt(), "");
        CHECK_TRUE(!plugins.remove("probe"));
    }

    // --- loaded rw: the writing tools, and the write rules ---------------------------
    {
        tapto::Plugins plugins;
        CHECK_EQ(plugins.add_file(probe, /*writable=*/true), "");
        const auto tools = plugins.tools();
        CHECK_EQ(names(tools), "probe_read probe_write probe_image probe_temp probe_long probe_config");
        Context ctx;
        CHECK_EQ(call(tools, ctx, "probe_write", {{"path", "probe-output.txt"}}),
                 "write:" + (cwd / "probe-output.txt").u8string());
        // @tmp is read-only, and a git directory is never written.
        CHECK_TRUE(starts_with(call(tools, ctx, "probe_write", {{"path", "@tmp/x.txt"}}), "ERROR"));
        CHECK_TRUE(starts_with(call(tools, ctx, "probe_write", {{"path", ".git/config"}}), "ERROR"));
        // Its config: the keys it declared, none set here.
        CHECK_EQ(call(tools, ctx, "probe_config", json::object()), "{}");
    }

    // --- the tool-name rule ------------------------------------------------------------
    {
        tapto::Plugins plugins;
        const std::string err = plugins.add_file(misnamed, false);
        CHECK_TRUE(err.find("'other_tool' is not named probe_") != std::string::npos);
        CHECK_TRUE(plugins.tools().empty());
        CHECK_TRUE(plugins.loaded().empty());
    }

    // --- by name, from the install directory -------------------------------------------
    {
        tapto::Plugins plugins;
        CHECK_TRUE(plugins.add("Bad Name", false).find("not a plugin name") != std::string::npos);
        CHECK_TRUE(plugins.add("no-such-plugin", false).find("there is no plugin") !=
                   std::string::npos);
    }

    // --- where the plugins of an executable are --------------------------------------
    {
        using tapto::plugin_directory_for;
        // The build tree: beside the exe, everywhere.
        CHECK_EQ(plugin_directory_for(fs::path("/src/build/tapto-code")).generic_string(),
                 "/src/build/plugins");
#ifdef _WIN32
        // Windows: beside the exe, installed too.
        CHECK_EQ(plugin_directory_for(fs::path("C:/Users/u/AppData/Local/Programs/tapto-code/bin/"
                                               "tapto-code.exe")).generic_string(),
                 "C:/Users/u/AppData/Local/Programs/tapto-code/bin/plugins");
#else
        // Installed in a bin/: the program's own lib directory, not bin/.
        CHECK_EQ(plugin_directory_for(fs::path("/usr/bin/tapto-code")).generic_string(),
                 "/usr/lib/tapto-code/plugins");
        CHECK_EQ(plugin_directory_for(fs::path("/opt/tapto/bin/tapto-code")).generic_string(),
                 "/opt/tapto/lib/tapto-code/plugins");
        // Only a directory named exactly bin.
        CHECK_EQ(plugin_directory_for(fs::path("/home/u/sbin2/tapto-code")).generic_string(),
                 "/home/u/sbin2/plugins");
#endif
        // The running test is in the build tree.
        CHECK_EQ(tapto::plugin_directory().filename().string(), "plugins");
    }

    // --- the image plugin ----------------------------------------------------------------
    {
        tapto::Plugins plugins;
        CHECK_EQ(plugins.add_file(image_module, /*writable=*/false), "");
        const auto tools = plugins.tools();
        CHECK_EQ(names(tools), "image_read");
        CHECK_TRUE(contains(plugins.prompt(), "image_read"));
        Context ctx;

        // A 2x1 PNG, red then blue: attached, and what is attached decodes to it.
        CHECK_TRUE(write_png(cwd / "probe-2x1.png", 2, 1, [](int x, int, uint8_t* p) {
            p[0] = x ? 0 : 255; p[1] = 0; p[2] = x ? 255 : 0; p[3] = 255;
        }));
        std::string r = call(tools, ctx, "image_read", {{"path", "probe-2x1.png"}});
        CHECK_EQ(r, "image attached: 2x1 PNG");
        tapto::ToolImage img;
        CHECK_TRUE(tapto::takeToolImage(ctx, img));
        CHECK_TRUE(img.width == 2 && img.height == 1);
        CHECK_EQ(img.label, "probe-2x1.png"); // the path as the model wrote it
        int dw = 0, dh = 0, dc = 0;
        stbi_uc* d = stbi_load_from_memory(img.png.data(), static_cast<int>(img.png.size()),
                                           &dw, &dh, &dc, 0);
        CHECK_TRUE(d != nullptr && dw == 2 && dh == 1);
        if (d) {
            CHECK_TRUE(d[0] == 255 && d[dc] == 0 && d[dc + 2] == 255);
            stbi_image_free(d);
        }

        // Larger than 1600 on its longest side: down-scaled, aspect kept.
        CHECK_TRUE(write_png(cwd / "probe-big.png", 3200, 1000, [](int, int, uint8_t* p) {
            p[0] = 10; p[1] = 20; p[2] = 30; p[3] = 255;
        }));
        r = call(tools, ctx, "image_read", {{"path", "probe-big.png"}});
        CHECK_EQ(r, "image attached: 1600x500 PNG (down-scaled from 3200x1000)");
        CHECK_TRUE(tapto::takeToolImage(ctx, img) && img.width == 1600 && img.height == 500);

        // A header claiming 20000x10 is refused before it is decoded.
        {
            std::ifstream in(cwd / "probe-2x1.png", std::ios::binary);
            std::string png((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
            png[16] = 0; png[17] = 0; png[18] = char(0x4e); png[19] = char(0x20); // width 20000
            png[20] = 0; png[21] = 0; png[22] = 0; png[23] = 10;
            std::ofstream(cwd / "probe-huge.png", std::ios::binary) << png;
        }
        r = call(tools, ctx, "image_read", {{"path", "probe-huge.png"}});
        CHECK_TRUE(starts_with(r, "ERROR: probe-huge.png is 20000x10"));
        CHECK_TRUE(!tapto::takeToolImage(ctx, img));

        // Not images: a text file, a binary one, none at all.
        r = call(tools, ctx, "image_read", {{"path", "probe-input.txt"}});
        CHECK_TRUE(starts_with(r, "ERROR: probe-input.txt is not an image"));
        { std::ofstream(cwd / "probe-blob.bin", std::ios::binary) << std::string("MZ\0\0\0\0PE\0\0", 10); }
        r = call(tools, ctx, "image_read", {{"path", "probe-blob.bin"}});
        CHECK_TRUE(contains(r, "binary, so it is not an image"));
        r = call(tools, ctx, "image_read", {{"path", "probe-none.png"}});
        CHECK_EQ(r, "ERROR: Not found: probe-none.png");
        // Outside every folder: the host refuses, the plugin is never asked.
        r = call(tools, ctx, "image_read", {{"path", (fake_home.parent_path() / "x.png").u8string()}});
        CHECK_TRUE(starts_with(r, "ERROR") && !contains(r, "Not found"));

        // The core's text tools no longer name a tool that may not be loaded.
        tapto::set_granted_folders(&folders);
        for (const auto& t : tapto::builtin_tools()) {
            CHECK_TRUE(t.name != "read_image");
            if (t.name != "str_replace_based_edit_tool") continue;
            r = t.executor(ctx, {{"command", "view"}, {"path", "probe-2x1.png"}});
            CHECK_TRUE(contains(r, "is a binary file") && !contains(r, "read_image"));
        }

        for (const char* f : {"probe-2x1.png", "probe-big.png", "probe-huge.png", "probe-blob.bin"})
            fs::remove(cwd / f, ec);
    }

    tapto::set_granted_folders(nullptr);
    fs::remove(cwd / "probe-input.txt", ec);
    set_env(kHomeVar, saved_home);
    fs::remove_all(fake_home, ec);

    std::cout << (g_failures ? "FAILED " : "ok ") << (g_checks - g_failures) << "/" << g_checks
              << " checks\n";
    return g_failures ? 1 : 0;
}
