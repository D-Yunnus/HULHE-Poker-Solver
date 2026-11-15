#ifndef GENERATE_HISTORIES_H
#define GENERATE_HISTORIES_H

#include <map>
#include <array>
#include <vector>
#include <algorithm>

#include "constants/constants.h"

enum Streets
{
    PREFLOP = 0,
    FLOP,
    TURN,
    RIVER
};

enum Actions
{
    FOLD = 0,
    CALL,
    RAISE
};

// identify what action is legal to play next
inline std::array<bool, NUM_ACTIONS> legal_actions
(
    const std::vector<Actions>& streetHistory
)
{
    // look at the current streets history and count the number of times raise was called
    const int numRaises = std::count(streetHistory.begin(), streetHistory.end(), RAISE);
    // can only raise if the number of raises is less than the raise cap
    return std::array<bool, NUM_ACTIONS> {true, true, numRaises < RAISE_CAP};
}

// depth-first search to find possible histories at a given betting round
inline void generate_map
(
    std::map<std::vector<Actions>, int>& histories,
    std::vector<Actions>& history,
    int& count
)
{
    // most recent action taken at a given history
    Actions lastAction = history[history.size() - 1];
    // next possible actions that are legal
    std::array<bool, NUM_ACTIONS> legalActions = legal_actions(history);

    // enumerate over next possible actions
    for (int i = 0; i < NUM_ACTIONS; ++i)
    {
        // no further action possible if history's most recent action was a fold
        if (lastAction == FOLD) break;
        // skip non-legal actions
        if (legalActions[i] == false) continue;

        // next action
        Actions nextAction = static_cast<Actions>(i);
        // add new action to the recent history to create a new history
        history.push_back(nextAction);
        ++count;
        // add to the list of history's
        histories.emplace(history, count);

        // terminal histories so no such histories can branch from here
        if (nextAction == FOLD || nextAction == CALL)
        {
            history.pop_back();
            continue;
        }

        // otherwise iterate recursively: new history -> recent history
        generate_map(histories, history, count);
        history.pop_back();
    }
}

// global map of possible betting action histories on a single street
// each history of bet actions correspond to a number
std::map<std::vector<Actions>, int> HISTORIES;

// load HISTORIES
inline void load_histories()
{
    // special case: no bets made yet
    std::map<std::vector<Actions>, int> histories = {{{}, 0}};

    std::vector<Actions> history (6);
    int count = 0;
    
    // special case: first action on the betting round is fold
    history = {FOLD};
    histories.emplace(history, ++count);

    // generate all histories with the first action call
    history[0] = {CALL};
    histories.emplace(history, ++count);
    generate_map(histories, history, count);

    // generate all histories with the first action raise
    history[0] = {RAISE};
    histories.emplace(history, ++count);
    generate_map(histories, history, count);

    // index the map in order
    int idx = 0;
    for (auto& [betHistory, id] : histories)
    {
        (void)betHistory;
        id = idx++;
    }

    // move map to correspond to global HISTORIES
    HISTORIES = std::move(histories);
}


#endif