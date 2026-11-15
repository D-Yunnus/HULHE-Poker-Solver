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
        // keep only the legal actions with positive regret
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
    const auto cardIndex = [](const uint32_t card)
    {
        const uint32_t ranks = card & rankMask;
        const uint32_t suits = card >> NUM_RANKS;
        if (ranks == 0 || suits == 0)
            throw std::invalid_argument("invalid encoded hole card");
        const int rank = count_trailing_zeros(ranks);
        const int suit = count_trailing_zeros(suits);
        return rank * NUM_SUITS + suit;
    };

    int first = cardIndex(cards[0]);
    int second = cardIndex(cards[1]);
    if (first > second) std::swap(first, second);
    return second * (second - 1) / 2 + first;
}


// given the cards in play, draw uniformly at random a new card
#if 0
template <std::size_t NUM_CARDS_IN_PLAY>
uint32_t draw_cards
(
    const std::array<uint32_t, NUM_CARDS_IN_PLAY>& cardsInPlay
)
{
    const std::size_t deckSize = NUM_CARDS - NUM_CARDS_IN_PLAY;
    std::array<uint32_t, deckSize> deck = {};
    std::size_t count = 0;
    for (int i = 0; i < NUM_CARDS; ++i)
    {
        const uint32_t card = bitwise_card(static_cast<Ranks>(i / NUM_SUITS), static_cast<Suits>(i % NUM_SUITS));
        bool alreadyPlayed = false;
        for (const uint32_t playCards: cardsInPlay)
        {
            if (card == playCards) alreadyPlayed = true;
        }
        if (!alreadyPlayed) deck[count++] = card;
    }

    std::uniform_int_distribution<std::size_t> dist(0, deckSize - 1);
    
    return deck[dist(rng())];
}
#endif

// define a map between infoset indices and associated nodes containing the strategies
inline std::unordered_map<uint64_t, Node> NODEMAP;

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
    double currPlayerBet = isFlopDealt && currPlayer == 0
        ? bets[1]
        : isFlopDealt
            ? bets[0]
            : currPlayer == 0
                ? bets[0]
                : bets[1];
    double oppPlayerBet = isFlopDealt && currPlayer == 0
        ? bets[0]
        : isFlopDealt
            ? bets[1]
            : currPlayer == 0
                ? bets[1]
                : bets[0];

    // current street betting history
    std::vector<Actions>& currStreetHistory = isRiverDealt
        ? history.betHistories[3]
        : isTurnDealt
            ? history.betHistories[2]
            : isFlopDealt
                ? history.betHistories[1]
                : history.betHistories[0];

    // current player and opposing players cards
    const std::array<uint32_t, 7>& currPlayerCards = currPlayer == 0 ? bb : sb;
    const std::array<uint32_t, 7>& oppPlayerCards = oppPlayer == 0 ? bb : sb;

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

    // identify player reach probabilities
    const double currPlayerReachProb = isFlopDealt && currPlayer == 0
        ? reach_prob_sb
        : isFlopDealt
            ? reach_prob_bb
            : currPlayer == 0
                ? reach_prob_bb
                : reach_prob_sb;
    const double oppPlayerReachProb = isFlopDealt && currPlayer == 0
        ? reach_prob_bb
        : isFlopDealt
            ? reach_prob_sb
            : currPlayer == 0
                ? reach_prob_sb
                : reach_prob_bb;

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
        if (isFlopDealt && currPlayer == 0)
            util[i] = - cfr_min(sb, bb, history, reach_prob_sb * strategy[i], reach_prob_bb, flopCards, turnCard, riverCard);
        else if (isFlopDealt)
            util[i] = - cfr_min(sb, bb, history, reach_prob_sb, reach_prob_bb * strategy[i], flopCards, turnCard, riverCard);
        else if (currPlayer == 0)
            util[i] = - cfr_min(sb, bb, history, reach_prob_sb, reach_prob_bb * strategy[i], flopCards, turnCard, riverCard);
        else
            util[i] = - cfr_min(sb, bb, history, reach_prob_sb * strategy[i], reach_prob_bb, flopCards, turnCard, riverCard);
        
        // compute expected utility
        expected_util += strategy[i] * util[i];

        currStreetHistory.pop_back();
    }

    // use utility to compute regrets and hence edit regret sums
    #pragma omp critical(node_map_access)
    {
        Node& currNode = NODEMAP.try_emplace(infoIndex).first->second;;
        for (int i = 0; i < NUM_ACTIONS; ++i)
        {
            if (!legalActions[i]) continue;
            double regret = util[i] - expected_util;
            currNode.regretSum[i] += oppPlayerReachProb * regret;
        }
    }

    // return expected utility
    return expected_util;
}

#endif