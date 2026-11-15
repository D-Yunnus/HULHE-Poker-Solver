#ifndef LOADER_H
#define LOADER_H

#include "cfr/betting.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>

#ifndef POKER_NODEMAP_PATH
#define POKER_NODEMAP_PATH "./build/generated/nodemap.bin"
#endif

inline void load_trained_poker_maps
(
    const std::string& filePath = {}
)
{
    const std::filesystem::path path = filePath.empty()
        ? std::filesystem::path(POKER_NODEMAP_PATH)
        : std::filesystem::path(filePath);

    std::ifstream input(path, std::ios::binary);

    std::uint64_t count = 0;
    input.read(reinterpret_cast<char*>(&count), sizeof(count));

    NODEMAP.clear();
    NODEMAP.reserve(static_cast<std::size_t>(count));

    for (std::uint64_t i = 0; i < count; ++i)
    {
        std::uint64_t key = 0;
        input.read(reinterpret_cast<char*>(&key), sizeof(key));

        Node node {};
        for (std::size_t j = 0; j < node.regretSum.size(); ++j)
        {
            double value = 0.0;
            input.read(reinterpret_cast<char*>(&value), sizeof(value));
            node.regretSum[j] = value;
        }

        for (std::size_t j = 0; j < node.stratSum.size(); ++j)
        {
            double value = 0.0;
            input.read(reinterpret_cast<char*>(&value), sizeof(value));
            node.stratSum[j] = value;
        }

        NODEMAP.emplace(key, node);
    }
}

#endif
