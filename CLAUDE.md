# qol_ui: working notes

Interface features for single-player Diablo II LoD **1.13d** as a 32-bit Windows DLL (`qol_ui.dll`), cross-built from Linux. It is loaded the
same way as other game DLLs (PlugY `DllToLoad`, d2gl `load_dlls_late`). Everything is read from `qol_ui.ini` and the loot rules file
`qol_ui_filter.cfg`, both in the game folder (no BH config file is ever read). Nothing can be changed in game except through the keys in `[keys]`, and there are no monster or missile markers. The one deliberate exception, requested by the user: the automap destination lines cover the whole level, explored or not (as BH's AutoTele does).

## Build and test
- One-time: `pip install --target .tools ziglang` (`.tools/` is gitignored). `./build.sh` writes `build/qol_ui.dll` and `build/qol_ui.ini` (intermediates in `.out/`).
- `./test.sh` builds the engine pieces natively with gcc and runs the unit tests. It must print no FAIL. The game-memory side (`lootfilter.h`) cannot be tested offline,
  so keep logic in `filter.c` / `itemdb.c` / `sig.h` where it can be.

## Conventions (keep)
- Target stock 1.13d only. Every patch checks the module's PE timestamp (D2Client 1318970890) and finds its site by byte signature (`sig.h`); a site must match exactly
  once, else log and do nothing. Never edit game files; patch in memory only.
- Settings are optional: no numeric value may be required, every key has a default, and a missing file must not crash. Features are switchable in `qol_ui.ini`.
- Log through `logf_` to `qol_ui.log`. Logging is **off by default** (`logging=1` enables it). Never flush on the game thread; `log_flusher` does it. A vectored exception handler writes `EXCEPTION` lines (module+offset, registers, callers) to the log if logging is on: the first thing to read after a crash.
- Code stubs (`stub_alloc`) come from one pool allocated before any feature thread starts and are handed out with an atomic counter. Feature threads patch the game at the same time; a non-thread-safe allocator handed two threads the same stub and caused map-open crashes.
- Never run game code that is not on the same thread as the game (chat printing, packet sends, room-data adds): the key thread queues a message (`g_msg`) and the game loop prints it.
- Hooks run on the game thread: no allocation-heavy or blocking work in them, and read game memory only after `readable()`.
- Committed files carry no personal names or machine paths. The user commits; do not run git commands that create commits.
- Licence is **AGPL-3.0** (switched from MIT once the BH ports landed; README says so). Keep a credit to BH in README for anything newly ported.
- Do not name the user's machine paths or game folder in committed files; use "the game folder".

## Where the details are
`NOTES.md` (not auto-loaded) holds the source map, rule syntax, 1.13d game facts (offsets, structs, hook sites), how each feature was ported from BH, performance notes, a crash-debugging recipe and the ideas list. **Read the relevant section of it before touching a hook, an offset or a ported feature**, and keep it up to date when you learn something about the game.
Quick map: `src/qol_ui.c` (DllMain, config, log, patch helpers) unity-includes `lootfilter.h`, `automap.h`, `hud.h` (+ `stats.h`, `buffs.h`), `idclick.h` (shift-click ID, item move), `inspect.h`, `statrange.h`, `keys.h`; the pure-C rule engine is `filter.c` / `itemdb.c`; byte-signature site finders are in `sig.h`; BH reference source: https://github.com/BeLikeLeBron/slashdiablo-maphack (AGPL-3.0).
