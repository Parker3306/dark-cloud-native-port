"""Print the fishing-related functions (and the internal functions calling them) of the town event scripts.
usage: fishscripts.py <map> [ext name substring ...]      e.g. fishscripts.py e02"""
import os, struct, sys
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import event_usage as eu
import stbtext


def main():
    m = sys.argv[1]
    keys = sys.argv[2:] or ['FISH', 'CHANGE_ESA', 'FP_CHANGE']
    b = open(os.path.join(eu.DATA, 'gedit', m, 'event.stb'), 'rb').read()
    names = eu.event_names()
    code, fl = eu.functions_of(b)
    for fid, rec, pc, end in fl:
        wanted = set()
        lines = stbtext.text_function(b, code, names, pc, end, wanted)
        if any(k in l for l in lines for k in keys):
            nslots, nargs = struct.unpack_from('<2I', b, rec + 8)
            print('\n%s // record %#x slots %d args %d' % (('function %d' % fid) if fid is not None else 'fn_%x' % rec, rec, nslots, nargs))
            for l in lines:
                if '?op' not in l:
                    print('  ' + l)


main()
