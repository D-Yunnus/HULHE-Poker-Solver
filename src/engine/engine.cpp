#include <iostream>
#include <array>

#include "cfr/betting.h"
#include "cfr/cfr.h"
#include "constants/constants.h"
#include "metrics/equity.h"
#include "evaluation/hand_evaluation.h"
#include "cfr/loader.h"

void printCard
(
    const uint32_t card
)
{
        const uint32_t rank {card & 0x1FFF};
        const uint32_t suit {card & 0x1E000};

        switch (rank)
        {
        case 0x1:
            std::cout << "2 ";
            break;
        case 0x2:
            std::cout << "3 ";
            break;
        case 0x4:
            std::cout << "4 ";
            break;
        case 0x8:
            std::cout << "5 ";
            break;
        case 0x10:
            std::cout << "6 ";
            break;
        case 0x20:
            std::cout << "7 ";
            break;
        case 0x40:
            std::cout << "8 ";
            break;
        case 0x80:
            std::cout << "9 ";
            break;
        case 0x100:
            std::cout << "10 ";
            break;
        case 0x200:
            std::cout << "J ";
            break;
        case 0x400:
            std::cout << "Q ";
            break;
        case 0x800:
            std::cout << "K ";
            break;
        case 0x1000:
            std::cout << "A ";
            break;
        }

        switch (suit)
        {
        case 0x2000:
            std::cout << "♠";
            break;
        case 0x4000:
            std::cout << "♥";
            break;
        case 0x8000:
            std::cout << "♦";
            break;
        case 0x10000:
            std::cout << "♣";
            break;
        }
}

void printAction
(
    const Actions a
)
{
    switch (a)
    {
    case FOLD:
        std::cout << "Fold";
        break;
    case CALL:
        std::cout << "Call";
        break;
    case RAISE:
        std::cout << "Raise";
        break;
    default:
        break;
    }
}

class Engine
{
    public:

        History gameHistory = 
        {
            .street = PREFLOP,
            .betHistories = 
                {
                    std::vector<Actions> {},
                    std::vector<Actions> {},
                    std::vector<Actions> {},
                    std::vector<Actions> {}
                }
        };

        std::array<uint32_t, HOLE_SIZE + RIVER_SIZE> playerCards {};
        std::array<uint32_t, HOLE_SIZE + RIVER_SIZE> aiCards {};
        int playerBlind {};
        int aiBlind {};
        double playerBets = 0;
        double aiBets = 0;
        double totalPlayerGain = 0;
        double totalAIGain = 0;

        std::mt19937& randGen = rng();
        std::vector<uint32_t> deck = construct_deck(std::array<uint32_t, 0> {});

        void game()
        {
            player_blind();
            initial_draw();

            bool isTerminal = false;
            do
            {
                const int playerBetOrder = gameHistory.street == PREFLOP ? playerBlind : 1 - playerBlind;
                const int aiBetOrder = gameHistory.street == PREFLOP ? aiBlind : 1 - aiBlind;
                std::vector<Actions>& currStreetHistory = gameHistory.betHistories[gameHistory.street];
                const int actingPlayer = currStreetHistory.size() % 2;
                if (actingPlayer == playerBetOrder)
                {
                    std::cout << "Player: ";
                    player_choose_action();
                }
                else
                {
                    std::cout << "Fregly: ";
                    ai_choose_action();
                }

                isTerminal = is_terminal();
                chance_node();
            } while (!isTerminal);
            terminal_node();
            
        }

        void reset()
        {
            gameHistory = 
            {
                .street = PREFLOP,
                .betHistories = 
                    {
                        std::vector<Actions> {},
                        std::vector<Actions> {},
                        std::vector<Actions> {},
                        std::vector<Actions> {}
                    }
            };

            playerCards = std::array<uint32_t, HOLE_SIZE + RIVER_SIZE> {};
            aiCards = std::array<uint32_t, HOLE_SIZE + RIVER_SIZE> {};
            playerBlind = 0;
            aiBlind = 0;
            playerBets = 0;
            aiBets = 0;
            deck = construct_deck(std::array<uint32_t, 0> {});
        }

    private:

        void player_blind()
        {
            std::cout << "Do you want to be the Small Blind [S] or Big Blind[B]? ";
            bool in = true;
            do
            {
                char response {};
                std::cin >> response;
                switch (response)
                {
                case 'S':
                {
                    playerBlind = 0;
                    aiBlind = 1;
                    in = false;
                    break;
                }
                case 'B':
                {
                    playerBlind = 1;
                    aiBlind = 0;
                    in = false;
                    break;
                }
                default:
                {
                    std::cout << "Incorrect Input, Try Again.\n";
                    break;
                }
                }
            } while (in);
        }

