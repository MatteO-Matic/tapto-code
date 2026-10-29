# tapto-code todo

Open work first, newest release last. The libtapto port further down is done
and kept for its notes.

---

# Backlog

Not scheduled into a release yet. The plugin framework is 1.3.0 below, and
moving the project memory into a plugin is listed there.

- [x] **System labels: `@memory` and `@tmp`.** Done for 1.2.0: libtapto
      `v0.3.0` (`FolderSet::add_system`, deepest owner wins), `@tmp` and
      `@memory` in tapto-code; `Scope::Memory` and `is_memory_file` are gone,
      and the symlink item under PR #1 with them. The project's own folders
      under `~/.tapto/projects/<project>/`, reachable by label the way
      `/add-folder` grants are, so the model, the tool results and the
      prompt name them the same short way in every session.

      How labels work today (libtapto `fstools.h/.cpp`, `tools.cpp`): a
      grant's label is the folder's last component, made unique with `-2`,
      `-3` (`label_for`). The model writes `<label>/<rest>` or an absolute
      path, and tool results print `<label>/<rest>` (`display()`), so what
      the model was shown resolves back. A label is taken only when the
      project has no entry of that name: the project's own files win. Each
      grant is ro or rw: reads reach every grant; the editor's writes and a
      command's `cwd`/`%p` need rw; `.git` and `~/.tapto` are refused for
      every write on top. `session.json` saves path and mode only, and
      labels are derived again on `/resume`. The working directory is
      already a program-owned entry (`set_home`): not removable, its mode
      fixed, kept by `clear()`, not counted by `has_grants()`.

      | label | folder | mode |
      |---|---|---|
      | `@memory` | `~/.tapto/projects/<p>/memory/` | rw, the editor only |
      | `@tmp` | `~/.tapto/projects/<p>/tmp/` | ro, only tapto-code writes |

      - libtapto: `FolderSet::add_system(label, path, writable)`, program
        owned like `set_home`. New libtapto tag, then the pin here.
      - `@` marks system labels only: a folder name does not normally have
        it, so they never clash with a grant. Unlike grants, a system label
        wins over a project entry of the same name (a project folder called
        `@memory` does not shadow it).
      - The `~/.tapto` write refusal still applies. Only the editor skips it
        for `@memory`, in `resolve_for_editor` as today, so no command writes
        there. A [ro] command (below) could take `%p` paths in `@tmp`.
      - Not saved in `session.json`: the paths come from the working
        directory, and are set up again at every start.
      - `Scope::Memory` and `is_memory_file` go: the folder rules replace
        them. That closes the symlink item under PR #1, since a folder's
        resolution canonicalizes before the under-root check, so a link out
        of `memory/` is refused.
      - The model gets a memory folder, not one file: topic files beside
        `memory.md`, which stays the index the prompt carries.
      - The libtapto folder tools (`read_file`, `search_files`) reach both,
        so the model can search an overflow file instead of paging it.
      - To decide: `@` as the mark. And `@tmp` per project (simpler) or per
        session (clean-up is removing one folder).
- [x] **Tool overflow.** Done for 1.2.0 (`src/overflow.cpp`), with `@tmp`
      per session (`tmp/<pid>-<time>/`, stale ones removed at start), head and
      tail inline (6000 bytes each, whole lines), past 16000 bytes; only for
      `run_command` (built-ins and commands), since `view` and `read_file`
      read files the model can range over itself. Decided 20 Oct: `@` as the
      mark, `@tmp` per session, head and tail. Still open: `exec_capture`
      keeps all of a command's output in memory, so a runaway command can
      exhaust it; a cap (say 50 MB, the rest discarded with a note) belongs
      there. Original notes:
      When a tool's output passes the limit (a large
      `git status`, a build log), the whole output is written to a file in
      `@tmp` and the model gets what fits, plus a line saying where the rest
      is: "The output was cut. Lines 500-1132 are in `@tmp/sdjksf.txt`." It
      reads or searches the rest, a range at a time, instead of having all of
      it in its context.
      - Needs `@tmp` (above). The label, not the absolute path: short, the
        same in every session, and the user's home path stays out of the
        conversation.
      - Random names, so one output cannot be guessed or overwritten by
        another.
      - Removed when the session ends, or at the next start for a session
        that crashed; `/clear` can go too. The files hold whatever the
        command printed, secrets included, so they should not pile up.
      - In the core, on every tool result, plugin tools' included (1.3.0).
      - To decide: what the model gets inline, the first lines, the last, or
        both. A build log's errors are usually at its end.
- [x] **`/resume` shows where you were.** Print the end of the old
      conversation, about one screenful, so the user sees what was said
      before carrying on, not only the pending prompt. For 1.2.0:
      `transcript()` in session.cpp reads the prompts and the model's text
      out of all three dialects' histories; `ui::print_transcript` sizes it
      to the terminal, counting what is printed after it.
- [x] **[ro] commands.** Let a configured command run in, and take `%p`
      paths from, a read-only `/add-folder` grant. Today only the built-ins
      (`cat`, `ls`, ...) reach read-only grants: every other command's `cwd`
      and `%p` resolve with write scope (`tools.cpp`, `subst_path` and the
      `cwd` check), since nothing says the command only reads. A mark on the
      command's definition would say so, at the user's or the administrator's
      word. For 1.2.0: a `[ro] ` prefix on the command line (`command add
      --ro`, `/add-command --ro`, or by hand and in policy); `.git` and
      `~/.tapto` stay refused to [ro] commands, since the mark is unchecked.
      With `@tmp` (above), a [ro] command should reach it, e.g. to grep an
      overflow file: that needs an exception for `@tmp` in the protected
      check, for [ro] commands only.
