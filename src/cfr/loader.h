#ifndef LOADER_H
#define LOADER_H


#include <array>
#include <stdexcept>
#include <string>

#ifndef POKER_NODEMAP_PATH
#define POKER_NODEMAP_PATH "./build/generated/nodemap.bin" // pathway for binary file storing the infoset nodemap of strategies
#endif

// read the binary file containg nodemap and store onto NODEMAP
inline void load_trained_poker_maps()
{
    const std::filesystem::path path = POKER_NODEMAP_PATH;

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
