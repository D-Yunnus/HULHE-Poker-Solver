#include "constants/constants.h"
#include "constants/helper.h"

#include <cstdint>
#include <array>

#ifndef CARD_EVALUATION_H
#define CARD_EVALUATION_H

enum PokerHands : uint64_t
{
    HIGH_CARD = 0,
    PAIR,
    TWO_PAIR,
    THREE_KIND,
    STRAIGHT,
    FLUSH,
    FULL_HOUSE,
    FOUR_KIND,
    STRAIGHT_FLUSH,
    ROYAL_FLUSH,
};

enum Ranks : uint16_t
{
    TWO = 0,
    THREE,
    FOUR,
    FIVE,
    SIX,
    SEVEN,
    EIGHT,
    NINE,
    TEN,
    JACK,
    QUEEN,
    KING,
    ACE
};

enum Suits : uint16_t
{
    SPADES = 0,
    HEARTS,
    DIAMONDS,
    CLUBS,
};

struct HandInfo
{
    PokerHands pokerRank {0};

    int straightHigh = -1;
    int lowPair = 0;
    int highPair = 0;
    int trip = 0;
    int quad = 0;
    std::array<int,5> kickers {};
};

enum winStatus: int
{
    LOSS = 0,
    WIN,
    DRAW,
};

// converts card described by enums (Ranks, Suits) into a sequence of bits to do bitwise operations
constexpr uint32_t bitwise_card(
    const Ranks rank,
    const Suits suit)
{
    return (1u << static_cast<uint16_t>(rank)) | (1u << (static_cast<uint16_t>(suit) + NUM_RANKS));
}

template <const std::size_t N>
HandInfo determine_score(
    const std::array<uint32_t, N>& cards)
{
    static_assert(N >= 5, "array must be at least of size 5");

    HandInfo info {};
    std::array<int, NUM_RANKS> freq {};
    std::array<uint32_t, NUM_SUITS> suitRanks {};
    uint32_t rankMask = 0;

    for (const uint32_t card : cards)
    {
        // determine ranks and suits
        const uint32_t cardRanks {card & 0x1FFFu};
        const int rank {__builtin_ctz(cardRanks)};
        const int suit {__builtin_ctz(card & 0x1E000u) - NUM_RANKS};

        // number of times a rank is in cards
        ++freq[rank];
        // mask of the ranks
        rankMask |= cardRanks;
        // mask of the ranks of a given suit
        suitRanks[suit] |= cardRanks;
    }

    // lambda function to find the highest value of a straight
    const auto straightHigh = [&](const uint32_t ranks)
    {
        for (int high = ACE; high >= SIX; --high)
        {
            const uint32_t straightMask {0x1Fu << (high - STRAIGHT_SIZE + 1)};
            if ((ranks & straightMask) == straightMask)
                return high;
        }

        // wheel straight A-2-3-4-5
        if ((ranks & 0x100Fu) == 0x100Fu)
            return static_cast<int>(FIVE);

        // if no straight is present then -1
        return -1;
    };

    // if there is a suit with five different ranks then flush is present
    int flushSuit = -1;
    for (int suit = 0; suit < NUM_SUITS; ++suit)
    {
        if (__builtin_popcount(suitRanks[suit]) >= STRAIGHT_SIZE)
        {
            flushSuit = suit;
            break;
        }
    }

    // if flush
    if (flushSuit != -1)
    {
        // check if the flush suit is also a straight
        const int flushStraightHigh = straightHigh(suitRanks[flushSuit]);
        // if its a straight flush
        if (flushStraightHigh != -1)
        {
            info.straightHigh = flushStraightHigh;
            // must check if royal flush
            info.pokerRank = (flushStraightHigh == ACE) ? ROYAL_FLUSH : STRAIGHT_FLUSH;
            return info;
        }

        // else flush suit is not a straight
        info.pokerRank = FLUSH;

        // determine kicker cards
        int kickerCount = 0;
        for (int rank = ACE; rank >= TWO && kickerCount < 5; --rank)
        {
            if ((suitRanks[flushSuit] & (1u << rank)) != 0)
                info.kickers[kickerCount++] = rank;
        }
        return info;
    }

    // check if straight
    info.straightHigh = straightHigh(rankMask);
    if (info.straightHigh != -1)
    {
        info.pokerRank = STRAIGHT;
        return info;
    }

    // initialise counting for trips and pairs
    // for six or more cards need to consider the case of two trips present (highTrip/lowTrip)
    int tripsCount = 0;
    int highTrip = -1;
    int lowTrip = -1;
    int pairCount = 0;

    for (int rank = ACE; rank >= TWO; --rank)
    {
        // check for quads
        if (freq[rank] == 4)
        {
            info.pokerRank = FOUR_KIND;
            info.quad = rank;

            // determine kicker
            for (int kicker = ACE; kicker >= TWO; --kicker)
            {
                if (kicker != rank && freq[kicker] > 0)
                {
                    info.kickers[0] = kicker;
                    return info;
                }
            }
        }
        // check for trips
        else if (freq[rank] == 3)
        {
            if (tripsCount++ == 0)
                highTrip = rank;
            else
                lowTrip = rank; 
        }
        // check for pairs
        else if (freq[rank] == 2)
        {
            if (pairCount++ == 0)
                info.highPair = rank;
            else if (info.lowPair == 0)
                info.lowPair = rank;// 
        }
    }

    // if there are more than two trips or a trip and pair
    if (tripsCount > 0 && (pairCount > 0 || lowTrip != -1))
    {
        info.pokerRank = FULL_HOUSE;
        info.trip = highTrip;
        info.highPair = lowTrip != -1 ? lowTrip : info.highPair;
        return info;
    }

    // if there is a trip
    if (tripsCount > 0)
    {
        info.pokerRank = THREE_KIND;
        info.trip = highTrip;
        int kickerCount {0};

        // determine kickers
        for (int rank = ACE; rank >= TWO && kickerCount < 2; --rank)
        {
            if (rank != highTrip && freq[rank] > 0)
                info.kickers[kickerCount++] = rank;
        }
        return info;
    }

    // if there is two pairs
    if (pairCount >= 2)
    {
        info.pokerRank = TWO_PAIR;

        // determine kicker
        int kickerCount = 0;
        for (int rank = ACE; rank >= TWO; --rank)
        {
            if (freq[rank] == 1 && kickerCount == 0)
                info.kickers[kickerCount++] = rank;
        }
        return info;
    }

    // if there is only one pair
    if (pairCount == 1)
    {
        info.pokerRank = PAIR;
        int kickerCount {0};

        // determine kickers
        for (int rank = ACE; rank >= TWO && kickerCount < 3; --rank)
        {
            if (freq[rank] == 1)
                info.kickers[kickerCount++] = rank;
        }
        return info;
    }

    // if its none of the other poker ranks it is a high card
    info.pokerRank = HIGH_CARD;

    // determine kickers
    for (int rank = ACE, kickerCount = 0; rank >= TWO && kickerCount < 5; --rank)
    {
        if (freq[rank] > 0)
            info.kickers[kickerCount++] = rank;
    }

    // encodes all scoring data in HandInfo struct
    return info; 
}

