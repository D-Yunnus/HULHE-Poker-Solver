#include "evaluation/hand_evaluation.h"
#include "constants/helper.h"
#include "constants/constants.h"

#ifndef SUIT_ISOMORPHISM_H
#define SUIT_ISOMORPHISM_H

// the indexing and unindexing algorithm is derived from Kevin Waugh "Fast and Optimal Hand Isomorphism Algorithm"
// the combinatorial math is explained in Waugh's paper
// Waugh's implementation: https://github.com/kdub0/hand-isomorphism

// the following is my own implementation
// whilst Waugh's is more general in its uses
// my implementation is optimised for specifically 52 card, 13 rank texas holdem boards
// further mathematical discussions and explaination can be found within the docs

// INDEXING //

// for each suit
struct SuitData
{
    // number of that suit that appears preflop
    int numSuitPreflop {};
    // number of that suit that appears postflop
    int numSuitPostflop {};
    
    // rank mask of that suit preflop
    uint16_t suitRanksetPreflop = 0;
    // rank mask of that suit postflop
    uint16_t suitRanksetPostflop = 0;
};

// a sorting algorithm to sort the suit data
// input suit configuration: <<c11, c12>, <c21, c22>, <c31, c32>, <c41, c42>>
// an ordered configuration is <c11, c12> >= <c21, c22> >= <c31, c32> >= <c41, c42>
// the order is such that <a1, a2> > <b1, b2>: if a1 > a2 or if a1=a2 then b1 > b2
constexpr void constexpr_sort_config(
    std::array<SuitData, NUM_SUITS>& config)
{
    // input <a1, a2>, <b1, b2> and return if <a1, a2> > <b1, b2>
    auto precedes = [](const SuitData& a, const SuitData& b) {
        if (a.numSuitPreflop != b.numSuitPreflop)
            return a.numSuitPreflop > b.numSuitPreflop;
        return a.numSuitPostflop > b.numSuitPostflop;
    };

    // insertion sort for short array
    for (std::size_t i = 1; i < NUM_SUITS; ++i)
    {
        SuitData key = config[i];
        int j = static_cast<int>(i) - 1;
        while (j >= 0 && precedes(key, config[j]))
        {
            config[j + 1] = config[j];
            --j;
        }
        config[j + 1] = key;
    }
}

// given cards, return the suit configuration
template <std::size_t STREETSIZE>
constexpr std::array<SuitData, NUM_SUITS> suit_config(
    const std::array<uint32_t, HOLE_SIZE + STREETSIZE>& cards)
{
    std::array<SuitData, NUM_SUITS> config {};

    // iterate over hole cards and populate config for each suit
    for (int i = 0; i < HOLE_SIZE; ++i)
    {
        const uint8_t suitMask = (cards[i] >> NUM_RANKS) & 0xFu;
        for (int j = 0; j < NUM_SUITS; ++j)
            if ((suitMask & (1u << j)) != 0)
            {
                config[j].numSuitPreflop++;
                config[j].suitRanksetPreflop |= (cards[i] & 0x1FFFu);
            }
    }

    // iterate over street cards and populate config for each suit
    for (int i = HOLE_SIZE; i < HOLE_SIZE + static_cast<int>(STREETSIZE); ++i)
    {
        const uint8_t suitMask = (cards[i] >> NUM_RANKS) & 0xFu;
        for (int j = 0; j < NUM_SUITS; ++j)
            if ((suitMask & (1u << j)) != 0)
            {
                config[j].numSuitPostflop++;
                config[j].suitRanksetPostflop |= (cards[i] & 0x1FFFu);
            }
    }

    // sort configuration
    constexpr_sort_config(config);

    return config;
}

// colex helper function used for indexing <M1, M2> pairs within a suit configuration
// mathematics further explained in docs
constexpr int colex(
    const int N,
    const int M1,
    const int M2)
{
    return helper::binCoeff(N, M1) * helper::binCoeff(N - M1, M2);
}

// a set is <M1, M2> with a configuration having 4 sets
// a multiset is a subset of sets within a configuration such that <M1, M2> == <M1', M2'>
// each multiset has a multiplicity (the number of sets that a given set is equjal to in a given configuration)
// multiset colex function used for indexing <M1, M2> pairs within a suit configuration that appear with some multiplicity
// mathematics further explained in docs
// note a non multiset set has by default multiplicty 1
constexpr int multiset_colex(
    const std::array<int, 2>& C_j,
    const int& multiplicity)
{
    return helper::binCoeff(colex(NUM_RANKS, C_j[0], C_j[1]) + multiplicity - 1, multiplicity);
}


