/**
 * Per-step counts for builds with MS_STEP_STATS (make ms2-steps): where in a
 * read the repositions, the rows they read and the query time fall.  They
 * measure how much of a query an ftab, a table that jumps over a read's
 * first k steps, would skip.  Normal builds leave this header empty.
 *
 * A step is one character, counted from where the walk starts, the read's
 * right end.  The query records each step's repositions, rows read and
 * cycles (from the time stamp counter) as it goes, and flush() files the
 * read's steps by their index and by their distance from the last character
 * other than A, C, G and T (such as N), and works out which of the read's
 * windows an ftab over ACGT could jump: the last j bases, and the j bases
 * after each run of other characters, can be jumped when they occur in the
 * text, which the read's matching statistics say.  An index whose text holds
 * N (an FMD's, for one) matches a read's N rather than restarting there, but
 * an ftab over ACGT has no entry for a window holding one either way.  Reads are recorded only when one is in flight
 * at a time (query with --interleave 0).
 *
 * flush() also files every step into a latency histogram (LatAgg): cycles
 * per step in log-spaced buckets, and for each bucket what its steps did
 * (repositions, LCE walk steps, rows crossed by fast-forwards), so that the
 * slowest steps can be attributed after the run.  query writes it to the
 * file named by the environment variable MS_LAT_OUT.
 */

#ifndef _STEP_STATS_HPP
#define _STEP_STATS_HPP

#ifdef MS_STEP_STATS

#include "orbit/common.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <ostream>
#include <string>
#include <vector>
#if defined(__x86_64__) || defined(__i386__)
#include <x86intrin.h>
#endif
#include <map>
#ifdef __linux__
#include <fstream>
#include <sched.h>
#include <sstream>
#include <sys/resource.h>
#endif

using step_ulint = orbit::ulint;

inline uint64_t step_ticks() {
#if defined(__x86_64__) || defined(__i386__)
    return __rdtsc();
#elif defined(__aarch64__)
    // The virtual counter: coarse (24 MHz on Apple silicon), for tests.
    uint64_t v;
    asm volatile("mrs %0, cntvct_el0" : "=r"(v));
    return v;
#else
    return 0;
#endif
}

/** Ticks of step_ticks per second, measured against steady_clock over about ms milliseconds. */
inline double step_ticks_hz(unsigned ms = 200) {
    using clock = std::chrono::steady_clock;
    const auto t0 = clock::now();
    const uint64_t c0 = step_ticks();
    auto t1 = t0;
    while (t1 - t0 < std::chrono::milliseconds(ms)) t1 = clock::now();
    const uint64_t c1 = step_ticks();
    return double(c1 - c0) / std::chrono::duration<double>(t1 - t0).count();
}

// Rows crossed by the fast-forwards of every move (MS_FF) since the step in
// flight began.
inline thread_local step_ulint step_ff_rows = 0;

/**
 * Per-step latency: a histogram of ticks per step with 32 log-spaced buckets
 * per power of two (each about 2% wide), exact below 32 ticks, and for each
 * bucket the steps' repositions, LCE walk steps, fast-forward rows and scan
 * rows as sums, and two counts by size class (class 0 is 0, class c >= 1 is
 * [2^(c-1), 2^c), the last class open above): steps with a reposition by
 * the size of their walk, and steps without one by the rows their
 * fast-forwards crossed.
 */
struct LatAgg {
    static constexpr unsigned SUB_BITS = 5;
    static constexpr size_t SUB = size_t(1) << SUB_BITS;
    static constexpr size_t NB = SUB * (64 - SUB_BITS + 1);
    static constexpr size_t L = 26;
    struct Bin {
        step_ulint n = 0, reps = 0, walk = 0, ff = 0, rows = 0;
        std::array<step_ulint, L> rep_walk_cls{}, norep_ff_cls{};
    };
    std::vector<Bin> bins;  // NB once any step is added
    step_ulint steps = 0, reps = 0, max = 0;
    double sum = 0, sumsq = 0;

