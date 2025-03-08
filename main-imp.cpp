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

// Thread pool for managing worker threads
class ThreadPool {
private:
    std::vector<std::thread> workers;
    std::queue<std::function<void()>> tasks;
    std::mutex queue_mutex;
    std::condition_variable condition;
    bool stop;

public:
    ThreadPool(size_t num_threads) : stop(false) {
        workers.reserve(num_threads);
        for (size_t i = 0; i < num_threads; ++i) {
            workers.emplace_back([this] {
                while (true) {
                    std::function<void()> task;
                    {
                        std::unique_lock<std::mutex> lock(queue_mutex);
                        condition.wait(lock, [this] { return stop || !tasks.empty(); });
                        if (stop && tasks.empty()) return;
                        task = std::move(tasks.front());
                        tasks.pop();
                    }
                    task();
                }
            });
        }
    }

    ~ThreadPool() {
        {
            std::unique_lock<std::mutex> lock(queue_mutex);
            stop = true;
        }
        condition.notify_all();
        for (auto& worker : workers) {
            if (worker.joinable()) {
                worker.join();
            }
        }
    }

    template<typename F, typename... Args>
    auto enqueue(F&& f, Args&&... args) -> std::future<typename std::invoke_result<F, Args...>::type> {
        using return_type = typename std::invoke_result<F, Args...>::type;
        auto task = std::make_shared<std::packaged_task<return_type()>>(
            std::bind(std::forward<F>(f), std::forward<Args>(args)...)
        );
        std::future<return_type> res = task->get_future();
        {
            std::unique_lock<std::mutex> lock(queue_mutex);
            tasks.emplace([task]() { (*task)(); });
        }
        condition.notify_one();
        return res;
    }
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
    int max_moves = 10000; // Limit to prevent infinite games
    
    // For cycle detection
    std::unordered_set<std::pair<std::vector<int>, std::vector<int>>, GameStateHash> seen_states;

public:
    game() : p1(1), p2(2), rng(std::random_device{}()) {}

    void start() {
        d.shuffle(rng);
        split_cards();
        active_player = &p1;
        cards_played_total = 0;
        tricks = 0;
        pile.clear();
        remaining_penalties = 0;
        face_card_active = false;
        seen_states.clear();
    }

    void split_cards() {
        auto mid = d.cards.size() / 2;
        p1.cards = std::vector<int>(d.cards.begin(), d.cards.begin() + mid);
        p2.cards = std::vector<int>(d.cards.begin() + mid, d.cards.end());
    }

    std::tuple<int, int, int, deck> play() {
        while (!is_game_over() && cards_played_total < max_moves) {
            // Check for cycles (same cards in same order for both players)
            auto state = std::make_pair(p1.cards, p2.cards);
            if (seen_states.count(state) > 0) {
                // We've seen this exact state before - it's a cycle
                return {-1, cards_played_total, tricks, d};
            }
            seen_states.insert(state);
            
            turn();
        }
        
        return {
            active_player->id, 
            cards_played_total, 
            tricks,
            d  // Return the deck that was used for this game
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
        
        if (card > 0) { // Face card played
            face_card_active = true;
            remaining_penalties = card;
            switch_player();
        } else if (face_card_active) {
            remaining_penalties--;
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

// Function to run a single game simulation
std::tuple<int, int, int, deck> run_game_simulation() {
    game g;
    g.start();
    return g.play();
}

int main(int argc, char* argv[]) {
    long num_games = 100000;
    int num_threads = std::thread::hardware_concurrency();
    
    // Parse command line arguments
    if (argc > 1) {
        num_games = std::stoi(argv[1]);
    }
    if (argc > 2) {
        num_threads = std::stoi(argv[2]);
    }
    
    std::cout << "Running " << num_games << " games with " << num_threads << " threads\n";
    
    std::ofstream file("high_score.txt", std::ios_base::app);
    if (!file.is_open()) {
        std::cerr << "Error opening file 'high_score.txt'" << std::endl;
        return 1;
    }

    ThreadPool pool(num_threads);

    int high_score = 0;
    int games_completed = 0;
    auto start_time = std::chrono::high_resolution_clock::now();

    std::vector<std::future<std::tuple<int, int, int, deck>>> results;
    results.reserve(num_games);

    // Start all game simulations
    for (long i = 0; i < num_games; ++i) {
        results.push_back(pool.enqueue(run_game_simulation));
    }

    // Collect results
    for (auto& result : results) {
        try {
            auto [winner, cards_played, tricks, game_deck] = result.get();
            games_completed++;
            
            // Only record valid games (not cycles)
            if (winner > 0 && cards_played > high_score) {
                high_score = cards_played;
                
                std::cout << "New high score: " << high_score 
                          << " cards, " << tricks << " tricks, winner: Player " 
                          << winner << std::endl;
                
                file << high_score << "," << tricks << "," << winner << "," << game_deck << "\n";
                file.flush();
            }
            
            // Progress update
            if (games_completed % 10000 == 0) {
                auto now = std::chrono::high_resolution_clock::now();
                auto duration = std::chrono::duration_cast<std::chrono::seconds>(now - start_time).count();
                std::cout << "Completed " << games_completed << " games. "
                          << "Games per second: " << (games_completed / (duration + 0.1)) << std::endl;
            }
        } catch (const std::exception& e) {
            std::cerr << "Error in game simulation: " << e.what() << std::endl;
        }
    }

    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::seconds>(end_time - start_time).count();
    
    std::cout << "Completed " << games_completed << " games in " << duration << " seconds" << std::endl;
    std::cout << "Games per second: " << (games_completed / (duration + 0.1)) << std::endl;
    std::cout << "Highest score: " << high_score << std::endl;

    file.close();
    return 0;
}
