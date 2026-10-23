# Native co-op (Godot front end + dccore) - design and state

Goal (user, 2026-10-19; this is the point of the project): up to 4 players; each cycles through all six characters; clothes colour by player number
(red, blue, green, yellow); together in town, fishing, shopping; when one player enters a dungeon everybody enters; players can drop items for each other.
Reusable parts are listed in notes/reusable.md.

## Model
* Every peer runs its own game (own save, own inventory, own player sim: movement, attack, items, status). Responsive, no prediction needed for the local player.
* `coop/coop_net.gd` (ENet): roster with player numbers (0 = host), presence (20 Hz dictionary), reliable events.
* Towns: each peer runs its town; the others are puppets (`coop_town.gd`: make_player_model(character, colour), name tag, interpolation). DONE, verified with two instances.
* Colour: `dccore/recolor.*` (hue swap of saturated pixels, wash for Xiao's pale dress) via `DcCharacter::set_player_color`. DONE (viewer: color_view.tscn).
* Dungeons: SAME floor on every peer (host sends dungeon, floor, rng state; `start_floor` is deterministic). The HOST owns the monsters and what depends on them.

## Dungeon protocol (host-authoritative world, client-local player)
* Client -> host every tick (unreliable): player pose/motion/time/stick/idle + the player's attack spheres of the pool (`ColEntry` with a serial). The host injects them
  into its pool (target 2) once per serial (update pos/radius while the same serial lives) so `check_dmg` of the monsters hurts them exactly like the host's own sword.
* Host steps the monsters once; each monster targets the NEAREST player (`MonsterWorld::player` is switched per monster).
* Monster attack spheres (target 1) are tested by the host against every remote player (`ColPool::check_hit_user`); a hit goes to that client as a reliable message with the
  entry's data; the client applies it with `Walker::apply_hit(entry)` (the tail of `check_damage`): life, knock-back, motion, status effects.
* Host -> clients (20-30 Hz): monster snapshot (slot, state, hp, pos, rot, motion, time), drops, hit numbers, door/gate/chest events. Clients render only.
* Chests / doors / gate: every peer has the same floor events; opening is broadcast (`chest_opened(i)`, `door_opened`, `gate_opened`); the item goes to the opener.
* Floor exit (stairs): any player taking it moves everyone (reliable `floor_exit`); game over only when every player is down; a downed player waits until the next floor.
* Drops: host owns the drop pool; a pick-up by a player is a request to the host, which answers `got(item)` to that player only. Item gifts between players:
  `drop_item(slot)` creates a world pick-up owned by nobody (synced like drops) that any other player can take.

## Implementation notes (what was built and why)
* **Hit spheres are replicated, not the damage.** The game's attack boxes live one tick in a pool of 96 `ColEntry`. The client captures its own sword spheres right after its player step
  (`DcWalker::coop_capture`, they would be gone after `pool.step`), sends them (`ColPool::pack`, 45 floats each, with a serial); the host injects them (`ColPool::inject`, `remote` = the
  remote player id) and its normal `check_dmg` hurts the monster. The host sends the monsters' spheres (target mask 1) the same way and the client's own `Walker::check_damage` hurts
  the client with its own defence, invulnerability and knock-back. A serial is remembered for 180 ticks so a lost message cannot make a sphere hit twice.
* **The damage code runs with the attacker's record** (`RemoteScope` in monster_combat.cpp swaps `MonsterWorld::status` / `player` with the remote's copy kept by the host); what it changed in the
  record (weapon wear and experience, kills, gold and items picked up) is diffed and sent back as `StatusPatch` byte ranges that the client applies. The client sends its whole record on arrival
  and once a second.
* **Monsters target the nearest player** (`MonsterWorld::step_ai` swaps `player` per monster; `check_view_level` uses the nearest distance).
* **Client world**: `coop_client` skips monster stepping, the host's drops and monster shot effects; monsters and drops come from snapshots (30 Hz), floating numbers from `hits`.
* **Floor events** (chests, statues, traps, trigger areas, doors, event slots) are one int vector (`coop_event_state`); each peer sends the entries it changed (`ev_state`), the others apply them
  (the item went to the opener). A guest who joins late gets the whole vector.
* **Same floor**: `DcModels::set_coop_floor(seed, host record)`: the seed replaces the running generator, the host's record feeds `build_event_data` (Atla lists), the player's own Atla list uses a
  separate generator. Floor changes, boss clear, leaving: reliable events (`next_floor`, `dungeon_end`, `dungeon_left`). Late join: the host sends `enter_dungeon` when a guest is `ready`.
