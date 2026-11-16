#include "cfr/cfr.h"

#include <chrono>
#include <array>
#include <fstream>
#include <iostream>
#include <string>

void trainer
(
    const int iterations
)
{
    // loads HISTORIES
    load_histories();
    NODEMAP.reserve(4'200'000);
    
    // start timer
    const auto start = std::chrono::high_resolution_clock::now();

    #pragma omp parallel for schedule(dynamic, 16)
    for (int i = 0; i < iterations; ++i)
    {
        std::mt19937& randGen = rng();
        std::vector<uint32_t> deck = construct_deck(std::array<uint32_t, 0> {});

        // chance node:

        // deal small blind hole cards
        std::array<uint32_t, HOLE_SIZE + RIVER_SIZE> sbPlayer {};
        sbPlayer[0] = draw_card(deck, randGen);
        sbPlayer[1] = draw_card(deck, randGen);
            
        // deal big blind hole
        std::array<uint32_t, HOLE_SIZE + RIVER_SIZE> bbPlayer {};
        bbPlayer[0] = draw_card(deck, randGen);
        bbPlayer[1] = draw_card(deck, randGen);

        // initial history
        History history {};

        // cfr algorithm
        cfr_min(sbPlayer, bbPlayer, history, 1.0, 1.0, nullptr, nullptr, nullptr);
    }

    // stop timer
    const auto end = std::chrono::high_resolution_clock::now();
    
    // output training time
    const std::chrono::duration<double> duration = end - start;
    std::cout << "\tTraining time: " << duration.count() << '\n';
}

// write to a binary file
void write_generated_binary
(
    const std::string& outputPath
)
{
    std::ofstream output(outputPath, std::ios::binary);

    const uint64_t numGameStates = static_cast<uint64_t>(NODEMAP.size());
    output.write(reinterpret_cast<const char*>(&numGameStates), sizeof(numGameStates));

    for (const auto& [idx, source] : NODEMAP)
    {
        output.write(reinterpret_cast<const char*>(&idx), sizeof(idx));

        for (const double regrets : source.regretSum)
            output.write(reinterpret_cast<const char*>(&regrets), sizeof(regrets));

        for (const double num : source.stratSum)
            output.write(reinterpret_cast<const char*>(&num), sizeof(num));
    }
}

int main(int argc, char** argv)
{
    try
    {
        const int iterations = std::stoi(argv[1]);

        NODEMAP.clear();
        trainer(iterations);
        write_generated_binary(argv[2]);

        std::cout << "\tTrained nodes: " << NODEMAP.size() << '\n';
    }
    catch (const std::exception& error)
    {
        std::cerr << "Map generation failed: " << error.what() << '\n';
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

