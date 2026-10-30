# ms2

Matching statistics on move structures, built on the Orbit move-structure library.

- `orbit/`: the Orbit library (submodule); ms2 compiles against `orbit/include`.
- `teratools/`: TeraTools (submodule), whose TeraLCP writes the LCP files ms2 builds indexes from.
- `src/`: the ms2 program; `make -C src` builds `src/ms2`.

## Use

```
ms2 build HEADS LENS INDEX --layout full --minima MINIMA
ms2 query INDEX READS -o OUT
```

`build` reads an RLBWT (HEADS and LENS) and a TeraLCP `-ominima` file (MINIMA). `--layout` picks what the index keeps, and so which query modes it serves: `min`, `psi`, `phi` or `full`. `ms2 --help` describes each, and `ms2 --help-advanced` adds the development options and commands.

## Build and test

```
git submodule update --init
make
make test
```

`make test` runs `./src/ms2 test data`, which expects the minishred test files in `data/`. On macOS, build with GCC (for example `make CXX=g++-15`): Apple clang cannot compile Orbit's `compute_img_rank_inv`.