    static size_t bucket(uint64_t c) {
        if (c < SUB) return size_t(c);
        const unsigned e = 63u - unsigned(__builtin_clzll(c));
        return SUB * (e - SUB_BITS + 1) + size_t((c >> (e - SUB_BITS)) & (SUB - 1));
    }
    /** The smallest tick count in bucket b; bucket b + 1 starts where it ends. */
    static uint64_t bucket_lo(size_t b) {
        if (b < SUB) return b;
        const unsigned e = unsigned(b / SUB) + SUB_BITS - 1;
        return uint64_t(SUB + b % SUB) << (e - SUB_BITS);
    }
    static size_t cls(step_ulint x) {
        return x == 0 ? 0 : std::min<size_t>(L - 1, size_t(64 - __builtin_clzll(uint64_t(x))));
    }
    void add(uint64_t cyc, step_ulint r, step_ulint walk, step_ulint ff, step_ulint rows) {
        if (bins.empty()) bins.resize(NB);
        Bin& b = bins[bucket(cyc)];
        ++b.n;
        b.reps += r;
        b.walk += walk;
        b.ff += ff;
        b.rows += rows;
        if (r) ++b.rep_walk_cls[cls(walk)];
        else ++b.norep_ff_cls[cls(ff)];
        ++steps;
        reps += r;
        max = std::max<step_ulint>(max, cyc);
        sum += double(cyc);
        sumsq += double(cyc) * double(cyc);
    }
    LatAgg& operator+=(const LatAgg& o) {
        if (o.bins.empty()) return *this;
        if (bins.empty()) bins.resize(NB);
        for (size_t i = 0; i < NB; ++i) {
            Bin& a = bins[i];
            const Bin& b = o.bins[i];
            a.n += b.n; a.reps += b.reps; a.walk += b.walk; a.ff += b.ff; a.rows += b.rows;
            for (size_t c = 0; c < L; ++c) {
                a.rep_walk_cls[c] += b.rep_walk_cls[c];
                a.norep_ff_cls[c] += b.norep_ff_cls[c];
            }
        }
        steps += o.steps; reps += o.reps; max = std::max(max, o.max);
        sum += o.sum; sumsq += o.sumsq;
        return *this;
    }
    /** The lower edge of the bucket holding the step of rank ceil(q * steps). */
    uint64_t quantile(double q) const {
        if (steps == 0) return 0;
        const double want = std::max(1.0, std::ceil(q * double(steps)));
        step_ulint acc = 0;
        for (size_t i = 0; i < bins.size(); ++i) {
            acc += bins[i].n;
            if (double(acc) >= want) return bucket_lo(i);
        }
        return max;
    }
    /** One summary line, in ticks and, with hz, in nanoseconds. */
    void print_summary(std::ostream& os, const char* tag, double hz) const {
        const double mean = steps ? sum / double(steps) : 0;
        const double var = steps ? std::max(0.0, sumsq / double(steps) - mean * mean) : 0;
        os << tag << ": steps=" << steps << " reps=" << reps << " mean=" << mean
           << " sd=" << std::sqrt(var) << " p50=" << quantile(0.5) << " p99=" << quantile(0.99)
           << " p999=" << quantile(0.999) << " max=" << max << " ticks_hz=" << hz;
        if (hz > 0) {
            const double ns = 1e9 / hz;
            os << " p50_ns=" << double(quantile(0.5)) * ns << " p99_ns=" << double(quantile(0.99)) * ns
               << " p999_ns=" << double(quantile(0.999)) * ns << " max_ns=" << double(max) * ns;
        }
        os << "\n";
    }
    /**
     * The whole histogram: a header, the summary, then one line per
     * nonempty bucket: its tick range [lo, hi), the counts and sums, and
     * the class counts of steps with and without a reposition.
     */
    void print(std::ostream& os, double hz) const {
        os << "lat-format: ms2-latency-v2 sub_bits=" << SUB_BITS << " classes=" << L << "\n";
        print_summary(os, "lat", hz);
        os << "lat-cols: lo hi n reps walk ff rows rep_walk_cls[" << L << "] norep_ff_cls[" << L << "]\n";
        for (size_t i = 0; i < bins.size(); ++i) {
            const Bin& b = bins[i];
            if (b.n == 0) continue;
            os << "lat-bin " << bucket_lo(i) << ' ' << bucket_lo(i + 1) << ' ' << b.n << ' ' << b.reps << ' '
               << b.walk << ' ' << b.ff << ' ' << b.rows;
            for (size_t c = 0; c < L; ++c) os << ' ' << b.rep_walk_cls[c];
            for (size_t c = 0; c < L; ++c) os << ' ' << b.norep_ff_cls[c];
            os << "\n";
        }
    }
};

