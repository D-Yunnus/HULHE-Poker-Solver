#include <fstream>
#include <filesystem>
#include <string>
#include "metrics/histogram.h"
#include "evaluation/suit_isomorphism.h"
#include <chrono>
#include <iomanip>
#include <iostream>
#include <algorithm>

using namespace pkrmet;

void csv_histograms()
{
    for (int count = 200; count < 250; ++count)
    {
        const std::filesystem::path filePath =
            "data/histograms/river_histograms/river_equity_histogram_" + std::to_string(count + 1) + ".csv";
        std::filesystem::create_directories(filePath.parent_path());

        std::ofstream csvFile(filePath);

        csvFile << "index";
        for (int bin = 0; bin < 3; ++bin)
        {
           csvFile << ",bin_" << bin;
        }
        csvFile << '\n' << std::setprecision(6);

        const int boardStartIndex = NUM_RIVER_BOARDS * 1LL * count / 250;
        const int boardEndIndex = std::min(NUM_RIVER_BOARDS * 1LL * (count + 1) / 250, NUM_RIVER_BOARDS * 1LL);
        constexpr int batchSize = NUM_RIVER_BOARDS / NUM_BATCHES;
        for (int batchStart = boardStartIndex; batchStart < boardEndIndex; batchStart += batchSize)
        {
            const int batchLocalIndex = std::min(batchSize, boardEndIndex - batchStart);
            std::array<std::array<double, 3>, batchSize> histograms{};

            #pragma omp parallel for schedule(static)
            for (int offset = 0; offset < batchLocalIndex; ++offset)
            {
                const int i = batchStart + offset;
                const std::array<uint32_t, HOLE_SIZE + RIVER_SIZE> cards = unindex<RIVER_SIZE>(i);
                const std::array<uint32_t, HOLE_SIZE> holeCards = {cards[0], cards[1]};
                const std::array<uint32_t, RIVER_SIZE> riverCards = {cards[2], cards[3], cards[4], cards[5], cards[6]};
                histograms[offset] = compute_equity_histogram_river(holeCards, riverCards);
            }

            for (int offset = 0; offset < batchLocalIndex; ++offset)
            {
                const int i = batchStart + offset;
                csvFile << i;
                for (int bin = 0; bin < 3; ++bin)
                {
                    csvFile << ',' << histograms[offset][bin];
                }
                csvFile << '\n';
            }
        }
    }
}

int main()
{
    auto start = std::chrono::high_resolution_clock::now();
    csv_histograms();

    auto end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> elapsed_seconds = end - start;
    std::cout << "Elapsed time: " << elapsed_seconds.count() << " s\n";

    return 0;
}