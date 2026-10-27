// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Centlake Software AB

#pragma once

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "tapto/config.h"
#include "tapto/tool_registry.h"

namespace tapto {

// ---------------------------------------------------------------------------
// Plugins: tools from modules beside tapto-code, spoken to through the
// interfaces in libtapto's tapto/plugin.h.
//
// The core decides everything a plugin could get wrong, once, here:
//
//  - Where from: plugin_directory() only (beside the exe; see below). Not PATH, not
//    the working directory, not the project.
//  - Whose: on Windows a Release build loads a module only when it carries a
//    valid Authenticode signature by the certificate that signed tapto-code
//    itself. A Debug build loads unsigned ones and says so in the trace.
//  - What: the plugin's tools must be named "<plugin>_...", so it can neither
//    collide with another plugin nor stand in for a core tool. A tool that
//    declares "writes" is not offered at all unless the plugin was loaded rw.
//  - Paths: a schema property marked "x-tapto-path": "read" | "write" is
//    resolved and checked by the host (resolve_plugin_path) before the call,
//    and the mark is stripped from the schema the model sees.
//  - Output: every result goes through overflow(), like the core tools'.
//  - Config: the plugin's keys are named "<plugin>-...", so it cannot read a
//    core key or another plugin's. A key it declares secret is resolved here
//    (wincred:, env:, cmd:) and handed over as its value in Configure.
//  - Policy: allowed-plugins (only these names) and allow-user-plugins = 0
//    (only the plugins the policy's own `plugins` key names).
// ---------------------------------------------------------------------------

// The folder plugins are loaded from: plugin_directory_for() the running
// executable.
std::filesystem::path plugin_directory();

// The plugin folder of the executable at `exe`, one per executable and found
// from where it is: <exe dir>/plugins on Windows, and elsewhere too, except
// that an executable in a bin directory (installed: /usr/bin/tapto-code) has
// <bin>/../lib/tapto-code/plugins, so the packages put modules in
// /usr/lib/tapto-code/plugins rather than in /usr/bin.
std::filesystem::path plugin_directory_for(const std::filesystem::path& exe);

// The names of the modules in plugin_directory().
std::vector<std::string> available_plugins();

// A `plugins` config value, "image:ro, jira:rw", as (name, writable) pairs;
// a name without a mode is ro. Sets `error` for an entry it cannot read and
// skips it.
std::vector<std::pair<std::string, bool>> parse_plugin_list(const std::string& value,
                                                            std::string& error);

// Why policy forbids loading `name`, or "" when it is allowed. `by_policy`
// is true when the policy's own `plugins` key asks for it.
std::string plugin_policy_refusal(const std::string& name, bool by_policy,
                                  const std::vector<Config::Entry>& policy);

// A config key an installed plugin declares, which `config set` accepts.
// A plugin's keys are named "<plugin>-...", the tool-name rule, so the key
// names the module: it is loaded (signature checked, as for /add-plugin),
// described, and freed again, without being configured. nullopt when no
// installed, allowed plugin declares `key`; `why` then says what was found,
// or stays empty when no module's name matched at all.
struct PluginConfigKey {
    std::string plugin;
    bool secret = false;
};
std::optional<PluginConfigKey> plugin_config_key(const std::string& key, std::string& why,
                                                 const std::filesystem::path& dir =
                                                     plugin_directory());

class Plugins {
public:
    Plugins();
    ~Plugins(); // shuts down and releases every plugin
    Plugins(const Plugins&) = delete;
    Plugins& operator=(const Plugins&) = delete;

    // Loads plugin `name` from plugin_directory(), or changes its mode when it
    // is loaded already. Policy is applied. "" on success, else what went wrong.
    std::string add(const std::string& name, bool writable, bool by_policy = false);

    // Loads the module at `file` without the directory or name rules, for
    // tests; the signature rule still applies.
    std::string add_file(const std::filesystem::path& file, bool writable);

    // /remove-plugin: its tools go first (the caller rebuilds the tool table),
    // then Shutdown, then every pointer is released, and the module is freed
    // when it says it can be. A plugin without ITaptoPluginUnload stays loaded,
    // hidden, until exit. False when `name` is not loaded.
    bool remove(const std::string& name, bool* unloaded = nullptr);

    // The tools to offer: every loaded plugin's, without the writing ones of a
    // plugin loaded ro. Their executors refer to this object, which must
    // outlive them.
    std::vector<ToolSpec> tools() const;

    // System-prompt text: each plugin's fixed prompt and its changing one.
    std::string prompt() const;

    struct Entry {
        std::string name;
        std::string version;
        bool writable = false;
        std::size_t tools = 0;       // all its tools
        std::size_t write_tools = 0; // of them, those declared "writes"
        std::filesystem::path file;
    };
    std::vector<Entry> loaded() const;

    struct Loaded; // defined in plugins.cpp

private:
    std::vector<std::unique_ptr<Loaded>> m_loaded;
    std::vector<std::unique_ptr<Loaded>> m_hidden; // removed, module kept until exit
};

} // namespace tapto
