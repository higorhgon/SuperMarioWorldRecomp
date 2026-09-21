# The disassembly overlay

This project carries IsoFrieze's SMWDisX as a submodule and treats it as an
**authority**, not a reference:

```
third_party/SMWDisX   github.com/IsoFrieze/SMWDisX
third_party/asar      github.com/RPGHacker/asar (v1.91)
```

It assembles to a ROM that is **byte-identical** to the clean U dump this
project targets. That is the whole reason it can be used the way it is used
here: for every byte in the ROM it says which source statement produced it, so
it can answer questions the recompiler cannot answer about itself.

The assembler is pinned too. SMWDisX, unlike Yoshi's Island's disassembly,
does not ship an asar binary, and "the disassembly is byte-exact" is a claim
about a *pair* — a source tree and the assembler that reads it. Pinning only
half of it would leave the proof depending on whatever asar happened to be on
the machine. `tools/ingest_smwdisx.py` builds the pinned asar on first use
into `_smw_build/asar/` and caches it, so a clean checkout needs nothing but
CMake and a C++ compiler.

## Verifying the claim

`tools/ingest_smwdisx.py` re-assembles the disassembly on every run and
refuses to import anything if the output differs from `smw.sfc`:

```
$ python tools/ingest_smwdisx.py --report
SMWDisX assembles byte-identical to smw.sfc (524288 bytes)
symbol labels        12561 (7115 ROM addresses)
mapped addresses     170867
  65816    code     62412
  65816    data     106758
  spc700   code     1643
  spc700   data     54
macros classified    17 (BorW=code, DMASettings=data, …, insert_empty=data)
mnemonic cross-check 62258 literal code addresses, 0 disagreements with the ROM
non-65816 regions    747
pointer-table entries 82 new 65816 entry points named by data statements
                     (657 already declared by hand, left alone)
```

asar writes a 512-byte copier header when handed an empty output file; the
importer strips it before comparing, because the project ROM is headerless.

## What is imported, and why those four things

`tools/ingest_smwdisx.py` writes the block between
`# >>> BEGIN disassembly overlay` and `# <<< END disassembly overlay` in each
`recomp/bank*.cfg`. This project's cfgs are heavily hand-tuned — 1,978 entries
with explicit `end:` bounds, `exclude_range`, `hle_*` and dispatch
directives — and every one of them lives outside the markers and survives a
re-run untouched.

| Directive | Count | What it settles |
|---|---|---|
| `data_region` | 747 | Every span that is **not** 65816 code |
| `name` | 82 | Entry points named by a pointer table |
| `symbol` | 6,817 | Naming only — feeds the emitter's name resolver |

**`data_region`.** asar's `[addr-to-line mapping]` resolves every emitted ROM
address to the exact source line that produced it; the statement on that line
says whether the address is an instruction or a `db`/`dw`/`dl`/`incbin`.
Spans that are not 65816 code become `data_region` lines, and the decoder
stops at them. That includes the SPC-700 sound driver: `arch` is global
assembler state that survives `incsrc`, so the importer reconstructs the
assembler's `arch` timeline by walking `smw.asm` in evaluation order — a
depth-first walk, not a flat list of its includes — which is what separates
"bank $0E is code" from "bank $0E is code the 65816 decoder must never walk
into".

**`name` from pointer tables.** This is the one the overlay could not have
done without, and it is worth stating why. The framework finds a jump table
by scanning ROM forward from the dispatching instruction, and it abandons the
scan when the table base falls inside a `data_region` (`decoder.py`,
`_autorecover_*`). That gate is right when there is no authority — walking
arbitrary data invents targets. But the moment the data_regions become
*accurate*, the gate starts firing on exactly the tables it most wants to
read: SMW's sprite-main `dw` tables **are** data, so marking them data hid all
of their targets. Measured: adding data_regions alone dropped 67 addresses
from the emitted program, 62 of them at real instruction boundaries.

The disassembly does not have to scan. `dw BanzaiBnCGrayPlat` inside a data
statement names its target outright, and the symbol file says what address
that label stands at. Every such target whose address is 65816 code is
declared as a `name`. 657 of the 739 the authority finds were already declared
by hand in these cfgs, which is the corroboration that the extraction is
finding the right thing; the remaining 82 are new. A target that already has a
hand-written `func` or `name` is **left alone** — two names for one address
make the emitter publish an alias against a variant set the owning entry never
produced, and the link closure rejects the build.

**`symbol`.** A non-promoting overlay: the emitter's name resolver uses it, so
a function the analyzer discovers at a labelled address comes out of codegen
carrying the disassembly's name, but nothing becomes a function because of it.
The effect is visible in the generated C and in the runtime rings —
`InitReznor`, `SumosLightning`, `MarioSprInteract`, `WallFollowers` rather
than `bank_01_9138`.

`name` for every hand-named parent label (what the Yoshi's Island importer
does) is available behind `--promote-names` and is **off by default here**.
That project's cfgs had no `func` entries of their own; this one's have 1,978,
with bounds and width annotations that promotion interacts with. Turning it on
is a coverage decision to make and measure, not an import default.

## Two traps this importer is built around