inline thread_local LatAgg lat_agg;

/** Counts summed over reads. */
struct StepAgg {
    // Window lengths and step indexes 0 to J - 1 are kept apart; later steps
    // share bucket J.
    static constexpr size_t J = 32;
    using Arr = std::array<step_ulint, J + 1>;
    // By step index from the read's right end.
    Arr steps{}, reps{}, rows{}, cyc{};
    // By steps since the end of the last run of characters other than ACGT,
    // for steps after one.
    Arr n_steps{}, n_reps{}, n_rows{}, n_cyc{};
    // For j = 1 to J: reads with at least j bases, those whose last j bases
    // occur, and the repositions, rows and cycles of the first j steps of
    // those reads.
    Arr start_fit{}, start_jump{}, start_jump_reps{}, start_jump_rows{}, start_jump_cyc{};
    // The same for the j bases after each run of characters other than ACGT.
    Arr postn_fit{}, postn_jump{}, postn_jump_reps{}, postn_jump_rows{}, postn_jump_cyc{};
    step_ulint reads = 0, bases = 0, reads_other = 0, other_chars = 0, other_runs = 0;
    step_ulint tot_reps = 0, tot_rows = 0, tot_cyc = 0;

    StepAgg& operator+=(const StepAgg& o) {
        auto add = [](Arr& a, const Arr& b) { for (size_t i = 0; i <= J; ++i) a[i] += b[i]; };
        add(steps, o.steps); add(reps, o.reps); add(rows, o.rows); add(cyc, o.cyc);
        add(n_steps, o.n_steps); add(n_reps, o.n_reps); add(n_rows, o.n_rows); add(n_cyc, o.n_cyc);
        add(start_fit, o.start_fit); add(start_jump, o.start_jump); add(start_jump_reps, o.start_jump_reps);
        add(start_jump_rows, o.start_jump_rows); add(start_jump_cyc, o.start_jump_cyc);
        add(postn_fit, o.postn_fit); add(postn_jump, o.postn_jump); add(postn_jump_reps, o.postn_jump_reps);
        add(postn_jump_rows, o.postn_jump_rows); add(postn_jump_cyc, o.postn_jump_cyc);
        reads += o.reads; bases += o.bases; reads_other += o.reads_other;
        other_chars += o.other_chars; other_runs += o.other_runs;
        tot_reps += o.tot_reps; tot_rows += o.tot_rows; tot_cyc += o.tot_cyc;
        return *this;
    }

