<p align="center">
  <img src="https://i.imgur.com/pibtyie.png" height="150px">
</p>

# <p align="center">Better Notepad 2.0</p>

<p align="center"> A fast, modern text editor written in native C++ with Qt 6. No web view, no JavaScript. </p>

<p align="center">  <b>Supported Formats:</b> txt, md, log, ini, bat, cfg, vbs, reg, sh, ps1, js, html, htm, css, xml, json </p>

This is the native rewrite of the Tauri version. It looks and behaves the same, starts faster, uses far less memory, and adds crash-safe backups, regex find, and more settings.

## Features

- 🗂️ Multi-Tab Interface - Tabs live in the title bar, with a `+` button, middle-click to close and drag to reorder
- 📄 Smart Encoding Detection - Reads UTF-8, UTF-8 BOM, UTF-16 LE/BE BOM and ANSI (files are saved as UTF-8)
- 🛟 Crash-Safe Backup - Unsaved tabs are backed up automatically and come back after a crash or a forced close
- 🔁 Remember Last Session - Optional: reopen the tabs you had open, with cursor position and zoom (off by default)
- ♻ Reopen Closed Tabs - Ctrl+Shift+T brings back recently closed tabs, including their unsaved text
- 🔍 Find & Replace - Floating panel with Match Case, Whole Word and Regex toggles, a "3 of 28" counter and `$1`-`$9` capture groups in Replace
- 📌 Visual Search Map - Every match is highlighted in the text and marked in the scrollbar, like VS Code's overview ruler
- ↪ Go to Line - Ctrl+G, jumps as you type, understands `line:column`
- ✂️ Smart Line Operations - Ctrl+X cuts the whole line when nothing is selected
- ⇥ Tab Key - Insert a real tab or 2/4/6/8 spaces
- ⛶ Zoom Controls - Per-file zoom (50% - 300%) with a configurable default (100/120/150%) and step (5% or 10%)
- 🎨 8 Built-in Skins - Dark, Light, Nord, Dark Grey, Light Grey, Dark Blue, Dark Pink, Dark Green
- 🧮 Custom Skins - Drop a JSON file in the skins folder and pick it in Settings (a Dracula skin is included)
- ¹²³ Line Numbers - Optional gutter with current line highlighting
- 📊 Customizable Status Bar - Choose which items to show: path, line/col, characters, words, zoom, lines
- 🖱️ Drag and Drop - Drop files on the window to open them
- 🪟 Single Instance - Opening a file while the app is running opens it as a new tab
- ⚙️ Live Settings - Every change in Settings applies immediately and is saved automatically

### Keyboard Shortcuts

#### File Operations
- `Ctrl+N` - New file
- `Ctrl+O` - Open file(s)
- `Ctrl+S` - Save current file
- `Ctrl+Shift+S` - Save As
- `Ctrl+Shift+Alt+S` - Save all tabs
- `Ctrl+W` - Close current tab
- `Ctrl+Shift+T` - Reopen closed tab
- `Ctrl+P` - Print

#### Navigation
- `Ctrl+Tab` / `Ctrl+Shift+Tab` - Next / previous tab
- `Ctrl+G` - Go to line
- `Ctrl+F` - Find
- `Ctrl+H` - Replace
- `F3` / `Shift+F3` - Next / previous match

#### Editing
- `Tab` - Insert a tab or spaces (Settings → Tab Key)
- `Ctrl+X` - Cut line (when nothing is selected)
- `Ctrl+Z` / `Ctrl+Y` - Undo / Redo

#### View
- `Ctrl+0` - Reset zoom
- `Ctrl++` (or `Ctrl+=`) / `Ctrl+-` - Zoom in / out
- `Ctrl+Wheel` - Zoom with mouse

## Settings

Open it from the menu button (top left). Changes apply and save instantly.

| Section | Options |
| --- | --- |
| Appearance | Skin, skins folder, font family, size and weight |
| Editor | Word wrap, line numbers, select results on find, Tab key behavior |
| Zoom | Default zoom (100/120/150%), zoom step (5/10%) |
| Status bar | Show or hide the bar and each item in it |
| Session | Remember last session (backup and restore of open tabs) |

Settings and data live in `%LOCALAPPDATA%\com.pyrus.better-notepad\`:

- `settings.ini` - your settings and per-file zoom
- `skins\` - custom skins
- `session\` - tab backups (only used when Remember Last Session is on)

### Custom skins

A skin is a JSON file in the skins folder (Settings → Skins Folder). The file name is the name shown in the dropdown. It needs a `vars` object with the same color variables the built-in skins use; any variable you leave out falls back to the Dark skin. `skins/Dracula.json` in this repo is a complete example.

```json
{
  "name": "My Skin",
  "vars": {
    "--text-color": "rgb(240, 240, 240)",
    "--editor-bg": "rgb(20, 24, 32)",
    "--tab-active": "rgb(120, 180, 255)"
  }
}
```

## Download

Available for Windows

[Download Latest Release](https://github.com/hudsonpear/better-notepad/releases)

The installer adds Better Notepad to the **Open with** list for the supported file types. It never becomes the default app for any of them, so double-clicking a `.bat` still runs it.

## How to Build

<b>Requirements:</b> Windows, Qt 6.5+ (Widgets, PrintSupport, Concurrent, Network, Svg), CMake 3.21+ and the matching Visual Studio C++ tools

Configure and build:

```powershell
cmake -S . -B build-native -G "Visual Studio 18 2026" -A x64 `
  -DCMAKE_PREFIX_PATH="C:/Qt/6.10.1/msvc2022_64"
cmake --build build-native --config Release
```

Stage the Qt runtime beside the executable:

```powershell
New-Item -ItemType Directory -Force dist-native
Copy-Item build-native/Release/BetterNotepad.exe dist-native/ -Force
C:/Qt/6.10.1/msvc2022_64/bin/windeployqt.exe --release --no-translations dist-native/BetterNotepad.exe
```

The packaged executable is `dist-native/BetterNotepad.exe`.

Build the installer with [Inno Setup 6](https://jrsoftware.org/isinfo.php):

```powershell
& "C:\Users\<you>\AppData\Local\Programs\Inno Setup 6\ISCC.exe" installer.iss
```

The setup file is written to `installer-output/`.

## Project Layout

- `src-cpp/native_main.cpp` - the whole application
- `src-cpp/skins.inc`, `src-cpp/menu_icons.inc` - built-in skin colors and menu icons
- `skins/` - skins bundled with the installer
- `installer.iss` - Inno Setup script
- `Versions.txt` - changelog

## Credits

Made by Hudson Pear (pyrus).
