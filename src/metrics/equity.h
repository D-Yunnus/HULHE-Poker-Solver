
#include "constants/constants.h"
#include "evaluation/hand_evaluation.h"

#include <array>
#include <vector>
#include <cstdint>
#include <algorithm>
#include <random>
#include <omp.h>

#ifndef EQUITY_H
#define EQUITY_H

// gives a deck of face-down cards (excluding cards already in play) to draw from
template <const std::size_t cardCount>
constexpr std::vector<uint32_t> construct_deck(
    const std::array<uint32_t, cardCount>& cards_in_play)
{
    std::vector<uint32_t> deck;
    deck.reserve(NUM_CARDS - cardCount);
    for (int suit {SPADES}; suit <= CLUBS; ++suit)
    {
        for (int rank {TWO}; rank <= ACE; ++rank)
        {
            uint32_t card {bitwise_card(static_cast<Ranks>(rank), static_cast<Suits>(suit))};
            if (std::find(cards_in_play.begin(), cards_in_play.end(), card) != cards_in_play.end())
            {
                continue;
            }
            deck.push_back(card);
        }
    }
    return deck;
}

// draws a random card from a deck of face-down cards
uint32_t draw_card
(
    std::vector<uint32_t>& deck,
    std::mt19937& randGen
)
{
    std::uniform_int_distribution<std::size_t> dist(0, deck.size() - 1);
    std::size_t randomIndex = dist(randGen);
    uint32_t draw = deck[randomIndex];

    // partial swap
    deck[randomIndex] = deck.back();
    // removes card from deck
    deck.pop_back();                 

    return draw;
}

// computes raw equity using a monte-carlo method
template <const std::size_t P2HOLESIZE, const std::size_t STREETSIZE>
double equity_monte_carlo(
    const std::array<uint32_t, HOLE_SIZE>& p1Holes,
    const std::array<uint32_t, P2HOLESIZE>& p2KnownCards,
    const std::array<uint32_t, STREETSIZE>& street,
    std::mt19937& randGen,
    const int numIter)
{   
    static_assert((P2HOLESIZE == HOLE_SIZE || P2HOLESIZE == 0), "invalid input (hole) array size");
    static_assert((STREETSIZE == RIVER_SIZE || STREETSIZE == TURN_SIZE || STREETSIZE == FLOP_SIZE || STREETSIZE == 0), "invalid input (street) array size");


    // construct deck
    std::array<uint32_t, HOLE_SIZE + P2HOLESIZE + STREETSIZE> cardsInPlay {};
    for (std::size_t i = 0; i < HOLE_SIZE; ++i)
        cardsInPlay[i] = p1Holes[i];
    for (std::size_t i = 0; i < P2HOLESIZE; ++i)
        cardsInPlay[i + HOLE_SIZE] = p2KnownCards[i];
    for (std::size_t i = 0; i < STREETSIZE; ++i)
        cardsInPlay[i + HOLE_SIZE + P2HOLESIZE] = street[i];
    const std::vector<uint32_t> deck = {construct_deck(cardsInPlay)};

    double equity {};
    const std::mt19937::result_type baseSeed = {randGen()};

    // monte carlo parallelisation using OpenMP
    #pragma omp parallel reduction(+:equity)
    {
        // one PRNG per thread initialized with unique thread seed
        std::mt19937 localGen {baseSeed + static_cast<uint32_t>(omp_get_thread_num() * 1000003)};

        #pragma omp for
        for (int iter = 0; iter < numIter; ++iter)
        {
            // local deck draw
            std::vector<uint32_t> localDeck {deck};

            // assign p2 hole cards
            std::array<uint32_t, HOLE_SIZE> p2Holes {};
            if constexpr (P2HOLESIZE == 0)
            {
                p2Holes[0] = draw_card(localDeck, localGen);
                p2Holes[1] = draw_card(localDeck, localGen);
            }
            else p2Holes = p2KnownCards;

            // complete board cards to 5 (river)
            std::array<uint32_t, RIVER_SIZE> riverCards {};
            for (std::size_t i = 0; i < STREETSIZE; ++i)
                riverCards[i] = street[i];
            for (std::size_t i = STREETSIZE; i < RIVER_SIZE; ++i)
                riverCards[i] = draw_card(localDeck, localGen);

            // construct 7-card holdings for showdown evaluation
            std::array<uint32_t, HOLE_SIZE + RIVER_SIZE> p1Cards {};
            std::array<uint32_t, HOLE_SIZE + RIVER_SIZE> p2Cards {};

            // cards available to p1 and p2
            p1Cards[0] = p1Holes[0]; p1Cards[1] = p1Holes[1];
            p2Cards[0] = p2Holes[0]; p2Cards[1] = p2Holes[1];
            for (std::size_t i = 0; i < RIVER_SIZE; ++i)
            {
                p1Cards[i + HOLE_SIZE] = riverCards[i];
                p2Cards[i + HOLE_SIZE] = riverCards[i];
            }

            // showdown result
            const winStatus result = compare_hands(determine_score(p1Cards), determine_score(p2Cards));

            // weighted increment equity
            if (result == WIN) equity += 1.0;
            else if (result == DRAW) equity += 0.5;
        }
    }

    // normalise
    return equity / numIter;
}