    /** One line of totals, then one line per j of the counts above. */
    void print(std::ostream& os) const {
        os << "stepstats: reads=" << reads << " bases=" << bases << " reads_other=" << reads_other
           << " other_chars=" << other_chars << " other_runs=" << other_runs << " reps=" << tot_reps
           << " rows=" << tot_rows << " cycles=" << tot_cyc << "\n";
        os << "stepstats-cols: j steps reps rows cyc n_steps n_reps n_rows n_cyc start_fit start_jump start_jump_reps "
              "start_jump_rows start_jump_cyc postn_fit postn_jump postn_jump_reps postn_jump_rows postn_jump_cyc\n";
        for (size_t j = 0; j <= J; ++j) {
            os << "stepstats-j " << j << ' ' << steps[j] << ' ' << reps[j] << ' ' << rows[j] << ' ' << cyc[j] << ' '
               << n_steps[j] << ' ' << n_reps[j] << ' ' << n_rows[j] << ' ' << n_cyc[j] << ' ' << start_fit[j] << ' '
               << start_jump[j] << ' ' << start_jump_reps[j] << ' ' << start_jump_rows[j] << ' ' << start_jump_cyc[j]
               << ' ' << postn_fit[j] << ' ' << postn_jump[j] << ' ' << postn_jump_reps[j] << ' '
               << postn_jump_rows[j] << ' ' << postn_jump_cyc[j] << "\n";
        }
    }
};

inline thread_local StepAgg step_agg;

/** The steps of the read in flight. */
struct StepRecorder {
    std::vector<step_ulint> reps, rows, cyc, walk, ff;
    const step_ulint* ms = nullptr;  // the read's statistics, complete when it is finished
    const char* pat = nullptr;       // the read
    size_t m = 0;
    bool pending = false;

    /** Start recording read p of length len whose statistics go to out, filing the previous one. */
    void begin(const char* p, size_t len, const step_ulint* out) {
        flush();
        pat = p;
        m = len;
        ms = out;
        reps.assign(m, 0);
        rows.assign(m, 0);
        cyc.assign(m, 0);
        walk.assign(m, 0);
        ff.assign(m, 0);
        pending = true;
    }
    void add(size_t t, step_ulint r, step_ulint w, step_ulint c, step_ulint wk, step_ulint f) {
        if (!pending || t >= m) return;
        reps[t] += r;
        rows[t] += w;
        cyc[t] += c;
        walk[t] += wk;
        ff[t] += f;
    }
    /** File the read in flight, if any, into step_agg. */
    void flush() {
        if (!pending) return;
        pending = false;
        StepAgg& a = step_agg;
        constexpr size_t J = StepAgg::J;
        ++a.reads;
        a.bases += m;
        // Step t reads pattern position m - 1 - t.
        auto other = [&](size_t t) {
            const char c = pat[m - 1 - t];
            return c != 'A' && c != 'C' && c != 'G' && c != 'T';
        };
        // Prefix sums over steps, for the windows.
        std::vector<step_ulint> pr(m + 1, 0), pw(m + 1, 0), pc(m + 1, 0);
        for (size_t t = 0; t < m; ++t) {
            pr[t + 1] = pr[t] + reps[t];
            pw[t + 1] = pw[t] + rows[t];
            pc[t + 1] = pc[t] + cyc[t];
        }
        a.tot_reps += pr[m];
        a.tot_rows += pw[m];
        a.tot_cyc += pc[m];
        auto window = [&](size_t s, StepAgg::Arr& fit, StepAgg::Arr& jump,
                          StepAgg::Arr& jr, StepAgg::Arr& jw, StepAgg::Arr& jc) {
            for (size_t j = 1; j <= J && s + j <= m; ++j) {
                ++fit[j];
                // The window is steps s to s + j - 1, pattern positions
                // m - s - j to m - s - 1; it occurs when the statistic at its
                // left end covers it and it holds only ACGT.
                bool acgt = true;
                for (size_t t = s; t < s + j && acgt; ++t) acgt = !other(t);
                if (acgt && ms[m - s - j] >= j) {
                    ++jump[j];
                    jr[j] += pr[s + j] - pr[s];
                    jw[j] += pw[s + j] - pw[s];
                    jc[j] += pc[s + j] - pc[s];
                }
            }
        };
        window(0, a.start_fit, a.start_jump, a.start_jump_reps, a.start_jump_rows, a.start_jump_cyc);
        bool any_other = false;
        size_t seg = SIZE_MAX;  // first step after the last run of characters other than ACGT
        for (size_t t = 0; t < m; ++t) lat_agg.add(cyc[t], reps[t], walk[t], ff[t], rows[t]);
        for (size_t t = 0; t < m; ++t) {
            const size_t b = std::min(t, J);
            a.steps[b] += 1;
            a.reps[b] += reps[t];
            a.rows[b] += rows[t];
            a.cyc[b] += cyc[t];
            if (other(t)) {
                ++a.other_chars;
                any_other = true;
                if (t + 1 == m || !other(t + 1)) {
                    ++a.other_runs;
                    seg = t + 1;
                    if (seg < m)
                        window(seg, a.postn_fit, a.postn_jump, a.postn_jump_reps, a.postn_jump_rows,
                               a.postn_jump_cyc);
                }
                continue;
            }
            if (seg != SIZE_MAX) {
                const size_t u = std::min(t - seg, J);
                a.n_steps[u] += 1;
                a.n_reps[u] += reps[t];
                a.n_rows[u] += rows[t];
                a.n_cyc[u] += cyc[t];
            }
        }
        if (any_other) ++a.reads_other;
    }
};

