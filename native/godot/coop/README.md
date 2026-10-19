# coop/ - reusable co-op layer (game independent)

Drop this folder into any Godot 4 project. Nothing in here knows about Dark Cloud.

* `coop_net.gd` (`CoopNet`): ENet host/join, player roster with player numbers (0 = host; the number also picks the player colour), 20 Hz
  *presence* (any small Dictionary per peer, unreliable) and reliable *events* (`send_event`, `send_event_to`, `send_event_to_host`).
  One instance per process at `/root/CoopNet` (`CoopNet.create(node)`); the path must be identical on every peer.
* Game side adapters live next to the game code (for Dark Cloud: `coop_town.gd` = puppets in towns, later `coop_dungeon.gd`).

Pattern used: every peer simulates its OWN player locally (responsive, per-player save/inventory); only presence and discrete events travel.
Shared-world things (monsters, chests) are owned by the host and streamed as snapshots. See notes/coop_native.md and notes/reusable.md.

Colour of a player's clothes: `native/dccore/include/dccore/recolor.h` (HSV hue swap of strongly coloured pixels; engine independent).
