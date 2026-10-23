# Changelog

## 1.1.0

- Works with the separate menu window (POEFixer 366): the host hands the plugin the right drawing context for its settings tab and for its overlay.

## 1.0.9

- Rebuilt with the updated plugin SDK headers (POEFixer 365 adds optional calls for UI control names, stash objects and tabs, wider entity scans and the host automation flag). No change in what the plugin does.

## 1.0.8

- The "?" next to a section title now sits in its own gap instead of on the section's line.
- Diagnostics now looks the same as in the other POEFixer plugins.
- The bottom of the settings tab names the plugin's original author, as in the other plugins maintained by POEFixer.

## 1.0.7

- Now maintained by POEFixer: new settings tab in the POEFixer style (cards, clear labels, explanations on every option).
- Aiming now lands correctly when the game runs in a window or on a second monitor.
- Line of sight now uses the new area's terrain right after a map change instead of the previous one.
- The random offset no longer makes the cursor shake around the target.
- Key setup: click, then press a key or mouse button; Esc cancels, Clear removes it, and a Left mouse button binds the left mouse button. A key you just set never triggers until you release it.
- New in-game status chip with a choice of corner, size and opacity, and a clearer target marker and range ring.
- Less work per frame: only monsters are read while aiming, and buff checks are reused instead of repeated every frame.
- Choosing the target (reading monsters, line of sight, buffs) moved to a background thread: aiming no longer costs frame time. "Update every" starts at 16 ms (an old "every frame" setting becomes 16 ms).
- "Pause while an item panel is open" now really notices an opened inventory, stash or vendor window (it could miss one and keep moving the cursor over it).
- Settings are saved right after a change and can no longer be lost or half-written.
- A settings file that cannot be read (a typo after a hand edit, or another program holding the file) is no longer replaced by the defaults: a broken file is kept as `settings.json.corrupt`, and nothing is saved until you change a setting.

## 1.0.6

- LMB and RMB can be assigned to Aim (hold) and Toggle (always-on).
- Binding waits for existing keys and mouse buttons to be released before accepting the next press.