- [ ] **[protected] commands.** Run code the model generated (a test it
      wrote, a script) in a sandbox instead of with the user's full rights.
      Look at the AppContainer code in the test project.
      - Decided 29 Oct: a sandbox plugin, behind a capability interface of
        its own, not an event (events only notify; what hands a decision of
        the core's to a plugin is an interface that says so). Roughly
        `ITaptoPluginSandbox::Run(spec, result_out)`, spec `{program, args,
        cwd, env, read: [paths], write: [paths], network, timeout}`, the
        paths worked out by the host's folder rules and only enforced by the
        plugin; ESC through IsCancelled. Fails closed: a [protected] command
        with no sandbox loaded is refused, never run bare. One provider,
        chosen by the host (a policy key), not the first to load. Designed
        when the plugin is started, not frozen ahead of it.

---

# 1.2.0: project memory, POSIX prompt editing (PR #1)

Merged 20 Oct (`6163cd3`, from MatteO-Matic): a per-project memory file at
`~/.tapto/projects/<project>/memory/memory.md`, whose first 50 lines ride in
the system prompt; a line editor of our own for the prompt on Linux/macOS
(arrows, history, bracketed paste); the user's input in yellow.

Reviewed the same day. Fixed in `486523b` and `592fe14`:

- [x] `run_command`'s cwd and `%p` could reach the memory file through a
      writable grant covering `~`: the exception sat in
      `refuse_protected_write`, which commands share. Now in
      `resolve_for_editor` alone.
- [x] The index was capped in lines, not bytes: now 8 KiB, with a note to
      the model when it is cut.
- [x] Pasted escape sequences and controls were echoed raw, and a pasted
      `ESC[201~` plus newline submitted the prompt. Dropped and handled.
- [x] 4-byte UTF-8 (emoji) split in the editor.
- [x] CHANGELOG: the arrow-key entries had landed under the released 1.1.0.
- [x] The prompt frames the memory as notes, not instructions, says what may
      be written to it, and fences the index in `<memory-index>` markers the
      file cannot fake.

Open:

- [x] **Build and try the editor on Linux.** Built and tested in an Ubuntu
      24.04 container (GCC 13), driven through a pty and rendered with pyte.
- [ ] Prompt injection that outlives the session: the wording reduces it, it
      does not close it. Structural options: a notice to the user whenever
      the memory file is written, and the index carried in the first user
      message instead of the system prompt.
- [x] A symlinked `memory.md` is followed (`is_memory_file` compares
      canonically), so its target becomes writable by the editor. The model
      cannot plant the link; refuse it anyway. Goes away with `@memory`
      (Backlog, system labels).
- [x] Editor: the text after the cursor is not drawn (`reprint` stops at
      `pos`), so moving left hides the rest of the line.
- [x] Editor: `char_cols` counts every non-ASCII character as 2 columns, so
      åäö throw the wrapping off. Now a table of zero-width and wide ranges
      (`display_cols`); `wcwidth` follows the C locale, which is never set.
- [x] Editor: Ctrl-C at the prompt exits with echo and canonical mode off
      (ISIG is on and there is no SIGINT handler to restore the terminal).
      Also true of Ctrl-C while the model worked, since before PR #1.
- [x] C4100 on Windows: `read_user_input`'s `prompt` is unused there.
- [x] Found while testing: Backspace after a multi-byte character removed
      one byte of it, and Enter did not move to a new line, so a command's
      output started on the prompt's row.
- [x] Release as 1.2.0 (memory is a feature), with /resume, [ro] commands,
      tool overflow and @memory. Decided 20 Oct: plugins wait for 1.3.0.
      Released 20 Oct (local tag `v1.2.0`). Push libtapto `main` and `v0.3.0`
      **before** tapto-code `main` and `v1.2.0`, then the pin in
      tapto-code-install.
- [x] Longer term the memory becomes a plugin, so an administrator can turn
      it off (see 1.3.0). Done 27 Oct: the memory plugin.

---

# 1.1.0: the version in every request, commands without a shell

Released 4 Oct as 1.1.0, not the planned 1.0.5: commands no longer running
through a shell is a breaking change, and it was not big enough for 2.0.

Pushed 25 Sep: libtapto `91151da` tagged `v0.2.0`
(`AiConfig::setUserAgent`, sent by all three clients) and tapto-code `f1e7e2b`
(`User-Agent: tapto-code/<version> (<commit>; <os>)`, `LIBTAPTO_TAG` v0.2.0,
README policy section, `[Unreleased]` entry).

- [x] Push libtapto `main` and `v0.2.0` **before** tapto-code `main`: CI
      fetches the tag at configure time and fails without it.
- [ ] The LiteLLM side: a pre-call hook that reads the request's User-Agent
      and refuses anything without `tapto-code/` or below a minimum version,
      with a 4xx whose message says where to get the update. tapto-code does
      not retry a 4xx and prints its text, so the message reaches the user
      as written. Versions before 1.1.0 send cpp-httplib's default, so
      "requires tapto-code/" catches all of them.
- [x] Release 1.1.0 as 1.0.4 was: CMakeLists version, `[1.1.0]` section with
      date and compare links, tag, then the pin in tapto-code-install.

## Also in 1.1.0: commands without a shell, `.tapto` read-only

A user's allow-listed `nmake` ran a `nmake.bat` the model wrote into the
project (cmd.exe searches the current directory first). Now: the program is
looked up by tapto-code (script folders, then PATH, skipping folders the agent
can write to) and started by absolute path; no command goes through a shell;
shell syntax is refused and belongs in a script in the machine's
(`%ProgramFiles%\Centlake\tapto\scripts`, policy `script-folder`) or the
user's (`~/.tapto/scripts`) script folder. `~/.tapto`, any `.tapto`, the
machine store and the script folders are write-protected like `.git`.

- No installer creates the machine's script folder: the MSI is per user and
  cannot write under Program Files. Administrators create it when they deploy
  the scripts (GPP Folders/Files, Intune, SCCM); under Program Files it
  inherits Users: read/execute, which is what tapto-code checks for. Decided
  4 Oct: Program Files stays the default (ProgramData lets users add files and
  squat the folder). A per-machine `tapto-admin.msi` only if a customer asks.
- [ ] Release notes / mail to administrators: allow-lists with pipes, `&&`,
      `dir`/`echo` stop working in 1.1.0; move them into `.cmd` scripts.
- [ ] Follow-up, not 1.1.0: the agent can still edit what an allow-listed
      build reads (Makefile, CMakeLists.txt, package.json scripts). Idea: a
      per-command list of build-input globs, and a confirmation before the
      command runs when the agent changed one since the last confirmed run.
- [ ] Follow-up, maybe: refuse *reading* plain-text API keys in `~/.tapto/config`
      (reads of `.tapto` are allowed, like `.git`).

---

# 1.3.0: plugins

Moved from 1.2.0 on 20 Oct, so the memory release does not wait for it.

Features beyond the core come as plugin modules, a DLL/SO each, instead of
growing tapto-code. The core stays small enough to audit; a customer who
wants Jira adds `jira.dll` rather than getting Jira code in every install.
First plugin: the Jira tools from tapto-jira, so one session can compare code
with tickets (`/add-plugin jira`).

Decisions (25 Sep):

- **sCom, in-process.** The plugin boundary is sCom from cpr (the Centlake
  Portable Runtime), the shell-extension style of COM: the host loads a
  module, asks it for an interface, calls it.
- **Closed, not MCP.** MCP lets the user plug in anything; the point here is
  a set of plugins the organization controls. If a customer wants MCP, it is
  one more plugin (an MCP bridge), under the same policy as the rest.
- **ro/rw per plugin,** as folders have it: `/add-plugin jira` gives the
  read tools, `/add-plugin jira rw` also the ones that write. The Jira in
  question is self-hosted and closed, which is what keeps prompt injection
  through ticket text in bounds; ro by default keeps it there.

## Interfaces (`tapto/plugin.h`)

JSON crosses the boundary, not typed methods. The tool surface is JSON
already (schema out, arguments in, text back), so a small vtable carries any
plugin, and a small vtable is one that stays stable.

```cpp
// Exported by each module; the DllGetClassObject of this system. The host
// comes first, so the plugin can allocate before its first call returns.
extern "C" SRESULT SCOM_CALL TaptoGetPlugin(ITaptoHost* host, const GUID& riid,
                                            void** ppv);
// OK once no object of the module is alive; see /remove-plugin below.
extern "C" SRESULT SCOM_CALL TaptoCanUnloadNow();

interface ITaptoPlugin : public IBase {
    // {name, version, config_keys[], prompt,
    //  tools[{name, description, schema, writes}]}
    virtual SRESULT SCOM_CALL Describe(char*& json_out) = 0;
    // The plugin's own config keys, secrets already resolved by the host.
    virtual SRESULT SCOM_CALL Configure(const char* json_config) = 0;
    virtual SRESULT SCOM_CALL CallTool(const char* name, const char* json_args,
                                       char*& result_out) = 0;
};

// Optional, found by QueryInterface: a plugin that can be unloaded.
interface ITaptoPluginUnload : public IBase {
    virtual SRESULT SCOM_CALL Shutdown() = 0; // close connections, stop threads
};

// Optional, found by QueryInterface: prompt text that changes, asked for
// each time the system prompt is rebuilt (Describe's `prompt` is fixed).
interface ITaptoPluginPrompt : public IBase {
    virtual SRESULT SCOM_CALL Prompt(char*& text_out) = 0;
};

// What a plugin may ask of the core.
interface ITaptoHost : public IBase {
    // The one heap for all memory that crosses the boundary: strings now,
    // arrays of structs if an interface ever needs them. Aligned as malloc
    // is (alignof(max_align_t)); null on failure.
    virtual void* SCOM_CALL MemAlloc(size_t size) = 0;
    virtual void SCOM_CALL MemFree(void* p) = 0;        // null is fine
    virtual SRESULT SCOM_CALL Log(const char* line) = 0; // to the trace file
    virtual SRESULT SCOM_CALL IsCancelled() = 0;         // ESC, for long calls
    // Put a file in @tmp for the model to read (a downloaded attachment);
    // `label_path_out` is "@tmp/<name>", for the plugin's result text.
    virtual SRESULT SCOM_CALL WriteTemp(const uint8_t* data, size_t size,
                                        const char* name_hint,
                                        char*& label_path_out) = 0;
    // Attach a PNG to the result of the call in progress, for a model that
    // can see. The host checks the signature, reads width and height from
    // IHDR itself and caps the size; a null `label` is the display path of
    // the call's x-tapto-path argument.
    virtual SRESULT SCOM_CALL AttachImage(const uint8_t* png, size_t size,
                                          const char* label) = 0;
};
```

`IsCancelled`, `WriteTemp` and `AttachImage` act on the call in progress:
the host keeps the `Context` of the current `CallTool` and refuses them
outside one. `AttachImage` ends in `tapto::putToolImage`, so the backends
attach it in each provider's shape as they do for core tools.

A new capability is a new interface (`ITaptoPlugin2`) found by
QueryInterface, so old plugins load in new hosts and the other way round.

## Memory across the boundary

The sCom convention: an in-string is `const char*`, an out or inout string
is `char*&` in C++ and `char**` in C (the same thing in the vtable, a
reference is passed as a pointer). Everything that crosses and is freed on
the other side is allocated with `ITaptoHost::MemAlloc` and freed with
`MemFree`, so allocation and free are always the host's, whatever compiler
built the plugin. For an inout parameter the callee frees the old value
before it stores the new one.

Today only strings cross (JSON carries anything structured). If a call
ever needs an array, it is a pointer and a count, out as `T*&` plus
`size_t&`, the block from `MemAlloc`, and `T` a plain C struct: fixed-width
fields from `<cstdint>`, strings in it as `char*` from the same heap, no
STL, no virtuals. Freeing it frees each string and then the block.

The host pointer from `TaptoGetPlugin` goes into a global in the plugin
(`AddRef`ed there, released in `Shutdown` or the last `Release`); a module is
loaded once per process and the host outlives every plugin, so one global is
enough. In the host the same global points at its own `ITaptoHost`.

`plugin.h` carries one RAII class, `TaptoString`, used on both sides through
that global: it owns a `char*` from `MemAlloc`, frees it with `MemFree`,
hands out `char*&` for an out parameter (freeing what it held first) and
`detach()`es to pass ownership on. Inside the plugin, `std::string` and its
own heap are fine; only what crosses goes through `TaptoString`. The one
mistake left is returning a string from another allocator (cpr's
`sComStringDup` linked in statically, `strdup`): the rule is in `plugin.h`,
and the test host in a debug build tags its allocations and refuses to free
one it did not make.

No `scom.dll` / `libscom.so`, then: the allocator lives behind the
interface, and nothing but a header is shared. A plugin needs only a C ABI,
so any compiler, and any language with a C FFI, can build one.

## The core decides, so no plugin can get it wrong

- **ro/rw:** each tool declares `writes`. In ro mode the host does not offer
  those tools to the model at all, which is stronger than refusing the call,
  and it is one rule in the audited core rather than one per plugin.
- **Secrets:** the plugin names `jira-api-token`; the host resolves
  `wincred:` / `env:` / `cmd:` and hands over the value in `Configure`. No
  plugin parses a secret reference.
- **Config keys:** the keys a plugin declares are accepted by `config set`
  while it is installed. That also settles, for plugins, the old follow-up
  about `config set` refusing sibling programs' keys (see the libtapto port's
  follow-ups below).
