#include <znc/Client.h>
#include <znc/Chan.h>
#include <znc/IRCNetwork.h>
#include <znc/Modules.h>
#include <vector>
#include <string>
#include <cstdlib>
#include <ctime>
#include <algorithm>

using namespace std;

static inline string CardValue(const string& card) {
    if (card.size() >= 2 && card[0] == '1' && card[1] == '0') return "10"; // "10"
    return card.empty() ? string() : string(1, card[0]);
}

class CBlackjackGame {
public:
    vector<string> playerCards;
    vector<string> playerCards2;
    vector<string> dealerCards;
    vector<string> deck;
    bool gameActive;
    bool waitingForAction;
    bool hasSplit;
    string currentPlayer;
    string currentChannel;
    time_t lastActionTime;
    int currentHand; // 1 or 2 when split

    CBlackjackGame() : gameActive(false), waitingForAction(false),
                       hasSplit(false), currentHand(1), lastActionTime(0) {
        InitializeDeck();
    }

    void InitializeDeck() {
        deck.clear();
        string suits[] = {"\xE2\x99\xA5", "\xE2\x99\xA6", "\xE2\x99\xA3", "\xE2\x99\xA0"}; // ♥ ♦ ♣ ♠ (UTF-8)
        string values[] = {"2", "3", "4", "5", "6", "7", "8", "9", "10", "J", "Q", "K", "A"};

        for (const auto& suit : suits) {
            for (const auto& value : values) {
                deck.push_back(value + suit);
            }
        }
        ShuffleDeck();
    }

    void ShuffleDeck() {
        srand((unsigned int)time(0));
        for (size_t i = 0; i < deck.size(); i++) {
            size_t j = (size_t)(rand() % deck.size());
            swap(deck[i], deck[j]);
        }
    }

    string DrawCard() {
        if (deck.empty()) {
            InitializeDeck();
        }
        string card = deck.back();
        deck.pop_back();
        return card;
    }

    int CalculateScore(const vector<string>& cards) {
        int score = 0;
        int aces = 0;

        for (const auto& card : cards) {
            if (card.empty()) continue;
            string v = CardValue(card);

            if (v == "A") {
                aces++;
                score += 1; // count A as 1 first
            } else if (v == "K" || v == "Q" || v == "J" || v == "10") {
                score += 10;
            } else {
                // 2..9
                score += atoi(v.c_str());
            }
        }

        // Promote Aces to 11 where possible
        for (int i = 0; i < aces; i++) {
            if (score + 10 <= 21) score += 10;
        }
        return score;
    }

    bool CanSplit() {
        if (playerCards.size() != 2) return false;
        return CardValue(playerCards[0]) == CardValue(playerCards[1]);
    }

    void StartGame(const string& player, const string& channel) {
        playerCards.clear();
        playerCards2.clear();
        dealerCards.clear();
        InitializeDeck();
        gameActive = true;
        waitingForAction = true;
        hasSplit = false;
        currentPlayer = player;
        currentChannel = channel;
        currentHand = 1;
        lastActionTime = time(0);

        playerCards.push_back(DrawCard());
        playerCards.push_back(DrawCard());
        dealerCards.push_back(DrawCard());
        dealerCards.push_back(DrawCard());
    }

    void DoSplit() {
        if (!CanSplit()) return;
        hasSplit = true;
        playerCards2.push_back(playerCards[1]);
        playerCards.pop_back();
        playerCards.push_back(DrawCard());
        playerCards2.push_back(DrawCard());
    }

    string GetCardsString(const vector<string>& cards, bool hideFirst = false) {
        string result;
        for (size_t i = 0; i < cards.size(); i++) {
            if (hideFirst && i == 0) result += "?? ";
            else result += cards[i] + " ";
        }
        return result;
    }

    bool IsTimeOut() {
        return difftime(time(0), lastActionTime) > 30.0;
    }

    int SecondsLeft() {
        int left = (int)(30 - difftime(time(0), lastActionTime));
        return left < 0 ? 0 : left;
    }

    void UpdateActionTime() { lastActionTime = time(0); }
};

class CBlackjackMod;

class CActionTimer : public CTimer {
public:
    CActionTimer(CModule* pModule, unsigned int seconds)
        : CTimer(pModule, seconds, 1, "bj_action_timeout", "Blackjack action timeout") {}
    void RunJob() override;
};

