#include "evaluation/hand_evaluation.h"
#include "constants/helper.h"

#include <cstdint>
#include <array>
#include <vector>
#include <omp.h>

#ifndef POTENTIAL_METRICS_H
#define POTENTIAL_METRICS_H

// calculate the hand potential metrics (effective hand strength, raw hand strength, positive/negative potential)
// potential is the potential to improve (or worsen) a hand
template <const int LookAhead, const std::size_t STREETSIZE>
std::array<std::array<double, 3>, 3> calculate_HP(
    HandInfo p1Info,
    const std::array<uint32_t, HOLE_SIZE>& hole,
    const std::array<uint32_t, STREETSIZE>& street,
    const std::vector<uint32_t>& deck)
{
    // given the current p1 hole cards and community cards
    // construct a 3x3 hand potential table
    // enumerate over all possible p2 cards
    // compare the score of p1 and p2 cards with the current community cards (result_1)
    // draw one (or two) community cards and determine another score with the new set of community cards
    // compare the new score of p1 and p2 cards with the new community cards (result_2)
    // the table counts the number of p2 cards + drawn community cards that have (result_1, result_2)
    std::array<std::array<double, 3>, 3> HP {};

    // each thread computes its own local 3x3 hand potential table
    std::vector<std::array<std::array<double, 3>, 3>> threadHP (omp_get_max_threads());
    #pragma omp parallel
    {
        const int threadId = omp_get_thread_num();
        auto& localHP = threadHP[threadId];

        #pragma omp for // parallel for loop
        for (std::size_t i = 0; i < deck.size() - 1; ++i)
        {
            for (std::size_t j = i + 1; j < deck.size(); ++j)
            {
                // enumerate over all possible hands the p2
                const std::array<uint32_t, HOLE_SIZE> draw = {deck[i], deck[j]};
                std::array<uint32_t, HOLE_SIZE + STREETSIZE> currP2Cards {};
                for (std::size_t i = 0; i < HOLE_SIZE; ++i)
                    currP2Cards[i] = draw[i];
                for (std::size_t i = 0; i < STREETSIZE; ++i)
                    currP2Cards[i + HOLE_SIZE] = street[i];

                // determine p2 scrore
                const HandInfo p2Info = determine_score(currP2Cards);
                // compare scores
                const winStatus p1Wins = compare_hands(p1Info, p2Info);

                // draw additional community cards
                std::array<uint32_t, HOLE_SIZE + STREETSIZE + LookAhead> p1NewCards {};
                std::array<uint32_t, HOLE_SIZE + STREETSIZE + LookAhead> p2NewCards {};
                for (std::size_t i = 0; i < HOLE_SIZE; ++i)
                {
                    p1NewCards[i] = hole[i];
                    p2NewCards[i] = draw[i];
                }
                for (std::size_t i = 0; i < STREETSIZE; ++i)
                {
                    p1NewCards[HOLE_SIZE + i] = street[i];
                    p2NewCards[HOLE_SIZE + i] = street[i];
                }

                // lambda function takes the original street cards + drawn cards to create a new street and adds it to the players available cards
                // then it compute showdown result and populate the thread local table
                auto recordResult = [&](const auto& futureCards)
                {
                    // additional cards added to players available cards
                    for (int card = 0; card < LookAhead; ++card)
                    {
                        p1NewCards[HOLE_SIZE + STREETSIZE + card] = futureCards[card];
                        p2NewCards[HOLE_SIZE + STREETSIZE + card] = futureCards[card];
                    }

                    // determine new scores
                    const HandInfo p1NewInfo = determine_score(p1NewCards);
                    const HandInfo p2NewInfo = determine_score(p2NewCards);
                    // compare new scores
                    const winStatus p1WinsAgain = compare_hands(p1NewInfo, p2NewInfo);

                    // increment the appropriate table position
                    localHP[static_cast<int>(p1Wins)][static_cast<int>(p1WinsAgain)] += 1.0; // increment the table elements of HP
                };

                // draw one card and compare how the hero's hand improve or worsens (HP1)...
                if constexpr (LookAhead == 1)
                {
                    for (std::size_t k = 0; k < deck.size(); ++k)
                    {
                        if (k == i || k == j) continue;
                        std::array<uint32_t, 1> cards = {deck[k]};
                        recordResult(cards);
                    }
                }
                // ...or draw two (HP2)
                else
                {
                    for (std::size_t k = 0; k < deck.size() - 1; ++k)
                    {
                        if (k == i || k == j) continue;
                        for (std::size_t l = k + 1; l < deck.size(); ++l)
                        {
                            if (l == i || l == j) continue;
                            std::array<uint32_t, 2> cards = {deck[k], deck[l]};
                            recordResult(cards);
                        }
                    }
                }
            }
        }
    }

    // aggregate local thread tables 
    for (const auto& localHP : threadHP)
        for (std::size_t row = 0; row < 3; ++row)
            for (std::size_t column = 0; column < 3; ++column)
                HP[row][column] += localHP[row][column];

    // return the table
    return HP;
}



template <const std::size_t STREETSIZE>
std::array<double, 4> potentialEHS(
    const std::array<uint32_t, HOLE_SIZE> hole,
    const std::array<uint32_t, STREETSIZE>& street,
    const int lookAhead)
{

    // construct deck
    const std::array<uint32_t, HOLE_SIZE + STREETSIZE> cardsInPlay {};
    for (std::size_t i = 0; i < HOLE_SIZE; ++i)
        cardsInPlay[i] = hole[i];
    for (std::size_t i = 0; i < STREETSIZE; ++i)
        cardsInPlay[i + HOLE_SIZE] = street[i];
    const std::vector<uint32_t> deck = {construct_deck(cardsInPlay)};

    // construct HP table
    const HandInfo p1Info = determine_score(cardsInPlay);
    std::array<std::array<double, 3>,3> HP {};
    if (lookAhead == 1)
        HP = calculate_HP<1>(p1Info, hole, street, deck);
    else if (lookAhead == 2)
        HP = calculate_HP<2>(p1Info, hole, street, deck);

    // compute the total number of games played starting at ahead/tied/behind originally
    const std::array<double, 3> totalHP {
        HP[0][0] + HP[0][1] + HP[0][2],
        HP[1][0] + HP[1][1] + HP[1][2],
        HP[2][0] + HP[2][1] + HP[2][2]
    };

    // total number of possible games played
    const double total = totalHP[0] + totalHP[1] + totalHP[2]; 
    if (total == 0.0) return {0.0, 0.0, 0.0, 0.0};

    // compute raw hand strength
    double HS = (totalHP[1] + 0.5 * totalHP[2]) / total; 

    // weighted number of games starting in a positive/neutral (ahead & tied)
    const double positiveDenominator = totalHP[0] + 0.5 * totalHP[2];
    // weighted number of games that start in a negative/neutral (behjind & tied)
    const double negativeDenominator = totalHP[1] + 0.5 * totalHP[2];

    // compute positive potential i.e. improving a hand
    double PPot = (positiveDenominator > 0.0) ? (HP[0][1] + 0.5 * HP[0][2] + 0.5 * HP[2][1]) / positiveDenominator : 0.0;

    // compute negative potential i.e. worsening a hand
    double NPot = (negativeDenominator > 0.0) ? (HP[1][0] + 0.5 * HP[1][2] + 0.5 * HP[2][0]) / negativeDenominator : 0.0;

    // compute effective hand stength
    double EHS = HS * (1.0 - NPot) + (1.0 - HS) * PPot;

    // return potential metrics
    return {EHS, HS, PPot, NPot};
}

#endif