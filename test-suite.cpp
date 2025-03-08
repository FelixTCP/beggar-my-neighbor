#include <atomic>
#include <chrono>
#include <condition_variable>
#include <future>
#include <mutex>
#include <queue>
#include <random>
#include <thread>
#include <vector>
#include <iostream>
#include <fstream>
#include <functional>
#include <unordered_set>
#include <string>

// Card representation: 0=non-face card, 1=J, 2=Q, 3=K, 4=A
struct deck {
    static constexpr int size = 52;
    std::vector<int> cards;

    deck() : cards(size, 0) {}

    void shuffle(std::mt19937& rng) {
        std::fill(cards.begin(), cards.end(), 0);
        // Place face cards (4 of each type)
        for (int card = 1; card <= 4; ++card) {
            for (int iter = 0; iter < 4; ++iter) {
                int pos;
                do {
                    pos = std::uniform_int_distribution<>(0, size - 1)(rng);
                } while (cards[pos] != 0);
                cards[pos] = card;
            }
        }
    }

    // Create a deck from a string representation
    static deck from_string(const std::string& str) {
        deck d;
        // Initialize all cards to 0 (non-face cards)
        d.cards.assign(size, 0);
        
        for (size_t i = 0; i < std::min(str.size(), d.cards.size()); ++i) {
            switch (str[i]) {
                case 'J': d.cards[i] = 1; break;
                case 'Q': d.cards[i] = 2; break;
                case 'K': d.cards[i] = 3; break;
                case 'A': d.cards[i] = 4; break;
                default: d.cards[i] = 0;
            }
        }
        return d;
    }

    // Check if the deck is valid (correct number of face cards)
    bool is_valid() const {
        int counts[5] = {0}; // Index 0 for non-face cards, 1-4 for J,Q,K,A
        for (int card : cards) {
            if (card < 0 || card > 4) return false;
            counts[card]++;
        }
        
        // Check if we have exactly 4 of each face card type
        for (int i = 1; i <= 4; ++i) {
            if (counts[i] != 4) return false;
        }
        
        // Check total cards
        if (cards.size() != size) return false;
        
        return true;
    }

    friend std::ostream& operator<<(std::ostream& os, const deck& d) {
        for (auto i : d.cards) {
            char c;
            switch (i) {
                case 1: c = 'J'; break;
                case 2: c = 'Q'; break;
                case 3: c = 'K'; break;
                case 4: c = 'A'; break;
                default: c = '-';
            }
            os << c;
        }
        return os;
    }
};

struct player {
    int id;
    std::vector<int> cards;

    player(int id) : id(id) {}
};

// Game state hash for cycle detection
struct GameStateHash {
    std::size_t operator()(const std::pair<std::vector<int>, std::vector<int>>& state) const {
        std::size_t seed = state.first.size();
        for (auto& i : state.first) {
            seed ^= i + 0x9e3779b9 + (seed << 6) + (seed >> 2);
        }
        for (auto& i : state.second) {
            seed ^= i + 0x9e3779b9 + (seed << 6) + (seed >> 2);
        }
        return seed;
    }
};

class game {
private:
    deck d;
    player p1;
    player p2;
    std::mt19937 rng;
    
    int cards_played_total = 0;
    int tricks = 0;
    std::vector<int> pile;
    int remaining_penalties = 0;
    bool face_card_active = false;
    player* active_player;
    int max_moves;
    bool verbose;
    
    // For cycle detection
    std::unordered_set<std::pair<std::vector<int>, std::vector<int>>, GameStateHash> seen_states;

public:
    game(const deck& initial_deck, int move_limit = 100000, bool verbose_output = false) 
        : d(initial_deck), p1(1), p2(2), 
          rng(std::random_device{}()), 
          max_moves(move_limit),
          verbose(verbose_output) {}

    void start() {
        split_cards();
        active_player = &p1;
        cards_played_total = 0;
        tricks = 0;
        pile.clear();
        remaining_penalties = 0;
        face_card_active = false;
        seen_states.clear();
        
        if (verbose) {
            std::cout << "Starting game with deck: " << d << std::endl;
            std::cout << "Player 1 cards: ";
            for (auto card : p1.cards) {
                switch (card) {
                    case 1: std::cout << "J "; break;
                    case 2: std::cout << "Q "; break;
                    case 3: std::cout << "K "; break;
                    case 4: std::cout << "A "; break;
                    default: std::cout << "- ";
                }
            }
            std::cout << std::endl;
            
            std::cout << "Player 2 cards: ";
            for (auto card : p2.cards) {
                switch (card) {
                    case 1: std::cout << "J "; break;
                    case 2: std::cout << "Q "; break;
                    case 3: std::cout << "K "; break;
                    case 4: std::cout << "A "; break;
                    default: std::cout << "- ";
                }
            }
            std::cout << std::endl;
        }
    }