class CNextRoundTimer : public CTimer {
public:
    CNextRoundTimer(CModule* pModule, unsigned int seconds)
        : CTimer(pModule, seconds, 1, "bj_next_round", "Blackjack next round delay") {}
    void RunJob() override;
};
        
class CBlackjackMod : public CModule {
private:
    CBlackjackGame game;

    void ResetActionTimer(unsigned int seconds = 30) {
        RemTimer("bj_action_timeout");
        AddTimer(new CActionTimer(this, seconds));
    }
    void StopActionTimer() { RemTimer("bj_action_timeout"); }

    void StartNextRoundTimer(unsigned int seconds = 10) {
        RemTimer("bj_next_round");
        AddTimer(new CNextRoundTimer(this, seconds));
    }

public:
    MODCONSTRUCTOR(CBlackjackMod) {
        PutModule("BlackjackMod loaded! Ketik !blackjack untuk mulai.");
    }

    // Called by timers
    void OnActionTimeout() {
        if (game.gameActive && game.waitingForAction) {
            PutIRC("PRIVMSG " + game.currentChannel + " :\xE2\x8F\xB0 Timeout 30s! Game dibatalkan karena tidak ada aksi.");
            game.gameActive = false;
            game.waitingForAction = false;
            StartNextRoundTimer(8);
        }
    }

    void OnNextRound() {
        if (!game.gameActive) {
            PutIRC("PRIVMSG " + (game.currentChannel.empty() ? CString("") : CString(game.currentChannel)) +
                   " :\xF0\x9F\x8E\xB2 Round baru siap! Ketik !blackjack untuk bermain lagi.");
        }
    }

    void ShowHelp(const string& channelName) {
        PutIRC("PRIVMSG " + channelName + " :\xF0\x9F\x8E\xB0 Blackjack Commands:");
        PutIRC("PRIVMSG " + channelName + " :!blackjack - Mulai permainan baru");
        PutIRC("PRIVMSG " + channelName + " :!hit       - Ambil kartu");
        PutIRC("PRIVMSG " + channelName + " :!stand     - Berhenti");
        PutIRC("PRIVMSG " + channelName + " :!split     - Split (kalau nilainya sama)");
        PutIRC("PRIVMSG " + channelName + " :!bjhelp    - Tampilkan bantuan");
        PutIRC("PRIVMSG " + channelName + " :!bjstatus  - Cek status & sisa waktu");
    }

