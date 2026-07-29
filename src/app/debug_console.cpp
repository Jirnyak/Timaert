// Developer console implementation — see debug_console.h for the design.
#include "app/debug_console.h"

#include "imgui.h"

#include <algorithm>
#include <cctype>
#include <cstdarg>
#include <cstdio>
#include <cstring>

namespace sm::dev {
namespace {

// ── Small string utilities (ASCII, case-insensitive) ──────────────
char lower(char c) { return char(std::tolower((unsigned char)c)); }

bool iequals(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (lower(a[i]) != lower(b[i])) return false;
    return true;
}

bool istarts_with(std::string_view s, std::string_view prefix) {
    if (prefix.size() > s.size()) return false;
    for (std::size_t i = 0; i < prefix.size(); ++i)
        if (lower(s[i]) != lower(prefix[i])) return false;
    return true;
}

std::string common_prefix(const std::string& a, const std::string& b) {
    std::size_t n = 0;
    while (n < a.size() && n < b.size() && lower(a[n]) == lower(b[n])) ++n;
    return a.substr(0, n);
}

std::string trim(std::string_view s) {
    std::size_t a = 0, b = s.size();
    while (a < b && std::isspace((unsigned char)s[a])) ++a;
    while (b > a && std::isspace((unsigned char)s[b - 1])) --b;
    return std::string(s.substr(a, b - a));
}

// Whitespace tokeniser with simple double-quote spans ("a b" → one token).
std::vector<std::string> tokenize(std::string_view s) {
    std::vector<std::string> out;
    std::size_t i = 0;
    while (i < s.size()) {
        while (i < s.size() && std::isspace((unsigned char)s[i])) ++i;
        if (i >= s.size()) break;
        std::string tok;
        if (s[i] == '"') {
            ++i;
            while (i < s.size() && s[i] != '"') tok.push_back(s[i++]);
            if (i < s.size()) ++i;  // consume closing quote
        } else {
            while (i < s.size() && !std::isspace((unsigned char)s[i]))
                tok.push_back(s[i++]);
        }
        out.push_back(std::move(tok));
    }
    return out;
}

// ── ImGui scrollback colours ──────────────────────────────────────
ImVec4 colour_for(ConsoleLevel lvl) {
    switch (lvl) {
        case ConsoleLevel::Echo:  return ImVec4(0.55f, 0.85f, 0.55f, 1.0f);
        case ConsoleLevel::Info:  return ImVec4(0.82f, 0.84f, 0.80f, 1.0f);
        case ConsoleLevel::Ok:    return ImVec4(0.50f, 0.90f, 0.55f, 1.0f);
        case ConsoleLevel::Warn:  return ImVec4(0.95f, 0.80f, 0.35f, 1.0f);
        case ConsoleLevel::Error: return ImVec4(0.95f, 0.45f, 0.40f, 1.0f);
    }
    return ImVec4(1, 1, 1, 1);
}

// ── InputText callback (completion / history / char-filter) ───────
void complete_command(Console& c, ImGuiInputTextCallbackData* data) {
    // Only the first token (command name) is completed; args are left alone.
    std::string buf(data->Buf, std::size_t(data->BufTextLen));
    std::size_t start = 0;
    while (start < buf.size() && std::isspace((unsigned char)buf[start])) ++start;
    std::size_t end = start;
    while (end < buf.size() && !std::isspace((unsigned char)buf[end])) ++end;
    if (end != buf.size()) return;  // already past the command name
    const std::string prefix = buf.substr(start, end - start);

    std::vector<const ConsoleCommand*> matches;
    for (const auto& cmd : c.commands)
        if (istarts_with(cmd.name, prefix)) matches.push_back(&cmd);
    if (matches.empty()) return;

    if (matches.size() == 1) {
        data->DeleteChars(0, data->BufTextLen);
        data->InsertChars(0, matches[0]->name.c_str());
        data->InsertChars(data->CursorPos, " ");
        return;
    }
    std::string line = "candidates:";
    std::string lcp = matches[0]->name;
    for (const auto* m : matches) { line += ' '; line += m->name; lcp = common_prefix(lcp, m->name); }
    c.print(ConsoleLevel::Info, line);
    if (lcp.size() > prefix.size()) {
        data->DeleteChars(0, data->BufTextLen);
        data->InsertChars(0, lcp.c_str());
    }
}

void navigate_history(Console& c, ImGuiInputTextCallbackData* data) {
    if (c.history.empty()) return;
    const int prev = c.historyPos;
    if (data->EventKey == ImGuiKey_UpArrow) {
        if (c.historyPos == -1) c.historyPos = int(c.history.size()) - 1;
        else if (c.historyPos > 0) --c.historyPos;
    } else if (data->EventKey == ImGuiKey_DownArrow) {
        if (c.historyPos != -1) {
            ++c.historyPos;
            if (c.historyPos >= int(c.history.size())) c.historyPos = -1;
        }
    }
    if (prev != c.historyPos) {
        const char* s = (c.historyPos >= 0) ? c.history[std::size_t(c.historyPos)].c_str() : "";
        data->DeleteChars(0, data->BufTextLen);
        data->InsertChars(0, s);
    }
}

int text_edit_callback(ImGuiInputTextCallbackData* data) {
    Console& c = *static_cast<Console*>(data->UserData);
    switch (data->EventFlag) {
        case ImGuiInputTextFlags_CallbackCharFilter:
            // The toggle glyph must never type into the buffer.
            if (data->EventChar == '`' || data->EventChar == '~') return 1;
            return 0;
        case ImGuiInputTextFlags_CallbackCompletion: complete_command(c, data); break;
        case ImGuiInputTextFlags_CallbackHistory:    navigate_history(c, data);  break;
        default: break;
    }
    return 0;
}

}  // namespace

// ── Registry ──────────────────────────────────────────────────────
void Console::register_cmd(std::string name, std::string usage, std::string help,
                           ConsoleFn fn) {
    for (auto& c : commands) {
        if (iequals(c.name, name)) {  // re-registration overwrites
            c.usage = std::move(usage);
            c.help  = std::move(help);
            c.fn    = std::move(fn);
            return;
        }
    }
    commands.push_back({std::move(name), std::move(usage), std::move(help), std::move(fn)});
}

const ConsoleCommand* Console::find(std::string_view name) const {
    for (const auto& c : commands)
        if (iequals(c.name, name)) return &c;
    return nullptr;
}

// ── Output ────────────────────────────────────────────────────────
void Console::print(ConsoleLevel level, std::string text) {
    scrollback.push_back({std::move(text), level});
    constexpr std::size_t kMax = 1000;
    if (scrollback.size() > kMax)
        scrollback.erase(scrollback.begin(),
                         scrollback.begin() + long(scrollback.size() - kMax));
    scrollToBottom = true;
}

void Console::printfln(ConsoleLevel level, const char* fmt, ...) {
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    print(level, std::string(buf));
}

// ── Dispatch ──────────────────────────────────────────────────────
void Console::execute(std::string_view line) {
    const std::string cmdline = trim(line);
    if (cmdline.empty()) return;

    print(ConsoleLevel::Echo, "> " + cmdline);
    history.push_back(cmdline);
    historyPos = -1;

    const std::vector<std::string> toks = tokenize(cmdline);
    if (toks.empty()) return;

    const ConsoleCommand* cmd = find(toks[0]);
    if (!cmd) {
        error("unknown command: " + toks[0]);
        // Nearest suggestion by longest shared prefix (≥1 char).
        const ConsoleCommand* best = nullptr;
        std::size_t bestLen = 0;
        for (const auto& c : commands) {
            const std::size_t n = common_prefix(c.name, toks[0]).size();
            if (n > bestLen) { bestLen = n; best = &c; }
        }
        if (best && bestLen >= 1) info("did you mean '" + best->name + "'?  (try 'help')");
        else info("type 'help' for the command list");
        return;
    }

    const std::vector<std::string> args(toks.begin() + 1, toks.end());
    const bool ok = cmd->fn ? cmd->fn(*this, args) : false;
    if (!ok) print(ConsoleLevel::Warn, "usage: " + cmd->usage);
}

// ── Built-ins ─────────────────────────────────────────────────────
void Console::register_builtins() {
    register_cmd("help", "help [command]",
                 "list commands, or show usage for one",
        [](Console& c, const std::vector<std::string>& a) {
            if (!a.empty()) {
                const ConsoleCommand* cmd = c.find(a[0]);
                if (!cmd) { c.error("no such command: " + a[0]); return true; }
                c.printfln(ConsoleLevel::Info, "%s  -  %s",
                           cmd->usage.c_str(), cmd->help.c_str());
                return true;
            }
            std::vector<const ConsoleCommand*> v;
            v.reserve(c.commands.size());
            for (const auto& cc : c.commands) v.push_back(&cc);
            std::sort(v.begin(), v.end(),
                      [](const ConsoleCommand* x, const ConsoleCommand* y) {
                          return x->name < y->name;
                      });
            c.printfln(ConsoleLevel::Info, "%zu commands:", v.size());
            for (const auto* cc : v)
                c.printfln(ConsoleLevel::Info, "  %-16s %s",
                           cc->name.c_str(), cc->help.c_str());
            return true;
        });

    register_cmd("clear", "clear", "clear the scrollback",
        [](Console& c, const std::vector<std::string>&) { c.clear(); return true; });

    register_cmd("echo", "echo <text...>", "print the arguments back",
        [](Console& c, const std::vector<std::string>& a) {
            std::string s;
            for (std::size_t i = 0; i < a.size(); ++i) { if (i) s += ' '; s += a[i]; }
            c.info(s);
            return true;
        });
}

// ── ImGui window ──────────────────────────────────────────────────
void draw_debug_console(Console& console) {
    // Toggle works regardless of focus: ImGui key state, not SDL keydown.
    if (ImGui::IsKeyPressed(ImGuiKey_GraveAccent, false)) {
        console.open = !console.open;
        if (console.open) console.reclaimFocus = true;
    }
    if (!console.open) return;

    const ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(io.DisplaySize.x, io.DisplaySize.y * 0.42f),
                             ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.88f);
    const ImGuiWindowFlags wflags =
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar;
    if (!ImGui::Begin("##debug_console", nullptr, wflags)) { ImGui::End(); return; }

