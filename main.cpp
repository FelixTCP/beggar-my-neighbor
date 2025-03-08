#include <cctype>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <vector>


struct deck {
  const int size = 52;
  std::vector<int> cards;

  void shuffle() {
    cards = std::vector<int>(size, 0);
    for (int card = 1; card < 5; card++) {
        for (int iter = 0; iter < 4; iter++) {
            int pos = rand() % size;
            while (cards.at(pos) != 0) {
              pos = rand() % size;
            }
            cards.at(pos) = card;
        }
    }
  }

  friend std::ostream& operator<<(std::ostream& os, const deck& d) {
    for(auto &i : d.cards) {
        char c;
        switch (i) {
          case 1:
            c = 'J';
            break;
          case 2:
            c = 'Q';
            break;
          case 3:
            c = 'K';
            break;
          case 4:
            c = 'A';
            break;
          default: 
            c = '-';
        }
        os << c;
    }
    return os;
  }
};


struct player {
    int id;
    std::vector<int> cards;
    int played_moves = 0;

    player(int id): id(id) {}

  friend std::ostream& operator<<(std::ostream& os, const player& p) {
    for(auto &i : p.cards) {
        os << i;
    }
    return os;
  }

  bool operator==(const player& p) {
    return id == p.id;
  }
};

struct game {
    deck& d;
    player& p1;
    player& p2;
    
    bool game_over = false;

    int tricks = 0;
    int cards_played_total = 0;
    bool face_card_active = false;
    int remaining_turns_for_trick = 0;


    std::vector<int> cards_played;

    player* active_player;

    game(): d(*(new deck)), p1(*(new player(1))), p2(*(new player(2))) {}

    void start() {
        d.shuffle();
        active_player = &p1;
        split_cards();
    }

    void split_cards() {
      p1.cards = std::vector<int>(d.cards.begin(), d.cards.begin() + d.size / 2);
      p2.cards = std::vector<int>(d.cards.begin() + d.size / 2, d.cards.end());
    }

    std::pair<int, int> play() {
        while (!game_over) {
            turn();
        }
        return std::make_pair(cards_played_total, tricks);
    }

    void reset() {
        game_over = false;
        tricks = 0;
        cards_played_total = 0;
        face_card_active = false;
        remaining_turns_for_trick = 0;
        cards_played.clear();
        p1.cards.clear();
        p2.cards.clear();
    }

    void turn () {
        // check if the game is over
        if (active_player->cards.size() == 0) {
            game_over = true;
            return;
        }

        int card = active_player->cards.front();
        active_player->cards.erase(active_player->cards.begin());
        cards_played.push_back(card);
        cards_played_total++;
        
        bool face_card_played = false;

        // check if the card is a face card
        if (card > 0) {
          face_card_active = true;
          face_card_played = true;
          remaining_turns_for_trick = card;
        } else {
          remaining_turns_for_trick--;
        }

        //std::cout << "Player " << active_player->id << " plays " << card << " and now has " << active_player->cards.size() << " left - " << face_card_played << std::endl;        

        if (!face_card_active || face_card_played) {
            switch_player();
            return;
        }

        if (remaining_turns_for_trick == 0 && face_card_active) {
            tricks++;
            face_card_active = false;
            switch_player();
            active_player->cards.insert(active_player->cards.end(), cards_played.begin(), cards_played.end());
            cards_played.clear();
            //std::cout << "Trick " << tricks << " won by player " << active_player->id << " that now has "<< active_player->cards.size() << " cards." << std::endl;
            //std::cout << *this;
        }
    }
    void switch_player(){
      //std::cout << active_player->id << " " << p1.id << " " << p2.id << std::endl;

      if (active_player->id == p1.id) {
        active_player = &p2;
      } else {
        active_player = &p1;
      }
      //std::cout << "Switching to player " << active_player->id << std::endl;
    }

  friend std::ostream& operator<<(std::ostream& os, const game& g) {
    os << "Player 1 cards: " << g.p1 << std::endl;
    os << "Player 2 cards: " << g.p2 << std::endl;
    return os;
  }
};


int main() {
    srand(time(NULL));
    game g;
    int high_score = 0;

    std::ofstream file("high_score.txt");
    if (!file.is_open()) {
        std::cerr << "Fehler beim Öffnen der Datei 'high_score.txt'" << std::endl;
        return 1;
    }

    int i = 0;
    while (true) {
        g.start();
        auto res = g.play();
        int cards_played_total = res.first;
        int tricks = res.second;

        if (cards_played_total > high_score) {
            high_score = cards_played_total;
            std::cout << "Neuer Highscore: " << high_score << std::endl;
            
            file << high_score << "," << tricks << "," << g.active_player->id <<"," << g.d << std::endl;
            
            if (!file.good()) {
                std::cerr << "Fehler beim Schreiben in die Datei" << std::endl;
                file.close();
                return 1;
            }
        }
        g.reset();
        i++;
    }

    file.close();
    return 0;
}
