# Auto My Aim

Auto My Aim points your cursor at the best monster near your character so your
skills cast at it. Hold the aim key (or press the toggle key once for always-on
aiming) and the plugin keeps the cursor on the highest-priority monster in range
that you can actually see. It only moves the cursor: you still press your skills.

## How to use it

1. Install **Auto My Aim** from the Plugin Manager (POEFixer's Plugins tab),
   which also keeps it up to date, and tick *Enabled* on its tab.
2. Under **Keys**, click the *Aim (hold)* button and press the key or mouse
   button you want to hold while aiming (Mouse 4 by default). Optionally set a
   *Toggle always-on* key (F6 by default).
3. In game, hold the aim key: the cursor follows the best target. Release it to
   get your cursor back. Press the toggle key to aim all the time; press it
   again to stop.

To aim while you use a skill on a mouse button, give *Aim (hold)* that same
button: holding it casts and aims, releasing it stops. Keep always-on aiming
off with that setup.

## Settings

**Keys**
- *Aim (hold)* - aim while this key or mouse button is held. A click cannot
  bind the left mouse button, so the *Left mouse* button beside it does that.
- *Toggle always-on* - turns permanent aiming on and off.
- Click a key button, then press the key or mouse button to use; Esc cancels,
  *Clear* removes the key.

**Targeting**
- *Aim range* - how far (in map cells) a monster may be. Turn on the aim range
  ring under Display to see it while you adjust it.
- *Update every* - how often the target is re-chosen and the cursor moved
  (16 ms by default, about as often as the game updates monster positions).
- *Only monsters in line of sight* - skips monsters behind walls; *Wall
  threshold* sets how low terrain must be to block the view.
- *Skip monsters with these buffs* - ignores monsters carrying a listed buff
  (comma separated), for monsters your skills cannot hit, such as Delirium
  phantoms or monsters frozen in time.

**Which monster first**
- *Closeness* - how much being close counts.
- *Prefer rarer monsters* - bonus for magic, rare and unique monsters.
- *Weigh by remaining life* - bonus by life left; *Prefer healthier monsters*
  reverses it (healthiest first instead of finishing wounded ones).

**Cursor**
- *Keep the cursor near your character* - the cursor never goes further from
  you than *Max distance*.
- *Add a small random offset* - places the cursor slightly off the monster's
  centre; the offset changes with the target and a few times a second.

**Safety**
- *Pause while an item panel is open* - inventory, stash or vendor.
- *Pause while Pick My Loot picks up items* - the two plugins never fight over
  the cursor (the badge shows whether the shared link is ready).
- Aiming always pauses while the POEFixer menu is open, during loading screens,
  while the game window is not in front and while you are dead.

**Display**
- *Show status and target marker* - a small status chip (corner, size and
  opacity adjustable) and a crosshair on the aimed monster.
- *Show aim range ring* - the aim range drawn on the ground.

**Diagnostics** (collapsed) - *Show the target's flags and buffs in game*
prints the aimed monster's rarity, distance, targetable flags and buff names.
If the cursor sticks to a monster that cannot be hit, aim at it, read the buff
name there and add it to the buff list.

## Known limits

- Monster positions come from game memory, so aiming follows the monster's
  position, not where a projectile will meet it.
- Line of sight uses the map's walkable terrain: low obstacles that the game
  lets projectiles pass may still block, and the reverse. Adjust *Wall
  threshold* or turn line of sight off.

## Credits

Originally created by Omer Faruk ARPA (https://github.com/omrfarukarpa/poefixer-automyaim).
Maintained by POEFixer since October 2026.
Licensed under GPL-3.0, see LICENSE.
