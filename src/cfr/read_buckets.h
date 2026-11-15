#ifndef READ_BUCKET_H
#define READ_BUCKET_H

#include <fstream>
#include <iostream>
#include <filesystem>
#include <array>
#include <string>
#include <sstream>

#include "constants/constants.h"
#include "cfr/betting.h"

// load csv bucket csv files
template <std::size_t BOARD_NUM>
std::array<int, BOARD_NUM> get_buckets()
{
    // the csv files directory for each street
    std::filesystem::path path {};
    if (BOARD_NUM == NUM_FLOP_BOARDS)
        path = "data/abstractions/flop_buckets.csv";
    if (BOARD_NUM == NUM_TURN_BOARDS)
        path = "data/abstractions/turn_buckets.csv";
    if (BOARD_NUM == NUM_RIVER_BOARDS)
        path = "data/abstractions/river_buckets.csv";
    std::ifstream file(path);

    // skip the first line
    std::string line;
    std::getline(file, line);

    std::array<int, BOARD_NUM> bucketList {};
    // enumerate over each card index of a street
    for (int i = 0; i < BOARD_NUM; ++i)
    {
        // get csv row
        std::getline(file, line);
        // stream all characters in this row
        std::stringstream s(line);
        // parse the card index and the associated bucket
        std::string idx {};
        std::string bucket {};
        std::getline(s, idx, ',');
        std::getline(s, bucket, ',');

        // fill list
        bucketList[i] = std::stoi(bucket);

    }

    return bucketList;
}

#ifndef BUCKETS
#define BUCKETS

const std::array<int, NUM_FLOP_BOARDS> FLOP_BUCKETS = get_buckets<NUM_FLOP_BOARDS>();
const std::array<int, NUM_TURN_BOARDS> TURN_BUCKETS = get_buckets<NUM_TURN_BOARDS>();
const std::array<int, NUM_RIVER_BOARDS> RIVER_BUCKETS = get_buckets<NUM_RIVER_BOARDS>();

#endif

#endif