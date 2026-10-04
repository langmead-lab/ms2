/**
 * Serialization implementation for our compressed-LCP MS index, along with
 * the spillover vector(s).
 * 
 * Binary format: MAGIC "MSIX", version 1: r, bwt_heads, bwt_run_lengths,
 * run_data, spillover, max_lcp_top, max_lcp_min_sub.
 * 
 * VERSION 2 adds: spill_align (ulint), raw alignment value (0=none).
 * VERSION 3 adds: spill_split_bits (uchar), then 2^spill_split_bits spillover arrays.
 *
 * Author: Ben Langmead (langmea@cs.jhu.edu)
 * Date: Feb 17, 2026
 */

#include "serialize.hpp"
#include "ms_rlbwt.hpp"
#include <fstream>
#include <cstring>

namespace ms_serialize {

static const char MAGIC[] = "MSIX";
static constexpr uint32_t VERSION = 3;

/**
 * Write the index to a single binary file.  Order of serialized components:
 *   1. 4-byte magic string ("MSIX")
 *   2. 4-byte version integer (uint32_t)
 *   3. Number of runs (ulint, r)
 *   4. BWT head characters, length r (uchar[])
 *   5. BWT run lengths, length r (ulint[])
 *   6. Per-run LCP/run data table, r x 3 (ulint[r][3])
 *   7. spill_split_bits (uchar) [V3]
 *   8. For each of 2^spill_split_bits arrays: (length ulint, bytes uchar[])
 *   9. max_lcp_top (ulint)
 *  10. max_lcp_min_sub (ulint)
 *  11. spill_align (ulint) [VERSION>=2]
 */
bool write_index(const std::string& path, MSIndexSpillLCP<false>& index) {
    std::ofstream out(path, std::ios::binary);
    if (!out.good()) return false;

    // 1. Magic string
    out.write(MAGIC, 4);
    // 2. Version
    uint32_t v = VERSION;
    out.write(reinterpret_cast<const char*>(&v), sizeof(v));

    // 3. Number of runs
    const ulint r = index.move_runs();
    out.write(reinterpret_cast<const char*>(&r), sizeof(r));

    std::vector<uchar> chars(r);
    std::vector<ulint> lens(r);
    std::vector<std::array<ulint, 3>> run_data(r);
    for (ulint i = 0; i < r; ++i) {
        chars[i] = index.get_character(i);
        lens[i] = index.get_length(i);
        run_data[i][0] = index.template get<LCPSpillRunCols::LCP_TOP>(i);
        run_data[i][1] = index.template get<LCPSpillRunCols::LCP_MIN_SUB>(i);
        run_data[i][2] = index.template get<LCPSpillRunCols::LCP_SPILL>(i);
    }

    // 4. BWT head characters
    for (ulint i = 0; i < r; ++i)
        out.write(reinterpret_cast<const char*>(&chars[i]), sizeof(uchar));
    // 5. BWT run lengths
    out.write(reinterpret_cast<const char*>(lens.data()), static_cast<std::streamsize>(r * sizeof(ulint)));
    // 6. Per-run LCP/run data table
    out.write(reinterpret_cast<const char*>(run_data.data()), static_cast<std::streamsize>(r * 3 * sizeof(ulint)));

    // 7. spill_split_bits
    uchar spill_split_bits = index.spill_split_bits();
    out.write(reinterpret_cast<const char*>(&spill_split_bits), sizeof(spill_split_bits));
    const size_t num_arrays = (spill_split_bits > 0) ? (size_t{1} << spill_split_bits) : 1;
    // 8. Spillover arrays, each as its length and then its bytes
    const auto& spill_vecs = index.spillover_vectors();
    for (size_t b = 0; b < num_arrays; ++b) {
        ulint spill_len = static_cast<ulint>(spill_vecs[b].size());
        out.write(reinterpret_cast<const char*>(&spill_len), sizeof(spill_len));
        out.write(reinterpret_cast<const char*>(spill_vecs[b].data()), static_cast<std::streamsize>(spill_vecs[b].size()));
    }

    // 9. max_lcp_top, then 10. max_lcp_min_sub
    ulint max_top = index.max_lcp_top();
    ulint max_sub = index.max_lcp_min_sub();
    out.write(reinterpret_cast<const char*>(&max_top), sizeof(max_top));
    out.write(reinterpret_cast<const char*>(&max_sub), sizeof(max_sub));

    // 11. spill_align
    ulint spill_align = index.spill_align();
    out.write(reinterpret_cast<const char*>(&spill_align), sizeof(spill_align));

    return out.good();
}

/**
 * Read the index from a single binary file.  See above comment for order.
 */
std::optional<MSIndexSpillLCP<false>> read_index(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in.good()) return std::nullopt;