    EModRet OnChanTextMessage(CTextMessage& Message) override {
        const CNick& Nick = Message.GetNick();
        CString sMessage = Message.GetText();
        CChan& Channel = *Message.GetChan();
        string player = Nick.GetNick();
        string channelName = Channel.GetName();

        if (sMessage.Equals("!bjhelp", false) || sMessage.Equals("!help", false)) {
            ShowHelp(channelName);
            return HALT;
        }

        if (sMessage.Equals("!bjstatus", false)) {
            if (game.gameActive) {
                string status = "Game aktif - giliran " + game.currentPlayer;
                if (game.hasSplit) status += " (Hand " + to_string(game.currentHand) + "/2)";
                status += " - " + to_string(game.SecondsLeft()) + "s tersisa";
                PutIRC("PRIVMSG " + channelName + " :" + status);
            } else {
                PutIRC("PRIVMSG " + channelName + " :Tidak ada game. Ketik !blackjack untuk mulai");
            }
            return HALT;
        }

        if (game.gameActive && game.IsTimeOut()) {
            PutIRC("PRIVMSG " + channelName + " :\xE2\x8F\xB0 Timeout 30s! Game dibatalkan.");
            game.gameActive = false;
            game.waitingForAction = false;
            StopActionTimer();
            StartNextRoundTimer(8);
            return HALT;
        }

        if (sMessage.Equals("!blackjack", false)) {
            if (game.gameActive) {
                PutIRC("PRIVMSG " + channelName + " :Game sedang berlangsung oleh " + game.currentPlayer + "!");
            } else {
                game.StartGame(player, channelName);
                ResetActionTimer();

                string playerHand = game.GetCardsString(game.playerCards);
                string dealerHand = game.GetCardsString(game.dealerCards, true);
                int playerScore = game.CalculateScore(game.playerCards);

                PutIRC("PRIVMSG " + channelName + " :\xF0\x9F\x8E\xB2 " + player + " memulai Blackjack!");
                PutIRC("PRIVMSG " + channelName + " :Kartumu: " + playerHand + " (Skor: " + to_string(playerScore) + ")");
                PutIRC("PRIVMSG " + channelName + " :Kartu dealer: " + dealerHand);

                if (game.CanSplit()) PutIRC("PRIVMSG " + channelName + " :\xF0\x9F\x92\x8E Kamu bisa !split kartu kembar!");
                PutIRC("PRIVMSG " + channelName + " :Ketik !hit, !stand" + (game.CanSplit()? ", !split" : "") + " dalam 30 detik");
            }
            return HALT;
        }

        if (!game.gameActive || game.currentChannel != channelName) return CONTINUE;

        if (game.currentPlayer != player) {
            PutIRC("PRIVMSG " + channelName + " :Bukan giliranmu! Giliran " + game.currentPlayer + ".");
            return HALT;
        }

        game.UpdateActionTime();
        ResetActionTimer();

        if (sMessage.Equals("!split", false) && game.waitingForAction) {
            if (game.CanSplit()) {
                game.DoSplit();
                string hand1 = game.GetCardsString(game.playerCards);
                string hand2 = game.GetCardsString(game.playerCards2);
                int score1 = game.CalculateScore(game.playerCards);
                int score2 = game.CalculateScore(game.playerCards2);

                PutIRC("PRIVMSG " + channelName + " :\xF0\x9F\x92\x8E " + player + " melakukan split!");
                PutIRC("PRIVMSG " + channelName + " :Hand 1: " + hand1 + " (Skor: " + to_string(score1) + ")");
                PutIRC("PRIVMSG " + channelName + " :Hand 2: " + hand2 + " (Skor: " + to_string(score2) + ")");
                PutIRC("PRIVMSG " + channelName + " :Mainkan Hand 1 dulu - ketik !hit atau !stand");
            } else {
                PutIRC("PRIVMSG " + channelName + " :Tidak bisa split - nilai kartu tidak sama");
            }
            return HALT;
        }

        if (sMessage.Equals("!hit", false) && game.waitingForAction) {
            vector<string>& cur = (game.currentHand == 1) ? game.playerCards : game.playerCards2;
            string newCard = game.DrawCard();
            cur.push_back(newCard);
            int score = game.CalculateScore(cur);
            string hand = game.GetCardsString(cur);

            string handInfo = game.hasSplit ? (" (Hand " + to_string(game.currentHand) + "/2)") : "";
            PutIRC("PRIVMSG " + channelName + " :\xE2\xAC\x87\xEF\xB8\x8F " + player + " hit" + handInfo + ", dapat: " + newCard);
            PutIRC("PRIVMSG " + channelName + " :Hand sekarang: " + hand + " (Skor: " + to_string(score) + ")");

            if (score > 21) {
                PutIRC("PRIVMSG " + channelName + " :\xF0\x9F\x92\xA5 " + player + " bust!" + handInfo);
                if (game.hasSplit && game.currentHand == 1) {
                    game.currentHand = 2;
                    string hand2 = game.GetCardsString(game.playerCards2);
                    int score2 = game.CalculateScore(game.playerCards2);
                    PutIRC("PRIVMSG " + channelName + " :Sekarang main Hand 2: " + hand2 + " (Skor: " + to_string(score2) + ")");
                    PutIRC("PRIVMSG " + channelName + " :Ketik !hit atau !stand untuk Hand 2");
                } else {
                    EndGame(channelName);
                }
            }
            return HALT;
        }

        if (sMessage.Equals("!stand", false) && game.waitingForAction) {
            vector<string>& cur = (game.currentHand == 1) ? game.playerCards : game.playerCards2;
            int score = game.CalculateScore(cur);
            string hand = game.GetCardsString(cur);
            string handInfo = game.hasSplit ? (" (Hand " + to_string(game.currentHand) + "/2)") : "";

            PutIRC("PRIVMSG " + channelName + " :\xE2\x9C\x85 " + player + " stand: " + hand + " (Skor: " + to_string(score) + ")" + handInfo);

            if (game.hasSplit && game.currentHand == 1) {
                game.currentHand = 2;
                string hand2 = game.GetCardsString(game.playerCards2);
                int score2 = game.CalculateScore(game.playerCards2);
                PutIRC("PRIVMSG " + channelName + " :Sekarang main Hand 2: " + hand2 + " (Skor: " + to_string(score2) + ")");
                PutIRC("PRIVMSG " + channelName + " :Ketik !hit atau !stand untuk Hand 2");
            } else {
                PutIRC("PRIVMSG " + channelName + " :\xE2\x9C\x85 " + player + " selesai. Giliran dealer...");
                DealerPlay(channelName);
            }
            return HALT;
        }

        return CONTINUE;
    }