// insertion sort the first count number of indices of an array
template <std::size_t N>
constexpr void constexpr_sort(
    std::array<int, N>& arr,
    std::size_t count)
{
    for (std::size_t i = 1; i < count; ++i)
    {
        int key = arr[i];
        int j = static_cast<int>(i) - 1;
        while (j >= 0 && arr[j] > key)
        {
            arr[j + 1] = arr[j];
            --j;
        }
        arr[j + 1] = key;
    }
}

// for a given multiset with some multiplicity, index within the multiset space
// mathematics further discused in docs
template <std::size_t N>
constexpr int index_multiset(
    std::array<int, N> multisetIndices, // the indices of each multiset the
    std::size_t multiplicity)
{
    constexpr_sort(multisetIndices, multiplicity);

    int val = 0;
    for (int i = 0; i < static_cast<int>(multiplicity); ++i)
        val += helper::binCoeff(multisetIndices[i] + i, i + 1);

    return val;
}

// helper find minimum function
constexpr int min(
    const int a,
    const int b)
{
    return a > b ? b : a;
};

// helpher order <a1, a2>, <b1,b2> function
inline constexpr auto order(
    const std::array<int, 2>& a,
    const std::array<int, 2>& b)
{
    if (a[0] != b[0]) return a[0] >= b[0];
    return a[1] >= b[1];
};

// count the number of possible suit configurations
template <std::size_t STREETSIZE>
constexpr std::size_t COUNT_CONFIGS()
{
    std::size_t count = 0;

    // nested loop to decrement over all possible combinations
    for (int c11 = HOLE_SIZE; c11 > -1; --c11)
    {
        for (int c12 = min(c11, HOLE_SIZE - c11); c12 > -1; --c12)
        {
            for (int c13 = min(c12, HOLE_SIZE - c11 - c12); c13 > -1; --c13)
            {
                int c14 = HOLE_SIZE - c11 - c12 - c13;
                if (c14 > c13) continue;

                for (int c21 = static_cast<int>(STREETSIZE); c21 > -1; --c21)
                {
                    for (int c22 = static_cast<int>(STREETSIZE) - c21; c22 > -1; --c22)
                    {
                        for (int c23 = static_cast<int>(STREETSIZE) - c21 - c22; c23 > -1; --c23)
                        {
                            int c24 = STREETSIZE - c21 - c22 - c23;

                            std::array<std::array<int, 2>, NUM_SUITS> config {
                                {{c11, c21}, {c12, c22}, {c13, c23}, {c14, c24}}
                            };
                            if (order(config[0], config[1]) && order(config[1], config[2]) && order(config[2], config[3])) {
                                ++count;
                            }
                        }
                    }
                }
            }
        }
    }
    return count;
}

#ifndef GLOBAL_NUM_CONFIG
#define GLOBAL_NUM_CONFIG

// global variables for number of configurations for each street
constexpr std::size_t NUM_CONFIGS_FLOP = COUNT_CONFIGS<FLOP_SIZE>();
constexpr std::size_t NUM_CONFIGS_TURN = COUNT_CONFIGS<TURN_SIZE>();
constexpr std::size_t NUM_CONFIGS_RIVER = COUNT_CONFIGS<RIVER_SIZE>();

#endif

