#include <fstream>
#include <filesystem>
#include <string>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <algorithm>

#include "metrics/histogram.h"
#include "evaluation/suit_isomorphism.h"

void csv_histograms
(
    const int street,
    const int numFiles
)
{
    for (int count = 0; count < numFiles; ++count)
    {
        // csv file paths containing equity histograms data
        std::filesystem::path filePath {};
        if (street == 0)
            filePath =
                "data/histograms/flop_histograms/flop_equity_histogram_" + std::to_string(count + 1) + ".csv";
        if (street == 1)
            filePath =
                "data/histograms/turn_histograms/turn_equity_histogram_" + std::to_string(count + 1) + ".csv";
        if (street == 2)
            filePath =
                "data/histograms/turn_histograms/turn_equity_histogram_" + std::to_string(count + 1) + ".csv";
        std::filesystem::create_directories(filePath.parent_path());

        // first row titles
        std::ofstream csvFile(filePath);
        csvFile << "index";
        for (int bin = 0; bin < 3; ++bin)
        {
           csvFile << ",bin_" << bin;
        }
        csvFile << '\n' << std::setprecision(6);

        // index of the first histogram in a csv batch and the last
        const int boardStartIndex = NUM_RIVER_BOARDS * 1ULL * count / numFiles;
        const int boardEndIndex = std::min(NUM_RIVER_BOARDS * 1ULL * (count + 1) / numFiles, NUM_RIVER_BOARDS * 1ULL);
        constexpr int batchSize = NUM_RIVER_BOARDS / NUM_BATCHES;
        for (int batchStart = boardStartIndex; batchStart < boardEndIndex; batchStart += batchSize)
        {
            const int batchLocalIndex = std::min(batchSize, boardEndIndex - batchStart); // localised index within batches
            // compute equity histogram for each index
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

            // write histogram to csv file
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

    csv_histograms(0, NUM_CSV_FLOP);
    csv_histograms(1, NUM_CSV_TURN);
    csv_histograms(2, NUM_CSV_RIVER);

    auto end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> elapsed_seconds = end - start;
    std::cout << "Elapsed time: " << elapsed_seconds.count() << " s\n";

    return 0;
}