    void split_cards() {
        auto mid = d.cards.size() / 2;
        p1.cards = std::vector<int>(d.cards.begin(), d.cards.begin() + mid);
        p2.cards = std::vector<int>(d.cards.begin() + mid, d.cards.end());
    }

    std::tuple<int, int, int, bool> play() {
        bool cycled = false;
        
        while (!is_game_over() && cards_played_total < max_moves) {
            // Check for cycles (same cards in same order for both players)
            auto state = std::make_pair(p1.cards, p2.cards);
            if (seen_states.count(state) > 0) {
                // We've seen this exact state before - it's a cycle
                cycled = true;
                break;
            }
            seen_states.insert(state);
            
            turn();
            
            if (verbose && cards_played_total % 100 == 0) {
                std::cout << "Move " << cards_played_total 
                          << ", Player 1: " << p1.cards.size() << " cards"
                          << ", Player 2: " << p2.cards.size() << " cards"
                          << ", Tricks: " << tricks << std::endl;
            }
        }
        
        int winner = -1;
        if (is_game_over()) {
            winner = p1.cards.empty() ? 2 : 1;
        }
        
        return {
            winner, 
            cards_played_total, 
            tricks,
            cycled
        };
    }

    bool is_game_over() const {
        return p1.cards.empty() || p2.cards.empty();
    }

    void turn() {
        if (active_player->cards.empty()) {
            return;
        }

        int card = active_player->cards.front();
        active_player->cards.erase(active_player->cards.begin());
        pile.push_back(card);
        cards_played_total++;
        
        if (verbose) {
            std::cout << "Player " << active_player->id << " plays: ";
            switch (card) {
                case 1: std::cout << "Jack"; break;
                case 2: std::cout << "Queen"; break;
                case 3: std::cout << "King"; break;
                case 4: std::cout << "Ace"; break;
                default: std::cout << "non-face card";
            }
            std::cout << std::endl;
        }
        
        if (card > 0) { // Face card played
            face_card_active = true;
            remaining_penalties = card;
            switch_player();
            
            if (verbose) {
                std::cout << "Face card! Player " << active_player->id 
                          << " must pay " << remaining_penalties << " penalties." << std::endl;
            }
        } else if (face_card_active) {
            remaining_penalties--;
            
            if (verbose) {
                std::cout << "Penalty paid. " << remaining_penalties << " remaining." << std::endl;
            }
            
            if (remaining_penalties == 0) {
                // Trick completed, current player takes all cards
                tricks++;
                face_card_active = false;
                
                // Add cards to the back of player's hand
                active_player->cards.insert(
                    active_player->cards.end(),
                    pile.begin(),
                    pile.end()
                );
                pile.clear();
                
                if (verbose) {
                    std::cout << "Trick completed! Player " << active_player->id 
                              << " takes the pile (" << pile.size() << " cards)" << std::endl;
                }
            } else {
                switch_player();
            }
        } else {
            switch_player();
        }
    }

    void switch_player() {
        active_player = (active_player == &p1) ? &p2 : &p1;
    }
};

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cout << "Usage: " << argv[0] << " <deck-string> [--verbose]" << std::endl;
        std::cout << "Example: " << argv[0] << " \"J--K---A--Q--J---A-K--Q-J--A--K-Q-J---A--Q--K--\"" << std::endl;
        std::cout << "Use '-' for non-face cards and J,Q,K,A for face cards" << std::endl;
        return 1;
    }
    
    std::string deck_str = argv[1];
    bool verbose = false;
    
    for (int i = 2; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "--verbose" || arg == "-v") {
            verbose = true;
        }
    }
    
    // Create a deck from the input string
    deck test_deck = deck::from_string(deck_str);
    
    // Validate the deck
    if (!test_deck.is_valid()) {
        std::cerr << "Error: Invalid deck configuration." << std::endl;
        std::cerr << "A valid deck must have exactly 4 of each face card (J,Q,K,A) and a total of 52 cards." << std::endl;
        return 1;
    }
    
    std::cout << "Testing deck: " << test_deck << std::endl;
    
    // Create a game with the specified deck
    game g(test_deck, 1000000, verbose);
    g.start();
    
    auto start_time = std::chrono::high_resolution_clock::now();
    
    auto [winner, cards_played, tricks, cycled] = g.play();
    
    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time).count();
    
    std::cout << "\nGame results:" << std::endl;
    std::cout << "------------" << std::endl;
    
    if (cycled) {
        std::cout << "Cycle detected after " << cards_played << " moves and " << tricks << " tricks" << std::endl;
    } else if (winner > 0) {
        std::cout << "Player " << winner << " won after " << cards_played << " moves and " << tricks << " tricks" << std::endl;
    } else {
        std::cout << "Game reached move limit (" << cards_played << " moves, " << tricks << " tricks)" << std::endl;
    }
    
    std::cout << "Time elapsed: " << duration_ms << " ms" << std::endl;
    
    return 0;
}
