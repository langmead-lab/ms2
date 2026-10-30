# ms2

ms2 computes matching statistics and super-maximal exact matches (SMEMs) of reads against a large, repetitive text, such as a pangenome, using move structures from the [Orbit](https://github.com/drnatebrown/orbit) library.

- The **matching statistics** of a read give, for each offset i, the length of the longest prefix of the read's suffix starting at i that occurs somewhere in the text.
- An **SMEM** is a match between the read and the text that cannot be extended in either direction and is not contained in a longer such match. ms2 can write SMEMs with all of their text occurrences in the format of `ropebwt3 mem -p`.

ms2 works on the run-length Burrows-Wheeler transform (RLBWT) of the text, so its indexes grow with the number of BWT runs r rather than the text length n.

## Index layouts and query modes

When a match ends, the query moves to a nearby BWT run holding the next read character and needs the length of the longest common extension (LCE) between the current suffix and the new one. Layouts differ in how they get that LCE, and so in size and speed.

| Layout | Holds | Serves |
|---|---|---|
| `min` | LF with each row's LCP minima | `--mode min` |
| `psi` | LF and psi | `--mode psi`, matching statistics only |
| `phi` | LF, phi, phi_inv and per-row LCP summaries | `--mode phi`, positions and SMEMs |
| `full` | everything `psi` and `phi` hold | every mode, including `--mode dual` |
| `psi-locate` | `full` without phi's lengths-based form | `--mode psi` with positions and SMEMs; smaller on disk than `full`, but loads as much |

LF and psi are the move-structure forms of the BWT's LF mapping and its inverse; phi maps a suffix array entry to the one above it. In `min`, LCEs come from LCP values stored with each row. In `psi`, the query compares the read with the candidate suffix by stepping psi. In `phi`, whole rows' LCP minima come from the summaries and phi is walked over the current row only. `dual` races a psi walk against the phi walk.

## Building

ms2 needs a C++17 compiler and zlib. Clone with the submodules:

```
git clone --recursive https://github.com/langmead-lab/ms2.git
cd ms2
make all
make test
```

`make all` builds `src/ms2` (plain `make` lists the targets), and `make test` builds it and runs its self-tests on the small fixture in `data/`, the RLBWT and per-row LCPs of a small text. In an existing clone, run `git submodule update --init` first. On macOS, build with GCC (for example `make all CXX=g++-15`), since Apple clang cannot compile part of Orbit.

The `teratools` submodule holds TeraLCP, which computes the LCP information ms2 builds from. It needs [sdsl-lite](https://github.com/simongog/sdsl-lite); see `teratools/README.md`. `src/tools/fmd2rlbwt` converts a ropebwt3 FMD to an RLBWT; build it with `make -C src/tools/fmd2rlbwt`.

## Building an index

An index is built in three steps.

1. **An RLBWT of the text**, as `NAME.bwt.heads` (one byte per run) and `NAME.bwt.len` (5-byte little-endian run lengths). A BWT tool such as [grlBWT](https://github.com/ddiazdom/grlBWT) produces it. For a ropebwt3 FMD, `fmd2rlbwt` writes these files from the FMD's maximal runs.
2. **TeraLCP** computes per-run LCP minima (`-ominima`) and, for the `phi` and `full` layouts, per-run suffix array and LCP samples (`-ophi`), from the RLBWT or from the FMD:
   ```
   TeraLCP -f rlbwt -i NAME -t TMP -ominima NAME.minima -ophi NAME.phi
   TeraLCP -f fmd -i NAME.fmd -t TMP -ominima NAME.minima -ophi NAME.phi
   ```
3. **`ms2 build`** with the layout:
   ```
   ms2 build NAME.bwt.heads NAME.bwt.len NAME.idx --layout full --minima NAME.minima --phi NAME.phi
   ```
   Every layout but `psi` needs `--minima`. With `--phi`, a `phi` or `full` index takes phi from TeraLCP's samples, and the BWT may have one terminator per string, as an FMD does. `--seq-ids NAME.len.gz` stores ropebwt3's sequence-ID table, so that `smem-all` reports contig positions.

`ms2 index-psi-locate NAME.idx OUT.idx` writes a `psi-locate` index from a `full` one. `ms2 ftab-build NAME.idx NAME.ftab` builds an optional table of query states by k-mer, which starts each read k steps in.

## Querying

```
ms2 query NAME.idx reads.fq -o out.txt -p 16
```

This writes one line per read: its name, a tab, and its matching statistics. `--mode` picks the LCE method; the default is `min` on a `min` index, `phi` on a `phi` index and `psi` otherwise. With a `phi` or `full` index:

- `--positions` adds a text position for each value.
- `--report smem-one` adds the read's SMEMs, each with one position.
- `--report smem-all` writes every SMEM with its occurrences as `ropebwt3 mem -p N` does, with `--max-smem-positions N` and `--min-smem-len L` as ropebwt3's `-p` and `-l`.

`ms2 --help` describes every option, and `ms2 --help-advanced` adds development options. The formats are described in [`src/QUERY_OUTPUT.md`](src/QUERY_OUTPUT.md), [`src/INDEX_FORMAT.md`](src/INDEX_FORMAT.md), [`src/FTAB_FORMAT.md`](src/FTAB_FORMAT.md), [`src/ORACLE_FORMAT.md`](src/ORACLE_FORMAT.md) and [`src/tools/fmd2rlbwt/FMD_RUNS.md`](src/tools/fmd2rlbwt/FMD_RUNS.md).

## Citation

A paper describing ms2 is in preparation. Until it appears, please cite this repository, along with Orbit and TeraTools.

## License

ms2's own source is under the MIT license (see [LICENSE](LICENSE)), with parts that follow TeraTools under its MIT license and ropebwt3's `rld0.c` and `rld0.h` under theirs (see [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)). ms2 compiles against Orbit, which is under the GNU General Public License v3.0, so a built ms2 binary is distributed under the GPL-3.0.

## Acknowledgements

ms2 is by Ben Langmead. It is built on Orbit, the move-structure library by Nathaniel Brown. Its psi and phi query engine follows the design and code of TeraTools (TeraMS and TeraLCP) by Ahsan Sanaullah, Shaojie Zhang and the S. Zhang Lab at UCF, and it builds from TeraLCP's output. fmd2rlbwt uses ropebwt3's FMD reader by Heng Li.

Prepared by Ben Langmead with help from Claude Code (Opus 5.5).