        void initial_draw()
        {
            std::cout << "Dealing Cards...\n";

            this->playerCards[0] = draw_card(deck, randGen);
            this->playerCards[1] = draw_card(deck, randGen);

            std::cout << "Your cards: ";
            for (int i = 0; i < HOLE_SIZE; ++i)
            {
                printCard(this->playerCards[i]);
                std::cout << "  ";
            }
            std::cout << '\n';

            this->aiCards[0] = draw_card(deck, randGen);
            this->aiCards[1] = draw_card(deck, randGen);
        }

        void player_choose_action()
        {
            std::vector<Actions>& streetHistory = gameHistory.betHistories[static_cast<int>(gameHistory.street)];
            bool in = true;
            do
            {
                const std::array<bool, NUM_ACTIONS> legalActions = legal_actions(streetHistory);
                std::cout << "What action do you want to play?\n\t";
                for (int i = 0; i < NUM_ACTIONS; ++i)
                {
                    if (!legalActions[i]) continue;
                    const Actions a = static_cast<Actions>(i);
                    switch (a)
                    {
                    case FOLD:
                        std::cout << "Fold[F]   ";
                        break;
                    case CALL:
                        std::cout << "Call/Check[C]   ";
                        break;
                    case RAISE:
                        std::cout << "Raise[R]   ";
                        break;
                    default:
                        break;
                    }
                }
                std::cout << '\n';

                char playerAction {};
                std::cin >> playerAction;
                switch (playerAction)
                {
                case 'F':
                {
                    if (legalActions[1])
                    {
                        streetHistory.push_back(FOLD);
                        in = false;
                        break;
                    }
                    std::cout << "Incorrect Input, Try Again.\n";
                    break;
                }
                case 'C':
                {
                    if (legalActions[0]) 
                    {
                        streetHistory.push_back(CALL);
                        in = false;
                        break;
                    }
                    std::cout << "Incorrect Input, Try Again.\n";
                    break;
                }
                case 'R':
                {
                    if (legalActions[2])
                    {
                        streetHistory.push_back(RAISE);
                        in  = false;
                        break;

                    }
                    std::cout << "Incorrect Input, Try Again?\n";
                    break;
                }
                default: std::cout << "Incorrect Input, Try Again.\n";
                }
            } while (in);
        }

        void ai_choose_action()
        {
            int bucket {};
            switch (gameHistory.street)
            {
            case PREFLOP:
            {
                bucket = index_hole_cards(aiCards);
                break;
            }
            case FLOP:
            {
                std::array<uint32_t, HOLE_SIZE + FLOP_SIZE> cards = {};
                for (int i = 0; i < HOLE_SIZE + FLOP_SIZE; ++i)
                    cards[i] = aiCards[i];

                bucket = FLOP_BUCKETS[index_cards<FLOP_SIZE>(cards)];
                break;
            }
            case TURN:
            {
                std::array<uint32_t, HOLE_SIZE + TURN_SIZE> cards = {};
                for (int i = 0; i < HOLE_SIZE + TURN_SIZE; ++i)
                    cards[i] = aiCards[i];

                bucket = TURN_BUCKETS[index_cards<TURN_SIZE>(cards)];
                break;
            }
            case RIVER:
            {
                std::array<uint32_t, HOLE_SIZE + RIVER_SIZE> cards = {};
                for (int i = 0; i < HOLE_SIZE + RIVER_SIZE; ++i)
                    cards[i] = aiCards[i];

                bucket = RIVER_BUCKETS[index_cards<RIVER_SIZE>(cards)];
                break;
            }
            default:
                break;
            }

            unsigned long long int infoIndex = index_infoset(InfoSet {.history = gameHistory, .bucketindex = bucket});
            std::array<double, NUM_ACTIONS> strategy = NODEMAP.find(infoIndex)->second.stratSum;

            double normalisation = 0;
            std::array<bool, NUM_ACTIONS> legalActions = legal_actions(gameHistory.betHistories[gameHistory.street]);
            int numLegalActions = NUM_ACTIONS;
            for (int i = 0; i < NUM_ACTIONS; ++i)
            {
                if (!legalActions[i])
                {
                    --numLegalActions;
                    continue;
                }
                normalisation += strategy[i];
            }

            std::cout << normalisation << '\t';
            std::uniform_real_distribution<> dist(0, 1);
            double randNum = dist(randGen);
            std::cout << randNum << '\t';
            double lowerbound = 0;
            double upperbound = 0;
            Actions a = CALL;
            std::cout << "(  ";
            for (int i = 0; i < NUM_ACTIONS; ++i)
            {
                if (!legalActions[i]) continue;
                lowerbound = upperbound;
                upperbound += normalisation > 0.0 ? strategy[i] / normalisation : 1 / numLegalActions;
                if (lowerbound < randNum && randNum <= upperbound)
                {
                    a = static_cast<Actions>(i);
                }
                printAction(static_cast<Actions>(i));
                std::cout << " = " << upperbound;
                std::cout << "  ";
            }
            std::cout << ")\n";

            gameHistory.betHistories[gameHistory.street].push_back(a);
            std::cout << "I ";
            printAction(a);
            std::cout << '\n';    
        }

