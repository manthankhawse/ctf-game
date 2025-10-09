// server.cpp
#include <iostream>
#include <string>
#include <vector>
#include <thread>
#include <mutex>
#include <cstring>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <map>

#include "game_common.h"

// ==========================================================
// 2a. Server Module: Game State Manager & Sync Controller
// ==========================================================
struct Player {
    int id;
    int x, y;
    int score = 0;
    bool hasFlag = false;
    int sock_fd;
    char tile;
    char base_tile;
    char enemy_flag_tile;
};

struct GameState {
    std::vector<std::string> map;
    std::map<int, Player> players;
    int p1_flag_x, p1_flag_y;
    int p2_flag_x, p2_flag_y;
};

// Shared game state and the mutex to protect it
GameState sharedGameState;
std::mutex gameStateMutex;

// ==========================================================
// 2a. Server Module: Broadcast Manager
// ==========================================================
void broadcastGameState() {
    std::lock_guard<std::mutex> lock(gameStateMutex);
    std::string serialized_map;
    for (const auto& row : sharedGameState.map) {
        serialized_map += row;
    }
    
    // Add score info to the payload
    int p1_score = sharedGameState.players.count(0) ? sharedGameState.players[0].score : 0;
    int p2_score = sharedGameState.players.count(1) ? sharedGameState.players[1].score : 0;
    std::string score_info = "\n\nPlayer 1 Score: " + std::to_string(p1_score) + " | Player 2 Score: " + std::to_string(p2_score);
    if (p1_score >= 3) score_info += "\nPLAYER 1 WINS!";
    if (p2_score >= 3) score_info += "\nPLAYER 2 WINS!";
    
    serialized_map += score_info;


    for (auto const& [id, player] : sharedGameState.players) {
        send(player.sock_fd, serialized_map.c_str(), serialized_map.length(), 0);
    }
}

// ==========================================================
// 2a. Server Module: Command Handler
// ==========================================================
void handleCommand(int player_id, const std::string& command) {
    std::lock_guard<std::mutex> lock(gameStateMutex);

    if (!sharedGameState.players.count(player_id)) return;

    Player& player = sharedGameState.players[player_id];
    int new_x = player.x;
    int new_y = player.y;

    if (command == "w") new_y--;
    else if (command == "s") new_y++;
    else if (command == "a") new_x--;
    else if (command == "d") new_x++;
    else return; // Invalid command

    // Game Rule Validation: Check boundaries and walls
    if (new_x > 0 && new_x < GRID_WIDTH - 1 && new_y > 0 && new_y < GRID_HEIGHT - 1 &&
        sharedGameState.map[new_y][new_x] != WALL_TILE) {

        // Erase old position
        sharedGameState.map[player.y][player.x] = EMPTY_TILE;

        // Update player position
        player.x = new_x;
        player.y = new_y;

        // Game Rule Validation: Flag interaction
        if (sharedGameState.map[player.y][player.x] == player.enemy_flag_tile) {
            player.hasFlag = true;
        }

        // Game Rule Validation: Score condition
        if (player.hasFlag && sharedGameState.map[player.y][player.x] == player.base_tile) {
            player.score++;
            player.hasFlag = false;
            // Reset enemy flag to its original position
            if (player.id == 0) {
                 sharedGameState.map[sharedGameState.p2_flag_y][sharedGameState.p2_flag_x] = P2_FLAG_TILE;
            } else {
                 sharedGameState.map[sharedGameState.p1_flag_y][sharedGameState.p1_flag_x] = P1_FLAG_TILE;
            }
        }

        // Update map with new player position
        char display_tile = player.hasFlag ? (player.id == 0 ? P1_WITH_FLAG_TILE : P2_WITH_FLAG_TILE) : player.tile;
        sharedGameState.map[player.y][player.x] = display_tile;
    }
}


void clientHandler(int client_socket, int player_id) {
    std::cout << "Player " << player_id + 1 << " connected." << std::endl;

    // Initialize player state
    {
        std::lock_guard<std::mutex> lock(gameStateMutex);
        Player newPlayer;
        newPlayer.id = player_id;
        newPlayer.sock_fd = client_socket;
        
        if (player_id == 0) { // Player 1
            newPlayer.x = 2;
            newPlayer.y = GRID_HEIGHT / 2 - 2;
            newPlayer.tile = P1_TILE;
            newPlayer.base_tile = P1_BASE_TILE;
            newPlayer.enemy_flag_tile = P2_FLAG_TILE;
        } else { // Player 2
            newPlayer.x = GRID_WIDTH - 3;
            newPlayer.y = GRID_HEIGHT / 2 + 2;
            newPlayer.tile = P2_TILE;
            newPlayer.base_tile = P2_BASE_TILE;
            newPlayer.enemy_flag_tile = P1_FLAG_TILE;
        }
        
        sharedGameState.players[player_id] = newPlayer;
        sharedGameState.map[newPlayer.y][newPlayer.x] = newPlayer.tile;
    }

    broadcastGameState();

    char buffer[BUFFER_SIZE] = {0};
    while (read(client_socket, buffer, BUFFER_SIZE) > 0) {
        handleCommand(player_id, std::string(buffer));
        broadcastGameState();
        memset(buffer, 0, BUFFER_SIZE);
    }
    
    // Cleanup on disconnect
    std::cout << "Player " << player_id + 1 << " disconnected." << std::endl;
    {
        std::lock_guard<std::mutex> lock(gameStateMutex);
        Player& p = sharedGameState.players[player_id];
        sharedGameState.map[p.y][p.x] = EMPTY_TILE; // Remove player from map
        sharedGameState.players.erase(player_id);
    }
    broadcastGameState();
    close(client_socket);
}


int main() {
    int server_fd, new_socket;
    struct sockaddr_in address;
    int opt = 1;
    int addrlen = sizeof(address);

    // ==========================================================
    // 2a. Server Module: Connection Manager
    // ==========================================================
    std::cout << "Starting server..." << std::endl;
    
    // Create socket
    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        perror("socket failed");
        exit(EXIT_FAILURE);
    }

    // Forcefully attaching socket to the port 8080
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt))) {
        perror("setsockopt");
        exit(EXIT_FAILURE);
    }
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);

    // Bind socket to the network address and port
    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("bind failed");
        exit(EXIT_FAILURE);
    }

    // Listen for incoming connections
    if (listen(server_fd, 2) < 0) { // Listen for a max of 2 players
        perror("listen");
        exit(EXIT_FAILURE);
    }
    
    // Initialize the game state
    sharedGameState.map = createInitialMap();
    sharedGameState.p1_flag_y = GRID_HEIGHT / 2;
    sharedGameState.p1_flag_x = 3;
    sharedGameState.p2_flag_y = GRID_HEIGHT / 2;
    sharedGameState.p2_flag_x = GRID_WIDTH - 4;


    std::cout << "Server is listening on port " << PORT << std::endl;
    int player_count = 0;

    while (player_count < 2) { // Game is for 2 players
        if ((new_socket = accept(server_fd, (struct sockaddr *)&address, (socklen_t*)&addrlen)) < 0) {
            perror("accept");
            continue;
        }
        
        // Spawn a new thread for each client
        std::thread client_thread(clientHandler, new_socket, player_count);
        client_thread.detach(); // Detach the thread to run independently
        player_count++;
    }

    // Server will just keep running while client threads handle the game
    // A more robust server might have a command to shut down gracefully
    while(true) {
        std::this_thread::sleep_for(std::chrono::seconds(10));
    }

    return 0;
}