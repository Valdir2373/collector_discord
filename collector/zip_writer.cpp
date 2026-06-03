#include "zip_writer.h"

// ── CRC-32 ────────────────────────────────────────────────────────────────────
static uint32_t s_crc_table[256];
static bool     s_crc_ready = false;

static void init_crc_table() {
    if (s_crc_ready) return;
    for (uint32_t i = 0; i < 256; ++i) {
        uint32_t c = i;
        for (int j = 0; j < 8; ++j)
            c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
        s_crc_table[i] = c;
    }
    s_crc_ready = true;
}

uint32_t ZipWriter::compute_crc32(const uint8_t* data, size_t len) {
    init_crc_table();
    uint32_t c = 0xFFFFFFFFu;
    for (size_t i = 0; i < len; ++i)
        c = s_crc_table[(c ^ data[i]) & 0xFF] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}

// ── Helpers ───────────────────────────────────────────────────────────────────
void ZipWriter::write_u16(std::vector<uint8_t>& buf, uint16_t v) {
    buf.push_back(v & 0xFF);
    buf.push_back((v >> 8) & 0xFF);
}
void ZipWriter::write_u32(std::vector<uint8_t>& buf, uint32_t v) {
    buf.push_back(v & 0xFF);
    buf.push_back((v >> 8) & 0xFF);
    buf.push_back((v >> 16) & 0xFF);
    buf.push_back((v >> 24) & 0xFF);
}

// ── Public API ────────────────────────────────────────────────────────────────
void ZipWriter::add_file(const std::string& name, const std::string& content) {
    std::vector<uint8_t> data(content.begin(), content.end());
    add_file(name, data);
}

void ZipWriter::add_file(const std::string& name, const std::vector<uint8_t>& data) {
    ZipEntry e;
    e.name   = name;
    e.data   = data;
    e.crc    = compute_crc32(data.data(), data.size());
    e.offset = 0;
    entries_.push_back(e);
}

bool ZipWriter::save(const std::string& path) {
    SYSTEMTIME st;
    GetLocalTime(&st);
    uint16_t dos_time = (uint16_t)((st.wHour << 11) | (st.wMinute << 5) | (st.wSecond / 2));
    uint16_t dos_date = (uint16_t)(((st.wYear - 1980) << 9) | (st.wMonth << 5) | st.wDay);

    std::vector<uint8_t> buf;

    // ── Local file headers + data ─────────────────────────────────────────────
    for (auto& e : entries_) {
        e.offset = (uint32_t)buf.size();
        write_u32(buf, 0x04034b50u);                     // signature
        write_u16(buf, 20);                              // version needed
        write_u16(buf, 0);                               // flags
        write_u16(buf, 0);                               // method: stored
        write_u16(buf, dos_time);
        write_u16(buf, dos_date);
        write_u32(buf, e.crc);
        write_u32(buf, (uint32_t)e.data.size());         // compressed size
        write_u32(buf, (uint32_t)e.data.size());         // uncompressed size
        write_u16(buf, (uint16_t)e.name.size());
        write_u16(buf, 0);                               // extra field len
        for (char c : e.name)  buf.push_back((uint8_t)c);
        buf.insert(buf.end(), e.data.begin(), e.data.end());
    }

    uint32_t cd_offset = (uint32_t)buf.size();

    // ── Central directory ─────────────────────────────────────────────────────
    for (const auto& e : entries_) {
        write_u32(buf, 0x02014b50u);
        write_u16(buf, 0x0314);                          // version made by (Windows 2.0)
        write_u16(buf, 20);
        write_u16(buf, 0);
        write_u16(buf, 0);
        write_u16(buf, dos_time);
        write_u16(buf, dos_date);
        write_u32(buf, e.crc);
        write_u32(buf, (uint32_t)e.data.size());
        write_u32(buf, (uint32_t)e.data.size());
        write_u16(buf, (uint16_t)e.name.size());
        write_u16(buf, 0); write_u16(buf, 0); write_u16(buf, 0);
        write_u16(buf, 0); write_u32(buf, 0); write_u32(buf, e.offset);
        for (char c : e.name) buf.push_back((uint8_t)c);
    }

    uint32_t cd_size = (uint32_t)buf.size() - cd_offset;

    // ── End-of-central-directory ──────────────────────────────────────────────
    write_u32(buf, 0x06054b50u);
    write_u16(buf, 0); write_u16(buf, 0);
    write_u16(buf, (uint16_t)entries_.size());
    write_u16(buf, (uint16_t)entries_.size());
    write_u32(buf, cd_size);
    write_u32(buf, cd_offset);
    write_u16(buf, 0);

    std::ofstream f(path, std::ios::binary);
    if (!f) return false;
    f.write(reinterpret_cast<const char*>(buf.data()), (std::streamsize)buf.size());
    return f.good();
}
