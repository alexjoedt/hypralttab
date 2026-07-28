-- hypr-alt-tab — classic hold-modifier Alt-Tab (Lua / hyprland.lua)
--
-- Drop into core/plugins.lua or core/keybinding.lua after the plugin loads.
-- Session: first next opens overlay (selects MRU index 1), further Tab cycles.
-- Release Alt → focus selected. Esc → abort, keep prior focus.

hl.config({
    plugin = {
        alttab = {
            release_key = "Alt_L",
            include_special = 0,
            animations = 1,
        },
    },
})

-- Callback form works even when plugin loads after first config parse (hyprpm reload).
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
