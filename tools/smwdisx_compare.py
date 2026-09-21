#!/usr/bin/env python3
"""Conformance harness: the recompiler's decode vs the byte-exact SMWDisX.

The pinned SMWDisX disassembly assembles, under the pinned asar, to this
project's exact ROM image, so for every byte it says which source statement
produced it. That makes it an oracle for two questions the recompiler cannot
answer about itself:

  CODE-VS-DATA  Did the decoder walk into something that is not 65816 code?
                A data table or the SPC-700 sound driver decoded as 65816
                produces C that compiles and links and is nonsense.

  PARITY        At each address the decoder claims is an instruction, does it
                agree with the disassembly on the MNEMONIC and on the
                instruction BOUNDARY? A landing mid-instruction is invisible
                in the generated C and shows up much later as a wrong branch.

Method. The recompiler's own decoder is used — not a reimplementation — by
wrapping v2_analyze's `decode_function` binding for the duration of a real
analysis and keeping every graph it produces, restricted to the variants the
manifest made AOT-eligible. Each decoded instruction's address is then looked
up in asar's address-to-line map, which names the producing source line; the
line's first token is the authoritative mnemonic and the `arch` in force at
that line is the authoritative architecture.

This replaces a previous version of this file that parsed bank_XX.asm as text
and tracked the program counter itself. That approach had to reimplement
asar — macro expansion, the five-version `con()` picker, `%BorW` address-mode
macros, `rep` fills — and its own docstring listed what it could not do. The
symbol file is asar's own answer to all of it, so nothing here re-derives the
assembler.

Exit status is 1 when any hard-failure class is non-empty. Mnemonic
mismatches at a clean boundary are reported but do not by themselves fail the
run: asar spells some opcodes differently (BRA vs BRL, JML vs JMP.l) and the
table below folds the known pairs, so anything left over is listed in full to
be judged rather than assumed.

Usage:
    python tools/smwdisx_compare.py                  # every bank
    python tools/smwdisx_compare.py --bank 01        # one bank
    python tools/smwdisx_compare.py --limit 40       # cap listed violations
    python tools/smwdisx_compare.py --json report.json
"""
from __future__ import annotations

import argparse
import bisect
import json
import re
import sys
from collections import Counter, defaultdict
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parent
sys.path.insert(0, str(REPO / 'snesrecomp'))
sys.path.insert(0, str(HERE))

import ingest_smwdisx as disasm                                # noqa: E402

sys.path.insert(0, str(REPO / 'snesrecomp' / 'tools'))
sys.path.insert(0, str(REPO / 'snesrecomp' / 'recompiler'))
import v2_analyze                                              # noqa: E402

# asar spells some 65816 opcodes differently from the recompiler's decoder.
# These are the same instruction under two names, not a disagreement.
MNEMONIC_ALIASES = {
    'JML': 'JMP',     # asar: long jump; decoder: JMP with a long operand
    'JSL': 'JSR',     # same, for the call
    'BRL': 'BRA',     # long relative branch
    'RTL': 'RTS',     # long return
}


def normalise(mnemonic: str) -> str:
    return MNEMONIC_ALIASES.get(mnemonic.upper(), mnemonic.upper())


# 65816 mnemonics whose only addressing modes are implied or accumulator, so
# they can never take a real immediate operand. asar reads `#<n>` after one of
# these as a REPEAT COUNT — `DEX #3` assembles three DEX bytes, `NOP #4` four
# NOPs — and emits a single address-to-line row for the whole run.
IMPLIED_ONLY = frozenset("""
ASL LSR ROL ROR INC DEC CLC CLD CLI CLV DEX DEY INX INY NOP PHA PHB PHD PHK
PHP PHX PHY PLA PLB PLD PLP PLX PLY RTI RTL RTS SEC SED SEI STP SWA TAD TAS
TAX TAY TCD TCS TDA TDC TSA TSC TSX TXA TXS TXY TYA TYX WAI XBA XCE
""".split())
# `rep <n> : <stmt>` is asar's other repeat form.
REPEAT_SUFFIX_RE = re.compile(r"^([A-Za-z]{3})\s*#", re.IGNORECASE)


def is_repeated(text: str) -> bool:
    """Does this statement assemble more than one copy of one instruction?"""
    if disasm.REP_PREFIX_RE.match(text):
        return True
    match = REPEAT_SUFFIX_RE.match(text)
    return bool(match) and match.group(1).upper() in IMPLIED_ONLY


