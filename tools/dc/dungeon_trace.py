"""Headless trace of a dungeon event function (dun/script/d0N/event.stb) with the DUNGEON ext table answered.
usage: dungeon_trace.py d01 100 [floor=14] [old=13] [action=1] [flag=24:1,32:0] [int=0:50] [use=207|none] [dfloor=-1] [user=0] [frames=600] [all]
  floor / old     answers of _GET_FLOOR_LEVEL / _GET_OLD_FLOOR_LEVEL (the script calls _SET_FLOOR_LEVEL itself when old != -1: floor+1)
  action          _GET_ACTION_MODE (0 stepped on, 1 OK pressed, 2 item-use button)
  flag / int      game flags / game int flags (_SGET_GAME_FLAG / _SGET_GAME_INT_FLAG); everything else is 0
  use             what _ITEM_USE_WINDOW answers (an item id or none = -1)
  dfloor          _GET_DUNGEON_FLOOR (BtMapJumpFloor, -1 = none)
  all             print message/camera/ASQ calls too (default: only calls that matter for the game state)
Built on tools/dc/stbrun.py (Python port of the script VM): polls (_LOAD_SYNC, _CHECK_FADE, _ASQ_CHECK, _CHECK_MES*) answer "done" at once, so cut-scenes run to their end in a few frames.
"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import event_usage as eu
import stbrun

NOISE = ('_ASQ', '_SET_CAMERA', '_GET_CAMERA', '_SET_NPC', '_GET_NPC', '_NPC_', '_MES_', '_SET_MES', '_CHECK_MES', '_GET_PAD', '_SET_PROJ', '_PLAY_', '_LOAD_SND', '_LOAD_SOUND',
         '_LOAD_BGM', '_STOP_', '_BGM', '_FADE', '_CHECK_FADE', '_LOAD_CHR', '_LOAD_CHARA', '_LOAD_SYNC', '_ACTIVE_FILE', '_SET_CURRENT', '_INITIALIZE', '_CLEAR_', '_SYNC_', '_RELEASE_',
         '_HOBJ', '_OBJ_', '_INIT_', '_DRAW', '_SP_', '_LOAD_SPRITE', '_LOAD_TEXTURE', '_SET_WORLD', '_CAMERA_', '_ADD_CAMERA', '_SET_OBJ_', '_SCREEN_FILTER', '_SET_LIGHT', '_SET_NPC',
         '_DELETE_', '_SKIP', '_FINISH', '_TURN', '_SET_SCENE', '_SET_TEX', '_SET_FOG', '_SET_BACK', '_SET_EXCL')


class DHost(stbrun.Host):
    def __init__(self, names, flags, ints, opt):
        super().__init__(names, flags, ints)
        self.opt = opt
        self.talk_info = 0
        self.place = 0

    def ext(self, fid, vm, base, n):
        name = self.names.get(fid, 'ext%d' % fid)
        o = self.opt
        S = lambda i, v: vm.setv(base, i, v)
        if name == '_GET_FLOOR_LEVEL':
            self.log.append('%s() -> %d' % (name, o['floor'])); S(0, o['floor']); return
        if name == '_GET_OLD_FLOOR_LEVEL':
            self.log.append('%s() -> %d' % (name, o['old'])); S(0, o['old']); return
        if name == '_GET_ACTION_MODE':
            self.log.append('%s() -> %d' % (name, o['action'])); S(0, o['action']); return
        if name == '_GET_DUNGEON_FLOOR':
            self.log.append('%s() -> %d' % (name, o['dfloor'])); S(0, o['dfloor']); return
        if name in ('_GET_NOW_USER_ID', '_CHECK_CHR_HELP', '_GET_RUBY_ELEMENT', '_CHECK_MARDAN', '_GET_MAP_CODE'):
            v = {'_GET_NOW_USER_ID': o['user'], '_GET_MAP_CODE': o['map'] + 1}.get(name, 0)
            self.log.append('%s() -> %d' % (name, v)); S(0, v); return
        if name in ('_ITEM_USE_WINDOW', '_OPEN_ENTRANCE_WINDOW', '_OPEN_ESCAPE_WINDOW'):
            args = ', '.join(vm.argv(base, i) for i in range(n))
            if name == '_ITEM_USE_WINDOW':
                v = o['use']
            elif name == '_OPEN_ENTRANCE_WINDOW':
                v = o['floor']
            else:
                v = 1
            self.log.append('%s(%s) -> %d' % (name, args, v)); S(0, v); return
        if name in ('_CHECK_EVENT_FLG', '_GET_ITEM_TRAPID', '_CHK_ATRA_HAVE', '_GET_GATEKEY_NO', '_GET_PIERO_ITEM', '_EASTKING_COMPLETE', '_GET_ITEM_UNIT_NO'):
            for i in range(n):
                S(i, 0)
            self.log.append('%s(...) -> 0' % name); return
        super().ext(fid, vm, base, n)


def main():
    d, fid = sys.argv[1], int(sys.argv[2])
    opt = {'floor': 0, 'old': -1, 'action': 0, 'use': -1, 'dfloor': -1, 'user': 0, 'frames': 600, 'map': int(d[1:]) - 1}
    flags, ints, show_all = {}, {}, False
    for a in sys.argv[3:]:
        if a == 'all':
            show_all = True
            continue
        k, v = a.split('=')
        if k == 'flag':
            for kv in v.split(','):
                x, y = kv.split(':'); flags[int(x)] = int(y)
        elif k == 'int':
            for kv in v.split(','):
                x, y = kv.split(':'); ints[int(x)] = int(y)
        elif k == 'use':
            opt['use'] = -1 if v == 'none' else int(v)
        else:
            opt[k] = int(v)
    names = eu.event_names()
    data = open(os.path.join(eu.DATA, 'dun', 'script', d, 'event.stb'), 'rb').read()
    host = DHost(names, flags, ints, opt)
    vm = stbrun.VM(data, host, names)
    r = vm.run(fid)
    frames = 0
    while r == 1 and frames < opt['frames']:
        r = vm.resume(); frames += 1
        host.log.append('-- frame %d' % frames)
    print('result', r, 'frames', frames, 'flags', flags, 'ints', ints)
    out = []
    for l in host.log:
        if l.startswith('-- frame'):
            if out and out[-1].startswith('-- frame'):
                out[-1] = l
            else:
                out.append(l)
            continue
        if not show_all and l.startswith(NOISE):
            continue
        out.append(l)
    print('\n'.join(out))


if __name__ == '__main__':
    main()