inline thread_local StepRecorder step_rec;

/**
 * Measures one visit to step t: the repositions counted in *reps, the rows
 * in *rows_a + *rows_b, the LCE walk steps in *walk_a + *walk_b, the rows
 * crossed by fast-forwards (step_ff_rows), and the cycles, between its
 * construction and record(), which the destructor calls if nothing else has.
 */
struct StepProbe {
    size_t t;
    const step_ulint *reps, *rows_a, *rows_b, *walk_a, *walk_b;
    step_ulint r0, w0, k0, f0;
    uint64_t c0;
    bool done = false;
    StepProbe(size_t step, const step_ulint* r, const step_ulint* wa, const step_ulint* wb, const step_ulint* ka,
              const step_ulint* kb)
        : t(step), reps(r), rows_a(wa), rows_b(wb), walk_a(ka), walk_b(kb), r0(*r), w0(*wa + *wb),
          k0(*ka + *kb), f0(step_ff_rows), c0(step_ticks()) {}
    void record() {
        if (done) return;
        done = true;
        const uint64_t c1 = step_ticks();
        step_rec.add(t, *reps - r0, *rows_a + *rows_b - w0, c1 - c0, *walk_a + *walk_b - k0, step_ff_rows - f0);
    }
    ~StepProbe() { record(); }
};

/**
 * The cost of the probe itself: n probes around no work, through the same
 * recorder, in a histogram of their ticks.  Call it outside any read.
 */
inline LatAgg step_probe_calibration(size_t n = 1000000) {
    const std::string pat(n, 'A');
    std::vector<step_ulint> ms(n, 0);
    const step_ulint zero = 0;
    step_rec.begin(pat.data(), n, ms.data());
    for (size_t t = 0; t < n; ++t) {
        StepProbe probe(t, &zero, &zero, &zero, &zero, &zero);
    }
    LatAgg cal;
    for (size_t t = 0; t < n; ++t) cal.add(step_rec.cyc[t], 0, 0, 0, 0);
    step_rec.pending = false;
    return cal;
}

/**
 * What the operating system did to the query: interrupts on the CPUs the
 * process may run on, from /proc/interrupts, by source, and the process's
 * page faults and context switches, from getrusage.  query takes one
 * snapshot before the queries and one after, and prints the difference, so
 * that index loading is left out.  Empty off Linux.
 */
struct OsSnapshot {
    std::map<std::string, uint64_t> irq;
    long minflt = 0, majflt = 0, nvcsw = 0, nivcsw = 0;
    std::string cpus;

