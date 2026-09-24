/**
 * Serialization for the compressed-LCP MS index.
 *
 * Binary format: the 4-byte magic "MSIX", a 4-byte version (currently 4),
 * then MSIndexSpillLCP::serialize, which stores the Orbit move structure
 * (including the integrated LCP columns) in Orbit's packed serialization,
 * followed by the scalar settings and the spillover arrays.  Integers are in
 * host byte order.
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
static constexpr uint32_t VERSION = 4;

/**
 * Write the index to a single binary file.  Loading needs no rebuild, and the
 * file is about the size of the index in memory.  Order of serialized
 * components:
 *   1. 4-byte magic string ("MSIX")
 *   2. 4-byte version integer (uint32_t)
 *   3. The index, as MSIndexSpillLCP::serialize writes it
 */
bool write_index(const std::string& path, MSIndexSpillLCP<false>& index) {
    std::ofstream out(path, std::ios::binary);
    if (!out.good()) return false;
    // 1. Magic string
    out.write(MAGIC, 4);
    // 2. Version
    uint32_t v = VERSION;
    out.write(reinterpret_cast<const char*>(&v), sizeof(v));
    // 3. The index
    index.serialize(out);
    return out.good();
}

/**
 * Read an index written by write_index, whose comment gives the order of its
 * components.  Returns nullopt if the file cannot be opened, is not an MS
 * index, has another version, or is malformed.
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
    if (!in.good() || v != VERSION) return std::nullopt;

    // 3. The index
    MSIndexSpillLCP<false> idx;
    try {
        idx.load(in);
    } catch (const std::exception&) {
        return std::nullopt;
    }
    return idx;
}

}  // namespace ms_serialize
