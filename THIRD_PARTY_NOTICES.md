# Third-party notices

ms2's own source is under the MIT license in LICENSE. The files named below follow, or are copied from, the projects listed here, whose notices they carry.

ms2 also compiles against Orbit (https://github.com/drnatebrown/orbit), a submodule under the GNU General Public License v3.0 (orbit/LICENSE), so an ms2 binary is distributed under the GPL-3.0.

## TeraTools

ms2's psi and phi query engine follows TeraTools (https://github.com/ucfcbb/TeraTools) by Ahsan Sanaullah, Shaojie Zhang and the S. Zhang Lab at UCF.

- `src/tms_query.hpp`: the psi LCE, the reposition with its predecessor and successor scans, and the matching statistics loop follow `psi_lce`, `reposition_explicit` and `TeraIndex::ms_psi` in TeraTools' `src/include/TeraIndex/TeraIndex.h`.
- `src/reposition_oracle.hpp`: the oracle repositioning modes, which record and replay each reposition's outcome, follow TeraMS's `ms_oracle`.

```
MIT License

Copyright (c) 2025 S. Zhang Lab at UCF

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

## ropebwt3

`src/tools/fmd2rlbwt/rld0.c` and `src/tools/fmd2rlbwt/rld0.h` are copied from ropebwt3 (https://github.com/lh3/ropebwt3) by Heng Li, under the MIT license, copyright (c) 2024- Dana-Farber Cancer Institute. The full text is in `src/tools/fmd2rlbwt/LICENSE.ropebwt3`.
