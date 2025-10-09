// client.cpp
#include <iostream>
#include <string>
#include <thread>
#include <cstring>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>

#include "game_common.h"

// ==========================================================
// 2b. Client Module: Game Renderer
// ==========================================================
void clearScreen() {
    // A simple cross-platform way to clear the console
    #ifdef _WIN32
        system("cls");
    #else
        system("clear");
    #endif
}

void renderGame(const std::string& serialized_map) {
    clearScreen();
    std::cout << "--- Capture The Flag ---" << std::endl;
    std::cout << "Use W, A, S, D to move. Capture the enemy flag and return it to your base!" << std::endl;
    std::cout << "Player 1 is '1', Flag is 'A', Base is 'a'. Player 2 is '2', Flag is 'B', Base is 'b'." << std::endl;
    std::cout << "'!' means P1 has the flag. '@' means P2 has the flag." << std::endl;
    std::cout << "------------------------" << std::endl;
    
    // De-serialize the map part
    int map_char_count = GRID_HEIGHT * GRID_WIDTH;
    if(serialized_map.length() >= map_char_count) {
        for (int i = 0; i < GRID_HEIGHT; ++i) {
            std::cout << serialized_map.substr(i * GRID_WIDTH, GRID_WIDTH) << std::endl;
        }
        // Print the rest of the message (scores, etc.)
        std::cout << serialized_map.substr(map_char_count) << std::endl;
    } else {
        // Fallback for incomplete data
        std::cout << "Waiting for game state..." << std::endl;
    }

    std::cout << "Your move (w/a/s/d): " << std::flush;
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
            renderGame(std::string(buffer));
        } else {
            std::cout << "\nDisconnected from server. Game over." << std::endl;
            close(sock);
            exit(0);
        }
    }
}

int main(int argc, char const *argv[]) {
    if (argc != 2) {
        std::cerr << "Usage: " << argv[0] << " <Server IP Address>" << std::endl;
        return -1;
    }
    const char* server_ip = argv[1];

    int sock = 0;
    struct sockaddr_in serv_addr;

    // ==========================================================
    // 2b. Client Module: Connection Interface
    // ==========================================================
    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        std::cout << "\n Socket creation error \n";
        return -1;
    }

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(PORT);

    if (inet_pton(AF_INET, server_ip, &serv_addr.sin_addr) <= 0) {
        std::cout << "\nInvalid address/ Address not supported \n";
        return -1;
    }

    if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        std::cout << "\nConnection Failed \n";
        return -1;
    }
    std::cout << "Connected to the game server. Waiting for the game to start..." << std::endl;

    // Start a separate thread to receive and render game state updates
    std::thread receiver_thread(receiveFromServer, sock);
    receiver_thread.detach();
    
    // ==========================================================
    // 2b. Client Module: Input Handler
    // ==========================================================
    std::string input;
    while (true) {
        std::cin >> input;
        if (input == "w" || input == "a" || input == "s" || input == "d") {
            send(sock, input.c_str(), input.length(), 0);
        } else {
            std::cout << "Invalid command. Use w, a, s, d." << std::endl;
        }
    }

    close(sock);
    return 0;
}