* **Towns**: the guest builds the host's Georama (`town_layout_request`, the 3000-byte block of the save), only the host can edit. Puppets and gifts live in `coop_town.gd`.
* Menus do not pause the world in co-op (`set_input_blocked`). Keys: Q/E (or L1/R1) change the character in town, [ and ] in a dungeon, D in the item list drops an item for the others.
* Tests: `work/coop_smoke.sh` (two Godot instances on this PC), harness flags in play.gd / town_play.gd / game.gd (`--coop host|join:ip`, `--live-shots`, `--near-monster`, `--gift-at`, `--exit-at`, `--cycle-at`).

## Steps / status
1. DONE  town presence, colours, character cycling, title screen Host/Join, shared (host's) Georama, item gifts in town.
2. DONE  dungeon entry sync for everybody (also late join), puppets, shared floor, floor exit, boss clear, leaving.
3. DONE  host monsters + client snapshots, client attacks hurt monsters, monsters hurt clients, weapon exp / wear / gold / items as patches.
4. DONE  chest / door / statue sync, item gifts in dungeons (drop pool).
5. DONE  puppets with their weapon, minimap dots and a life panel of the other players, monster shots on the clients (snapshots), the other players' weapon effects (`coop_export_fx` / `coop_apply_fx`: instances of the player's own effect slot plus Xiao's bullets, Osmond's gun and flame as dots, sent at 20 Hz as data `fx`, drawn by `DcWalker::apply_remote_fx`).
6. PARTLY  a fallen player does not leave the floor: after about four seconds he gets up at the floor's entrance with half his life (`DcWalker::coop_revive`; the single-player game over would end the host's floor for everybody). Story rooms and character rooms run on each peer on its own. The back floor is shared: when one player's script calls `_SET_URA_DUNGEON` his peer tells the host (`ura_swap`), the host relays it, and every peer swaps at once (`DcWalker::coop_swap_floor`, the monsters are the host's again); the map key of presence and `ev_state` carries a `b` for the back floor so players on different sides never mix, a late guest is told the side (`enter_dungeon.back`). Test: work/coop_smoke.sh no. 7.
7. PARTLY  the host's Georama edits reach the guests in that town (`town_edited` after leaving the edit mode: they rebuild the town from the host's layout; untested with two instances). TODO: fishing / shop poses of puppets.

## Balance (2026-10-20, `dccore/coop_scale.h`)
The original game has no co-op, so the numbers are a design choice (research: Diablo 2 scales hit points by (n+1)/2 and rewards with them, Dungeon Siege scales hit points and experience, Diablo 3 dropped its damage scaling and kept hit points). For `n` players (2..4; more counts as 4):
* **Monster / boss hit points** x (n + 1) / 2: 1.5 / 2.0 / 2.5 (applied when a monster spawns: `MonsterWorld::spawn`, so the bosses of fixed rooms are scaled too).
* **Damage the monsters deal** x curve(n), **monsters on a random floor** x curve(n) (15 -> 16 / 18 / 22 on the main floor, 8 -> 8 / 9 / 12 on the back floor; `MonsterUnit::kSlots` is 24 now, the game has 16), **drops** x curve(n), with curve = 1.1 / 1.2 / 1.5 for 2 / 3 / 4 players ("10 % per player, 1.5 with four"). Damage: `ColPool::monster_damage_mult` scales every sphere with the player-target bit (monster attacks, shots, bombs) when it is made on the host, so clients receive the scaled entries. Drops: after the game's own loot rules ran for a dead monster, with probability curve - 1 whatever it dropped (gold / item) drops a second time next to the first one.
* Weapon experience, gold amounts and everything else are unchanged. One player = exactly the game's numbers (no extra rand() draws, parity tests unchanged).
* `n` is fixed per floor: the host sends `players` with `enter_dungeon` / `next_floor` (and again to a late guest), every peer builds the same monsters (the count must match the snapshots); players who join or leave in the middle of a floor change nothing until the next floor. Test: `play.tscn --coop-players N` (no network), `dccore/tests/test_coop_scale.cpp`.
* Known limits: chests / events are not scaled; with the 4-awake-monsters rule of the game (`check_view_level`) more monsters mean more waiting monsters, not bigger fights; the curve is one function (`coop_curve`) if "1.1 / 1.2 / 1.5" should become a straight line.

## Network notes
* Port 8211 UDP (ENet). Same network: join with the host's local address. Internet: the host forwards the port by hand, or presses the UPnP button on the Co-op page (asks the router to open it for 6 hours; the mapping is removed again when the session ends). UPnP is never started automatically.
