// headers for poker metrics

#include "constants/helper.h"
#include "evaluation/suit_isomorphism.h"
#include "metrics/potential_metrics.h"
#include "metrics/histogram.h"
#include "metrics/equity.h"
#include "cfr/betting.h"
#include "cfr/cfr.h"
#include "cfr/loader.h"

#include <cmath>
#include <fstream>
#include <iostream>
#include <vector>
#include <chrono>

int main()
{
    load_histories();
    load_trained_poker_maps();

    const std::array<std::vector<Actions>, NUM_BETTING_ROUNDS> betHistory =
    {
        std::vector<Actions> {CALL, RAISE, CALL},
        std::vector<Actions> {},
        std::vector<Actions> {},
        std::vector<Actions> {}
    };
    History history = {FLOP, betHistory};

    std::array<uint32_t, HOLE_SIZE + TURN_SIZE> cards = unindex<TURN_SIZE>(11283542);
    for (const auto card : cards)
        helper::printCard(card);
    const unsigned long long int index = index_cards<FLOP_SIZE>(std::array<uint32_t, HOLE_SIZE + FLOP_SIZE> {cards[0], cards[1], cards[2], cards[3], cards[4]});
    uint64_t infoIndex = index_infoset(InfoSet {history, FLOP_BUCKETS[index]});
    std::cout << "Gamestate: " << infoIndex << '\n';

    double normalisation = 0;
    const std::array<double, NUM_ACTIONS> stratSum = NODEMAP[infoIndex].stratSum;
    for (int i = 0; i < NUM_ACTIONS; ++i)
    {
        normalisation += stratSum[i];
    }
    std::cout << "Strategy: ";
    for (int i = 0; i < NUM_ACTIONS; ++i)
    {
        double val = normalisation > 0 ? stratSum[i] / normalisation : 0.0;
        std::cout << val << ' ';
    }
    std::cout << '\n';

    return 0;
}