// list all possible suit configurations
// repeat of COUNT_CONFIGS() but presence of global NUM_CONFIGS previously counted allows for array instead of dynamic allocation
template <std::size_t NUM_CONFIGS, std::size_t STREETSIZE>
constexpr std::array<std::array<std::array<int, 2>, NUM_SUITS>, NUM_CONFIGS> LIST_CONFIGS()
{
    std::array<std::array<std::array<int, 2>, NUM_SUITS>, NUM_CONFIGS> suitConfigs {};
    std::size_t count = 0;

    for (int c11 = HOLE_SIZE; c11 > -1; --c11)
        for (int c12 = min(c11, HOLE_SIZE - c11); c12 > -1; --c12)
            for (int c13 = min(c12, HOLE_SIZE - c11 - c12); c13 > -1; --c13)
            {
                int c14 = HOLE_SIZE - c11 - c12 - c13;
                if (c14 > c13) continue;

                for (int c21 = static_cast<int>(STREETSIZE); c21 > -1; --c21)
                    for (int c22 = static_cast<int>(STREETSIZE) - c21; c22 > -1; --c22)
                        for (int c23 = static_cast<int>(STREETSIZE) - c21 - c22; c23 > -1; --c23)
                        {
                            int c24 = static_cast<int>(STREETSIZE) - c21 - c22 - c23;

                            std::array<std::array<int, 2>, NUM_SUITS> config {
                                {{c11, c21}, {c12, c22}, {c13, c23}, {c14, c24}}
                            };

                            // ensure ordering (to avoid double counting ordered & unordered configurations that are the same)
                            if (order(config[0], config[1]) &&
                                order(config[1], config[2]) &&
                                order(config[2], config[3]))
                                suitConfigs[count++] = config;
                        }
            }
        
    return suitConfigs;
}

// compute an offset for each configuration
// a given gamestate: hole + street has a a local index that can be computed (irrespective of its suit configuration)
// it also has a specific configuration which provides an offset
// global index = local index + suit configuration offset
template <std::size_t NUM_CONFIGS>
constexpr std::array<int, NUM_CONFIGS + 1> LIST_OFFSETS(
    const std::array<std::array<std::array<int, 2>, NUM_SUITS>, NUM_CONFIGS>& configsList)
{
    std::array<int, NUM_CONFIGS + 1> offsetSizes {};
    int offset = 0;
    offsetSizes[0] = offset; // offset starts at 0

    // iterate over the number of configurations
    for (std::size_t k = 0; k < NUM_CONFIGS; ++k)
    {
        const std::array<std::array<int, 2>, NUM_SUITS>& config = configsList[k];
        int size = 1;
        int i = 0;

        // work out the size of each local index space by finding the set/multiset space size of the configuration
        // multiplying each space set/multiset size gives the local index space size
        while (i < NUM_SUITS)
        {
            const int M1 = config[i][0];
            const int M2 = config[i][1];


            int diff = 0;
            int j = i;
            // this while loop considers multisets
            while (j < NUM_SUITS && config[j][0] == M1 && config[j][1] == M2)
                j++;

            diff = j - i;
            i = j;

            // formula for working out local index space size
            size *= helper::binCoeff(colex(NUM_RANKS, M1, M2) + diff - 1, diff);
        }

        // the offset is the cumulation of these local index space sizes
        offset += size;
        offsetSizes[k + 1] = offset;
    }
    
    return offsetSizes;
}

#ifndef GLOBAL_LISTS
#define GLOBAL LISTS

constexpr auto FLOP_CONFIG_LIST = LIST_CONFIGS<NUM_CONFIGS_FLOP, FLOP_SIZE>();
constexpr auto TURN_CONFIG_LIST = LIST_CONFIGS<NUM_CONFIGS_TURN, TURN_SIZE>();
constexpr auto RIVER_CONFIG_LIST = LIST_CONFIGS<NUM_CONFIGS_RIVER, RIVER_SIZE>();

constexpr auto FLOP_OFFSET_LIST = LIST_OFFSETS<NUM_CONFIGS_FLOP>(FLOP_CONFIG_LIST);
constexpr auto TURN_OFFSET_LIST = LIST_OFFSETS<NUM_CONFIGS_TURN>(TURN_CONFIG_LIST);
constexpr auto RIVER_OFFSET_LIST = LIST_OFFSETS<NUM_CONFIGS_RIVER>(RIVER_CONFIG_LIST);

#endif

// for a given suit there are M1 cards part of the hole and M2 cards part of the street
// this is a set <M1, M2> in the suit configuration
// the hole has M1 different ranks and the street has M2 different ranks
// the function below indexes these M1/M2 ranks
template <typename T>
constexpr int index_rankset(
    const T& rankset, // M ranks
    const int M)
{
    // standard indexing
    // further mathematics discussed in docs
    int sum = 0;
    for (int i = 1; i <= M; ++i)
    {
        sum += helper::binCoeff(rankset[i - 1], M - i + 1);
    }
    return sum;
}

