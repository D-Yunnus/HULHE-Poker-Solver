#ifndef CFR_H
#define CFR_H

#include <array>
#include <vector>
#include <map>
#include <cstdint>
#include <bit>
#include <unordered_map>

#include "cfr/betting.h"
#include "evaluation/suit_isomorphism.h"
#include "cfr/read_buckets.h"
#include "metrics/equity.h"

// each infoset corresponds to a node
// the variables in this structure can be used to define the strategy after training
struct Node
{
    std::array<double, NUM_ACTIONS> regretSum {}; // how much each action is regreted for the node (cumulative)
    std::array<double, NUM_ACTIONS> stratSum {}; // weighted regret via reach probability
};

// regret matching algorithm is called to return a probability distribution of actions for a given node using the regret sum
std::array<double, NUM_ACTIONS> get_strategy
(
    const Node& gameNode,
    std::array<bool, NUM_ACTIONS>& legalActions
)
{
    std::array<double, NUM_ACTIONS> strategy {};
    double normalisation = 0;
    for (int a = 0; a < NUM_ACTIONS; ++a)
    {
        // ensure positive regret for strategy
        strategy[a] = legalActions[a] ? std::max(gameNode.regretSum[a], 0.0) : 0.0;
        normalisation += strategy[a];
    }
    const int legalActionCount = static_cast<int>(std::count(legalActions.begin(), legalActions.end(), true));
    for (int a = 0; a < NUM_ACTIONS; ++a)
    {
        // normalise all the positive regrets to get a distribution (if normalisation is non-zero)
        // otherwise default to a uniform strategy amongst legal actions
        strategy[a] = !legalActions[a]
            ? 0.0
            : (normalisation > 0.0 ? strategy[a] / normalisation : 1.0 / legalActionCount);
    }
    return strategy;
}

// random number generator
std::mt19937& rng()
{
    static thread_local std::mt19937 gen(std::random_device{}());
    return gen;
}


int count_trailing_zeros(uint32_t value)
{
    int count = 0;
    while ((value & 1u) == 0)
    {
        value >>= 1;
        ++count;
    }
    return count;
}

int index_hole_cards(const std::array<uint32_t, 7>& cards)
{
    constexpr uint32_t rankMask = (1u << NUM_RANKS) - 1;
    const auto rankOf = [](const uint32_t card)
    {
        const uint32_t ranks = card & rankMask;
        return count_trailing_zeros(ranks);
    };
    const auto suitOf = [](const uint32_t card)
    {
        const uint32_t suits = card >> NUM_RANKS;
        return count_trailing_zeros(suits);
    };

    const int firstRank = rankOf(cards[0]);
    const int secondRank = rankOf(cards[1]);

    if (firstRank == secondRank)
        return firstRank;

    const int lowerRank = std::min(firstRank, secondRank);
    const int higherRank = std::max(firstRank, secondRank);
    const bool sameSuit = suitOf(cards[0]) == suitOf(cards[1]);

    int rankPairIndex = 0;
    for (int low = 0; low < NUM_RANKS; ++low)
    {
        for (int high = low + 1; high < NUM_RANKS; ++high)
        {
            if (low == lowerRank && high == higherRank)
            {
                if (sameSuit)
                    return 13 + rankPairIndex;
                return 13 + 78 + rankPairIndex;
            }
            ++rankPairIndex;
        }
    }

    throw std::invalid_argument("invalid hole-card ranks");
}

// define a map between infoset indices and associated nodes containing the strategies
inline std::unordered_map<uint64_t, Node> NODEMAP;

inline bool is_small_blind(const Streets street, const int player)
{
    return street == PREFLOP ? player == 0 : player == 1;
}