// given two players best hands determine whether the Hero (p1) wins showdown
inline winStatus compare_hands(
    const HandInfo p1Info, const HandInfo p2Info)
{
    // highest rank wins
    if (p1Info.pokerRank != p2Info.pokerRank)
        return static_cast<winStatus>(p1Info.pokerRank > p2Info.pokerRank);

    // in cases of the hero and villain having the same rank
    switch (p1Info.pokerRank)
    {
    case STRAIGHT_FLUSH: // compare the straight's highest card
    {
        if (p1Info.straightHigh != p2Info.straightHigh)
            return static_cast<winStatus>(p1Info.straightHigh > p2Info.straightHigh);
        break;
    }
    case FOUR_KIND: // compare the quads first then kickers
    {
        if (p1Info.quad != p2Info.quad)
            return static_cast<winStatus>(p1Info.quad > p2Info.quad);
        else if (p1Info.kickers[0] != p2Info.kickers[0])
            return static_cast<winStatus>(p1Info.kickers[0] > p2Info.kickers[0]);
        break;
    }
    case FULL_HOUSE: // compare the trip first then pair
    {
        if (p1Info.trip != p2Info.trip)
            return static_cast<winStatus>(p1Info.trip > p2Info.trip);
        else if (p1Info.highPair != p2Info.highPair)
            return static_cast<winStatus>(p1Info.highPair > p2Info.highPair);
        break;
    }
    case FLUSH: // compare the kickers
    {
        int count = 0;
        for (int i = 0; i < 5; i++)
        {
            if (p1Info.kickers[count] != p2Info.kickers[count])
                return static_cast<winStatus>(p1Info.kickers[count] > p2Info.kickers[count]);
            ++count;
        }
        break;
    }
    case STRAIGHT: // same as straight flush
    {
        if (p1Info.straightHigh != p2Info.straightHigh)
            return static_cast<winStatus>(p1Info.straightHigh > p2Info.straightHigh);
        break;
    }
    case THREE_KIND: // compare trip first then kickers
    {
        if (p1Info.trip != p2Info.trip)
            return static_cast<winStatus>(p1Info.trip > p2Info.trip);
        else if (p1Info.kickers[0] != p2Info.kickers[0])
            return static_cast<winStatus>(p1Info.kickers[0] > p2Info.kickers[0]);
        else if (p1Info.kickers[1] != p2Info.kickers[1])
            return static_cast<winStatus>(p1Info.kickers[1] > p2Info.kickers[1]);
        break;
    }
    case TWO_PAIR: // compare the high pair first then the low pair then the kicker
    {
        if (p1Info.highPair != p2Info.highPair)
            return static_cast<winStatus>(p1Info.highPair > p2Info.highPair);
        else if (p1Info.lowPair != p2Info.lowPair)
            return static_cast<winStatus>(p1Info.lowPair > p2Info.lowPair);
        else if (p1Info.kickers[0] != p2Info.kickers[0])
            return static_cast<winStatus>(p1Info.kickers[0] > p2Info.kickers[0]);
        break;
    }
    case PAIR: // compare pair first then kickers
    {
        if (p1Info.highPair != p2Info.highPair)
            return static_cast<winStatus>(p1Info.highPair > p2Info.highPair);

        int count = 0;
        for (int i = 0; i < 3; i++)
        {
            if (p1Info.kickers[count] != p2Info.kickers[count])
                return static_cast<winStatus>(p1Info.kickers[count] > p2Info.kickers[count]);
            ++count;
        }
        break;
    }
    case HIGH_CARD: // same as flush
    {
        int count = 0;
        for (int i = 0; i < 5; i++)
        {
            if (p1Info.kickers[count] != p2Info.kickers[count])
                return static_cast<winStatus>(p1Info.kickers[count] > p2Info.kickers[count]);
            ++count;
        }
        break;
    }
    default:
        break;
    }

    // draw if no one can be a definitive victor the showdown
    return DRAW;
}

#endif