// indexes the pair <M1, M2> for a given suit
constexpr int index_rankset_group(   
    const uint16_t preflopRanks, // M1 ranks of the suit
    const uint16_t postflopRanks) // M2 ranks of the suit
{
    const int M1 = __builtin_popcount(preflopRanks);
    const int M2 = __builtin_popcount(postflopRanks);

    std::array<int, NUM_RANKS> preflopRankset{};
    std::array<int, NUM_RANKS> postflopRankset{};

    // bitmask of ranks -> array of ranks
    uint16_t preflopMask = preflopRanks;
    for (int idx = 0; idx < M1; ++idx)
    {
        int rank = 0x1Fu - __builtin_clz(preflopMask);
        preflopRankset[idx] = rank;
        preflopMask &= ~(1u << rank);
    }
    uint16_t postflopMask = postflopRanks;
    for (int idx = 0; idx < M2; ++idx)
    {
        int rank = 31 - __builtin_clz(postflopMask);
        int shift = __builtin_popcount(preflopRanks & ((1u << rank) - 1));
        
        postflopRankset[idx] = rank - shift;
        postflopMask &= ~(1u << rank);
    }

    // index M1 ranks and M2 ranks, respectively, and then compute from standard formula
    // formula further discussed in docs
    return index_rankset(preflopRankset, M1) + 
           helper::binCoeff(NUM_RANKS, M1) * index_rankset(postflopRankset, M2);
}

// index the cards
template <std::size_t STREETSIZE>
constexpr int index_cards(
    const std::array<uint32_t, HOLE_SIZE + STREETSIZE>& cards)
{
    // find the (ordered) suit configuration <<c11, c12>, <c21, c22>, <c31, c32>, <c41, c42>>
    std::array<SuitData, NUM_SUITS> config = suit_config<STREETSIZE>(cards);

    // for each <ci1, ci2> where i = 0, 1, 2, 3 find its index
    std::array<int, NUM_SUITS> suitIndices {};
    for (int i = 0; i < NUM_SUITS; ++i)
    {
        suitIndices[i] = index_rankset_group(config[i].suitRanksetPreflop, config[i].suitRanksetPostflop);
    }


    int i = 0;
    int localIndex = 0;
    int size = 1;

    // identify the multisets (subsets of the configuration with <ci1, ci2> == <cj1, cj2>)
    while (i < NUM_SUITS)
    {
        int M1 {config[i].numSuitPreflop};
        int M2 {config[i].numSuitPostflop};

        std::array<int, NUM_SUITS> multisetIndices {};
        std::size_t multiplicity = 0;

        // multisetIndices is the array of all indices of the multiset <M1, M2>
        int j = i;
        while (j < NUM_SUITS && config[j].numSuitPreflop == M1 && config[j].numSuitPostflop == M2)
        {
            multisetIndices[multiplicity] = suitIndices[j];
            ++multiplicity;
            ++j;
        }

        // dynamically compute local index over each multiset
        // formula further discussed in docs
        localIndex += size * index_multiset(multisetIndices, multiplicity);
        size *= multiset_colex(std::array<int, 2> {M1, M2}, static_cast<int>(multiplicity));
        i = j;
    }

    // define suit configuration
    const std::array<std::array<int, 2>, NUM_SUITS> suitConfig
    {{
        {config[0].numSuitPreflop, config[0].numSuitPostflop},
        {config[1].numSuitPreflop, config[1].numSuitPostflop},
        {config[2].numSuitPreflop, config[2].numSuitPostflop},
        {config[3].numSuitPreflop, config[3].numSuitPostflop},
    }};

    // find offset (brute force search algorithm)
    const auto findOffset = [](
        const auto& configList,
        const auto& offsetList,
        const auto& config)
    {
        for (std::size_t k = 0; k < configList.size(); ++k)
            if (config == configList[k])
                return offsetList[k];

        return -1;
    };

    int offset {};
    if constexpr (STREETSIZE == FLOP_SIZE)
        offset = findOffset(FLOP_CONFIG_LIST, FLOP_OFFSET_LIST, suitConfig);
    if constexpr (STREETSIZE == TURN_SIZE)
        offset = findOffset(TURN_CONFIG_LIST, TURN_OFFSET_LIST, suitConfig);
    if constexpr (STREETSIZE == RIVER_SIZE)
        offset = findOffset(RIVER_CONFIG_LIST, RIVER_OFFSET_LIST, suitConfig);

    // index = local index + offset
    return offset + localIndex;
}