// cfr algorithm
double cfr_min
(
    std::array<uint32_t, 7>& sb,
    std::array<uint32_t, 7>& bb,
    History& history,
    double reach_prob_sb,
    double reach_prob_bb,
    const std::array<uint32_t, FLOP_SIZE>* flopCards,
    const uint32_t* turnCard,
    const uint32_t* riverCard
)
{
    // number of actions taken in each betting round
    const std::size_t preflopPlays = history.betHistories[0].size();
    const std::size_t flopPlays = history.betHistories[1].size();
    const std::size_t turnPlays = history.betHistories[2].size();
    const std::size_t riverPlays = history.betHistories[3].size();

    // checks whether flop/turn/river has been dealt yet
    const bool isFlopDealt = preflopPlays > 1 && history.betHistories[0].back() == CALL;
    const bool isTurnDealt = flopPlays > 1 && history.betHistories[1].back() == CALL;
    const bool isRiverDealt = turnPlays > 1 && history.betHistories[2].back() == CALL;

    // assigns the current street to the history
    history.street = isRiverDealt ? RIVER
        : isTurnDealt ? TURN
        : isFlopDealt ? FLOP
        : PREFLOP;

    // identifies current player to make the decision and the opposing player thats already made the recent decision
    const int currPlayer = isRiverDealt
        ? riverPlays % 2
        : isTurnDealt
            ? turnPlays % 2
            : isFlopDealt 
                ? flopPlays % 2
                : preflopPlays % 2;
    const int oppPlayer = 1 - currPlayer;

    // calculate the bets wagered already by both players
    const std::array<double, 2> bets = calculate_bets(history);
    const bool currPlayerIsSmallBlind = is_small_blind(history.street, currPlayer);
    const bool oppPlayerIsSmallBlind = is_small_blind(history.street, oppPlayer);
    const double currPlayerBet = currPlayerIsSmallBlind ? bets[0] : bets[1];
    const double oppPlayerBet = oppPlayerIsSmallBlind ? bets[0] : bets[1];

    // current street betting history
    std::vector<Actions>& currStreetHistory = isRiverDealt
        ? history.betHistories[3]
        : isTurnDealt
            ? history.betHistories[2]
            : isFlopDealt
                ? history.betHistories[1]
                : history.betHistories[0];

    // current player and opposing players cards
    const std::array<uint32_t, 7>& currPlayerCards = currPlayerIsSmallBlind ? sb : bb;
    const std::array<uint32_t, 7>& oppPlayerCards = oppPlayerIsSmallBlind ? sb : bb;

    // terminal nodes:

    // 1. previous player folded
    if (!currStreetHistory.empty() && currStreetHistory.back() == FOLD)
        return oppPlayerBet;

    // 2. both players call at the river
    if (riverPlays >= 2
        && ((history.betHistories[3][riverPlays - 2] == RAISE && history.betHistories[3][riverPlays - 1] == CALL)
            || (history.betHistories[3][riverPlays - 2] == CALL && history.betHistories[3][riverPlays - 1] == CALL)))
    {
        const winStatus handResult = compare_hands(determine_score(currPlayerCards), determine_score(oppPlayerCards));
        if (handResult == WIN) return oppPlayerBet;
        if (handResult == LOSS) return -currPlayerBet;
        return 0.0;
    }

    // chance nodes:

    std::mt19937& randGen = rng();
    
    // 1. deal the river card
    if (isRiverDealt)
    {
        if (riverCard == nullptr)
        {
            const std::array<uint32_t, HOLE_SIZE + HOLE_SIZE + RIVER_SIZE> cardsInPlay = 
            {
                sb[0], sb[1], 
                bb[0], bb[1],
                sb[2], sb[3], sb[4], sb[5]
            };

            std::vector<uint32_t> deck = construct_deck(cardsInPlay);
            const uint32_t sampleCard = draw_card(deck, randGen);
            sb[6] = sampleCard;
            bb[6] = sampleCard;
            History boardHistory = history;
            return cfr_min(sb, bb, boardHistory, reach_prob_sb, reach_prob_bb, flopCards, turnCard, &sampleCard);
        }
        sb[6] = *riverCard;
        bb[6] = *riverCard;
    }
    
    // 2. deal the urn card
    if (isTurnDealt)
    {
        if (turnCard == nullptr)
        {
            const std::array<uint32_t, HOLE_SIZE + HOLE_SIZE + TURN_SIZE> cardsInPlay = 
            {
                sb[0], sb[1],
                bb[0], bb[1],
                sb[2], sb[3], sb[4]
            };

            std::vector<uint32_t> deck = construct_deck(cardsInPlay);
            const uint32_t sampleCard = draw_card(deck, randGen);
            sb[5] = sampleCard;
            bb[5] = sampleCard;
            History boardHistory = history;
            return cfr_min(sb, bb, boardHistory, reach_prob_sb, reach_prob_bb, flopCards, &sampleCard, riverCard);
        }
        sb[5] = *turnCard;
        bb[5] = *turnCard;
    }

    // 3. deal the flop cards
    if (isFlopDealt)
    {
        if (flopCards == nullptr)
        {
            std::array<uint32_t, FLOP_SIZE> sampleFlopCards {};

            const std::array<uint32_t, 4> cardsInPlay = 
            {
                sb[0],
                sb[1],
                bb[0], bb[1]
            };

            std::vector<uint32_t> deck = construct_deck(cardsInPlay);
            uint32_t sampleCard = draw_card(deck, randGen);
            sb[2] = sampleCard;
            bb[2] = sampleCard;
            sampleFlopCards[0] = sampleCard;

            sampleCard = draw_card(deck, randGen);
            sb[3] = sampleCard;
            bb[3] = sampleCard;
            sampleFlopCards[1] = sampleCard;

            sampleCard = draw_card(deck, randGen);
            sb[4] = sampleCard;
            bb[4] = sampleCard;
            sampleFlopCards[2] = sampleCard;

            History boardHistory = history;
            return cfr_min(sb, bb, boardHistory, reach_prob_sb, reach_prob_bb, &sampleFlopCards, turnCard, riverCard);
        }
        sb[2] = (*flopCards)[0];
        sb[3] = (*flopCards)[1];
        sb[4] = (*flopCards)[2];
        bb[2] = (*flopCards)[0];
        bb[3] = (*flopCards)[1];
        bb[4] = (*flopCards)[2];
    }

    // finding the board card index and corresponding bucket
    int index {};
    int bucket {};
    if (isRiverDealt)
    {
        index = index_cards<5>(currPlayerCards);
        bucket = RIVER_BUCKETS[index];
    }
    else if (isTurnDealt)
    {
        index = index_cards<4>(std::array<uint32_t, 6> {
            currPlayerCards[0],
            currPlayerCards[1],
            currPlayerCards[2],
            currPlayerCards[3],
            currPlayerCards[4],
            currPlayerCards[5]});
        bucket = TURN_BUCKETS[index];
    }
    else if (isFlopDealt)
    {
        index = index_cards<3>(std::array<uint32_t, 5> {
            currPlayerCards[0],
            currPlayerCards[1],
            currPlayerCards[2],
            currPlayerCards[3],
            currPlayerCards[4]});
        bucket = FLOP_BUCKETS[index];
    }
    else
        bucket = index_hole_cards(currPlayerCards);

    // index the infoset with the history and current board bucket
    InfoSet info = {history, bucket};
    const uint64_t infoIndex = index_infoset(info);

    // temporarily store node
    Node nodeTemp {};
    #pragma omp critical(node_map_access) // allow thread access
    {
        // find/cerate node
        nodeTemp = NODEMAP.try_emplace(infoIndex).first->second;;
    }

    // find legal actions
    std::array<bool, NUM_ACTIONS> legalActions = legal_actions(currStreetHistory);
    // obtain strategy
    std::array<double, NUM_ACTIONS> strategy = get_strategy(nodeTemp, legalActions);

    // identify player reach probabilities using the same SB/BB mapping as the game state.
    const double currPlayerReachProb = currPlayerIsSmallBlind ? reach_prob_sb : reach_prob_bb;
    const double oppPlayerReachProb = oppPlayerIsSmallBlind ? reach_prob_sb : reach_prob_bb;

    // update stratSum in the node using the current strategy and reach probability
    #pragma omp critical(node_map_access)
    {
        Node& currNode = NODEMAP.try_emplace(infoIndex).first->second;;
        for (int i = 0; i < NUM_ACTIONS; ++i)
            currNode.stratSum[i] += currPlayerReachProb * strategy[i];
    }

    // cfr utility calculations

    std::array<double, NUM_ACTIONS> util {};
    double expected_util = 0.0;

    // enumerate over the legal actions and find the utility for the new history recursively
    for (int i = 0; i < NUM_ACTIONS; ++i)
    {
        if (!legalActions[i]) continue; // ensure legal action
        currStreetHistory.push_back(static_cast<Actions>(i)); // add action to history

        // recursive cfr for new history returning utility for that action
        if (currPlayerIsSmallBlind)
            util[i] = - cfr_min(sb, bb, history, reach_prob_sb * strategy[i], reach_prob_bb, flopCards, turnCard, riverCard);
        else
            util[i] = - cfr_min(sb, bb, history, reach_prob_sb, reach_prob_bb * strategy[i], flopCards, turnCard, riverCard);
        
        // compute expected utility
        expected_util += strategy[i] * util[i];

        currStreetHistory.pop_back();
    }

    // use utility to compute regrets and then update regret sums
    #pragma omp critical(node_map_access)
    {
        Node& currNode = NODEMAP.try_emplace(infoIndex).first->second;;
        for (int i = 0; i < NUM_ACTIONS; ++i)
        {
            if (!legalActions[i]) continue;
            double regret = util[i] - expected_util;
            currNode.regretSum[i] = std::max(0.0, currNode.regretSum[i] + oppPlayerReachProb * regret); // implemented cfr+ for faster convergence
        }
    }

    // return expected utility
    return expected_util;
}

#endif