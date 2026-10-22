#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>
#include <algorithm>
#include <atomic>
#include <charconv>
#include <cmath>
#include <fstream>
#include <vector>
#include <SDL3/SDL_gamepad.h>
#include "launcher.h"
#include "launcher_settings.h"
#include "launcher_ids.h"
#include "app/app.h"
#include "controller.h"
#include "mouse.h"
#include "core/log.h"
#include "game_setup.h"
#include "host.h"
#include "performance_overlay.h"
#include "runtime_paths.h"

namespace fs = std::filesystem;
namespace shaolin::launcher {
namespace {
Settings current;
std::atomic<bool> persist{true};
std::wstring Wide(const std::string& text) { return fs::path(std::u8string(text.begin(), text.end())).wstring(); }
std::string Narrow(const std::wstring& text) { auto bytes = fs::path(text).u8string(); return {bytes.begin(), bytes.end()}; }
toml::table Disk() {
    std::ifstream file(paths::Config(), std::ios::binary);
    return file ? toml::parse(file) : toml::table{};
}
void Apply(const Settings& s, bool save) {
    current = s; persist = save;
    auto& host = app::Get();
    host.display_mode = s.display_mode == "fullscreen" ? app::DisplayMode::Fullscreen :
                        s.display_mode == "windowed" ? app::DisplayMode::Windowed : app::DisplayMode::Borderless;
    host.width = s.width; host.height = s.height; host.render_percent = s.render_percent;
    host.fps_cap = s.fps_cap; host.vsync = s.vsync; host.aniso = s.aniso;
    auto table = Merge({}, s);
    controller::Load(&table);
    mouse::Configure(s.mouse);
    performance::SetVisible(s.show_fps);
}
enum Id { Mode = 2301, Width, Height, Scale, Fps, Aniso, Vsync, Volume = 2401, Overlay,
          Deadzone = 2501, Rumble, Binding = 2600, Source = 2701, Browse, Saves, Logs, Data,
          Original = 2801, Music, Effects, Speech, Brightness, Rounds, MatchTime, Camera,
          Sensitivity = 2901, InvertMouse, MouseBinding = 2910 };
struct Dialog {
    Settings initial;
    fs::path source;
    bool source_changed = false;
    std::vector<HWND> pages[6];
};
HWND Add(HWND parent, Dialog& d, int page, const wchar_t* kind, const wchar_t* text, int id,
         int x, int y, int width, int height, DWORD style = 0) {
    RECT rect{x, y, x + width, y + height}; MapDialogRect(parent, &rect);
    auto child = CreateWindowExW(kind == std::wstring(L"EDIT") ? WS_EX_CLIENTEDGE : 0, kind, text,
        WS_CHILD | WS_VISIBLE | style, rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top,
        parent, reinterpret_cast<HMENU>(INT_PTR(id)), GetModuleHandleW(nullptr), nullptr);
    SendMessageW(child, WM_SETFONT, SendMessageW(parent, WM_GETFONT, 0, 0), TRUE);
    d.pages[page].push_back(child);
    return child;
}
void Label(HWND w, Dialog& d, int p, const wchar_t* text, int x, int y, int width = 155) {
    Add(w, d, p, L"STATIC", text, -1, x, y + 2, width, 14);
}
void Combo(HWND w, Dialog& d, int p, int id, int x, int y, int width, const std::vector<std::wstring>& choices, bool editable = false) {
    auto child = Add(w, d, p, L"COMBOBOX", L"", id, x, y, width, 180,
        WS_TABSTOP | WS_VSCROLL | (editable ? CBS_DROPDOWN : CBS_DROPDOWNLIST));
    for (const auto& choice : choices) SendMessageW(child, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(choice.c_str()));
}
void Field(HWND w, Dialog& d, int p, const wchar_t* label, int id, int y) {
    Label(w, d, p, label, 20, y);
    Add(w, d, p, L"EDIT", L"", id, 185, y, 220, 15, WS_TABSTOP | ES_AUTOHSCROLL);
}
std::wstring Text(HWND window, int id) {
    auto child = GetDlgItem(window, id);
    std::wstring value(GetWindowTextLengthW(child) + 1, L'\0');
    value.resize(GetWindowTextW(child, value.data(), int(value.size())));
    return value;
}
void Set(HWND w, int id, const std::wstring& value) { SetDlgItemTextW(w, id, value.c_str()); }
void Number(HWND w, int id, int value) { Set(w, id, std::to_wstring(value)); }
int Number(HWND w, int id) {
    const auto text = Narrow(Text(w, id));
    int value = 0;
    const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
    return result.ec == std::errc{} && result.ptr == text.data() + text.size() ? value : -1;
}
void Select(HWND w, int id, int index) { SendDlgItemMessageW(w, id, CB_SETCURSEL, index, 0); }
int Selected(HWND w, int id) { return int(SendDlgItemMessageW(w, id, CB_GETCURSEL, 0, 0)); }
void Check(HWND w, int id, bool value) { CheckDlgButton(w, id, value ? BST_CHECKED : BST_UNCHECKED); }
bool Checked(HWND w, int id) { return IsDlgButtonChecked(w, id) == BST_CHECKED; }
void Fill(HWND w, const Settings& s) {
    auto lang = std::find(languages.begin(), languages.end(), s.language);
    Select(w, IDC_LANGUAGE, lang == languages.end() ? 0 : int(lang - languages.begin()));
    Select(w, Mode, s.display_mode == "fullscreen" ? 0 : s.display_mode == "borderless" ? 1 : 2);
    Number(w, Width, s.width); Number(w, Height, s.height); Number(w, Scale, s.render_percent);
    Number(w, Fps, s.fps_cap); Select(w, Aniso, s.aniso == 16 ? 4 : s.aniso == 8 ? 3 : s.aniso == 4 ? 2 : s.aniso == 2 ? 1 : 0);
    Check(w, Vsync, s.vsync); Number(w, Volume, s.master_volume); Check(w, Overlay, s.show_fps);
    Number(w, Deadzone, int(s.deadzone * 100 + .5)); Check(w, Rumble, s.rumble);
    for (int i = 0; i < 10; ++i) Set(w, Binding + i, Wide(s.bindings[i]));
    Number(w, Sensitivity, s.mouse.sensitivity); Check(w, InvertMouse, s.mouse.invert_y);
    for (int i = 0; i < 5; ++i) {
        const auto action = std::find(mouse::actions.begin(), mouse::actions.end(), s.mouse.bindings[i]);
        Select(w, MouseBinding + i, action == mouse::actions.end() ? 0 : int(action - mouse::actions.begin()));
    }
    Check(w, Original, s.original_override);
    Number(w, Music, s.music_volume); Number(w, Effects, s.effects_volume); Number(w, Speech, s.speech_volume);
    Number(w, Brightness, s.brightness); Select(w, Rounds, s.versus_rounds == 1 ? 0 : 1);
    Select(w, MatchTime, s.versus_time == 120 ? 0 : s.versus_time == 180 ? 1 : 2);
    Select(w, Camera, s.camera_reverse ? 1 : 0);
    for (int id = Music; id <= Camera; ++id) EnableWindow(GetDlgItem(w, id), s.original_override);
}
Settings Form(HWND w) {
    Settings s;
    const int language = Selected(w, IDC_LANGUAGE);
    s.language = language >= 0 && language < 5 ? languages[language] : "";
    s.display_mode = Selected(w, Mode) == 0 ? "fullscreen" : Selected(w, Mode) == 1 ? "borderless" : "windowed";
    s.width = Number(w, Width); s.height = Number(w, Height); s.render_percent = Number(w, Scale); s.fps_cap = Number(w, Fps);
    const int levels[] = {0, 2, 4, 8, 16}; const int level = Selected(w, Aniso);
    s.aniso = level >= 0 && level < 5 ? levels[level] : -1;
    s.vsync = Checked(w, Vsync); s.master_volume = Number(w, Volume); s.show_fps = Checked(w, Overlay);
    const auto* state = reinterpret_cast<Dialog*>(GetWindowLongPtrW(w, DWLP_USER));
    const int zone = Number(w, Deadzone);
    s.deadzone = state && std::isfinite(state->initial.deadzone) && zone == int(state->initial.deadzone * 100 + .5)
        ? state->initial.deadzone : zone / 100.;
    s.rumble = Checked(w, Rumble);
    for (int i = 0; i < 10; ++i) s.bindings[i] = Narrow(Text(w, Binding + i));
    s.mouse.sensitivity = Number(w, Sensitivity); s.mouse.invert_y = Checked(w, InvertMouse);
    for (int i = 0; i < 5; ++i) {
        const int selected = Selected(w, MouseBinding + i);
        s.mouse.bindings[i] = selected >= 0 && selected < int(mouse::actions.size()) ? mouse::actions[selected] : "";
    }
    s.original_override = Checked(w, Original);
    s.music_volume = Number(w, Music); s.effects_volume = Number(w, Effects); s.speech_volume = Number(w, Speech);
    s.brightness = Number(w, Brightness); s.versus_rounds = Selected(w, Rounds) == 0 ? 1 : 3;
    s.versus_time = Selected(w, MatchTime) == 0 ? 120 : Selected(w, MatchTime) == 1 ? 180 : 0;
    s.camera_reverse = Selected(w, Camera) == 1;
    return s;
}
bool Valid(HWND w, const Settings& s) {
    std::string error;
    if (Validate(s, error)) {
        const std::vector<std::string> aliases{"south", "cross", "east", "circle", "west", "square", "north", "triangle"};
        for (const auto& binding : s.bindings)
            if (SDL_GetGamepadButtonFromString(binding.c_str()) == SDL_GAMEPAD_BUTTON_INVALID &&
                std::find(aliases.begin(), aliases.end(), binding) == aliases.end()) {
                error = "Unknown controller button: " + binding; break;
            }
        if (error.empty()) return true;
    }
    MessageBoxW(w, Wide(error).c_str(), L"Check your settings", MB_OK | MB_ICONWARNING);
    return false;
}
bool Commit(HWND w, Dialog& d, const Settings& s) {
    try {
        std::string error;
        if (d.source_changed) {
            setup::Source source; std::vector<uint8_t> executable;
            if (!setup::PrepareGame(d.source, source, executable, error, true) || !setup::RememberDisc(source.path, error))
                throw std::runtime_error(error);
            d.source = source.path; d.source_changed = false;
        }
        if (!Write(paths::Config(), Merge(Disk(), s), error)) throw std::runtime_error(error);
        return true;
    } catch (const std::exception& e) {
        MessageBoxW(w, Wide(e.what()).c_str(), L"Cannot save settings", MB_OK | MB_ICONERROR);
        return false;
    }
}
void OpenDirectory(const fs::path& path) { ShellExecuteW(nullptr, L"open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL); }
INT_PTR CALLBACK Procedure(HWND w, UINT message, WPARAM wp, LPARAM lp) {
    auto* d = reinterpret_cast<Dialog*>(GetWindowLongPtrW(w, DWLP_USER));
    if (message == WM_INITDIALOG) {
        d = reinterpret_cast<Dialog*>(lp); SetWindowLongPtrW(w, DWLP_USER, lp);
        SendMessageW(w, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(LoadIconW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(1))));
        if (options.configure_only) { Set(w, IDC_SAVE_PLAY, L"Save and close"); Set(w, IDC_PLAY, L"Close"); }
        const wchar_t* names[] = {L"General", L"Video", L"Controls", L"Mouse", L"Original game", L"Files and support"};
        for (int i = 0; i < 6; ++i) { TCITEMW item{}; item.mask = TCIF_TEXT; item.pszText = const_cast<wchar_t*>(names[i]); TabCtrl_InsertItem(GetDlgItem(w, IDC_PAGES), i, &item); }
        Label(w, *d, 0, L"Game language", 20, 38);
        Combo(w, *d, 0, IDC_LANGUAGE, 185, 36, 220, {L"English", L"French", L"German", L"Spanish", L"Italian"});
        Field(w, *d, 0, L"Master volume (%)", Volume, 66);
        Add(w, *d, 0, L"BUTTON", L"Show FPS and frame time (F9)", Overlay, 20, 94, 310, 16, WS_TABSTOP | BS_AUTOCHECKBOX);
        Label(w, *d, 0, L"Original game options", 20, 127, 380);
        Add(w, *d, 0, L"STATIC", L"The Original game tab can set music, effects, speech, brightness, versus rules, and camera control. You can also change them in the game's Options menu.", -1, 20, 147, 384, 44);
        Add(w, *d, 0, L"STATIC", L"Languages change the game's original text. Voice and movie languages depend on the files included with your copy.", -1, 20, 202, 384, 38);
        Label(w, *d, 1, L"Display mode", 20, 38);
        Combo(w, *d, 1, Mode, 185, 36, 220, {L"Fullscreen", L"Borderless", L"Windowed"});
        Field(w, *d, 1, L"Width (0 = desktop)", Width, 66); Field(w, *d, 1, L"Height (0 = desktop)", Height, 91);
        Field(w, *d, 1, L"Render scale (%)", Scale, 116); Field(w, *d, 1, L"FPS limit (0 = unlimited)", Fps, 141);
        Label(w, *d, 1, L"Anisotropic filtering", 20, 169);
        Combo(w, *d, 1, Aniso, 185, 167, 220, {L"Game default", L"2x", L"4x", L"8x", L"16x"});
        Add(w, *d, 1, L"BUTTON", L"V-Sync", Vsync, 20, 195, 200, 16, WS_TABSTOP | BS_AUTOCHECKBOX);
        Add(w, *d, 1, L"STATIC", L"The current target is 60 FPS. Higher limits can change game timing.", -1, 20, 225, 384, 27);
        Label(w, *d, 2, L"Dead zone (%)", 20, 37, 100);
        Add(w, *d, 2, L"EDIT", L"", Deadzone, 120, 35, 55, 15, WS_TABSTOP | ES_AUTOHSCROLL);
        Add(w, *d, 2, L"BUTTON", L"Vibration", Rumble, 275, 37, 120, 16, WS_TABSTOP | BS_AUTOCHECKBOX);
        const wchar_t* labels[] = {L"A", L"B", L"X", L"Y", L"Black / R1", L"White / L1", L"Start", L"Back", L"Left stick click", L"Right stick click"};
        const std::vector<std::wstring> buttons{L"a", L"b", L"x", L"y", L"leftshoulder", L"rightshoulder", L"start", L"back", L"leftstick", L"rightstick", L"dpup", L"dpdown", L"dpleft", L"dpright", L"guide"};
        for (int i = 0; i < 10; ++i) {
            const int x = i < 5 ? 20 : 225, y = 94 + i % 5 * 26;
            Label(w, *d, 2, labels[i], x, y, 80); Combo(w, *d, 2, Binding + i, x + 80, y, 100, buttons, true);
        }
        Add(w, *d, 2, L"STATIC", L"Keyboard: WASD, arrows, Space, Esc/Ctrl, J, K, Q, C, Shift, E, Enter, Backspace. Controller fields use SDL button names.", -1, 20, 233, 385, 31);
        Field(w, *d, 3, L"Mouse sensitivity (%)", Sensitivity, 36);
        Add(w, *d, 3, L"BUTTON", L"Invert vertical camera movement", InvertMouse, 20, 64, 385, 16, WS_TABSTOP | BS_AUTOCHECKBOX);
        const wchar_t* mouse_labels[] = {L"Left button", L"Right button", L"Middle button", L"Side button 1", L"Side button 2"};
        const std::vector<std::wstring> mouse_actions{L"None", L"A", L"B", L"X", L"Y", L"Black", L"White", L"Left trigger", L"Right trigger", L"Start", L"Back", L"Left stick click", L"Right stick click"};
        for (int i = 0; i < 5; ++i) {
            Label(w, *d, 3, mouse_labels[i], 20, 96 + i * 26);
            Combo(w, *d, 3, MouseBinding + i, 185, 94 + i * 26, 220, mouse_actions);
        }
        Add(w, *d, 3, L"STATIC", L"Move the mouse to control the original camera. F10 releases or captures the pointer. Click inside the game to capture it again.", -1, 20, 233, 385, 31);
        Add(w, *d, 4, L"BUTTON", L"Apply these original game options at startup", Original, 20, 35, 385, 16, WS_TABSTOP | BS_AUTOCHECKBOX);
        Field(w, *d, 4, L"Music volume (%)", Music, 62);
        Field(w, *d, 4, L"Sound effects (%)", Effects, 85);
        Field(w, *d, 4, L"Speech volume (%)", Speech, 108);
        Field(w, *d, 4, L"Brightness (%)", Brightness, 131);
        Label(w, *d, 4, L"Versus rounds", 20, 156);
        Combo(w, *d, 4, Rounds, 185, 154, 220, {L"1", L"3"});
        Label(w, *d, 4, L"Versus match time", 20, 179);
        Combo(w, *d, 4, MatchTime, 185, 177, 220, {L"120 seconds", L"180 seconds", L"Unlimited"});
        Label(w, *d, 4, L"Camera control", 20, 202);
        Combo(w, *d, 4, Camera, 185, 200, 220, {L"Normal", L"Reverse"});
        Add(w, *d, 4, L"STATIC", L"When disabled, the game keeps its own saved options. Brightness zero uses the original neutral adjustment.", -1, 20, 233, 385, 31);
        Label(w, *d, 5, L"Game source", 20, 38);
        Add(w, *d, 5, L"EDIT", L"", Source, 20, 58, 315, 31, ES_READONLY | ES_MULTILINE | ES_AUTOHSCROLL);
        Add(w, *d, 5, L"BUTTON", L"Choose...", Browse, 342, 58, 65, 18, WS_TABSTOP);
        Set(w, Source, d->source.empty() ? L"No game selected" : d->source.wstring());
        Add(w, *d, 5, L"BUTTON", L"Open saves", Saves, 20, 107, 112, 20, WS_TABSTOP);
        Add(w, *d, 5, L"BUTTON", L"Open logs", Logs, 153, 107, 112, 20, WS_TABSTOP);
        Add(w, *d, 5, L"BUTTON", L"Open configuration", Data, 286, 107, 121, 20, WS_TABSTOP);
        Add(w, *d, 5, L"STATIC", L"Saves use Windows Saved Games. Settings, logs, imported ZIP files, and cache use Local AppData. Error messages open the application data folder after you close them.", -1, 20, 145, 385, 48);
        Fill(w, d->initial);
        for (int page = 1; page < 6; ++page) for (auto child : d->pages[page]) ShowWindow(child, SW_HIDE);
        return TRUE;
    }
    if (!d) return FALSE;
    if (message == WM_CTLCOLORSTATIC) {
        const auto child = reinterpret_cast<HWND>(lp);
        wchar_t kind[32]{}; GetClassNameW(child, kind, 32);
        if (std::wstring(kind) == L"Static" || std::wstring(kind) == L"Button") {
            const bool page = std::any_of(std::begin(d->pages), std::end(d->pages), [&](const auto& controls) {
                return std::find(controls.begin(), controls.end(), child) != controls.end();
            });
            if (page) {
                SetBkColor(reinterpret_cast<HDC>(wp), GetSysColor(COLOR_WINDOW));
                return reinterpret_cast<INT_PTR>(GetSysColorBrush(COLOR_WINDOW));
            }
        }
    }
    if (message == WM_NOTIFY && reinterpret_cast<NMHDR*>(lp)->idFrom == IDC_PAGES && reinterpret_cast<NMHDR*>(lp)->code == TCN_SELCHANGE) {
        const auto selected = TabCtrl_GetCurSel(GetDlgItem(w, IDC_PAGES));
        for (int page = 0; page < 6; ++page) for (auto child : d->pages[page]) ShowWindow(child, page == selected ? SW_SHOW : SW_HIDE);
        return TRUE;
    }
    if (message == WM_COMMAND || message == WM_CLOSE) {
        const auto id = message == WM_CLOSE ? IDCANCEL : LOWORD(wp);
        if (id == IDC_DEFAULTS) { Fill(w, Settings{}); return TRUE; }
        if (id == Original) {
            for (int field = Music; field <= Camera; ++field) EnableWindow(GetDlgItem(w, field), Checked(w, Original));
            return TRUE;
        }
        if (id == Browse) {
            fs::path selected;
            if (setup::ChooseDisc(selected)) { d->source = selected; d->source_changed = true; Set(w, Source, selected.wstring()); }
            return TRUE;
        }
        if (id == Logs) { OpenDirectory(paths::Logs()); return TRUE; }
        if (id == Data) { OpenDirectory(paths::Root()); return TRUE; }
        if (id == Saves) {
            std::string error;
            const auto root = !options.save_dir.empty() ? fs::absolute(options.save_dir) : options.portable_saves ?
                app::ExeDir() / "hdd" : storage::SavedGamesRoot(error) / "hdd";
            std::error_code ec;
            if (fs::is_directory(root, ec)) OpenDirectory(root);
            else MessageBoxW(w, (L"Save folder:\n" + root.wstring() + L"\n\nThe game creates this folder when it starts.").c_str(), L"Saves", MB_OK);
            return TRUE;
        }
        if (id != IDC_SAVE_PLAY && id != IDC_PLAY && id != IDCANCEL) return FALSE;
        const auto draft = Form(w);
        const bool closing = id == IDCANCEL || options.configure_only;
        const bool dirty = draft != d->initial || d->source_changed;
        int choice = id == IDC_SAVE_PLAY ? IDYES : IDNO;
        if (dirty && id != IDC_SAVE_PLAY) choice = MessageBoxW(w,
            closing ? L"Save your changes before closing?" : L"Save your changes before starting the game?",
            L"Unsaved settings", MB_YESNOCANCEL | MB_ICONQUESTION);
        if (choice == IDCANCEL) return TRUE;
        if (!closing || choice == IDYES) if (!Valid(w, draft)) return TRUE;
        if (choice == IDYES && !Commit(w, *d, draft)) return TRUE;
        if (!closing) {
            Apply(draft, choice == IDYES || !dirty);
            if (!d->source.empty()) { options.disc = d->source; options.game.clear(); options.register_disc = d->source_changed && ShouldPersist(); }
        }
        EndDialog(w, closing ? 0 : 1);
        return TRUE;
    }
    return FALSE;
}
}
bool Show() {
    Dialog dialog;
    dialog.source = !options.disc.empty() ? options.disc : !options.game.empty() ? options.game : setup::RememberedDisc();
    std::error_code unavailable;
    if (!options.configure_only && options.disc.empty() && options.game.empty() &&
        (dialog.source.empty() || !fs::exists(dialog.source, unavailable))) {
        fs::path selected;
        if (!setup::ChooseDisc(selected)) return false;
        setup::Source source; std::vector<uint8_t> executable; std::string error;
        if (!setup::PrepareGame(selected, source, executable, error, true) || !setup::RememberDisc(source.path, error)) {
            paths::ReportError(Wide(error)); return false;
        }
        dialog.source = source.path;
        options.disc = source.path;
    }
    try { dialog.initial = Read(Disk()); }
    catch (const std::exception& e) { paths::ReportError(L"Cannot read settings.toml.\n" + Wide(e.what())); return false; }
    INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_TAB_CLASSES | ICC_STANDARD_CLASSES}; InitCommonControlsEx(&controls);
    const auto result = DialogBoxParamW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDD_LAUNCHER), nullptr, Procedure, reinterpret_cast<LPARAM>(&dialog));
    if (result == -1) paths::ReportError(L"Cannot open the settings window.");
    return result == 1;
}
bool ShouldPersist() { return persist.load(); }
void Load(const void* raw) {
    const auto& table = *static_cast<const toml::table*>(raw);
    current = Read(table); controller::Load(raw); mouse::Configure(current.mouse); performance::SetVisible(current.show_fps);
}
void Save(void* raw) {
    auto& table = *static_cast<toml::table*>(raw);
    table.insert_or_assign("language", current.language); table.insert_or_assign("master_volume", current.master_volume);
    table.insert_or_assign("show_fps", performance::Visible()); controller::Save(raw);
    const auto extras = Merge({}, current);
    table.insert_or_assign("game", *extras["game"].as_table());
    table.insert_or_assign("mouse", *extras["mouse"].as_table());
}
bool Persist(const toml::table& table, std::string& error) {
    if (!ShouldPersist()) return true;
    try { return Write(paths::Config(), Merge(Disk(), Read(table)), error); }
    catch (const std::exception& e) { error = e.what(); return false; }
}
unsigned Language() { return GameLanguage(current.language); }
float AudioGain() { return current.master_volume / 100.f; }
const Settings& Current() { return current; }
}