**asar's line attribution is exact at top level, and off by one inside a macro
body.** A probe over every mapped address found zero addresses credited to a
non-emitting top-level statement, so there is no global offset to correct. But
`insert_empty`'s 27,676 fill bytes are credited to line 70 (`elseif`) when the
`rep` that emits them is on line 71. The importer does not try to correct
that. It neutralises it: every macro that receives an address has a uniformly
code-or-data body, so an address landing anywhere inside a macro is classified
by **the macro**, never by the line. Each macro's kind is derived from its own
body — data directives, a nested `%Other(...)` call, or a mnemonic-position
`<param>` placeholder resolved against the arguments every call site passes —
and a macro whose body emits both is a hard failure, not a default.

**There is no "assume data" fallback.** An unrecognised statement aborts the
import. This is not pedantry: the first version of this importer did default,
and it silently classified `+ LDA.B #%00000011` as data, because asar's
anonymous `+` / `-` labels share a line with the instruction they label.
That one gap hid **2,931 instructions** behind spurious data_regions and
inflated the region count from 747 to 3,553. Defaulting made the failure
invisible; hard-failing made it a one-line fix.

Every code classification is also checked against the ROM: 62,258 addresses
where the source names a literal mnemonic are compared to the opcode actually
at that address, and any disagreement aborts the import. It currently reports
0.

## The conformance harness

`tools/smwdisx_compare.py` checks the recompiler's decode against the
disassembly. It drives the recompiler's **own** decoder (it wraps
`v2_analyze.decode_function` for the duration of a real analysis) rather than
reimplementing one, so a disagreement is a disagreement with the shipping
decoder.

This replaced an earlier harness that parsed `bank_XX.asm` as text and tracked
the program counter itself. That approach had to reimplement asar — macro
expansion, the five-version `con()` picker, the `%BorW` address-mode macros,
`rep` fills — and its own docstring listed what it could not do. The symbol
file is asar's answer to all of it.

```
$ python tools/smwdisx_compare.py
AOT-eligible variants     : 2125
instruction addresses     : 61148
PASS (clean boundary + mnemonic): 61008

FAIL code-vs-data (65816 decode inside a data statement): 45
FAIL wrong architecture (65816 decode over SPC-700 code): 0
FAIL mid-instruction landing (decoded inside another instruction): 95
FAIL unmapped (no statement of any kind covers this address): 0
WARN mnemonic parity: 0

implicated entry variants (>=1 mid-instruction landing): 28 of 2125
```

Read it as follows.

* **wrong architecture: 0.** The generated `data_region` lines keep the 65816
  decoder out of the SPC-700 driver completely. This is the check the overlay
  exists for, and it passes.
* **mnemonic parity: 0 mismatches.** At every address where the decoder and
  the disassembly agree on the instruction boundary, they agree on the
  instruction.
* **unmapped: 0.** Every address the decoder touched is an address the
  authority describes.
* **code-vs-data: 45, in three clusters.** All three are pre-existing cfg
  entries pointed at data, exposed rather than caused by the overlay:
  `$01E41F` (7 — `func Spr036_Unused_DataTable`, which bank01.cfg already
  annotates "points to data, not code"), `$038837`–`$038839` (3), and
  `$0CEC1E`… (35 — the `func` at `$0C:EC20` decodes straight through a data
  table). Each deserves its own issue.
* **mid-instruction landings: 95, across 28 variants, all `_M1X1`.** These are
  entry-width phantoms, not decoder bugs, and they are the same class Yoshi's
  Island reports: a cfg entry carries no entry `(m, x)`, so all four variants
  get materialized and the wrong-width ones decode a phantom instruction
  stream. `$00EB77_M1X1` alone accounts for 44. The fix is to propagate M/X
  from the disassembly and emit a per-entry width, which is `beads-8wg.2.49`,
  not to patch generated C.

The harness is honest about the current state rather than tuned to pass, so
it exits non-zero while any hard-failure class is non-empty.

Two asar spellings had to be taught to it, both of which otherwise show up as
phantom mid-instruction landings: `rep <n> : <insn>` and the repeat-count
suffix `DEX #3` / `NOP #4` / `ASL #2`, each of which assembles several copies
of a one-byte instruction under a single address-to-line row. Together they
accounted for 46 of the first run's 141 landings.

## Re-running the import

```sh
python tools/ingest_smwdisx.py           # assemble, verify, rewrite cfgs
python tools/smwdisx_compare.py          # check the decode against it
bash  tools/regen.sh --stock --quick
```

The importer is deterministic and writes only names, addresses and structural
metadata. No instruction bytes and no disassembly text are copied into this
repository.

## What it changed

Measured against the same cfgs without the overlay, on the same ROM:

| | before | after |
|---|---|---|
| emitted variants | 3,720 | 3,211 |
| AOT-eligible | 2,586 | 2,479 |
| **LLE-only (the interpreter floor)** | **1,134** | **732** |
| distinct emitted addresses | 2,097 | 2,092 |

The interpreter floor drops by 35%. The ten addresses that went away are
phantoms the authority disowns — seven are landings inside another statement,
two are inside a macro that emits fill bytes, and one ($02F018) is a real
instruction in the middle of `CODE_02F011` that nothing labelled and nothing
jumps to.
