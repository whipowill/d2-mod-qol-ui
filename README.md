# Will Scarlet's QOL UI for Diablo II

A game mod for Diablo II introducing various UI enhancements such as loot filters, map indicators, and more.

**Note this was designed for D2LOD v1.13 and will not work on version 1.14.**

## Features

- **Loot Filter** - filter out useless items with filter rules
- **Buff Icons** - see what buffs and debuffs are effecting you
- **Stat Ranges** - see the range of possible stats on items
- **Directions** - lines show you were to go on the map
- **HUD Clock** - game always shows the current time

All of these features are configured in the ``qol_ui.ini`` and ``qol_ui_filter.cfg`` files.

## Shortcuts

- ``Shift + left click`` - identify an unknown item in your inventory
- ``Shift + right click`` - move an item to the open stash or cube, or back
- ``Ctrl + right click`` - drop an item to the ground
- ``B`` - pull up player or mercenary stats screen
- ``+`` - increase item filter level
- ``-`` - decrease item filter level

## Commands

- ``.reload`` - reload all config files, including loot filter

## Install

Copy and paste the files from ``build/`` into your game folder, and add the ``qol_ui.dll`` to your game DLL loader.

## Filter

You'll have to create your own ``qol_ui_filter.cfg`` in the game folder to input your filter rules.

**Right now this is using the older Slash Diablo ruleset, but I'm hoping to update it soon to use the PD2 ruleset.**

## License

AGPL-3.0, see `LICENSE`. Includes code ported from [slashdiablo-maphack (BH)](https://github.com/BeLikeLeBron/slashdiablo-maphack), also AGPL-3.0.

## External Links

- [Will Scarlet's QOL](https://github.com/whipowill/d2-mod-qol) - a DLL mod that provides various quality-of-life features and bug fixes
- [Loot Filter Rules](https://github.com/planqi/slashdiablo-maphack/wiki/Stats) - a list of the official loot filter coding rules