def disassembly_facts(sym: Path, disasm_dir: Path):
    """(kind, arch, mnemonic) for every address the disassembly emits.

    The classification comes from the importer, so the harness and the cfgs
    it is judging are answering to the same authority — including the macro
    handling and the anonymous-label (`+` / `-`) stripping, both of which
    silently turn instructions into "data" when they are missing.
    """
    labels, files, addr_line = disasm.parse_symbols(sym)
    id_to_path, lines_by_id = disasm.resolve_source_files(disasm_dir, files)
    (kind_by_pc, arch_by_pc, _parents, _macros, macro_owner,
     _macro_kinds) = disasm.build_maps(disasm_dir, files, addr_line,
                                       lines_by_id, id_to_path)

    mnem_by_pc: dict[int, str] = {}
    repeated: set = set()
    for pc24, (file_id, number) in addr_line.items():
        if (file_id, number) in macro_owner:
            # The mnemonic is a macro parameter (`<cmd>.W <addr>`); the macro
            # proves the address is code, but there is no literal to compare.
            continue
        lines = lines_by_id.get(file_id)
        if lines is None or not (1 <= number <= len(lines)):
            continue
        text = disasm.strip_label(disasm.strip_comment(lines[number - 1]))
        # `DEX #3` and `rep 3 : DEX` each assemble three one-byte
        # instructions but get ONE address-to-line row, for the first of
        # them. Without this the harness reports the rest as landings inside
        # the first — 141 of them across this ROM, every one an artefact.
        if is_repeated(text):
            repeated.add(pc24)
            match = disasm.REP_PREFIX_RE.match(text)
            if match:
                text = disasm.strip_label(match.group(1))
        if not text:
            continue
        mnem_by_pc[pc24] = re.split(r'[\s.:]', text, 1)[0].upper()
    return kind_by_pc, arch_by_pc, mnem_by_pc, repeated


class Spans:
    """Which source statement OWNS each ROM address.

    asar's address-to-line map gives the START address of every emitted
    statement; a statement owns the bytes up to the next start in the same
    bank. Classifying only exact starts is not enough and is actively
    misleading: an address three bytes into a `dl` pointer table has no map
    row of its own, so a start-only check files it as "the disassembly says
    nothing here" when the disassembly in fact says "data". The same
    distinction separates a clean instruction boundary from a landing in the
    middle of one, which is the failure this harness exists to catch.
    """

    def __init__(self, kind_by_pc, arch_by_pc, mnem_by_pc, repeated=()):
        self.kind = kind_by_pc
        self.arch = arch_by_pc
        self.mnem = mnem_by_pc
        self.repeated = set(repeated)
        self.starts: dict[int, list] = defaultdict(list)
        for pc24 in kind_by_pc:
            self.starts[(pc24 >> 16) & 0xFF].append(pc24 & 0xFFFF)
        for addresses in self.starts.values():
            addresses.sort()

    def owner(self, pc24: int):
        """(start_pc24, kind, arch) of the statement covering pc24, or None."""
        bank = (pc24 >> 16) & 0xFF
        addresses = self.starts.get(bank)
        if not addresses:
            return None
        pc16 = pc24 & 0xFFFF
        index = bisect.bisect_right(addresses, pc16) - 1
        if index < 0:
            return None
        start = (bank << 16) | addresses[index]
        if start in self.repeated and self.kind.get(start) == 'code':
            # A `rep N : <insn>` span: every byte in it is a fresh boundary
            # for the same instruction, so the queried address IS a boundary.
            self.mnem[pc24] = self.mnem.get(start, '?')
            return pc24, self.kind.get(start), self.arch.get(start)
        return start, self.kind.get(start), self.arch.get(start)


