#!/usr/bin/env python3
"""A very small 6502 assembler for the host tests' ROMs (tests/make_test_rom.py): enough for test programs, written
here so that no game data is needed.

    a = Asm(origin)
    a.label("loop"); a.op("lda", "#", 0x42); a.op("sta", "zp", 0x09); a.op("bne", "rel", "loop")
    a.op("jmp", "abs", "loop"); a.byte(1, 2); a.word("loop")
    code = a.assemble()   # bytes from origin

Modes: imp (no operand), # (immediate), zp, zpx, abs, absx, absy, rel (a label), ind (jmp).
"""

OPS = {
    # name: {mode: opcode}
    "adc": {"#": 0x69, "zp": 0x65, "abs": 0x6D},
    "and": {"#": 0x29, "zp": 0x25, "abs": 0x2D},
    "asl": {"imp": 0x0A},
    "bcc": {"rel": 0x90}, "bcs": {"rel": 0xB0}, "beq": {"rel": 0xF0}, "bne": {"rel": 0xD0},
    "bmi": {"rel": 0x30}, "bpl": {"rel": 0x10}, "bvc": {"rel": 0x50}, "bvs": {"rel": 0x70},
    "bit": {"zp": 0x24, "abs": 0x2C},
    "clc": {"imp": 0x18}, "cld": {"imp": 0xD8}, "cli": {"imp": 0x58}, "sec": {"imp": 0x38}, "sei": {"imp": 0x78},
    "cmp": {"#": 0xC9, "zp": 0xC5, "abs": 0xCD},
    "cpx": {"#": 0xE0}, "cpy": {"#": 0xC0},
    "dex": {"imp": 0xCA}, "dey": {"imp": 0x88}, "inx": {"imp": 0xE8}, "iny": {"imp": 0xC8},
    "dec": {"zp": 0xC6, "abs": 0xCE}, "inc": {"zp": 0xE6, "abs": 0xEE},
    "eor": {"#": 0x49, "zp": 0x45, "abs": 0x4D},
    "jmp": {"abs": 0x4C, "ind": 0x6C}, "jsr": {"abs": 0x20}, "rts": {"imp": 0x60}, "rti": {"imp": 0x40},
    "lda": {"#": 0xA9, "zp": 0xA5, "zpx": 0xB5, "abs": 0xAD, "absx": 0xBD, "absy": 0xB9},
    "ldx": {"#": 0xA2, "zp": 0xA6, "abs": 0xAE},
    "ldy": {"#": 0xA0, "zp": 0xA4, "abs": 0xAC},
    "lsr": {"imp": 0x4A},
    "nop": {"imp": 0xEA},
    "ora": {"#": 0x09, "zp": 0x05, "abs": 0x0D},
    "pha": {"imp": 0x48}, "pla": {"imp": 0x68},
    "rol": {"imp": 0x2A}, "ror": {"imp": 0x6A},
    "sbc": {"#": 0xE9},
    "sta": {"zp": 0x85, "zpx": 0x95, "abs": 0x8D, "absx": 0x9D, "absy": 0x99},
    "stx": {"zp": 0x86, "abs": 0x8E}, "sty": {"zp": 0x84, "abs": 0x8C},
    "tax": {"imp": 0xAA}, "tay": {"imp": 0xA8}, "txa": {"imp": 0x8A}, "tya": {"imp": 0x98},
    "txs": {"imp": 0x9A}, "tsx": {"imp": 0xBA},
}
SIZE = {"imp": 1, "#": 2, "zp": 2, "zpx": 2, "rel": 2, "abs": 3, "absx": 3, "absy": 3, "ind": 3}


class Asm:
    def __init__(self, origin):
        self.origin = origin
        self.items = []  # (kind, ...)

    def label(self, name):
        self.items.append(("label", name))

    def op(self, name, mode="imp", arg=None):
        if mode not in OPS[name]:
            raise ValueError(f"{name} has no mode {mode}")
        self.items.append(("op", name, mode, arg))

    def byte(self, *values):
        self.items.append(("bytes", bytes(values)))

    def word(self, value):
        self.items.append(("word", value))

    def assemble(self):
        labels, pc = {}, self.origin
        for it in self.items:
            if it[0] == "label":
                labels[it[1]] = pc
            elif it[0] == "op":
                pc += SIZE[it[2]]
            elif it[0] == "bytes":
                pc += len(it[1])
            else:
                pc += 2
        out, pc = bytearray(), self.origin

        def value(v):
            return labels[v] if isinstance(v, str) else v

        for it in self.items:
            if it[0] == "label":
                continue
            if it[0] == "bytes":
                out += it[1]
                pc += len(it[1])
                continue
            if it[0] == "word":
                v = value(it[1])
                out += bytes([v & 0xFF, v >> 8])
                pc += 2
                continue
            _, name, mode, arg = it
            out.append(OPS[name][mode])
            if mode == "rel":
                d = value(arg) - (pc + 2)
                if not -128 <= d <= 127:
                    raise ValueError(f"branch to {arg} too far ({d})")
                out.append(d & 0xFF)
            elif SIZE[mode] == 2:
                out.append(value(arg) & 0xFF)
            elif SIZE[mode] == 3:
                v = value(arg)
                out += bytes([v & 0xFF, v >> 8])
            pc += SIZE[mode]
        self.labels = labels
        return bytes(out)