    static OsSnapshot take() {
        OsSnapshot o;
#ifdef __linux__
        cpu_set_t set;
        CPU_ZERO(&set);
        sched_getaffinity(0, sizeof(set), &set);
        for (int c = 0; c < CPU_SETSIZE; ++c)
            if (CPU_ISSET(c, &set)) o.cpus += (o.cpus.empty() ? "" : ",") + std::to_string(c);
        std::ifstream f("/proc/interrupts");
        std::string line;
        // The header names the online CPUs' columns.
        std::vector<int> cols;
        if (std::getline(f, line)) {
            std::istringstream h(line);
            std::string w;
            while (h >> w) cols.push_back(w.rfind("CPU", 0) == 0 ? std::stoi(w.substr(3)) : -1);
        }
        while (std::getline(f, line)) {
            std::istringstream l(line);
            std::string name;
            l >> name;
            if (name.empty() || name.back() != ':') continue;
            name.pop_back();
            uint64_t sum = 0;
            for (size_t i = 0; i < cols.size(); ++i) {
                uint64_t v;
                if (!(l >> v)) break;
                if (cols[i] >= 0 && CPU_ISSET(cols[i], &set)) sum += v;
            }
            o.irq[name] = sum;
        }
        rusage u{};
        getrusage(RUSAGE_SELF, &u);
        o.minflt = u.ru_minflt;
        o.majflt = u.ru_majflt;
        o.nvcsw = u.ru_nvcsw;
        o.nivcsw = u.ru_nivcsw;
#endif
        return o;
    }
    /** One line: the CPUs, total interrupts and each nonzero source, and the rusage counts, from a to b. */
    static void print_delta(std::ostream& os, const OsSnapshot& a, const OsSnapshot& b) {
        uint64_t total = 0;
        std::string detail;
        for (const auto& [name, v] : b.irq) {
            const auto it = a.irq.find(name);
            const uint64_t d = v - (it == a.irq.end() ? 0 : it->second);
            if (d == 0) continue;
            total += d;
            detail += (detail.empty() ? "" : ",") + name + ":" + std::to_string(d);
        }
        os << "lat-os: cpus=" << (b.cpus.empty() ? "-" : b.cpus) << " irq=" << total
           << " irq_by=" << (detail.empty() ? "-" : detail) << " minflt=" << b.minflt - a.minflt
           << " majflt=" << b.majflt - a.majflt << " nvcsw=" << b.nvcsw - a.nvcsw << " nivcsw=" << b.nivcsw - a.nivcsw
           << "\n";
    }
};

/**
 * Fast-forward lengths: for each move structure a query steps through, a
 * histogram of how many intervals each fast-forward crosses after a move,
 * that is, how far the landing interval lies past the one the pointer names.
 * A move resolved with exponential search (starts-based phi) reads about
 * 2 log2(k) + 2 interval starts to cross k intervals and a linear one k + 1,
 * so each histogram also sums the starts an exponential search would read
 * (exp_probes), which is the real count for starts-based phi and what it
 * would be for the others.  Counts are kept for every move, whatever
 * --interleave is.
 */
enum FFKind : int {
    FF_LF,          // LF steps
    FF_PSI,         // psi steps, over the same rows as LF in the psi and full layouts
    FF_PHI_LEN,     // phi steps of matching statistics, lengths-based (linear)
    FF_PHI_STARTS,  // phi steps of matching statistics, starts-based (exponential)
    FF_PHI_TOEHOLD, // the phi step from a pred candidate's next row head to its tail, either form
    FF_PHI_WALK,    // phi steps of SMEM position walks, starts-based (linear)
    FF_PHI_INV,     // phi_inv steps of SMEM position walks, starts-based (linear)
    FF_KINDS
};

inline const char* ff_kind_name(int k) {
    static const char* names[] = {"lf", "psi", "phi_len", "phi_starts", "phi_toehold", "phi_walk", "phi_inv"};
    return names[k];
}

/**
 * The interval starts fast_forward_exponential (move_structure_impl.hpp)
 * reads to cross k intervals, away from the table's end: one to test the
 * next start, then the doubling probes, then the binary search.
 */