def aot_decoded_instructions(rom_path: Path, cfg_dir: Path):
    """Every instruction the recompiler decodes inside an AOT-eligible
    variant, as {pc24: set(mnemonics)} plus the owning entry PCs.

    The recorder wraps v2_analyze's own `decode_function` binding rather than
    reading the decoder's cache afterwards: build_manifest clears that cache
    in a `finally`, so reading it after the call returns nothing at all and
    the harness silently reports zero addresses checked.
    """
    recorded: list = []
    original = v2_analyze.decode_function

    def recording(rom, bank, start, entry_m, entry_x, **kwargs):
        graph = original(rom, bank, start, entry_m, entry_x, **kwargs)
        recorded.append((((bank & 0xFF) << 16) | (start & 0xFFFF),
                         entry_m & 1, entry_x & 1, graph))
        return graph

    rom = v2_analyze.load_rom(str(rom_path))
    parsed = v2_analyze._load_cfgs(cfg_dir)
    v2_analyze.decode_function = recording
    try:
        manifest, _helpers, _inline = v2_analyze.build_manifest(
            rom, parsed, max_insns=4096, max_nodes=100_000,
            all_cfg_roots=True)
    finally:
        v2_analyze.decode_function = original

    aot_variants = set()
    for key, node in manifest.nodes.items():
        disposition = str(getattr(node.disposition, 'value',
                                  node.disposition)).lower()
        if 'aot_eligible' in disposition:
            aot_variants.add((key.pc24 & 0xFFFFFF, key.m & 1, key.x & 1))

    mnems: dict[int, set] = defaultdict(set)
    owner: dict[int, set] = defaultdict(set)
    for entry_pc24, entry_m, entry_x, graph in recorded:
        if (entry_pc24, entry_m, entry_x) not in aot_variants:
            continue
        for decoded in graph.insns.values():
            pc24 = decoded.key.pc & 0xFFFFFF
            mnems[pc24].add(decoded.insn.mnem.upper())
            owner[pc24].add((entry_pc24, entry_m, entry_x))
    return mnems, owner, manifest, aot_variants


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--rom', type=Path, default=REPO / 'smw.sfc')
    parser.add_argument('--cfg-dir', type=Path, default=REPO / 'recomp')
    parser.add_argument('--disasm', type=Path,
                        default=REPO / 'third_party' / 'SMWDisX')
    parser.add_argument('--sym', type=Path,
                        default=REPO / '_smw_build' / 'smw.sym')
    parser.add_argument('--bank', help='restrict the report to one bank (hex)')
    parser.add_argument('--limit', type=int, default=25,
                        help='violations listed per class (0 = all)')
    parser.add_argument('--json', type=Path, help='write the full report here')
    args = parser.parse_args()

    if not args.sym.exists():
        raise SystemExit(
            f'{args.sym} does not exist — run tools/ingest_smwdisx.py first; '
            'it assembles the pinned disassembly and proves the result is '
            "byte-identical to this project's ROM before writing anything.")

    kind_by_pc, arch_by_pc, mnem_by_pc, repeated = disassembly_facts(
        args.sym, args.disasm)
    spans = Spans(kind_by_pc, arch_by_pc, mnem_by_pc, repeated)
    mnems, owner, _manifest, aot_variants = aot_decoded_instructions(
        args.rom, args.cfg_dir)

    only_bank = int(args.bank, 16) if args.bank else None

    checked = 0
    into_data = []          # decoded as code inside a data statement
    wrong_arch = []         # decoded as 65816 over SPC-700 code
    mid_insn = []           # landed inside another instruction
    unmapped = []           # no statement of any kind covers the address
    mismatched = []         # mnemonic disagreement at a clean boundary
    per_bank = defaultdict(Counter)

    for pc24 in sorted(mnems):
        bank = (pc24 >> 16) & 0xFF
        if only_bank is not None and bank != only_bank:
            continue
        checked += 1
        counters = per_bank[bank]
        counters['checked'] += 1
        ours = {normalise(m) for m in mnems[pc24]}
        variants = sorted(owner[pc24])
        entries = [f'${p:06X}_M{m}X{x}' for p, m, x in variants[:3]]
        resolved = spans.owner(pc24)
        if resolved is None:
            unmapped.append((pc24, sorted(ours), entries))
            counters['unmapped'] += 1
            continue
        start, kind, arch = resolved
        if arch != '65816':
            wrong_arch.append((pc24, arch, sorted(ours)))
            counters['wrong_arch'] += 1
            continue
        if kind != 'code':
            into_data.append((pc24, sorted(ours), entries, f'${start:06X}'))
            counters['into_data'] += 1
            continue
        if start != pc24:
            mid_insn.append((pc24, sorted(ours), entries, f'${start:06X}',
                             normalise(spans.mnem.get(start, '?'))))
            counters['mid_insn'] += 1
            continue
        theirs = spans.mnem.get(pc24)
        if theirs is None:
            # A macro-expanded instruction: proved code, no literal mnemonic.
            counters['pass'] += 1
            continue
        if normalise(theirs) not in ours:
            mismatched.append((pc24, sorted(ours), normalise(theirs)))
            counters['mnemonic_mismatch'] += 1
            continue
        counters['pass'] += 1

    def show(title, rows, fmt):
        print(f'\n{title}: {len(rows)}')
        limit = len(rows) if args.limit == 0 else min(args.limit, len(rows))
        for row in rows[:limit]:
            print('   ' + fmt(row))
        if limit < len(rows):
            print(f'   ... {len(rows) - limit} more')

    total_pass = sum(c['pass'] for c in per_bank.values())
    print(f'AOT-eligible variants     : {len(aot_variants)}')
    print(f'instruction addresses     : {checked}')
    print(f'PASS (clean boundary + mnemonic): {total_pass}')
    show('FAIL code-vs-data (65816 decode inside a data statement)',
         into_data,
         lambda r: f'{r[0]:06X} decoder={",".join(r[1])} '
                   f'statement_at={r[3]} entries={r[2]}')
    show('FAIL wrong architecture (65816 decode over SPC-700 code)',
         wrong_arch, lambda r: f'{r[0]:06X} disassembly_arch={r[1]} '
                               f'decoder={",".join(r[2])}')
    show('FAIL mid-instruction landing (decoded inside another instruction)',
         mid_insn,
         lambda r: f'{r[0]:06X} decoder={",".join(r[1])} '
                   f'inside={r[3]} ({r[4]}) entries={r[2]}')
    show('FAIL unmapped (no statement of any kind covers this address)',
         unmapped, lambda r: f'{r[0]:06X} decoder={",".join(r[1])} '
                             f'entries={r[2]}')
    show('WARN mnemonic parity', mismatched,
         lambda r: f'{r[0]:06X} decoder={",".join(r[1])} disassembly={r[2]}')

    # A mid-instruction landing means the ENTRY WIDTH this variant was
    # decoded at contradicts the byte-exact disassembly: `LDA #$8000` under
    # M=1 becomes `LDA #$80` plus whatever the high byte happens to spell.
    # The variant, not the address, is the unit a cfg `entry_mx_at` /
    # `force_variant_at` directive acts on, so name the variants.
    phantom = Counter()
    for pc24, _ours, _entries, _inside, _mn in mid_insn:
        for entry_pc24, entry_m, entry_x in sorted(owner[pc24]):
            phantom[(entry_pc24, entry_m, entry_x)] += 1
    print(f'\nimplicated entry variants (>=1 mid-instruction landing): '
          f'{len(phantom)} of {len(aot_variants)} AOT-eligible')
    for (entry_pc24, entry_m, entry_x), count in phantom.most_common(12):
        print(f'   ${entry_pc24:06X}_M{entry_m}X{entry_x}  {count} landing(s)')
    if len(phantom) > 12:
        print(f'   ... {len(phantom) - 12} more')

    print('\nper bank:')
    for bank in sorted(per_bank):
        counters = per_bank[bank]
        print(f'  ${bank:02X}  checked={counters["checked"]:<7} '
              f'pass={counters["pass"]:<7} '
              f'into_data={counters["into_data"]:<5} '
              f'wrong_arch={counters["wrong_arch"]:<5} '
              f'mid_insn={counters["mid_insn"]:<5} '
              f'unmapped={counters["unmapped"]:<5} '
              f'mnemonic={counters["mnemonic_mismatch"]}')

    if args.json:
        args.json.parent.mkdir(parents=True, exist_ok=True)
        args.json.write_text(json.dumps({
            'checked': checked,
            'pass': total_pass,
            'into_data': [[f'{p:06X}', o, n, st] for p, o, n, st in into_data],
            'mid_insn': [[f'{p:06X}', o, n, st, mn]
                         for p, o, n, st, mn in mid_insn],
            'wrong_arch': [[f'{p:06X}', a, o] for p, a, o in wrong_arch],
            'unmapped': [[f'{p:06X}', o, n] for p, o, n in unmapped],
            'mnemonic_mismatch': [[f'{p:06X}', o, t]
                                  for p, o, t in mismatched],
            'per_bank': {f'{b:02X}': dict(c) for b, c in per_bank.items()},
            'phantom_variants': [
                {'entry': f'{e:06X}', 'm': m, 'x': x, 'landings': n}
                for (e, m, x), n in phantom.most_common()],
        }, indent=2), encoding='utf-8')
        print(f'\nwrote {args.json}')

    hard_failures = (len(into_data) + len(wrong_arch)
                     + len(mid_insn) + len(unmapped))
    return 1 if hard_failures else 0


if __name__ == '__main__':
    raise SystemExit(main())