- **Tool names:** anything not named `<plugin>_...` is refused, so a plugin
  can neither collide with another nor stand in for a core tool.
- **Session:** loaded plugins and their mode are saved in `session.json`
  beside the granted folders, and `/resume` loads them again.

## Paths, folders and overflow

The core keeps the path rules, so no plugin resolves a path itself. Builds on
the system labels in the Backlog (`@memory`, `@tmp`).

- **Path parameters are marked in the schema:** `"x-tapto-path": "read"` or
  `"write"`. The host resolves the label or path with the same rules as the
  editor and refuses before `CallTool`, so the plugin only ever sees an
  absolute path that has been checked. The same idea as `writes` on tools.
- **A plugin's own folder:** one at most, `"folder": "ro" | "rw"` in
  `Describe`. The host makes `~/.tapto/projects/<p>/plugins/<plugin>/` and
  gives it the system label `@<plugin>`: `@` followed by a plugin's name
  always means that plugin's folder, so no plugin may be called `tmp`
  (decided 27 Oct, in place of named folders and `@<plugin>-<name>`). A
  plugin loaded ro gets its folder ro; `/remove-plugin` takes the label
  away. Configure tells the plugin its path, label and mode.
- **Overflow** is the core's: the host gets every tool's `result_out`, so a
  long plugin result is cut and written to `@tmp` like any other. No plugin
  does it.
