"""Addresses and structure offsets for Dark Cloud (USA, SCUS-97111, CRC a5c05c78). See notes/coop.md for how each was found."""

GAME_CRC = "a5c05c78"

# --- globals (main segment) -------------------------------------------------------------------------
BT_GAME_MODE_FLAG = 0x002A355C    # 1 = playing the dungeon
USER_STATUS_PTR = 0x002A3468      # -> CDngStatusData; byte +2 = current floor
NOW_DNG_MAP = 0x002A34B8          # -> CDungeonMap
SELECT_MAP_NO = 0x002A3594        # which dungeon
BT_URA_DONGEON = 0x002A34B4       # "ura" (back) dungeon flag
READ_BUFFER_PTR = 0x002A2384      # temporary file-load buffer, idle while playing
NPC_CFG_NAME = 0x0029BDF8         # "info.cfg": the name SetNPC asks the pack for ("base.cfg" has the same length)

# --- objects ------------------------------------------------------------------------------------------
CHARA_MAIN = 0x01EA1D20           # the player (CMainChara)
CHARA_POS = 0x10                  # vec4
CHARA_YAW = 0x64                  # rotation vec at +0x60: x, y(yaw), z
CHARA_MOTION = 0xC68              # current motion id
MONSTER_POOL = 0x01F066D0         # bump allocator whose free space is REAL free memory (the "NPC pool" overlaps it!)

# --- functions ----------------------------------------------------------------------------------------
SET_NPC = 0x001C1C00              # CDungeonMap::SetNPC(slot, pack, partsIdx, pos*, rot*, enable, motion, pool)
RESERV_NPC_DRAW = 0x001C1EB0      # CDungeonMap::ReservNPC_Draw(slot, dir) with x,y,z in f12-f14
NPC_SET_MOTION = 0x001C22B0       # CDungeonMap::NPCSetMotion(slot, motion)
MEMSET = 0x00103568
SET_POSITION = 0x00138FB0         # CCharacter::SetPosition(x, y, z in f12-f14)
SET_ROTATION = 0x00139150         # CCharacter::SetRotation(x, y, z in f12-f14)

# --- NPC slots (stride 0x1330 inside CDungeonMap) -----------------------------------------------------
SLOT_STRIDE = 0x1330
SLOT_ACTIVE = 0xCFC4
SLOT_ENABLE = 0xCFC8
SLOT_YAW = 0xCFB4                 # base rotation Y used when drawing (must stay within -pi..pi!)
SLOT_DRAWCOUNT = 0xD110           # number of queued draw entries
SLOT_MOTION = 0xCA58              # the NPC's motion id (same field as CHARA_MOTION on its CCharacter)
FIRST_PUPPET_SLOT = 1             # slot 0 is used by the floor's own NPC; 1-3 are free

# --- paths ---------------------------------------------------------------------------------------------
PLAYER_PACK = "data/dun/mainchara/c01d.chr"   # Toan's playable-character pack (same model, textures and motions as the player)