// enumerate all possible complete boards + p2 hole cards, given the current flop, and compute exact equity
inline double equity_flop(
    const std::array<uint32_t, HOLE_SIZE>& hole,
    const std::array<uint32_t, FLOP_SIZE>& street )
{
    // cards available to p1 and p2
    std::array<uint32_t, HOLE_SIZE + RIVER_SIZE> p1Cards {};
    for (int i = 0; i < HOLE_SIZE; ++i)
        p1Cards[i] = hole[i];
    std::array<uint32_t, HOLE_SIZE + RIVER_SIZE> p2Cards {};
    for (int i = 0; i < FLOP_SIZE; ++i)
    {
        p1Cards[i + HOLE_SIZE] = street[i];
        p2Cards[i + HOLE_SIZE] = street[i];
    }

    // rank mask indicating the ranks present for each suit
    std::array<uint32_t, NUM_SUITS> suitRankMask {};
    for (int i = 0; i < HOLE_SIZE + FLOP_SIZE; ++i)
    {
        const uint32_t card = p1Cards[i];
        const int suit = __builtin_ctz(card & 0x1E000u) - NUM_RANKS;
        suitRankMask[suit] |= (card &  0x1FFFu);
    }


    double equity{};
    int count {};
    
    // enumerate over complete board and p2 hands
    for (int i = 0; i < NUM_CARDS - 1; ++i)
    {
        std::array<uint32_t, NUM_SUITS> localMask1 = suitRankMask;
        int suit = i / NUM_RANKS;
        int rank = i % NUM_RANKS;
        uint32_t card = bitwise_card(static_cast<Ranks>(rank), static_cast<Suits>(suit));

        // turn card drawn
        if ((card & localMask1[suit]) != 0) continue;
        p1Cards[HOLE_SIZE + FLOP_SIZE] = card;
        p2Cards[HOLE_SIZE + FLOP_SIZE] = card;
        localMask1[suit] |= (card &  0x1FFFu);

        for (int j = i + 1; j < NUM_CARDS - 1; ++j)
        {
            std::array<uint32_t, NUM_SUITS> localMask2 = localMask1;
            suit = j / NUM_RANKS;
            rank = j % NUM_RANKS;
            card = bitwise_card(static_cast<Ranks>(rank), static_cast<Suits>(suit));

            // river card drawn
            if ((card & localMask2[suit]) != 0) continue;
            p1Cards[HOLE_SIZE + TURN_SIZE] = card;
            p2Cards[HOLE_SIZE + TURN_SIZE] = card;
            localMask2[suit] |= (card &  0x1FFFu);

            // This score is identical for every opponent hand on this board.
            const HandInfo p1Score = determine_score(p1Cards);

            // enumerate over p2 hole cards
            for (int k = 0; k < NUM_CARDS - 1; ++k)
            {
                if (k == i || k == j) continue;

                suit = k / NUM_RANKS;
                rank = k % NUM_RANKS;
                card = bitwise_card(static_cast<Ranks>(rank), static_cast<Suits>(suit));

                if ((card & localMask2[suit]) != 0) continue;
                p2Cards[0] = card;
                localMask2[suit] |= (card &  0x1FFFu);

                for (int l = k + 1; l < NUM_CARDS; ++l)
                {
                    if (l == i || l == j) continue;

                    suit = l / NUM_RANKS;
                    rank = l % NUM_RANKS;
                    card = bitwise_card(static_cast<Ranks>(rank), static_cast<Suits>(suit));

                    if ((card & localMask2[suit]) != 0) continue;
                    p2Cards[1] = card;
                    
                    // showdown result
                    int result = static_cast<int>(compare_hands(
                        p1Score,
                        determine_score(p2Cards)));

                    // weighted increment equity
                    if (result == 1) equity += 1.0;
                    if (result == 2) equity += 0.5;
                    ++count;
                }
            }
        }
    }
        
    // normalise
    return equity / count;
}

