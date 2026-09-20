"""Entity-specific checks against an opt-in raster dump and the rendered BMP.

Unlike native-area parity, these checks require actual sprite pixels in the
expanded view. Fixtures stay local because they contain ROM-derived graphics.
"""
import struct

LINE_SIZE = 66656
OWNER_OFFSET = 0x20000 + 224 * LINE_SIZE


class Capture:
    def __init__(self, root, frame):
        path = root / f'frame-{frame:06d}.swr'
        self.raw = (path if path.exists() else root / 'frame.swr').read_bytes()
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

    def piece(self, slot, occluders=(), native=False):
        """Require the independently decoded opaque OBJ pixels to be visible.

        These fixtures place the objects clear of occluding terrain/objects and
        use full brightness without OBJ color math; assert those prerequisites.
        Explicit screen-space rectangles can exclude a known overlapping part.
        Native pieces must retain their original coordinate even if a host
        ownership record accidentally claims them.
        """
        x, pos, attr, valid = struct.unpack_from('<iHH?', self.raw, OWNER_OFFSET + slot*12)
        owner_x, owner_pos, owner_attr = x, pos, attr
        if native:
            pos, attr = struct.unpack_from('<HH', self.ram, 0x200+slot*4)
        else:
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
        if native:
            x = (pos & 255) - ((high & 1) << 8)
            if valid and (owner_pos, owner_attr) == (pos, attr):
                assert owner_x == x, f'OAM {slot}: native piece displaced from {x} to {owner_x}'
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
                if any(left <= sx < right and upper <= sy < lower
                       for left,upper,right,lower in occluders): continue
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


def check_pipes(root, frames):
    """Compare saved YI2 pipes across camera motion to their native PPU pixels.

    The oracle frame has both pipes inside the native view, so this does not
    duplicate the host's Map16 lookup or assume a particular palette. Interior
    strips avoid rounded corners, Yoshi at the right lip, and the flying Koopa
    passing the left edge of the shorter pipe in the 100:9 run.
    """
    captures = [Capture(root, frame) for frame in frames]
    oracle = next((cap for cap in captures if cap.camera <= 2704 and cap.camera+256 >= 2768), None)
    assert oracle is not None, 'route never put both saved pipes inside the native view'
    native = OWNER_OFFSET + 128*12
    def screen(cap, wx, wy):
        sy = wy-cap.word(0x1c)-1
        line = 0x20000+sy*LINE_SIZE
        hscroll = struct.unpack_from('<H',cap.raw,line+14)[0]
        vscroll = struct.unpack_from('<H',cap.raw,line+22)[0]
        assert vscroll == cap.word(0x1c), 'fixture has an unexpected vertical raster offset'
        # The captured raster may lag the frame-start camera by a few pixels.
        dx = ((hscroll-cap.camera+512)&1023)-512
        return wx-cap.camera-dx, sy
    samples = []
    for left, right, top in ((2720,2732,336), (2740,2752,320)):
        for wy in range(top+2,384):
            for wx in range(left,right):
                sx, sy = screen(oracle,wx,wy)
                expected = struct.unpack_from('<I', oracle.raw, native+(sy*256+sx)*4)[0] & 0xffffff
                samples.append((wx,wy,expected))
    areas = set()
    failures = []
    for frame, cap in zip(frames,captures):
        assert cap.ram[0x100] == 20 and cap.word(0x1c) == 192, 'pipe fixture left the saved scene'
        mismatches = 0
        for wx,wy,expected in samples:
            x, y = screen(cap,wx,wy)
            sx = x+cap.offset
            assert 0 <= sx < cap.width, 'pipe fixture requires a wider viewport'
            areas.add('native' if 0 <= x < 256 else 'expanded')
            actual = struct.unpack_from('<I', cap.bmp, cap.pixels+((223-y)*cap.width+sx)*4)[0] & 0xffffff
            mismatches += actual != expected
        if mismatches: failures.append((frame,mismatches))
    pointers = {cap.word(0xfbe+0x133*2) for cap in captures}
    assert areas == {'native','expanded'}, 'pipes did not cross the native viewport boundary'
    assert len(pointers) == 4, 'route did not exercise all four transient pipe tables'
    assert not failures, f'pipe pixels disagree with native PPU across camera motion: {failures}'
    return dict(pipe_frames=len(captures), native_pipe_pixels=len(samples),
                compared_pixels=len(samples)*len(captures), transient_pipe_tables=len(pointers))
