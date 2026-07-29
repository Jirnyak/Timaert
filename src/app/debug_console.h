// In-game developer console — a Quake-style REPL for the Playing state.
//
// This header defines ONLY the generic machinery: a table-driven command
// registry, coloured scrollback, input history, tab-completion and the ImGui
// window. It knows nothing about GameState, the ECS or the subworld.
//
// The actual commands — which mutate the world — are registered by the
// application layer (see `register_console_commands` in main.cpp) as
// std::function handlers that capture whatever they need. This keeps the
// project's no-hardcode rule: adding a command is one `register_cmd` call,
// never an edit to a dispatch switch. New content is a table row, as with the
// fauna / loot tables.
#pragma once
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace sm::dev {

// Reply-line severity → colour in the scrollback.
enum class ConsoleLevel : std::uint8_t { Echo, Info, Ok, Warn, Error };

struct ConsoleLine {
    std::string  text;
    ConsoleLevel level = ConsoleLevel::Info;
};

struct Console;  // fwd (a handler receives the console to print into)

// A command handler. `args` are the whitespace-split tokens AFTER the command
// name (quoted spans stay together). Handlers print results via `c.info/ok/
// error/...`. Returning false signals a usage error — the console then prints
// the command's `usage` string.
using ConsoleFn = std::function<bool(Console& c, const std::vector<std::string>& args)>;

struct ConsoleCommand {
    std::string name;   // dispatch token (matched case-insensitively)
    std::string usage;  // e.g. "tp <x> <y>"  (echoed on usage error)
    std::string help;   // one-line description (listed by `help`)
    ConsoleFn   fn;
};

// The console instance. One lives on App for the process lifetime; it is never
// copied or moved after commands are registered, so handler lambdas may safely
// capture `&app` / `&console`.
struct Console {
    bool open = false;

    std::vector<ConsoleCommand> commands;    // the registry (single source of truth)
    std::vector<ConsoleLine>    scrollback;  // printed lines (ring-capped)
    std::vector<std::string>    history;     // submitted command lines

    // ImGui input buffer + transient UI state.
    char  input[512]      = {};
    int   historyPos      = -1;    // -1 = editing a fresh line; else index into history
    bool  scrollToBottom  = false;
    bool  reclaimFocus     = false;

    // ── Registry ──────────────────────────────────────────────────
    void register_cmd(std::string name, std::string usage, std::string help,
                       ConsoleFn fn);
    const ConsoleCommand* find(std::string_view name) const;
    // Register the module-owned built-ins (help / clear / echo). Call once
    // before the app registers its own commands so `help` lists everything.
    void register_builtins();

    // ── Output (callable from handlers) ───────────────────────────
    void print(ConsoleLevel level, std::string text);
    void info (std::string text) { print(ConsoleLevel::Info,  std::move(text)); }
    void ok   (std::string text) { print(ConsoleLevel::Ok,    std::move(text)); }
    void warn (std::string text) { print(ConsoleLevel::Warn,  std::move(text)); }
    void error(std::string text) { print(ConsoleLevel::Error, std::move(text)); }
    void printfln(ConsoleLevel level, const char* fmt, ...)
#if defined(__GNUC__) || defined(__clang__)
        __attribute__((format(printf, 3, 4)))
#endif
        ;

    // ── Dispatch ──────────────────────────────────────────────────
    // Tokenise `line`, echo it, look up the command and run it. Unknown
    // commands print an error plus the nearest suggestion.
    void execute(std::string_view line);
    void clear() { scrollback.clear(); }
};

// Draw the console window (ImGui) and handle its toggle key (grave accent /
// backtick). Call once per frame in the Playing state — it early-outs to just
// the toggle check when the console is closed. Toggling is independent of
// input focus (uses ImGui key state), so it always opens and closes.
void draw_debug_console(Console& console);

// ── Small arg-parsing helpers for handlers ────────────────────────
// Return false (leaving `out` untouched) when the token is missing or not a
// valid number. Index `i` is into the handler's `args` vector.
bool arg_int  (const std::vector<std::string>& args, std::size_t i, int& out);
bool arg_float(const std::vector<std::string>& args, std::size_t i, float& out);

} // namespace sm::dev