    ImGui::TextColored(ImVec4(0.6f, 0.7f, 0.9f, 1.0f),
        "developer console   ( ` toggle | Tab complete | Up/Down history | 'help' )");
    ImGui::Separator();

    const float footer = ImGui::GetFrameHeightWithSpacing() + 6.0f;
    if (ImGui::BeginChild("##scroll", ImVec2(0, -footer), true,
                          ImGuiWindowFlags_HorizontalScrollbar)) {
        for (const auto& ln : console.scrollback) {
            ImGui::PushStyleColor(ImGuiCol_Text, colour_for(ln.level));
            ImGui::TextUnformatted(ln.text.c_str());
            ImGui::PopStyleColor();
        }
        if (console.scrollToBottom || ImGui::GetScrollY() >= ImGui::GetScrollMaxY())
            ImGui::SetScrollHereY(1.0f);
        console.scrollToBottom = false;
    }
    ImGui::EndChild();

    ImGui::Separator();
    ImGui::TextUnformatted(">");
    ImGui::SameLine();
    const ImGuiInputTextFlags iflags =
        ImGuiInputTextFlags_EnterReturnsTrue |
        ImGuiInputTextFlags_CallbackCompletion |
        ImGuiInputTextFlags_CallbackHistory |
        ImGuiInputTextFlags_CallbackCharFilter;
    ImGui::PushItemWidth(-1);
    const bool submitted = ImGui::InputText("##cmd", console.input,
                                            sizeof(console.input), iflags,
                                            &text_edit_callback, &console);
    ImGui::PopItemWidth();
    ImGui::SetItemDefaultFocus();

    if (submitted) {
        console.execute(console.input);
        console.input[0] = '\0';
        console.historyPos = -1;
        console.reclaimFocus = true;
    }
    if (console.reclaimFocus) {
        ImGui::SetKeyboardFocusHere(-1);
        console.reclaimFocus = false;
    }

    ImGui::End();
}

// ── Arg helpers ───────────────────────────────────────────────────
bool arg_int(const std::vector<std::string>& args, std::size_t i, int& out) {
    if (i >= args.size()) return false;
    const std::string& s = args[i];
    char* endp = nullptr;
    const long v = std::strtol(s.c_str(), &endp, 10);
    if (endp == s.c_str() || *endp != '\0') return false;
    out = int(v);
    return true;
}

bool arg_float(const std::vector<std::string>& args, std::size_t i, float& out) {
    if (i >= args.size()) return false;
    const std::string& s = args[i];
    char* endp = nullptr;
    const float v = std::strtof(s.c_str(), &endp);
    if (endp == s.c_str() || *endp != '\0') return false;
    out = v;
    return true;
}

}  // namespace sm::dev
