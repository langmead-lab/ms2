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
        case Kind::SEQ_TABLE: return "seq_table";
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
            os << " " << (e.kind == Kind::U64_ARRAY ? "values=" : e.kind == Kind::SEQ_TABLE ? "sequences=" : "arrays=")
               << e.rows;
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

void add_section(const std::string& from, const std::string& to, SectionSource& s, const std::string& info) {
    if (std::filesystem::exists(to) && std::filesystem::equivalent(from, to))
        throw std::runtime_error("the output must be another file than the input");
    std::ifstream in(from, std::ios::binary);
    if (!in.good()) throw std::runtime_error("cannot open " + from);
    Header h = read_header_file(from);
    std::vector<SectionEntry> kept;
    for (const auto& e : h.sections)
        if (e.id != s.entry.id) kept.push_back(e);
    // Size and checksum the new payload, then lay out every section.
    detail::CrcCountBuf sink;
    {
        std::ostream os(&sink);
        s.write(os);
    }
    std::vector<uint64_t> from_offsets;
    uint64_t at = HEADER_BYTES;
    for (auto& e : kept) {
        from_offsets.push_back(e.offset);
        e.offset = align_up(at);
        at = e.offset + e.bytes;
    }
    SectionEntry added = s.entry;
    added.offset = align_up(at);
    added.bytes = sink.count;
    added.crc = sink.crc;
    Header out_h = h;
    out_h.sections = kept;
    out_h.sections.push_back(added);
    out_h.info += info;
    const std::vector<char> hb = encode_header(out_h);
    std::ofstream out(to, std::ios::binary | std::ios::trunc);
    if (!out.good()) throw std::runtime_error("cannot open " + to + " for writing");
    out.write(hb.data(), static_cast<std::streamsize>(hb.size()));
    uint64_t pos = HEADER_BYTES;
    std::vector<char> buf(size_t(1) << 22);
    for (size_t i = 0; i < kept.size(); ++i) {
        const SectionEntry& e = kept[i];
        detail::write_zeros(out, e.offset - pos);
        in.seekg(static_cast<std::streamoff>(from_offsets[i]));
        uint32_t crc = 0;
        for (uint64_t left = e.bytes; left > 0;) {
            const size_t k = static_cast<size_t>(std::min<uint64_t>(left, buf.size()));
            in.read(buf.data(), static_cast<std::streamsize>(k));
            if (!in.good()) throw std::runtime_error(from + ": section " + section_name(e.id) + " is truncated");
            crc = crc32(crc, buf.data(), k);
            out.write(buf.data(), static_cast<std::streamsize>(k));
            left -= k;
        }
        if (crc != e.crc) throw std::runtime_error(from + ": section " + section_name(e.id) + " fails its CRC-32");
        pos = e.offset + e.bytes;
    }
    detail::write_zeros(out, added.offset - pos);
    detail::CrcCountBuf tee(out.rdbuf());
    {
        std::ostream os(&tee);
        s.write(os);
        os.flush();
    }
    if (tee.count != added.bytes || tee.crc != added.crc)
        throw std::logic_error(std::string("section ") + section_name(added.id) + " serialized differently twice");
    out.flush();
    if (!out.good()) throw std::runtime_error("failed writing " + to);
}

}  // namespace orbit_idx