    // 1. Magic string
    char magic[5] = {};
    in.read(magic, 4);
    if (std::strncmp(magic, MAGIC, 4) != 0) return std::nullopt;

    // 2. Version
    uint32_t v = 0;
    in.read(reinterpret_cast<char*>(&v), sizeof(v));
    if (v != 1 && v != 2 && v != 3) return std::nullopt;

    // 3. Number of runs
    ulint r = 0;
    in.read(reinterpret_cast<char*>(&r), sizeof(r));
    if (r == 0) return std::nullopt;

    std::vector<uchar> chars(r);
    std::vector<ulint> lens(r);
    std::vector<std::array<ulint, 3>> run_data(r);

    // 4. BWT head characters
    in.read(reinterpret_cast<char*>(chars.data()), static_cast<std::streamsize>(r * sizeof(uchar)));
    // 5. BWT run lengths
    in.read(reinterpret_cast<char*>(lens.data()), static_cast<std::streamsize>(r * sizeof(ulint)));
    // 6. Per-run LCP/run data table
    in.read(reinterpret_cast<char*>(run_data.data()), static_cast<std::streamsize>(r * 3 * sizeof(ulint)));

    if (!in.good()) return std::nullopt;

    std::vector<SpilloverVector> spill_vectors;
    uchar spill_split_bits = 0;
    if (v >= 3) {
        // 7. spill_split_bits, then 8. its spillover arrays
        in.read(reinterpret_cast<char*>(&spill_split_bits), sizeof(spill_split_bits));
        const size_t num_arrays = (spill_split_bits > 0) ? (size_t{1} << spill_split_bits) : 1;
        spill_vectors.resize(num_arrays);
        for (size_t b = 0; b < num_arrays; ++b) {
            ulint spill_len = 0;
            in.read(reinterpret_cast<char*>(&spill_len), sizeof(spill_len));
            spill_vectors[b].resize(spill_len);
            in.read(reinterpret_cast<char*>(spill_vectors[b].data()), static_cast<std::streamsize>(spill_len));
        }
    } else {
        // 8. Before version 3, a single spillover array and no item 7
        ulint spill_len = 0;
        in.read(reinterpret_cast<char*>(&spill_len), sizeof(spill_len));
        SpilloverVector spill(spill_len);
        in.read(reinterpret_cast<char*>(spill.data()), static_cast<std::streamsize>(spill_len));
        spill_vectors.push_back(std::move(spill));
    }

    // 9. max_lcp_top, then 10. max_lcp_min_sub
    ulint max_top = 0, max_sub = 0;
    in.read(reinterpret_cast<char*>(&max_top), sizeof(max_top));
    in.read(reinterpret_cast<char*>(&max_sub), sizeof(max_sub));

    // 11. spill_align, present from version 2
    ulint spill_align = 1;  // 1 = no alignment (stored value = raw offset)
    if (v >= 2) {
        in.read(reinterpret_cast<char*>(&spill_align), sizeof(spill_align));
    }

    if (!in.good()) return std::nullopt;

    std::vector<std::array<ulint, static_cast<size_t>(LCPSpillRunCols::COUNT)>> rd(r);
    for (ulint i = 0; i < r; ++i)
        for (size_t j = 0; j < 3; ++j) rd[i][j] = run_data[i][j];

    return MSIndexSpillLCP<false>(chars, lens, rd, std::move(spill_vectors), max_top, max_sub, spill_align, spill_split_bits);
}

}  // namespace ms_serialize