// repeat exact equity for turn
inline double equity_turn
(
    const std::array<uint32_t, HOLE_SIZE>& hole,
    const std::array<uint32_t, TURN_SIZE>& street 
)
{
    std::array<uint32_t, HOLE_SIZE + RIVER_SIZE> p1Cards {};
    for (int i = 0; i < HOLE_SIZE; ++i)
        p1Cards[i] = hole[i];
    std::array<uint32_t, HOLE_SIZE + RIVER_SIZE> p2Cards {};
    for (int i = 0; i < TURN_SIZE; ++i)
    {
        p1Cards[i + HOLE_SIZE] = street[i];
        p2Cards[i + HOLE_SIZE] = street[i];
    }

    std::array<uint32_t, NUM_SUITS> suitRankMask {};
    for (int i = 0; i < HOLE_SIZE + TURN_SIZE; ++i)
    {
        const uint32_t card = p1Cards[i];
        const int suit = __builtin_ctz(card & 0x1E000u) - NUM_RANKS;
        suitRankMask[suit] |= (card &  0x1FFFu);
    }

    double equity{};
    int count {};
    for (int i = 0; i < NUM_CARDS; ++i)
    {
        std::array<uint32_t, NUM_SUITS> localMask = suitRankMask;
        int suit = i / NUM_RANKS;
        int rank = i % NUM_RANKS;
        uint32_t card = bitwise_card(static_cast<Ranks>(rank), static_cast<Suits>(suit));

        if ((card & localMask[suit]) != 0) continue;
        p1Cards[HOLE_SIZE + TURN_SIZE] = card;
        p2Cards[HOLE_SIZE + TURN_SIZE] = card;
        localMask[suit] |= (card &  0x1FFFu);

        const HandInfo p1Score = determine_score(p1Cards);

        for (int j = 0; j < NUM_CARDS - 1; ++j)
        {
            if (j == i) continue;

            suit = j / NUM_RANKS;
            rank = j % NUM_RANKS;
            card = bitwise_card(static_cast<Ranks>(rank), static_cast<Suits>(suit));

            if ((card & localMask[suit]) != 0) continue;
            p2Cards[0] = card;
            localMask[suit] |= (card &  0x1FFFu);


            for (int k = j + 1; k < NUM_CARDS; ++k)
            {
                if (k == i) continue;

                suit = k / NUM_RANKS;
                rank = k % NUM_RANKS;
                card = bitwise_card(static_cast<Ranks>(rank), static_cast<Suits>(suit));

                if ((card & localMask[suit]) != 0) continue;
                p2Cards[1] = card;

                int result = static_cast<int>(compare_hands(p1Score, determine_score(p2Cards)));
                if (result == 1) equity += 1.0;
                if (result == 2) equity += 0.5;
                ++count;
            }
        }
    }
        
    return equity / count;
}

// repeat exact equity for river
inline double equity_river
(
    const std::array<uint32_t, HOLE_SIZE>& hole,
    const std::array<uint32_t, RIVER_SIZE>& street 
)
{
    std::array<uint32_t, HOLE_SIZE + RIVER_SIZE> p1Cards {};
    for (int i = 0; i < HOLE_SIZE; ++i)
        p1Cards[i] = hole[i];
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

    double equity{};
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
            if (result == 1) equity += 1.0;
            if (result == 2) equity += 0.5;
            ++count;
        }
    }
        
    return equity / count;
}

// compute the equity of p1 holes + street cards against an unknown random (uniform range) p2 hole cards
template <std::size_t STREETSIZE>
double compute_equity_against_rand(
    const std::array<uint32_t, HOLE_SIZE>& p1Holes,
    const std::array<uint32_t, STREETSIZE>& street)
{
    // use monte carlo for an empty street
    if constexpr (STREETSIZE == 0)
    {
        std::mt19937 randGen(SEED);
        const std::array<uint32_t, 0> p2Holes {};
        return equity_monte_carlo(p1Holes, p2Holes, street, randGen, ITERATION);
    }

    // use exact enumeration for non-empty streets
    if constexpr (STREETSIZE == FLOP_SIZE) return equity_flop(p1Holes, street);
    if constexpr (STREETSIZE == TURN_SIZE) return equity_turn(p1Holes, street);
    if constexpr (STREETSIZE == RIVER_SIZE) return equity_river(p1Holes, street);
}

#endif