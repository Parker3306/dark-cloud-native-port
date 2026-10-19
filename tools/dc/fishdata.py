"""Dump fishing-related data tables from the main ELF (strings, esa_info, fish_info, exitemlst, ...).  usage: fishdata.py"""
import os, struct, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from elfdata import Elf

def cstr(e, va):
    b = b''
    while True:
        c = e.read(va + len(b), 1)
        if c == b'\0': break
        b += c
    return b.decode('latin1')

if __name__ == '__main__':
    e = Elf()
    for a in sys.argv[1:]:
        va = int(a, 16)
        print(hex(va), repr(cstr(e, va)))