// UNINDEXING //

struct LocalIndexInfo
{
    std::array<std::array<int, 2>, NUM_SUITS> suitConfig {};
    int localIndex {};
};

// given a global index, find the offset and hence compute the local index
// it also finds the suit configuration that the index belongs to
template <std::size_t STREETSIZE>
LocalIndexInfo get_local(
    const int& index)
{
    // brute force search for finding offset and populating LocalIndexInfo type
    const auto findLocal = [](
        const auto& configList,
        const auto& offsetList,
        const auto& index)
    {
        LocalIndexInfo local {};
        for (int i = static_cast<int>(offsetList.size()) - 1; i >= 0; --i)
        {
            if (index >= offsetList[i])
            {
                local.localIndex = index - offsetList[i];
                local.suitConfig = configList[i];
                return local;
            }
        }

        return local;

    };

    if constexpr (STREETSIZE == FLOP_SIZE)
        return findLocal(FLOP_CONFIG_LIST, FLOP_OFFSET_LIST, index);
    if constexpr (STREETSIZE == TURN_SIZE)
        return findLocal(TURN_CONFIG_LIST, TURN_OFFSET_LIST, index);
    if constexpr (STREETSIZE == RIVER_SIZE)
        return findLocal(RIVER_CONFIG_LIST, RIVER_OFFSET_LIST, index);

}

// reduce suit configurations into their sets unique sets <ci1, ci2> and how often that set appears in the configuration (multiplicity)
struct configInfo
{
    std::array<std::array<int, 2>, NUM_SUITS> uniqueSuitConfig {};
    std::array<int, NUM_SUITS> multiplicity {};
    int count {}; // num of unique <ci1, ci2>
};

// suit configuration -> reduced configuration
inline const configInfo reduce_config(
    const std::array<std::array<int, 2>, NUM_SUITS>& config)
{
    configInfo reducedConfig {};
    int i = 0;
    while(i < NUM_SUITS)
    {
        // find unique sets within the configuration
        int j = i;
        while(j < NUM_SUITS && config[i] == config[j])
            ++j;

        // populate configInfo
        if (config[i] != std::array<int, 2> {0, 0})
        {
            reducedConfig.uniqueSuitConfig[reducedConfig.count] = config[i];
            reducedConfig.multiplicity[reducedConfig.count] = j - i;
            ++reducedConfig.count;
        }
        i = j;
    }

    return reducedConfig;
}

struct ranksetIndicesInfo
{
    std::array<int, NUM_SUITS> values {};
    int count {};
};

// finds the local index of each <ci1, ci2>
// ie. <<c11, c12>, ..., <c41, c42>> -> <index(<c11, c12>), ..., index(<c41, c42>)>
inline const ranksetIndicesInfo get_rankset_indices(
    const int& localIndex,
    const configInfo& reducedConfig)
{
    ranksetIndicesInfo result {};
    if (reducedConfig.count == 0)
        return result;

    result.count = reducedConfig.count;
    int index = localIndex;

    // enumerate over each unique <ci1, ci2> in the configuration
    for (int i = reducedConfig.count - 1; i > 0; --i)
    {
        int size = 1;
        // find size of those multiset spaces of previous <cj1, cj2> < <ci1, ci2>
        for (int j = 0; j < i; ++j)
        {
            size *= multiset_colex(reducedConfig.uniqueSuitConfig[j], reducedConfig.multiplicity[j]);
        }
        // index for <ci1, ci2> is the integer division of the remaining index
        result.values[i] = index / size;
        // subtract index(<ci1, ci2>) * space size from the local index and keep iterating
        index = index % size;
    }

    result.values[0] = index;
    return result;
}

struct suitedIndicesInfo
{
    std::array<int, NUM_SUITS> values {};
    int count {};
};