- **Files for the model:** `ITaptoHost::WriteTemp` writes into `@tmp` and
  hands back the label path.
- **Prompt text that changes:** `ITaptoPluginPrompt::Prompt`, asked at every
  rebuild.

The project memory needs nothing more than that: a `memory` plugin whose
folder is `@memory`, rw, and whose `Prompt()` returns the rules and the index.

## Loading and trust

- From `<install dir>/plugins/` only: not PATH, not the working directory,
  not the project.
- The MSI is per-user, so that directory is writable by the user and by
  anything running as them. Check the Authenticode signature
  (WinVerifyTrust, against our signing certificate) before `LoadLibrary`;
  plugins are signed in the same Jenkins job with the same eToken as the MSI.
  On Linux the root-owned directory the .deb/.rpm installs to does the same
  job.
- Policy, after `allowed-providers`: `allowed-plugins = jira` and
  `allow-user-plugins = 0`, in the ADMX template too. The MCP bridge, if it
  ever exists, is closed by the same key.

## /remove-plugin

`FreeLibrary` is only safe once every interface pointer into the module is
released. So, on `/remove-plugin`:

1. hide its tools and rebuild tools and prompt;
2. call `ITaptoPluginUnload::Shutdown` if the plugin has it;
3. release every pointer the host holds;
4. `FreeLibrary` only if `TaptoCanUnloadNow()` returns `OK`.

A plugin without the second interface stays loaded with its tools hidden
until exit, which is always safe.

## Commands and config

`/add-plugin <name> [ro|rw]`, `/list-plugins`, `/remove-plugin <name>`,
worded like the folder commands. `plugin.jira = ro` in config for a project
that always wants it.

## Why not STL across the boundary, and why libtapto stays static

Decided 20 Oct, after the sCom shared library was dropped. Passing
`std::string` with a custom allocator would fix the heap but not the
layout: MSVC's STL, libstdc++ and libc++ differ, and so do MSVC Debug and
Release (`_ITERATOR_DEBUG_LEVEL`). A mismatch corrupts memory instead of
failing to link. Only JSON crosses this boundary, so the comfort of
containers is not worth tying every plugin to the host's compiler.

The same reason keeps libtapto static. Its API is C++ (`std::string`,
`nlohmann::json`), so as a DLL it would tie every program and plugin to one
compiler, CRT and STL, and tapto-word builds with MinGW while tapto-code
builds with MSVC.

## Vendoring `cpr/com.h` as `scom.h`

Header-only, in libtapto (`include/tapto/scom.h`), so tapto-code, tapto-jira
and every plugin compile against one copy at one pinned tag. The header
comment names the cpr commit it came from.

It is cleaned up on the way in, keeping the binary layout cpr's modules
have:

- **Types from `<cstdint>`,** in place of the ones cpr's `_ede_types.h`
  supplies (which `com.h` uses without including). cpr already makes them
  fixed-width on both platforms, so nothing moves:

  | cpr | Windows | Linux | vendored |
  |---|---|---|---|
  | `LONG` (`SRESULT`) | `long`, 32-bit | `int32_t` | `int32_t` |
  | `ULONG` (`AddRef`/`Release`, `GUID::Data1`) | `unsigned long`, 32-bit | `uint32_t` | `uint32_t` |
  | `USHORT` / `UCHAR` | 16 / 8-bit | 16 / 8-bit | `uint16_t` / `uint8_t` |
  | `SIZE_T` | `ULONG_PTR` | `C_SIZE` | `size_t` |
  | `LPUCHAR` | `unsigned char*` | `unsigned char*` | `uint8_t*` |

  The GUID stays 16 bytes and the vtables stay as they are, so an object
  built against cpr's `com.h` can still be handed to the vendored one.
- **`C_API` becomes `SCOM_CALL`:** `__stdcall` on Windows, empty elsewhere,
  as cpr has it. It only changes code on 32-bit x86 (x64 has one calling
  convention and ignores it); it is kept so a win32 build would still match
  cpr's modules.
- **Status codes as `constexpr SRESULT`** in the header's namespace instead
  of `#define OK 0` and the `ERR_*` macros. Same values, so the ABI does not
  change, and the header no longer defines `OK` for every file that
  includes it. `interface` stays a keyword-like macro only if nothing
  better reads as well; `struct` is what it expands to anyway.
- **Only what the plugin system uses:** IBase, GUID and `DEFINE_IID`, the
  interface-map macros, `sComPtr`/`sComQIPtr`. Not the string and buffer
  functions (`ITaptoHost` allocates), and not the IPC error codes and typeinfo that belong to clcom's larger
  design stay behind.

## First plugin: `image` (read_image out of the core)

Decided 26 Oct. `read_image` (85a5b2f) leaves the core before it is ever
released: not every server can see, and an image tool offered to one that
cannot is noise. It goes first, before Jira, since it needs no network, no
secrets and no rw, so it exercises the loader, `Describe`, `x-tapto-path`,
the tool-name rule and the prompt alone; Jira then adds config, secrets, rw.

- Source in `tapto-code/plugins/image/`, built by the same CMake (stb moves
  there with it), installed to `<install>/plugins/`, in the tapto-code MSI.
- Its tool is `image_read` (the tool-name rule), its `path` marked
  `"x-tapto-path": "read"`; the PNG goes back through `AttachImage`.
- The editor's binary-file hint ("Use the read_image tool") goes from the
  core; the plugin's `Describe.prompt` tells the model to use `image_read`
  for image files, so the hint exists only while the plugin is loaded.
- Vision users turn it on with `/add-plugin image` or `plugins = image:ro`.
- Signature check: Release builds always verify; Debug builds load an
  unsigned plugin and say so in the trace log.

## Order of work

- [x] libtapto: `scom.h` (vendored, header-only); `tapto/plugin.h` with
      `TaptoString`; a test plugin built in-tree and a test that loads it,
      calls it, and unloads it through both paths, and whose debug host
      refuses to free a string it did not allocate. libtapto `v0.4.0`.
