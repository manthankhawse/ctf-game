#include <iostream>
#include <string>
#include <thread>
#include <cstring>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>
#include <ncurses.h>
#include <mutex>
#include <atomic>

#include "game_common.h"

std::string latestGameState;
std::mutex stateMutex;
std::atomic<bool> gameRunning{true};

void renderGame() {
    std::string currentGameState;
    {
        std::lock_guard<std::mutex> lock(stateMutex);
        currentGameState = latestGameState;
    }
    
    clear();

    mvprintw(0, 0, "--- Capture The Flag (Real-Time) ---");
    mvprintw(1, 0, "Use ARROW KEYS or W, A, S, D to move. Quit with 'q'.");
    mvprintw(2, 0, "--------------------------------------------------");

    // *** FIX: Robustly parse the game state using the newline delimiter ***
    size_t delimiter_pos = currentGameState.find('\n');
    if (delimiter_pos != std::string::npos) {
        std::string map_data = currentGameState.substr(0, delimiter_pos);
        std::string extra_info = currentGameState.substr(delimiter_pos + 1);

        if (map_data.length() == GRID_HEIGHT * GRID_WIDTH) {
            for (int i = 0; i < GRID_HEIGHT; ++i) {
                std::string row = map_data.substr(i * GRID_WIDTH, GRID_WIDTH);
                mvprintw(i + 4, 0, "%s", row.c_str());
            }
        }
        mvprintw(GRID_HEIGHT + 5, 0, "%s", extra_info.c_str());
    } else {
        mvprintw(4, 0, "Waiting for game state...");
    }

    refresh();
}

// *** FIX: Simplified and more reliable receiver function ***
void receiveFromServer(int sock) {
    char buffer[BUFFER_SIZE] = {0};
    while (gameRunning) {
        memset(buffer, 0, BUFFER_SIZE);
        int bytes_received = read(sock, buffer, BUFFER_SIZE - 1);
        
        if (bytes_received > 0) {
            std::lock_guard<std::mutex> lock(stateMutex);
            latestGameState = std::string(buffer);
        } else {
            // Server disconnected or error
            gameRunning = false; // Signal other threads to stop
            break;
        }
    }
}

void inputHandler(int sock) {
    while (gameRunning) {
        int ch = getch();

        if (ch != ERR) {
            std::string command = "";
            switch(ch) {
                case KEY_UP: case 'w': case 'W': command = "w"; break;
                case KEY_DOWN: case 's': case 'S': command = "s"; break;
                case KEY_LEFT: case 'a': case 'A': command = "a"; break;
                case KEY_RIGHT: case 'd': case 'D': command = "d"; break;
                case 'q': case 'Q':
                    gameRunning = false;
                    break;
            }
            if (!command.empty()) {
                if (send(sock, command.c_str(), command.length(), MSG_NOSIGNAL) <= 0) {
                    gameRunning = false; // Stop if we can't send
                }
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
}

int main(int argc, char const *argv[]) {
    if (argc != 2) {
        std::cerr << "Usage: " << argv[0] << " <Server IP Address>" << std::endl;
        return -1;
    }
    
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) { perror("Socket creation error"); return -1; }

    struct sockaddr_in serv_addr;
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(PORT);
    if (inet_pton(AF_INET, argv[1], &serv_addr.sin_addr) <= 0) { perror("Invalid address"); return -1; }
    if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) { perror("Connection Failed"); return -1; }

    initscr();
    cbreak();
    noecho();
    keypad(stdscr, TRUE);
    nodelay(stdscr, TRUE);
    curs_set(0);

    std::thread receiver_thread(receiveFromServer, sock);
    std::thread input_thread(inputHandler, sock);

    while (gameRunning) {
        renderGame();
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    // Cleanly join threads and shut down
    receiver_thread.join();
    input_thread.join();
    
    endwin();
    close(sock);
    std::cout << "Disconnected." << std::endl;
    
    return 0;
}