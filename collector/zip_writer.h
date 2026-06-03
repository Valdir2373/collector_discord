#pragma once
#include "pch.h"

struct ZipEntry {
    std::string name;
    std::vector<uint8_t> data;
    uint32_t crc;
    uint32_t offset;
};

class ZipWriter {
public:
    void add_file(const std::string& name, const std::string& content);
    void add_file(const std::string& name, const std::vector<uint8_t>& data);
    bool save(const std::string& path);

private:
    std::vector<ZipEntry> entries_;
    static uint32_t compute_crc32(const uint8_t* data, size_t len);
    static void write_u16(std::vector<uint8_t>& buf, uint16_t v);
    static void write_u32(std::vector<uint8_t>& buf, uint32_t v);
};
