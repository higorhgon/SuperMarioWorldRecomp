"""Entity-specific checks against an opt-in raster dump and the rendered BMP.

Unlike native-area parity, these checks require actual sprite pixels in the
expanded view. Fixtures stay local because they contain ROM-derived graphics.
"""
import struct

LINE_SIZE = 66656
OWNER_OFFSET = 0x20000 + 224 * LINE_SIZE


class Capture:
    def __init__(self, root, frame):
        self.raw = (root / 'frame.swr').read_bytes()
        assert len(self.raw) == OWNER_OFFSET + 128 * 12 + 256 * 224 * 4
        self.ram = self.raw[:0x20000]
        self.bmp = (root / f'frame-{frame:06d}.bmp').read_bytes()
        self.width, height = struct.unpack_from('<ii', self.bmp, 18)
        assert height == 224 and struct.unpack_from('<H', self.bmp, 28)[0] == 32
        self.pixels = struct.unpack_from('<I', self.bmp, 10)[0]
        self.camera = self.word(0x1a)
        level_width = (self.ram[0x5e] + 1) * 256
        origin = max(0, min(self.camera - (self.width - 256) // 2,
                            max(0, level_width - self.width)))
        self.offset = self.camera - origin

    def word(self, address):
        return struct.unpack_from('<H', self.ram, address)[0]

    def active(self):
        return [i for i in range(12) if self.ram[0x14c8+i]]

    def piece(self, slot):
        """Require the independently decoded opaque OBJ pixels to be visible.

        These fixtures place the objects clear of occluding terrain/objects and
        use full brightness without OBJ color math; assert those prerequisites.
        """
        x, pos, attr, valid = struct.unpack_from('<iHH?', self.raw, OWNER_OFFSET + slot*12)
        assert valid, f'OAM {slot}: missing host owner'
        top = pos >> 8
        line = 0x20000 + top * LINE_SIZE
        regs = self.raw[line:line+64]
        assert regs[0] == 15, 'fixture must be fully lit'
        palette = struct.unpack_from('<256H', self.raw, line+64)
        oam = struct.unpack_from('<256H', self.raw, line+576)
        assert (oam[slot*2], oam[slot*2+1]) == (pos, attr), 'owner does not match raster'
        vram = struct.unpack_from('<32768H', self.raw, line+1088)
        high = self.raw[line+66624+slot//4] >> (slot%4*2)
        sizes = ((8,16),(8,32),(8,64),(16,32),(16,64),(32,64),(16,32),(16,32))
        size = sizes[regs[1] >> 5][(high >> 1) & 1]
        base = (regs[1] & 7) * 8192
        if attr & 256: base += (((regs[1] >> 3) & 3) + 1) * 4096
        colours = 128 + ((attr >> 9) & 7) * 16
        expected = matched = 0
        for row in range(size):
            for col in range(size):
                sx, sy = self.offset+x+col, top+row
                if not (0 <= sx < self.width and 0 <= sy < 224): continue
                cx = size-1-col if attr & 0x4000 else col
                cy = size-1-row if attr & 0x8000 else row
                tile = (((attr & 255)//16 + cy//8) & 15)*16 + ((attr+cx//8) & 15)
                address = (base + tile*16 + cy%8) & 0x7fff
                bit = 7-cx%8
                p01, p23 = vram[address], vram[(address+8) & 0x7fff]
                index = ((p01>>bit)&1) | (((p01>>(bit+8))&1)<<1) | (((p23>>bit)&1)<<2) | (((p23>>(bit+8))&1)<<3)
                if not index: continue
                rgb = palette[colours+index]
                channels = [(rgb >> (5*i)) & 31 for i in range(3)]
                color = sum(((v<<3)|(v>>2)) << (16-8*i) for i,v in enumerate(channels))
                actual = struct.unpack_from('<I', self.bmp, self.pixels+((223-sy)*self.width+sx)*4)[0] & 0xffffff
                expected += 1
                matched += actual == color
        assert expected >= 16, f'OAM {slot}: empty sprite fixture'
        assert matched == expected, f'OAM {slot}: only {matched}/{expected} sprite pixels visible at x={x}'
        return expected


def check(root, frame, scenario):
    cap = Capture(root, frame)
    if scenario == 'standing':
        assert cap.camera == 0, 'Mario/camera moved before the standing check'
        # US Yoshi's Island 2: the eight red Koopas on the first platform.
        # Record indices, not a generic far-enemy count, identify this group.
        slots = [i for i in cap.active() if cap.ram[0x9e+i] == 5 and 1 <= cap.ram[0x161a+i] <= 8]
        assert len(slots) == 8, f'only {len(slots)}/8 platform Koopas loaded'
        # Native 16x32 Koopa graphics use OAM allocation +4 and +8; the first
        # reserved piece is hidden. Require both head and body for all eight.
        pixels = [cap.piece(64+cap.ram[0x15ea+i]//4+part) for i in slots for part in (1,2)]
        return dict(platform_koopas=len(slots), visible_sprite_pixels=sum(pixels), camera=cap.camera)
    slots = [i for i in cap.active() if cap.ram[0x9e+i] == 0x35]
    assert len(slots) == 1, 'Yoshi fixture must contain one adult Yoshi'
    slot = 64+cap.ram[0x15ea+slots[0]]//4
    return dict(yoshi_head_pixels=cap.piece(slot), yoshi_body_pixels=cap.piece(slot+1), camera=cap.camera)
