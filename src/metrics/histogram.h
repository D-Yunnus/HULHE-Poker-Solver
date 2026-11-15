#include "evaluation/hand_evaluation.h"
#include "metrics/equity.h"
#include "constants/helper.h"

#include <cstdint>
#include <array>
#include <vector>
#include <cmath>
#include <algorithm>
#include <string>

#ifndef EQUITY_HISTOGRAM_H
#define EQUITY_HISTOGRAM_H

// construct the equity histogram
template <const std::size_t BINNUMS, const std::size_t STREETSIZE>
std::array<double, BINNUMS> compute_equity_histogram
(
    const std::array<uint32_t, HOLE_SIZE>& holes,
    const std::array<uint32_t, STREETSIZE>& street
)
{

    static_assert((STREETSIZE == TURN_SIZE || STREETSIZE == FLOP_SIZE || STREETSIZE == 0), "invalid input (street) array size");

    std::array<uint32_t, STREETSIZE + 1> nextStreet {};
    for (std::size_t i = 0; i < STREETSIZE; ++i)
    {
        nextStreet[i] = street[i];
        nextStreet[i] = street[i];
    }

    // construct a rank mask for all ranks present for each suit
    std::array<uint32_t, NUM_SUITS> suitRankMask {};
    for (uint32_t card: holes)
    {
        const int suit = __builtin_ctz(card & 0x1E000u) - NUM_RANKS;
        suitRankMask[suit] |= (card &  0x1FFFu);
    }
    for (uint32_t card: street)
    {
        const int suit = __builtin_ctz(card & 0x1E000u) - NUM_RANKS;
        suitRankMask[suit] |= (card &  0x1FFFu);
    }
    
    // equity falls in interval [0,1]
    // divide this interval into bins
    std::array<double, BINNUMS> histogram {};

    // enumerate over drawing one additional community card
    for (int i = 0; i < NUM_CARDS; ++i)
    {
        const int suit = i / 13;
        const int rank = i % 13;
        const uint32_t card = bitwise_card(static_cast<Ranks>(rank), static_cast<Suits>(suit));
        
        // ignore any cards already known by p1
        if ((card & suitRankMask[suit]) != 0) continue;

        // new street from drawing one additional community card
        nextStreet[STREETSIZE] = card;

        // compute equity
        const double equity = compute_equity_against_rand(holes, nextStreet);

        // identify the bin the equity falls into
        std::size_t binIndex = static_cast<std::size_t>(equity * static_cast<double>(BINNUMS));
        // handle edge case where equity == 1.0
        if (binIndex >= BINNUMS) binIndex = BINNUMS - 1;

        // increment each bin and normalise over all possible additional card combinations
        histogram[binIndex] += 1.0 / (52 - HOLE_SIZE - STREETSIZE);
    }

    return histogram;
}

std::array<double, 3> compute_equity_histogram_river
(
    const std::array<uint32_t, HOLE_SIZE>& holes,
    const std::array<uint32_t, RIVER_SIZE>& street
)
{
    std::array<uint32_t, HOLE_SIZE + RIVER_SIZE> p1Cards {};
    for (int i = 0; i < HOLE_SIZE; ++i)
        p1Cards[i] = holes[i];
    std::array<uint32_t, HOLE_SIZE + RIVER_SIZE> p2Cards {};
    for (int i = 0; i < RIVER_SIZE; ++i)
    {
        p1Cards[i + HOLE_SIZE] = street[i];
        p2Cards[i + HOLE_SIZE] = street[i];
    }

    std::array<uint32_t, NUM_SUITS> suitRankMask {};
    for (uint32_t card: p1Cards)
    {
        const int suit = __builtin_ctz(card & 0x1E000u) - NUM_RANKS;
        suitRankMask[suit] |= (card &  0x1FFFu);
    }

    const HandInfo p1Score = determine_score(p1Cards);

    std::array<double, 3> histogram {};
    int count {};
    for (int i = 0; i < NUM_CARDS - 1; ++i)
    {
        std::array<uint32_t, NUM_SUITS> localMask = suitRankMask;
        int suit = i / NUM_RANKS;
        int rank = i % NUM_RANKS;
        uint32_t card = bitwise_card(static_cast<Ranks>(rank), static_cast<Suits>(suit));

        if ((card & localMask[suit]) != 0) continue;
        p2Cards[0] = card;

        for (int j = i + 1; j < NUM_CARDS; ++j)
        {
            suit = j / NUM_RANKS;
            rank = j % NUM_RANKS;
            card = bitwise_card(static_cast<Ranks>(rank), static_cast<Suits>(suit));

            if ((card & localMask[suit]) != 0) continue;
            p2Cards[1] = card;

            int result = static_cast<int>(compare_hands(p1Score, determine_score(p2Cards)));
            histogram[result] += 1;
            ++count;
        }
    }
    
    for (int i = 0; i < 3; ++ i)
    {
        histogram[i] /= count;
    }
    return histogram;
}

// computes Earth Mover's Distance (Wasserstein metric) between two equity histograms
template <const std::size_t BINNUMS>
double compute_EDM(
    const std::array<double, BINNUMS>& histogram1,
    const std::array<double, BINNUMS>& histogram2)
{
    constexpr double binWidth = 1.0 / static_cast<double>(BINNUMS);
    double cumulativeDifference = 0.0;
    double EDM = 0.0;

    // standard procedural EDM calculator for 1D ordered sets
    for (std::size_t i = 0; i < BINNUMS; ++i)
    {
        cumulativeDifference += histogram1[i] - histogram2[i];
        EDM += std::abs(cumulativeDifference);
    }

    return binWidth * EDM;
}

#endif