    void DealerPlay(const string& channelName) {
        game.waitingForAction = false;
        StopActionTimer();

        string dealerHand = game.GetCardsString(game.dealerCards, false);
        int dealerScore = game.CalculateScore(game.dealerCards);
        PutIRC("PRIVMSG " + channelName + " :\xF0\x9F\x83\x8F Dealer membuka: " + dealerHand + " (Skor: " + to_string(dealerScore) + ")");

        while (dealerScore < 17) {
            string newCard = game.DrawCard();
            game.dealerCards.push_back(newCard);
            dealerScore = game.CalculateScore(game.dealerCards);
            dealerHand = game.GetCardsString(game.dealerCards, false);
            PutIRC("PRIVMSG " + channelName + " :\xE2\xAC\x87\xEF\xB8\x8F Dealer hit: " + newCard);
            PutIRC("PRIVMSG " + channelName + " :Kartu dealer: " + dealerHand + " (Skor: " + to_string(dealerScore) + ")");
            if (dealerScore > 21) break;
            if (game.dealerCards.size() > 8) break; // safety
        }

        DetermineWinner(channelName);
        PutIRC("PRIVMSG " + channelName + " :\xF0\x9F\x8E\xB2 Game selesai! Round berikutnya akan siap sebentar lagi.");

        game.gameActive = false;
        game.waitingForAction = false;
        StartNextRoundTimer(10);
    }

    void DetermineWinner(const string& channelName) {
        int dealerScore = game.CalculateScore(game.dealerCards);
        PutIRC("PRIVMSG " + channelName + " :--- HASIL AKHIR ---");

        if (game.hasSplit) {
            int score1 = game.CalculateScore(game.playerCards);
            int score2 = game.CalculateScore(game.playerCards2);
            DetermineSingleWinner(score1, dealerScore, "Hand 1", channelName);
            DetermineSingleWinner(score2, dealerScore, "Hand 2", channelName);
        } else {
            int playerScore = game.CalculateScore(game.playerCards);
            DetermineSingleWinner(playerScore, dealerScore, "", channelName);
        }
    }

    void DetermineSingleWinner(int playerScore, int dealerScore, const string& handInfo, const string& channelName) {
        string tag = handInfo.empty() ? string("") : (" [" + handInfo + "]");
        string result;

        if (playerScore > 21) result = "\xF0\x9F\x92\xA5 Bust - Dealer menang" + tag;
        else if (dealerScore > 21) result = "\xF0\x9F\x8E\x89 Dealer bust - " + game.currentPlayer + " menang" + tag;
        else if (dealerScore > playerScore) result = "\xF0\x9F\x98\x9E Dealer menang" + tag + " " + to_string(dealerScore) + " vs " + to_string(playerScore);
        else if (playerScore > dealerScore) result = "\xF0\x9F\x8E\x89 " + game.currentPlayer + " menang" + tag + " " + to_string(playerScore) + " vs " + to_string(dealerScore);
        else result = "\xF0\x9F\xA4\x9D Seri (push)" + tag;

        PutIRC("PRIVMSG " + channelName + " :" + result);
    }

    void EndGame(const string& channelName) {
        game.waitingForAction = false;
        DealerPlay(channelName);
    }

    void OnModCommand(const CString& sCommand) override {
        if (sCommand.Equals("help")) {
            PutModule("Blackjack Commands:");
            PutModule("!blackjack - Mulai");
            PutModule("!hit       - Ambil kartu");
            PutModule("!stand     - Berhenti");
            PutModule("!split     - Split bila sama");
            PutModule("!bjhelp    - Bantuan di channel");
            PutModule("!bjstatus  - Status & timer");
        }
    }

    friend class CActionTimer;
    friend class CNextRoundTimer;
};

void CActionTimer::RunJob() {
    ((CBlackjackMod*)GetModule())->OnActionTimeout();
}

void CNextRoundTimer::RunJob() {
    ((CBlackjackMod*)GetModule())->OnNextRound();
}

template <>
void TModInfo<CBlackjackMod>(CModInfo& Info) {
    Info.SetWikiPage("blackjack");
    Info.SetHasArgs(false);
    Info.SetArgsHelpText("No arguments needed");
}

MODULEDEFS(CBlackjackMod, "Blackjack Game For ZNC")
