#ifndef BETTING_H
#define BETTING_H

#include <vector>
#include <array>
#include <unordered_map>
#include <algorithm>
#include <cstdint>

#include "evaluation/hand_evaluation.h"
#include "cfr/generate_histories.h"

struct History
{
    Streets street {}; // current street
    std::array<std::vector<Actions>, NUM_BETTING_ROUNDS> betHistories {}; // current and past betting actions
};

// infosets encode all information available to the player
// each infoset corresponds to a specific strategy
struct InfoSet
{
    History history {}; // history
    int bucketindex {}; // abstract bucket the current board belongs to
};

// helper function which returns corresponding bucket size for each street
int bucket_size
(
    const Streets street
)
{
    switch (street)
    {
    case PREFLOP: return NUM_PREFLOP_HOLES;
    case FLOP: return NUM_FLOP_BUCKETS;
    case TURN: return NUM_TURN_BUCKETS;
    case RIVER: return NUM_RIVER_BUCKETS;
    default: throw std::invalid_argument("invalid street value");
    }
}

// accumulates all the infosets information and gives a corresponding unqiue number that encodes it
inline uint64_t index_infoset
(
    const InfoSet& info
)
{
    const int currStreet = static_cast<int>(info.history.street); // current street
    const uint64_t numBetHistories = HISTORIES.size(); // number of possible betting action histories for a given street
    const uint64_t bucketSize = bucket_size(info.history.street); // bucket size for the current street

    // index the infoset current and past betting actions
    // the index is local to all possible 4 round betting histories
    uint64_t historyIndex = 0;
    // enumerate over the betting histories of each street up to and including the current one
    for (int i = 0; i <= currStreet; ++i)
    {
        // street i betting history is info.history.betHistory[i]
        const auto history = HISTORIES.find(info.history.betHistories[i]); // use HISTORIES to find corresponding enumerated index
        // returns unique history index
        historyIndex = historyIndex * numBetHistories + static_cast<uint64_t>(history->second);
    }

    // incorporate current board state into the index
    // each street offsets the index by cumulative numBetHistories
    // e.g. for a raise cap of 4 there are 29 possible betting histories over a given betting round
    // for the preflop betting round there is a 29 offset
    // for the flop betting round there is a 29 * 29 offset (29 preflop and 29 for the flop)
    // similar with turn and river
    uint64_t streetOffset = 0;
    uint64_t cumBetHistory = 1;
    for (int street = PREFLOP; street < currStreet; ++street)
    {
        cumBetHistory *= numBetHistories;

        // compute cumulative offset from each street up to desired current street
        const uint64_t streetBucketSize = bucket_size(static_cast<Streets>(street));
        streetOffset += cumBetHistory * streetBucketSize;
    }

    // index is the offset by street + local history index + current street bucket
    return streetOffset + historyIndex * bucketSize + static_cast<uint64_t>(info.bucketindex);
}

// calculate a given terminal node, compute the bets each player had made 
std::array<double, NUM_PLAYERS> calculate_bets 
(
    const History& history
)

{
    double sbBet = SMALL_BLIND_ANTE; // small blind player
    double bbBet = BIG_BLIND_ANTE; // big bling player

    // calculate bets made on a given street for a given sequence of betting actions
    const auto calcualte_street_bets = [&]
    (
        const std::vector<Actions>& streetHistory,
        double raiseSize // preflop & flop have raise size SMALL_BLIND_ANTE, turn & river have raise size BIG_BLIND_ANTE
    )
    {
        // for each action of the street's betting sequence
        for (int i = 0; i < static_cast<int>(streetHistory.size()); ++i)
        {
            // identify the player that made the action and what their current bet into the pot is...
            const int actingPlayer = static_cast<int>(i % NUM_PLAYERS);
            // preflop the first betting round is taken by the small blind
            // postflop all remaining betting rounds are taken by the big blind
            double& actingPlayerBet = history.street == PREFLOP && actingPlayer == 0
                ? sbBet
                : history.street == PREFLOP
                    ? bbBet
                    : actingPlayer == 0
                        ? bbBet
                        : sbBet;
            // ...and the opposing player and their current bet into the pot
            double& opposingPlayerBet = history.street == PREFLOP && actingPlayer == 0
                ? bbBet
                : history.street == PREFLOP
                    ? sbBet
                    : actingPlayer == 0
                        ? sbBet
                        : bbBet;

            // adjust the acting players bet size into the pot accordingly
            switch (streetHistory[i])
            {
            case RAISE:
                actingPlayerBet = opposingPlayerBet + raiseSize; // whatever opposing player bet plus more
                break;
            case CALL:
                actingPlayerBet = opposingPlayerBet; // whatever opposing player bet
                break;
            case FOLD:
                break; // no more additional money bet into the pot by acting player
            }
        }
    };

    // calculate bets for each street for a given 4-round betting history
    calcualte_street_bets(history.betHistories[0], SMALL_BET_SIZE);
    calcualte_street_bets(history.betHistories[1], SMALL_BET_SIZE);
    calcualte_street_bets(history.betHistories[2], BIG_BET_SIZE);
    calcualte_street_bets(history.betHistories[3], BIG_BET_SIZE);

    return std::array<double, NUM_PLAYERS> {sbBet, bbBet}; // size of sb & bb bet
}

#endif