- [x] tapto-code: the loader (`src/plugins.cpp`): `<exe dir>/plugins/` only;
      on Windows Release the module must be signed by the certificate that
      signed tapto-code.exe (an unsigned build loads none), held open
      deny-write from WinVerifyTrust through `LoadLibraryExW`; ro/rw
      filtering; the tool-name rule; `x-tapto-path` resolved by the host and
      stripped from the schema (Gemini refuses unknown keywords); overflow on
      every result; secret config keys resolved for `Configure`; `/add-plugin`,
      `/list-plugins`, `/remove-plugin`; `plugins =` in config; the policy
      keys and ADMX; plugins in `session.json`. Tested with
      `tests/plugin/probe.cpp`.
- [ ] Loader follow-ups:
      - [x] `config set` accepting a plugin's declared keys. Decided 27 Oct:
        neither of the two options above as they stood. A plugin's keys must
        be named `<plugin>-...` (the tool-name rule, for keys; the loader
        refuses a plugin that breaks it), so the key names its module, and
        `config set` describes that one module: loaded through the same
        signature check, never configured, freed again
        (`plugin_config_key`). Its declaration says whether the key is
        secret, so the plaintext warning and `config list`'s mask follow it.
        No manifest, which would be an unsigned second copy of what
        Describe says. An unsigned Release build refuses the key and says
        why. `config list` masks a non-core key no installed plugin claims
        when its name ends in -token/-password/-secret/-key.
      - [x] Plugin folders: `"folder": "ro" | "rw"` in Describe, one per
        plugin, at `projects/<p>/plugins/<plugin>/` as `@<plugin>`; the name
        `tmp` is refused. Written ro unless both say rw; the mode follows
        `/add-plugin` and the plugin is configured again with it;
        `/remove-plugin` takes the label away (libtapto
        `FolderSet::remove_system` / `set_system_writable`, for `v0.5.0`).
        Configure gets `"folder": {path, label, writable}`.
      - The signed path is untested: needs a build signed with the eToken.
      - [x] Linux: an exe in a `bin/` loads from `../lib/tapto-code/plugins`
        (`plugin_directory_for`), and the install rule puts them there, so the
        packages carry `/usr/lib/tapto-code/plugins/image.so`; any other exe
        location (the build tree) keeps `<exe dir>/plugins`.
      - [x] plugin.h says that strings are UTF-8, paths included (the
        host passes `u8string()`), and that no exception may leave a call.
        In libtapto for `v0.5.0`.
- [x] `image` plugin: `read_image` moved out of `tools.cpp` into
      `plugins/image/image.cpp` as `image_read`, stb with it; the core no
      longer links stb and the editor's binary-file message no longer names
      it. The plugin now reads the header's dimensions (`stbi_info`) before
      decoding, where the core tool decoded first. Built into
      `<build>/plugins/`, installed to `bin/plugins`. Tests moved from
      test-edits to test-plugins.
