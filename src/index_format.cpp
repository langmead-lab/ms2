/**
 * ORBITIDX container: header description and CRC verification (see
 * index_format.hpp).
 */

#include "index_format.hpp"
#include <filesystem>
#include <fstream>
#include <iomanip>

namespace orbit_idx {

Header read_header_file(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in.good()) throw std::runtime_error("cannot open " + path);
    std::vector<char> b(HEADER_BYTES);
    in.read(b.data(), HEADER_BYTES);
    if (in.gcount() < 8 || !has_magic(b.data(), 8)) throw std::runtime_error(path + " is not an Orbit index (no ORBITIDX magic)");
    if (!in.good()) throw std::runtime_error("truncated Orbit index header in " + path);
    return decode_header(b.data());
}

namespace {

std::string mb(uint64_t bytes) {
    std::ostringstream os;
    os << std::fixed << std::setprecision(bytes < 10'000'000 ? 3 : 1) << bytes / 1e6 << " MB";
    return os.str();
}

const char* kind_name(Kind k) {
    switch (k) {
        case Kind::MOVE_TABLE: return "move_table";
        case Kind::U64_ARRAY: return "u64_array";
        case Kind::SPILL_ARRAYS: return "spill_arrays";
    }
    return "?";
}

}  // namespace

void describe_header(const Header& h, std::ostream& os) {
    os << "format: ORBITIDX v" << FORMAT_VERSION << "  tool: " << (h.tool == Tool::MS ? "ms" : "tms")
       << "  layout: " << layout_name(h.layout) << "  n=" << h.n << "  bwt_runs=" << h.bwt_runs << "\n";
    if (h.tool == Tool::MS)
        os << "scalars: max_lcp_top=" << h.scalars[0] << " max_lcp_min_sub=" << h.scalars[1]
           << " spill_align=" << h.scalars[2] << " spill_split_bits=" << h.scalars[3] << "\n";
    os << "sections:\n";
    for (const auto& e : h.sections) {
        os << "  " << std::left << std::setw(10) << section_name(e.id) << std::right << " " << kind_name(e.kind)
           << " offset=" << e.offset << " bytes=" << e.bytes << " crc=" << std::hex << std::setw(8)
           << std::setfill('0') << e.crc << std::dec << std::setfill(' ');
        if (e.kind == Kind::MOVE_TABLE) {
            os << " rows=" << e.rows << " domain=" << e.domain << " row_bits=" << e.row_bits << " cols=";
            for (size_t c = 0; c < e.cols.size(); ++c)
                os << (c ? "," : "") << col_name(e.cols[c].first) << ":" << int(e.cols[c].second);
        } else {
            os << " " << (e.kind == Kind::U64_ARRAY ? "values=" : "arrays=") << e.rows;
        }
        os << "\n";
    }
    if (!h.info.empty()) {
        os << "info:\n";
        std::istringstream is(h.info);
        for (std::string line; std::getline(is, line);) os << "  " << line << "\n";
    }
}

std::string verify_file(const std::string& path) {
    try {
        const Header h = read_header_file(path);
        std::ifstream in(path, std::ios::binary);
        std::vector<char> buf(1 << 22);
        std::string bad;
        for (const auto& e : h.sections) {
            in.seekg(static_cast<std::streamoff>(e.offset));
            uint32_t crc = 0;
            for (uint64_t left = e.bytes; left > 0;) {
                const auto k = static_cast<std::streamsize>(std::min<uint64_t>(left, buf.size()));
                in.read(buf.data(), k);
                if (in.gcount() != k) return std::string("section ") + section_name(e.id) + " is truncated";
                crc = crc32(crc, buf.data(), static_cast<size_t>(k));
                left -= static_cast<uint64_t>(k);
            }
            if (crc != e.crc) bad += std::string(bad.empty() ? "" : "; ") + "section " + section_name(e.id) + " CRC mismatch";
        }
        const uint64_t end = h.sections.empty() ? HEADER_BYTES : h.sections.back().offset + h.sections.back().bytes;
        const uint64_t size = std::filesystem::file_size(path);
        if (size != end)
            bad += std::string(bad.empty() ? "" : "; ") + "file is " + std::to_string(size) + " bytes, sections end at " +
                   std::to_string(end);
        return bad;
    } catch (const std::exception& e) {
        return e.what();
    }
}

}  // namespace orbit_idx
