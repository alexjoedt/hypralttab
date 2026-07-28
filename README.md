# hypr-alt-tab

Windows-style Alt-Tab for Hyprland: MRU window list, hold-modifier session, live previews.

**Hyprland:** built against 0.56.x headers (hash-checked at load). Config examples use **Lua** (`hyprland.lua` / `hl.*`).

## Build

Deps: `hyprland` headers (`pkg-config hyprland`), C++23 compiler, `pixman-1`, `libdrm`, `pangocairo`, `xkbcommon`, CMake ≥ 3.19.

```bash
cmake -DCMAKE_BUILD_TYPE=Release -B build-cmake
cmake --build build-cmake -j$(nproc)
# → build-cmake/libhypr-alt-tab.so
```

Unit test (layout only):

```bash
ctest --test-dir build-cmake --output-on-failure
```

## Load

**Never** mix hyprpm + manual load of the same plugin in one session.

### Manual

```bash
hyprctl plugin unload "$PWD/build-cmake/libhypr-alt-tab.so"   # ok if not loaded
hyprctl plugin load   "$PWD/build-cmake/libhypr-alt-tab.so"
hyprctl plugin list
```

### hyprpm

```bash
hyprpm add /path/to/hypr-alt-tab   # or your git remote
hyprpm enable hypr-alt-tab
```

Optional autoload (e.g. in `core/autostart.lua`):

```lua
hl.on("hyprland.start", function()
    hl.exec_cmd("hyprpm reload -n")
end)
```

## Binds

Lua (`hl.bind`), same style as `~/.config/hypr/core/keybinding.lua`:

```lua
-- Callback form: safe when plugin loads after first config parse (hyprpm reload on start).
hl.bind("ALT + Tab", function()
    if hl.plugin.alttab then
        hl.plugin.alttab.next()
    end
end, { description = "Alt+Tab: next (hypr-alt-tab)" })

hl.bind("ALT + SHIFT + Tab", function()
    if hl.plugin.alttab then
        hl.plugin.alttab.prev()
    end
end, { description = "Alt+Tab: prev (hypr-alt-tab)" })
```

If plugin already loaded when config runs, factory form also works (`hl.bind(..., hl.plugin.alttab.next())`).

Also see `example.lua`.

| Input | Behavior |
|-------|----------|
| Alt+Tab (first) | Open strip, select next MRU window |
| Alt+Tab / Alt+Shift+Tab | Cycle selection |
| Release Alt | Focus selected, close |
| Esc | Abort, keep prior focus |

## Config

In `hl.config({ plugin = { … } })` (e.g. `core/plugins.lua`):

```lua
hl.config({
    plugin = {
        alttab = {
            release_key = "Alt_L",   -- hold-mod key (Alt_L, Super_L, …)
            include_special = 0,     -- 1 = include special workspaces
            animations = 1,          -- reserved; layout anim not wired yet
        },
    },
})
```

Guard if plugin may be missing:

```lua
if hl.plugin.alttab ~= nil then
    hl.config({
        plugin = {
            alttab = {
                release_key = "Alt_L",
                include_special = 0,
            },
        },
    })
end
```

Layout/theme sizes are hardcoded for v1.

## Conflicts

Only one overview-style plugin should own heavy window projection at a time. Avoid running **hypr-alt-tab** together with **hymission**, **Hyprspace**, **hyprexpo**, or **hyprland-scroll-overview** while their overview/alt-tab UI is active — render hooks and force-visible state can fight.

If you still use a Lua-only MRU cycle (`core/alttab`), drop those binds so they do not fight this plugin.

## NVIDIA

Live preview uses compositor `renderWindow` projection (same family as scroll-overview). If previews glitch on NVIDIA, file an issue with GPU + driver + Hyprland version.

## Debug

```bash
hyprctl rollinglog | less
```

Load notification: `[hypr-alt-tab] loaded`.