- [x] `jira.dll`: tapto-jira's `jira.cpp` behind `ITaptoPlugin`
      (`plugins/jira/jira.cpp`), its read tools ro and
      create/update/comment/transition rw. Its own httplib + OpenSSL, the
      pins libtapto brings; trace through `ITaptoHost::Log`, cancel checked
      between retries, the result cap left to overflow, the server and
      default project in `ITaptoPluginPrompt`. The fixed prompt tells the
      model that ticket text is data, not instructions. Two request bodies
      fixed on the way, to the shape Jira documents: create puts the fields
      under `fields`, and a transition's fields go beside `transition`.
      tapto-jira still has the old shapes. Tested in test-plugins against a
      fake Jira on localhost; not yet against a real server.
      - Not ported: the `JIRA_API_TOKEN` fallback (`jira-api-token
        env:JIRA_API_TOKEN` does it through the host) and tapto-jira's
        folders for issue text (the core's editor reads files already).
      - Settings are read at load; changing one needs /remove-plugin and
        /add-plugin. A `config set` inside the chat could reconfigure.
      - API v3 (Cloud) wants descriptions and comments in Atlassian
        Document Format; plain text is what v2 (Data Center) takes. As in
        tapto-jira.
- [x] `plugin.<name> = ro | rw | off`, one key per plugin, in place of
      `plugins = image:ro, jira:rw`. Decided 27 Oct: a list leaves no room
      for a comment on each plugin, and one key per plugin lets a project or
      a policy change one without restating the rest (`off` undoes a less
      specific level's). `plugin.` cannot clash with a plugin's own
      `<plugin>-...` keys, since a plugin name has no dot. `config set`
      checks the mode, and for ro/rw that the plugin is installed and the
      policy allows it. Comments by hand: `config set` writes none. The ADMX
      `LoadedPlugins` policy is gone; a policy sets `plugin.<name>` through
      Additional settings. `plugins =` was never released, so not read.
- [x] `skills` plugin (`plugins/skills/skills.cpp`), discussed with users
      27 Oct: skill files the user or admin drops in a folder, an index in
      the prompt, the model reads `@skills` for the rest. No tools. The
      folder is per user, `~/.tapto/plugins/skills/`, or the absolute path in
      `skills-folder` (policy can set it; must exist, never made); never the
      project's, so a clone cannot plant instructions. Read-only to the model
      whatever the plugin's mode. A skill is `<dir>/SKILL.md` (Anthropic's
      layout) or a top-level `.md`; `name`/`description` from front matter
      (plain, quoted, or a `>`/`|` block), else the file's first line. Index
      capped at 8 KiB, 300 bytes a description, fenced in `<skills-index>`;
      links and dot-files skipped. Framed as instructions from the user or
      organization, below the system prompt and the user's messages.
      - Loader: a folder may be `{"mode", "scope": "user", "key"}` besides
        `"rw"`: per user at `plugin_user_folder()`, or where one of the
        plugin's own config keys says.
      - Open: one folder only, so a policy `skills-folder` replaces the
        user's own skills rather than adding the organization's to them.
- [x] `mcp` plugin (`plugins/mcp/mcp.cpp`), the MCP bridge the decisions
      above left room for. Decided 29 Oct: stdio and Streamable HTTP both;
      the servers in `~/.tapto/plugins/mcp/servers.json`, or the file
      `mcp-servers` names (the skills-folder pattern: a policy setting it
      fixes the list), in the `mcpServers` layout. The host names every
      plugin's user folder in Configure (`"user_folder"`), without making it
      or putting it in the folder set, so the model cannot read the file and
      the tokens it may hold. Its env/header values are resolved by the host (libtapto `v0.6.0`:
      `ITaptoHostSecrets::ResolveSecret`, during Configure only); its tools
      from `ITaptoPluginTools`, which the loader asks after every Configure,
      since they are known only once the servers run; a tool writes unless
      the server says `readOnlyHint`, overridden per server with
      `"readOnly": "all" | "none" | [names]`. Tools `mcp_<server>_<tool>`,
      cut to 64 with a hash; schemas rebuilt from the keywords every provider
      reads, `$ref`s into `$defs` inlined. stdio commands from `PATH` only
      (never the project), a `.cmd` under `cmd.exe /d /s /c` with `"`, `%`, `!`
      refused in its arguments, the server in a kill-on-close job (a process
      group elsewhere); plain http to loopback only. Cancel sends
      `notifications/cancelled`; a stdio server that exited is started again
      on its next call. Tested in test-plugins with `tests/plugin/
      mcp_server.cpp` and an httplib server.
      - OAuth, decided 29 Oct for a user's own OAuth server: the spec's flow
        (401 + resource_metadata, RFC 9728 and 8414 discovery, dynamic client
        registration anew at each login so the loopback port may change,
        authorization code + PKCE S256, `state`, `resource`). The login is
        the user's `/mcp login <server>`, not the model's (an `mcp_login`
        tool came first and went: a model should not open browser windows),
        so ESC cancels it and nothing blocks the start. `/mcp logout`
        revokes (RFC 7009) where the server has an endpoint for it and
        forgets the tokens; `/mcp list`; `/mcp reload` configures again from
        the same settings. Plugin commands (libtapto `ITaptoPluginCommands`):
        `"command": {usage, description}` in Describe, only `/<plugin>`, run
        after every core command so none is shadowed, as a call (ESC, the
        secret store; ResolveSecret too, for reload), text to the user only,
        tools and prompt rebuilt after. The loader also asks for a plugin's
        tools after every CallTool, so a server that wants a login again
        loses its tools from the next turn (a turn keeps the tools it
        started with). The servers file is watched: the `before_turn` event
        (libtapto `ITaptoPluginEvents::OnEvent(event, json, text_out)`,
        subscribed to with `"events"` in Describe, run as a command is), which
        stats it (mtime
        and size) and reads it only when that moved; the servers are synced
        by entry (new or changed start, removed stop, the rest run on), and
        a file that does not parse keeps them as they were. Tokens
        through `ITaptoHostSecrets::StoreSecret`/`LoadSecret`: Credential
        Manager `tapto/plugin/<plugin>/<name>`, split past 2048 bytes, else
        `~/.tapto/plugin-secrets/<plugin>.json` 0600. Renewed at start when
        expired or refused, and once under a refused call. Debug builds take
        `TAPTO_MCP_TEST_LOGIN=get` in place of a browser, for the tests.
        Untested against a real authorization server.
      - Not done: the servers' `instructions` from initialize, which would put
        server text in the system prompt; resources and prompts; the old SSE
        transport; sampling, roots and elicitation (the client declares none).
      - `notifications/tools/list_changed` is let go: the list is the one
        from the start until the plugin is loaded again.
      - Untested on Linux and against real servers.
- [ ] General instructions (a code style guide, say), asked for by a user
      27 Oct: not a plugin. The core's `system-prompt-append` and
      `system-prompt-append-file` already do it, and a policy key cannot be
      dropped with /remove-plugin as a plugin can. But checked the same day,
      they do not stack: like every key, one value wins, policy over local
      over global over system. So a project's `system-prompt-append-file`
      hides the user's own, and a policy one hides both, silently. To decide:
      append every scope's (system, global, local, then policy), which
      changes how a key resolves and so counts as breaking by the CHANGELOG's
      rules; and maybe a folder of instruction files in place of one file.
- [ ] Installer: `plugins\` in the MSI, the plugin signed in
      the same job; plugins as their own feature or their own MSI, to decide.
- [ ] Open: tapto-jira as a thin host around the same plugin, or retired.
- [x] The project memory (PR #1) as a plugin, so an administrator can leave
      it out with `allowed-plugins`: `plugins/memory/memory.cpp`, no tools, its
      folder `@memory` and its rules and index from `ITaptoPluginPrompt`;
      `src/memory.cpp` and `tapto/memory.h` are gone from the core. Decided
      27 Oct: not loaded by default, like any plugin (`plugin.memory = rw`),
      and no `:off`. Loaded ro the prompt says the memory is read-only. On its
      first configure with an empty folder it moves the 1.2.0 memory
      (`projects/<p>/memory/`) in. test-memory loads the module.
      - Breaking for 1.2.0 users: the memory is off until they add the
        plugin. In CHANGELOG under Changed.
      - `plugin.memory = ro` lets the model read its notes and not write
        them.

---

# tapto-code: adopt libtapto

The shared tapto code now lives in one place: `../tapto-word/libtapto`, cut on
5 Sep 2026 from tapto-code (baseline) plus tapto-vnc's fixes, and built as one
static library. tapto-word's todo.md, section 1, lists "Port: tapto-code" as
the open item. This is that port.

Tick boxes are the unit of work. Steps 0-5 are done and committed (7 Sep);
the follow-ups at the end are open.

---

## Where things stand (measured 6 Sep)

`diff -w --strip-trailing-cr` of tapto-code's current tree against the library
(the raw diffs are mostly CRLF noise: tapto-code is checked out CRLF, the
library is LF via `.gitattributes`):

| file | state |
|---|---|
| `config.h/.cpp`, `secret.h/.cpp`, `log.h`, `cancel.h`, `context.h`, `encoding.h/.cpp`, `openai.h`, `gemini.h` | identical |
| `claude.h` | library drops the two timeout constants; they come from `AiConfig` now |
| `paths.h/.cpp` | library adds `global_dir()` |
| `aiconfig.h` | library adds `connectionTimeoutSeconds`/`readTimeoutSeconds` (dialect-neutral names, old `openai*` names kept as aliases), `effort` (Claude `output_config.effort`), `keepRecentImages` |
| `aibackend.h` | **library is missing `buildTrimmedHistoryForSummary()`** and `kCompactTrimPayloadChars`, the /compact trimmer from 0335060 (26 Aug). The merge said "union of both" but this one fell through. `tests/test_compact.cpp` covers it |
| `tool_registry.h` | library replaces the fixed display-name table with a per-tool hook `ToolSpec::display`; `getToolDisplayName()` now takes the tool table. Adds `tapto::ConnectionLost` |
| `ui.h` | library keeps the 7 functions the provider clients call; tapto-code's other 18 (banner, help, setup prompts, config/command listing, prompt, reply) are the program's own |
| `claude.cpp` | adaptive thinking (`{"type":"adaptive"}` + `output_config.effort`; the old `budget_tokens` form 400s on Opus 4.7+), image blocks in tool results, image pruning with cache-breakpoint pull-back, `ConnectionLost` passthrough, usage/cache logging, final text logged |
| `openai.cpp` | reasoning fields as a table (`reasoning_content`, then `reasoning`; same set as 0b2a0c6, other order), images as a user turn, permanent-error detection on 5xx ("not supported", "unsupported", "invalid_request", "does not support" are not retried), usage logging, final text logged |
| `gemini.cpp` | `thinkingLevel` instead of `thinkingBudget`, inline images, `ConnectionLost`, timeouts from config |
| new in the library | `provider.h/.cpp` (lifted from main.cpp 410-605), `base64.h`, `tool_image.h/.cpp`, `fstools.h/.cpp`, `certs.h/.cpp`, `test/test_libtapto.cpp`, `test/ui_null.cpp` |

Two facts that shape the steps:

- **Toolchains differ.** tapto-code builds with MSVC (Ninja + cl.exe, `/W4`);
  tapto-word builds with Strawberry MinGW. libtapto has never been compiled by
  MSVC. Expect a warning or two to fix upstream.
- **`first_run_setup()` needs four functions provider.cpp keeps private**:
  `default_provider_name`, `provider_dialect`, `api_key_env_var`,
  `resolve_api_key`. They sit in provider.cpp's anonymous namespace because
  tapto-word never runs setup.

## Decision

Vendor by copy, byte-identical to `../tapto-word/libtapto/`. The invariant after
every step is

```sh
diff -r ../tapto-word/libtapto libtapto     # empty
```

Library changes go into tapto-word's copy first (it has the library tests and
is where the merge was verified), get committed there, then are copied here.
The final home (own repo + submodule, or FetchContent from a tag) is tapto-word's
open question and is decided after tapto-vnc's port, not now.

Boundary rule, unchanged from the merge: the library holds what is
program-independent. `main.cpp`, `tools.cpp`, `commands.cpp`, `ui.cpp`, the
config CLI and its key validation stay here.

---

## Steps

### 0. Upstream first, in `../tapto-word/libtapto`

- [x] `aibackend.h`: restore `buildTrimmedHistoryForSummary()` and
      `kCompactTrimPayloadChars` from tapto-code's copy, verbatim. It is
      header-only and already handles all three history shapes.
- [x] `test/test_libtapto.cpp`: add a trimmer test (port the stub-backend cases
      from `tests/test_compact.cpp`; three shapes, under/over the limit, live
      history untouched). tapto-code's `test-compact` can then go, or stay as a
      second run of the same thing.
- [x] `provider.h`: export `default_provider_name()`, `provider_dialect()`,
      `api_key_env_var()`, `resolve_api_key()`. Move them out of the anonymous
      namespace; no body changes.
- [x] Build tapto-word, run `test-libtapto`, commit. Done 6 Sep as tapto-word
      commit c50a820.

### 1. Vendor

- [x] `cp -r ../tapto-word/libtapto libtapto` (all of it, `test/` included).
- [x] Add `.gitattributes` with `libtapto/** text=auto eol=lf` so the copy stays
      LF under `core.autocrlf=true` and `diff -r` stays clean. Repo-wide LF
      normalisation is a separate, noisy commit; not now.
- [x] Delete the private copies:
      `include/tapto/{aibackend,aiconfig,cancel,claude,config,context,encoding,gemini,log,openai,paths,secret,tool_registry,ui}.h`
      and `src/{claude,openai,gemini,config,paths,secret,encoding}.cpp`.
      `ui.h` in particular must go: `include/` is searched before the library's
      include dir, so a stale copy would shadow the library's header silently.
- [x] What remains under `include/tapto/`: `commands.h`, `tools.h`, and the new
      `termui.h` (step 2). Under `src/`: `main.cpp`, `commands.cpp`,
      `tools.cpp`, `ui.cpp`.
- [x] `diff -r ../tapto-word/libtapto libtapto` is empty.

### 2. Program side

- [x] **`include/tapto/termui.h`** (new): the 18 declarations the library's
      `ui.h` dropped -- `print_reply`, `print_usage`, `print_banner`,
      `print_chat_header`, `print_chat_hints`, `print_help`, the four
      `print_setup_*`, `print_prompt`, `print_prompt_accepted`,
      `print_config_entry`, `print_command_entry`, `print_command_added`,
      `print_command_removed`, `print_no_commands`. Same `tapto::ui` namespace,
      same bodies in `ui.cpp`; `ui.cpp` includes both headers, `main.cpp`
      includes `termui.h`. This is what tapto-word did with `paneui.h`.
- [x] **`main.cpp`**: delete the provider block (the anonymous-namespace
      functions from `get_effective` at ~410 through `provider_label` at ~605,
      and `effective_config` at ~130) and `#include "tapto/provider.h"`. Keep
      `has_suffix`, `provider_name_of_key`, `kProviderBlockKeys`,
      `is_valid_provider_name`, `is_supported_config_key`, `is_api_key_key`:
      they serve the config CLI, which is this program's. (`has_suffix` and
      `provider_name_of_key` are duplicated privately in provider.cpp; ten
      lines, leave it.)
- [x] **`tools.cpp`**: set `ToolSpec::display` on each of the four tools with
      the label logic from the old `getToolDisplayName` table, verbatim --
      `View src/foo.cpp:10-50`, `Edit src/foo.cpp`, `Search *.cpp ~"q"`,
      `Run build arg1`. Without this the status line regresses to raw tool
      names and nothing fails. Add a case to `tests/test_edits.cpp` that calls
      `getToolDisplayName(builtin_tools(), ...)` for each of the four.
- [x] **Timeouts as config**: read `connection-timeout` (default 30) and
      `read-timeout` (default 300) in `cmd_chat` and set them on `AiConfig`
      with the dialect-neutral setters; add both to `is_supported_config_key`,
      `kSupportedKeysHelp`, the usage text and the README. tapto-word already
      documents these keys as shared and points users at tapto-code to set
      them, so today `tapto-code config set read-timeout 1800` is refused.
      Keep calling `setOpenaiReasoningEffort`: the openai dialect reads
      `openaiReasoningEffort()`, Claude reads `effort()`. Leave `effort` unset
      (server default). Unifying the two knobs is tapto-word's open question,
      not this port.
- [x] **Claude thinking**: tapto-code never set a thinking budget, so the old
      client sent no `thinking` field and Claude did not think. The library
      sends `{"type":"adaptive"}` (plus `display: summarized` when `print-cot`
      is on) unless the budget is exactly 0. That is a behaviour change for
      every Claude user: slower and costlier turns, reasoning shown in the
      terminal. Accept it -- it is the current API and tapto-vnc/tapto-word
      already run this way -- and say so in the CHANGELOG. A `thinking = off`
      key can come later if someone asks.
- [x] `tests/test_compact.cpp`: deleted; `test-libtapto` runs the same cases.
      Step 3 drops the CMake target. Also found: `tools.h` included
      `tool_registry.h` by sibling path, now `tapto/tool_registry.h`.

### 3. CMake

- [x] `add_subdirectory(libtapto)` replaces the two `FetchContent_Declare`
      blocks, `find_package(OpenSSL)`, the `CPPHTTPLIB_OPENSSL_SUPPORT`
      definition and the `advapi32` link: the `tapto` target exports all of
      them PUBLIC. Keep the version stamp block as is.
- [x] `add_executable(tapto-code src/main.cpp src/commands.cpp src/tools.cpp
      src/ui.cpp)`, `target_link_libraries(tapto-code PRIVATE tapto::tapto)`,
      include dirs `include` and the generated dir.
- [x] `test-edits`: sources `tests/test_edits.cpp src/tools.cpp
      src/commands.cpp` plus `libtapto/test/ui_null.cpp`, link `tapto::tapto`.
      (`config.cpp`/`paths.cpp` come from the library now. `ui_null.cpp`
      because a static library only pulls the objects a test references, and
      the day a test touches provider.cpp it would otherwise fail to link on
      `tapto::ui::print_error`.)
- [x] `test-compact` (if kept): link `tapto::tapto` for the include path.
- [x] `set(LIBTAPTO_BUILD_TESTS ON)` before the `add_subdirectory`, so
      `ctest` here runs `test-libtapto` against the vendored copy under MSVC.
      That is the only MSVC coverage the library gets.
- [x] `tapto-code.vcxproj` still lists `include\minicode\*.h` from before the
      June rename, so it is already dead. Out of scope; delete it in its own
      commit or leave it.

### 4. Verify

- [x] `cmake -S . -B build && cmake --build build` under MSVC: clean at `/W4`.
      Anything the library trips on is fixed in `../tapto-word/libtapto`,
      committed there, re-copied (the invariant).
- [x] `ctest --test-dir build --output-on-failure`: `edits`, `compact` (if
      kept), `libtapto`.
- [x] `tapto-code config list`, `config set read-timeout 900`, `config get`,
      `config unset`, `command list`: the CLI path through the exported
      `effective_config`.
- [x] Setup path: move `~/.tapto/config` aside, run `tapto-code chat`, walk
      the first-run prompts, confirm the key lands in the global store.
- [x] One real turn per dialect (claude, a local openai-compatible server,
      gemini) with `trace-file` set: status line shows `Edit src/...` not
      `str_replace_based_edit_tool`; `/compact` runs and the trace shows the
      trimmed placeholders; ESC interrupts; `/env` reports input tokens; the
      trace has the new `usage:` lines.
- [x] `git diff --stat`: only `libtapto/` added, the 21 duplicates deleted,
      `CMakeLists.txt`, `main.cpp`, `tools.cpp`, `ui.cpp`, `termui.h`,
      `test_edits.cpp`, `.gitattributes`, README, CHANGELOG.


Notes from steps 3 and 4 (6 Sep):

- MSVC found two things in the library, both fixed upstream and re-copied:
  `certs.cpp` used `fopen` (C4996 at /W4, tapto-word 1ab7ba2), and then
  `test_certificates` died with 0xC0000409 because the program's UCRT opened
  a `FILE*` that Strawberry's MinGW-built libcrypto DLL then wrote through.
  PEM now goes through memory BIOs and the library's own file I/O
  (tapto-word d9e5cd4). Any program linking an OpenSSL from another
  toolchain was exposed to the same crash.
- Verified: `edits` (49 checks) and `libtapto` pass under MSVC; the config
  CLI incl. the new timeout keys and the refusal of `word-port`; first-run
  setup against a scratch `USERPROFILE`; one tool-using turn each on
  qwen38_gb10 (openai), claude and gemini with `View src/hello.cpp` on the
  status line; `/env` reporting input tokens; `/compact` trimming a 7 KB
  input to ~500 bytes; Claude's `cache_read` non-zero on the second request.
- Not verified: ESC interrupt (needs a console, not a pipe). The Claude
  request body is not in the trace, so adaptive thinking is inferred from
  the request succeeding on claude-sonnet-5, not read back.

### 5. Docs and version

- [x] README "Dependencies": nlohmann/httplib/OpenSSL now arrive through
      `libtapto/CMakeLists.txt`; a "libtapto" section names the library, what
      it holds, and that tapto-word shares it. `connection-timeout` and
      `read-timeout` are in the config-key table.
- [x] CHANGELOG `[Unreleased]`: Added -- the two timeout keys. Changed -- the
      move to libtapto, with the four user-visible consequences (adaptive
      thinking on Claude, permanent 5xx not retried, Gemini read timeout,
      trace usage lines).
- [x] One commit for the port, docs included (7 Sep). The version stays
      0.2.0: a release here is its own "Version x.y.z" commit plus a tag, and
      `[Unreleased]` already holds a month of other changes, so the number is
      a call to make over all of them. The port itself touches nothing on the
      breaking surface (flags, keys, tool schemas); on its own it would be
      0.2.1.

---

## Behaviour changes a tapto-code user will notice

| area | before | after |
|---|---|---|
| Claude | no `thinking` field | adaptive thinking, summarized reasoning shown when `print-cot` is on |
| Gemini read timeout | 120 s hardcoded | 300 s default, `read-timeout` key |
| Claude read timeout | 300 s hardcoded | same value, now configurable |
| openai 5xx with "not supported" etc. | retried 5 times with backoff | fails at once with the server's text |
| openai reasoning field | `reasoning` checked before `reasoning_content` | the other order; same fields |
| trace file | tool calls and responses | plus `usage:` per request and the model's closing text |
| status line | `Edit src/foo.cpp` | identical, provided step 2's hooks are ported |

## Follow-ups, not this port

- `config set` refuses sibling programs' keys (`word-port`, `word-cert`, and
  whatever tapto-vnc adds), while tapto-word tells its users to set config
  with tapto-code. Either tapto-code learns a pass-through for known program
  prefixes, or each program grows a `config set` of its own.
- One thinking knob across dialects (`<name>-effort` mapped per dialect)
  instead of `effort` + `reasoning-effort`. Open in tapto-word's todo.
- Final home of libtapto once tapto-vnc is ported: own repository consumed as
  a submodule or via FetchContent; then the copies here and in tapto-word go.
- Repo-wide `* text=auto eol=lf`, as tapto-word has, in a commit of its own.
- tapto-jira: not on this machine; its drift is unmeasured.
- `config list` masks only `api-key` values. Other secrets the shared store
  holds for sibling programs (tapto-vnc's `vcenter-password`) print in clear.