        bool check_round_end()
        {
            const Streets& currStreet = gameHistory.street;
            const std::vector<Actions>& currStreetHistory = gameHistory.betHistories[static_cast<int>(gameHistory.street)];

            const int numActions = currStreetHistory.size();
            if (numActions >= 2)
                if (currStreetHistory[numActions - 1] == CALL) return true;

            return false;
        }

        void chance_node()
        {
            Streets& currStreet = gameHistory.street;

            const bool isChanceNode = check_round_end();
            if (!isChanceNode || currStreet == RIVER) return;

            std::cout << "End of Betting Round...\n";
            std::cout << "Drawing Cards...\n";
            std::cout << "Board:\t";
            switch (currStreet)
            {
            case PREFLOP:
            {
                for (int i = 0; i < FLOP_SIZE; ++i)
                {
                    playerCards[i + HOLE_SIZE] = draw_card(deck, randGen);
                    aiCards[i + HOLE_SIZE] = playerCards[i + HOLE_SIZE];

                    printCard(playerCards[i + HOLE_SIZE]);
                    std::cout << "  ";
                }
                std::cout << '\n';

                break;
            }
            case FLOP:
            {
                playerCards[HOLE_SIZE + TURN_SIZE - 1] = draw_card(deck, randGen);
                aiCards[HOLE_SIZE + TURN_SIZE - 1] = playerCards[HOLE_SIZE + TURN_SIZE - 1];
                
                for (int i = 0; i < TURN_SIZE; ++i)
                {
                    printCard(playerCards[i + HOLE_SIZE]);
                    std::cout << "  ";
                }
                std::cout << '\n';

                break;
            }
            case TURN:
            {
                playerCards[HOLE_SIZE + RIVER_SIZE - 1] = draw_card(deck, randGen);
                aiCards[HOLE_SIZE + RIVER_SIZE - 1] = playerCards[HOLE_SIZE + RIVER_SIZE - 1];

                for (int i = 0; i < RIVER_SIZE; ++i)
                {
                    printCard(playerCards[i + HOLE_SIZE]);
                    std::cout << "  ";
                }
                std::cout << '\n';
                
                break;
            }
            default:
                return;
            }

            currStreet = static_cast<Streets>(static_cast<int>(currStreet) + 1);
        }

        bool is_terminal()
        {
            const Streets& currStreet = gameHistory.street;
            const std::vector<Actions>& currStreetHistory = gameHistory.betHistories[static_cast<int>(gameHistory.street)];
            const int numActions = currStreetHistory.size();

            return (currStreet == RIVER && check_round_end()) || (currStreetHistory.back() == FOLD);
        }

        void terminal_node()
        {
            std::cout << "End of the Game!\n";
            std::cout << "Opponent cards reveal: ";
            for (int i = 0; i < HOLE_SIZE; ++i)
            {
                printCard(this->aiCards[i]);
                std::cout << "  ";
            }
            std::cout << '\n';


            std::array<double, NUM_PLAYERS> bets = calculate_bets(gameHistory);
            playerBets = playerBlind == 0 ? bets[0] : bets[1];
            aiBets = aiBlind == 0 ? bets[0] : bets[1];
            
            std::vector<Actions>& currStreetHistory = gameHistory.betHistories[static_cast<int>(gameHistory.street)];
            int numActions = currStreetHistory.size();
            if (currStreetHistory[numActions - 1] == FOLD)
            {
                int foldingBlind = gameHistory.street == PREFLOP ? (numActions - 1) % 2 : 1 -  (numActions - 1) % 2;
                totalPlayerGain = playerBlind == foldingBlind ? totalPlayerGain - playerBets : totalPlayerGain + playerBets;
                totalAIGain = aiBlind == foldingBlind ? totalAIGain - aiBets : totalAIGain + aiBets;
            }
            else
            {
                const winStatus status = compare_hands(determine_score(playerCards), determine_score(aiCards));
                if (status == WIN)
                {
                    std::cout << "Player Win!\n";
                    totalPlayerGain += playerBets;
                    totalAIGain -= aiBets;
                }
                else if (status == LOSS)
                {
                    std::cout << "Ai Win!\n";
                    totalPlayerGain -= playerBets;
                    totalAIGain += aiBets;
                };
            }
            std::cout << "Player total: " << totalPlayerGain << '\t';
            std::cout << "Ai total: " << totalAIGain << '\n';
        }
};

void play()
{

    load_histories();
    load_trained_poker_maps();

    Engine gameEngine {};
    gameEngine.game();

    char response {};
    bool in = true;
    while (in)
    {
        std::cout << "Do you want to play again [Y/N]? ";
        std::cin >> response;
        switch (response)
        {
        case 'N':
        {
            std::cout << "Bye.\n";
            in = false;
            break;
        }
        case 'Y':
        {
            std::cout << "Okay.\n";
            gameEngine.reset();
            gameEngine.game();
            break;
        }
        default: std::cout << "Incorrect Input, Try Again.\n";
        }
    };

}

int main()
{

    play();

    return 0;
}