// given index(<M1, M2>) where <M1, M2> has some multiplicity within the configuration,
// find the index of each individual M1 ranks, M2 ranks within the <M1, M2> space
template <std::size_t STREETSIZE>
suitedIndicesInfo get_suited_indices(
    const int& ranksetIndex,
    const int& multiplicity)
{
    suitedIndicesInfo result {};
    result.count = multiplicity;

    // if the multiset has multiplicity 1 then it is just a set
    if (multiplicity == 1)
    {
        result.values[0] = ranksetIndex;
        return result;
    }

    int index = ranksetIndex;
    for (int k = multiplicity; k > 0; --k)
    {
        int x = k - 1;
        while (helper::binCoeff(x + 1, k) <= index)
            ++x;
            
        result.values[k - 1] = x - (k - 1);
        index -= helper::binCoeff(x, k);
    }

    return result;
}

template <std::size_t STREETSIZE>
struct cardRanksInfo
{
    std::array<int, HOLE_SIZE> preflop {};
    std::array<int, STREETSIZE> postflop {};
    int preflopCount {};
    int postflopCount {};
};

template <std::size_t STREETSIZE>
cardRanksInfo<STREETSIZE> get_suited_card_ranks
(
    const int& suitIndex,
    const std::array<int, 2>& suitConfig
)
{
    const int index = suitIndex;
    const int cardIndexPostflop = index / helper::binCoeff(NUM_RANKS, suitConfig[0]);
    const int numCardsPostflop = suitConfig[1];
    const int cardIndexPreflop = index % helper::binCoeff(NUM_RANKS, suitConfig[0]);
    const int numCardsPreflop = suitConfig[0];

    const auto highest_binCoeff = [] (const int numCards, int index)
    {
        std::array<int, HOLE_SIZE + STREETSIZE> cards {};
        int count = 0;

        for (int i = numCards; i > 0; --i)
        {
            int j = i - 1;
            while (helper::binCoeff(j + 1, i) <= index)
                ++j;

            cards[count++] = j;
            index -= helper::binCoeff(j, i);
        }

        return std::pair{cards, count};
    };

    const auto preflop = highest_binCoeff(numCardsPreflop, cardIndexPreflop);
    const auto postflop = highest_binCoeff(numCardsPostflop, cardIndexPostflop);

    cardRanksInfo<STREETSIZE> result {};
    result.preflopCount = preflop.second;
    result.postflopCount = postflop.second;

    for (int i = 0; i < result.preflopCount; ++i)
        result.preflop[i] = preflop.first[i];

    for (int i = 0; i < result.postflopCount; ++i)
    {
        result.postflop[i] = postflop.first[i];
        for (int j = result.preflopCount - 1; j > -1; --j)
        {
            if (result.preflop[j] <= result.postflop[i]) ++result.postflop[i];
        }
    }

    return result;
}

struct SuitGroup
{
    int preflopCards;
    int postflopCards;
    int Suit;
};

template <std::size_t STREETSIZE>
const std::array<uint32_t, HOLE_SIZE + STREETSIZE> unindex
(
    const int index
)
{
    const LocalIndexInfo local = get_local<STREETSIZE>(index);
    const configInfo parameters = reduce_config(local.suitConfig);
    const auto rankset_indices = get_rankset_indices(local.localIndex, parameters);

    std::array<int, NUM_SUITS> suitIndex {};
    int suitIndexCount = 0;
    for (int i = 0; i < parameters.count; ++i)
    {
        const auto indices = get_suited_indices<STREETSIZE>(rankset_indices.values[i], parameters.multiplicity[i]);
        for (int k = indices.count - 1; k >= 0; --k)
        {
            suitIndex[suitIndexCount++] = indices.values[k];
        }
    }

    std::array<uint32_t, HOLE_SIZE + STREETSIZE> cards {};
    std::size_t preflopWrite = 0;
    std::size_t postflopWrite = 0;

    for (int i = 0; i < NUM_SUITS; ++i)
    {
        const auto suit = local.suitConfig[i];
        if (suit[0] == 0 && suit[1] == 0)
            continue;

        const auto groupedCards = get_suited_card_ranks<STREETSIZE>(suitIndex[i], suit);

        for (int j = 0; j < groupedCards.preflopCount; ++j)
        {
            cards[preflopWrite++] = (1u << (i + NUM_RANKS)) | (1u << groupedCards.preflop[j]);
        }
        for (int j = 0; j < groupedCards.postflopCount; ++j)
        {
            cards[HOLE_SIZE + postflopWrite++] = (1u << (i + NUM_RANKS)) | (1u << groupedCards.postflop[j]);
        }
    }

    return cards;
}

#endif