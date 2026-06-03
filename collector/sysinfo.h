#pragma once
#include "pch.h"

struct SystemInfo {
    std::string hostname;
    std::string username;
    std::string os_version;
    std::string ip_raw;   // JSON bruto de ip-api.com
};

SystemInfo    collect_sysinfo();
std::string   sysinfo_to_text(const SystemInfo& info);
