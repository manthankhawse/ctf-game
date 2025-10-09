// client_realtime.cpp
#include <iostream>
#include <string>
#include <thread>
#include <cstring>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>
#include <ncurses.h> // The key library for TUI and real-time input
#include <mutex>

#include "game_common.h"

// ==========================================================
// 2b. Client Module: Game Renderer (Now with ncurses)
// ==========================================================

// Shared buffer for the receiver thread to write to and the main thread to render from
std::string latestGameState;
std::mutex stateMutex;

void renderGame() {
    std::lock_guard<std::mutex> lock(stateMutex);
    
    clear(); // ncurses clear screen function

    // Print static instructions
    mvprintw(0, 0, "--- Capture The Flag (Real-Time) ---");
    mvprintw(1, 0, "Use ARROW KEYS or W, A, S, D to move. Quit with 'q'.");
    mvprintw(2, 0, "--------------------------------------------------");

    // De-serialize and print the map
    int map_char_count = GRID_HEIGHT * GRID_WIDTH;
    if(latestGameState.length() >= map_char_count) {
        for (int i = 0; i < GRID_HEIGHT; ++i) {
            std::string row = latestGameState.substr(i * GRID_WIDTH, GRID_WIDTH);
            mvprintw(i + 4, 0, row.c_str());
        }
        // Print the rest of the message (scores, etc.) below the map
        std::string extra_info = latestGameState.substr(map_char_count);
        mvprintw(GRID_HEIGHT + 5, 0, extra_info.c_str());
    } else {
        mvprintw(4, 0, "Waiting for game state...");
    }

    refresh(); // ncurses function to draw the screen
}

// ==========================================================
// 2b. Client Module: Receiver Thread
// ==========================================================
void receiveFromServer(int sock) {
    char buffer[BUFFER_SIZE] = {0};
    while (true) {
        memset(buffer, 0, BUFFER_SIZE);
        int bytes_received = read(sock, buffer, BUFFER_SIZE - 1);
        if (bytes_received > 0) {
            std::lock_guard<std::mutex> lock(stateMutex);
            latestGameState = std::string(buffer);
        } else {
            // Server disconnected
            endwin(); // Clean up ncurses
            std::cout << "\nDisconnected from server. Game over." << std::endl;
            close(sock);
            exit(0);
        }
    }
}

// ==========================================================
// 2b. Client Module: Input Handler (Now non-blocking)
// ==========================================================
void inputHandler(int sock) {
    while (true) {
        int ch = getch(); // This is the NON-BLOCKING input call from ncurses

        if (ch != ERR) { // ERR means no key was pressed
            std::string command = "";
            switch(ch) {
                case KEY_UP:
                case 'w':
                    command = "w";
                    break;
                case KEY_DOWN:
                case 's':
                    command = "s";
                    break;
                case KEY_LEFT:
                case 'a':
                    command = "a";
                    break;
                case KEY_RIGHT:
                case 'd':
                    command = "d";
                    break;
                case 'q':
                    endwin(); // Important: Restore terminal settings
                    close(sock);
                    exit(0);
            }
            if (!command.empty()) {
                send(sock, command.c_str(), command.length(), 0);
            }
        }
        // Small delay to prevent the loop from consuming 100% CPU
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
}


int main(int argc, char const *argv[]) {
    if (argc != 2) {
        std::cerr << "Usage: " << argv[0] << " <Server IP Address>" << std::endl;
        return -1;
    }
    
    // ==========================================================
    // 2b. Client Module: Connection Interface
    // ==========================================================
    int sock = 0;
    struct sockaddr_in serv_addr;
    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("Socket creation error");
        return -1;
    }
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(PORT);
    if (inet_pton(AF_INET, argv[1], &serv_addr.sin_addr) <= 0) {
        perror("Invalid address");
        return -1;
    }
    if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        perror("Connection Failed");
        return -1;
    }

    // --- Initialize ncurses ---
    initscr();              // Start ncurses mode
    cbreak();               // Line buffering disabled, Pass on everthing
    noecho();               // Don't echo() while we do getch
    keypad(stdscr, TRUE);   // Enable function keys like arrow keys
    nodelay(stdscr, TRUE);  // Make getch() non-blocking
    curs_set(0);            // Hide the cursor
    // ---

    // Start receiver thread
    std::thread receiver_thread(receiveFromServer, sock);
    receiver_thread.detach();

    // Start input handler thread
    std::thread input_thread(inputHandler, sock);
    input_thread.detach();

    // Main thread is now the rendering loop
    while (true) {
        renderGame();
        std::this_thread::sleep_for(std::chrono::milliseconds(50)); // Redraw at ~20 FPS
    }

    // Cleanup
    endwin();
    close(sock);
    return 0;
}