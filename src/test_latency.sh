#!/bin/bash
# Checks the per-step latency histogram of the step-statistics build: for
# the seven latency configurations (min, psi, phi and dual, and psi, phi and
# dual with --report smem-one), on indexes built from the minishred RLBWT and
# LCP files in DATA_DIR, query output at --interleave 0 is byte-identical to
# the benchmark build's, and the histogram written to MS_LAT_OUT holds one
# step per base, its repositions match the summary, its buckets are
# contiguous and ordered, and the probe calibration is present.
# Usage: test_latency.sh BENCH_BINARY STEPS_BINARY DATA_DIR [WORK_DIR]
set -u
B=$(cd "$(dirname "$1")" && pwd)/$(basename "$1")
S=$(cd "$(dirname "$2")" && pwd)/$(basename "$2")
DATA=$3
W=${4:-$(mktemp -d)}
mkdir -p "$W"
P="$DATA/minishred1_20_002"
$B build "$P.bwt.heads" "$P.bwt.len" "$W/mini.idx" --layout min --lcp-bin "$P.lcp" > /dev/null 2>&1 || { echo "build --layout min failed"; exit 1; }
$B build "$P.bwt.heads" "$P.bwt.len" "$W/mini.tms" --layout full --lcp-bin "$P.lcp" > /dev/null 2>&1 || { echo "build --layout full failed"; exit 1; }
$B text "$W/mini.tms" 2> /dev/null > "$W/text.txt"
python3 - "$W" <<'PY'
import random, sys
d = sys.argv[1]
t = open(d + '/text.txt').read().strip().replace('%', '').replace('$', '')
random.seed(11)
with open(d + '/reads.fa', 'w') as out:
    for i in range(3000):
        L = random.choice([1, 50, 150, 400, 2000])
        if random.random() < 0.7 and L < len(t):
            p = random.randrange(len(t) - L)
            s = list(t[p:p + L])
            er = random.choice([0, 0.01, 0.05])
            for j in range(L):
                if random.random() < er: s[j] = random.choice('ACGT')
            s = ''.join(s)
        else:
            s = ''.join(random.choice('ACGT') for _ in range(L))
        out.write('>r%d\n%s\n' % (i, s))
PY
cd "$W"
fails=0
while read -r name idx opts; do
    $B query $idx reads.fa $opts --interleave 0 -o bench.out 2> bench.err || { echo "failed: bench $name"; cat bench.err; exit 1; }
    MS_LAT_OUT=lat.txt $S query $idx reads.fa $opts --interleave 0 -o steps.out 2> steps.err \
        || { echo "failed: steps $name"; cat steps.err; exit 1; }
    cmp -s bench.out steps.out || { echo "output differs: $name"; fails=$((fails + 1)); }
    python3 - "$name" lat.txt steps.err <<'PY' || fails=$((fails + 1))
import re, sys
name, lat, err = sys.argv[1:4]
lines = open(lat).read().splitlines()
e = open(err).read()
bases = int(re.search(r'query: reads=\d+ bases=(\d+)', e).group(1))
assert lines[0].startswith('lat-format: ms2-latency-v2'), lines[0]
summ = dict(kv.split('=') for kv in lines[1].split()[1:])
bins = [list(map(int, l.split()[1:])) for l in lines if l.startswith('lat-bin ')]
L = int(re.search(r'classes=(\d+)', lines[0]).group(1))
n = sum(b[2] for b in bins)
assert n == bases == int(summ['steps']), (name, n, bases, summ['steps'])
assert sum(b[3] for b in bins) == int(summ['reps'])
for b in bins:
    assert len(b) == 7 + 2 * L
    assert b[0] < b[1]
    assert sum(b[7:7 + L]) + sum(b[7 + L:]) == b[2]
    assert sum(b[7:7 + L]) <= b[3]
    assert b[3] or b[4] == 0
assert all(x[1] <= y[0] for x, y in zip(bins, bins[1:]))
assert bins[-1][0] <= int(summ['max']) < bins[-1][1]
if name != 'min':
    assert any(b[3] for b in bins), 'no repositions recorded'
    assert sum(b[4] for b in bins) > 0, 'no walk steps recorded'
assert sum(b[5] for b in bins) > 0, 'no fast-forward rows recorded'
assert any(l.startswith('lat-cal: steps=1000000 ') for l in lines)
assert any(l.startswith('lat-smem: select_s=') for l in lines)
assert any(l.startswith('lat-os: cpus=') for l in lines)
assert re.search(r'^lat: steps=%d ' % bases, e, re.M)
PY
    echo "checked $name"
done <<'CMDS'
min mini.idx
psi mini.tms --mode psi
phi mini.tms --mode phi
dual mini.tms --mode dual
psi-smem-one mini.tms --mode psi --report smem-one
phi-smem-one mini.tms --mode phi --report smem-one
dual-smem-one mini.tms --mode dual --report smem-one
CMDS
[ $fails -eq 0 ] && echo "test_latency: all passed" || { echo "test_latency: $fails failed"; exit 1; }