inline step_ulint exp_probes(step_ulint k) {
    if (k == 0) return 1;
    step_ulint probes = 1, hi = 1;
    while (hi <= k) { ++probes; hi *= 2; }
    ++probes;  // the doubling probe that fails
    step_ulint lo = hi / 2;
    while (lo + 1 < hi) {
        ++probes;
        const step_ulint mid = lo + (hi - lo) / 2;
        if (k >= mid) lo = mid;
        else hi = mid;
    }
    return probes;
}

struct FFHist {
    // Crossings 0 to B - 1 are counted apart, longer ones in bucket B.
    static constexpr size_t B = 1024;
    std::array<step_ulint, B + 1> by_k{};
    std::array<step_ulint, 64> by_probes{};
    step_ulint moves = 0, crossed = 0, probes = 0, max = 0;

    void add(step_ulint k) {
        ++moves;
        crossed += k;
        max = std::max(max, k);
        ++by_k[std::min<step_ulint>(k, B)];
        const step_ulint p = exp_probes(k);
        probes += p;
        ++by_probes[std::min<step_ulint>(p, 63)];
    }
    FFHist& operator+=(const FFHist& o) {
        for (size_t i = 0; i <= B; ++i) by_k[i] += o.by_k[i];
        for (size_t i = 0; i < 64; ++i) by_probes[i] += o.by_probes[i];
        moves += o.moves; crossed += o.crossed; probes += o.probes; max = std::max(max, o.max);
        return *this;
    }
    /** The smallest k with at least fraction q of the moves crossing k or fewer. */
    step_ulint quantile(double q) const {
        const double want = q * double(moves);
        step_ulint acc = 0;
        for (size_t i = 0; i <= B; ++i) {
            acc += by_k[i];
            if (double(acc) >= want) return i;
        }
        return B;
    }
    step_ulint range(size_t lo, size_t hi) const {
        step_ulint s = 0;
        for (size_t i = lo; i <= std::min(hi, B); ++i) s += by_k[i];
        return s;
    }
};

struct FFAgg {
    std::array<FFHist, FF_KINDS> h;
    FFAgg& operator+=(const FFAgg& o) {
        for (int k = 0; k < FF_KINDS; ++k) h[k] += o.h[k];
        return *this;
    }
    /**
     * One line per structure with moves: the mean, p50, p99 and largest
     * crossing count, the shares crossing 0, 1, 2-3, 4-7 and 8 or more, the
     * mean starts read by linear (k + 1) and exponential search, and then
     * the histogram of crossings as k:count pairs.
     */
    void print(std::ostream& os) const {
        for (int k = 0; k < FF_KINDS; ++k) {
            const FFHist& x = h[k];
            if (x.moves == 0) continue;
            const double m = double(x.moves);
            os << "ffstats: kind=" << ff_kind_name(k) << " moves=" << x.moves << " mean=" << double(x.crossed) / m
               << " p50=" << x.quantile(0.5) << " p99=" << x.quantile(0.99) << " p999=" << x.quantile(0.999)
               << " max=" << x.max << " share0=" << double(x.range(0, 0)) / m
               << " share1=" << double(x.range(1, 1)) / m << " share2_3=" << double(x.range(2, 3)) / m
               << " share4_7=" << double(x.range(4, 7)) / m << " share8plus=" << double(x.range(8, FFHist::B)) / m
               << " linear_reads=" << double(x.crossed + x.moves) / m << " exp_reads=" << double(x.probes) / m
               << "\n";
            os << "ffhist: kind=" << ff_kind_name(k);
            for (size_t i = 0; i <= FFHist::B; ++i)
                if (x.by_k[i]) os << ' ' << i << (i == FFHist::B ? "+" : "") << ':' << x.by_k[i];
            os << "\n";
        }
    }
};

inline thread_local FFAgg ff_agg;
#define MS_FF(kind, k) (ff_agg.h[(kind)].add(static_cast<step_ulint>(k)), step_ff_rows += static_cast<step_ulint>(k))

#endif /* MS_STEP_STATS */

#ifndef MS_FF
#define MS_FF(kind, k) ((void)0)
#endif

#endif /* _STEP_STATS_HPP */
