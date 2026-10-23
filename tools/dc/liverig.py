"""Shared helpers of the live-comparison rigs (PCSX2 + PINE): reload the dungeon save state, start a dungeon floor with the game's own floor-start function.
Used by validate_back_all.py and probe_preset.py. Needs coop/ on sys.path (the callers do that)."""
import time

USER_STATUS, SELECT_MAP, GAME_TASK = 0x002A3468, 0x002A3594, 0x002A35EC


def reload_state(p, slot=1, wait=8.0):
    """Load the PCSX2 save state (tools/dc/reset_game.sh's state 1: a dungeon floor in free movement)."""
    p.load_state(slot)
    time.sleep(wait)


def enter_floor(p, dungeon, floor, timeout=120):
    """gameTask 0xde = the game's own 'start a floor script' step (reloads the dungeon's event.stb, runs function 100 = the floor start); waits until the hero moves freely again."""
    us = p.read(USER_STATUS)
    p.write(us + 0, dungeon, 1)
    p.write(us + 2, floor - 1, 1)    # (the script adds one)
    p.write(us + 3, floor - 1, 1)
    p.write(SELECT_MAP, dungeon, 4)
    p.write(0x01D5648C, 100, 4)
    p.write(0x01D56490, 0, 4)
    p.write(GAME_TASK, 0xDE, 4)
    still = 0
    for _ in range(timeout):
        time.sleep(1)
        still = still + 1 if p.read(GAME_TASK) == 0 else 0
        if still >= 4:
            break
    time.sleep(1)
    return p.read(GAME_TASK), p.read_bytes(us + 2, 1)[0]
