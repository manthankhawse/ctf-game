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

// NEW: A queue to hold the latest command from each player for the game loop
std::map<int, std::string> commandQueue;
std::mutex commandMutex;

void broadcastGameState() {
    std::lock_guard<std::mutex> lock(gameStateMutex);
    std::string serialized_map;
    for (const auto& row : sharedGameState.map) {
        serialized_map += row;
    }
    
    // Add score info to the payload
    int p1_score = sharedGameState.players.count(0) ? sharedGameState.players[0].score : 0;
    int p2_score = sharedGameState.players.count(1) ? sharedGameState.players[1].score : 0;
    
    // CORRECTED: Removed the "\n\n" to prevent rendering bugs on the client
    std::string score_info = "Player 1 Score: " + std::to_string(p1_score) + " | Player 2 Score: " + std::to_string(p2_score);
    if (p1_score >= 3) score_info += "\nPLAYER 1 WINS!";
    if (p2_score >= 3) score_info += "\nPLAYER 2 WINS!";
    
    serialized_map += score_info;

    for (auto const& [id, player] : sharedGameState.players) {
        send(player.sock_fd, serialized_map.c_str(), serialized_map.length(), 0);
    }
}

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

    if (new_x > 0 && new_x < GRID_WIDTH - 1 && new_y > 0 && new_y < GRID_HEIGHT - 1 &&
        sharedGameState.map[new_y][new_x] != WALL_TILE) {

        sharedGameState.map[player.y][player.x] = EMPTY_TILE;
        player.x = new_x;
        player.y = new_y;

        if (sharedGameState.map[player.y][player.x] == player.enemy_flag_tile) {
            player.hasFlag = true;
        }

        if (player.hasFlag && sharedGameState.map[player.y][player.x] == player.base_tile) {
            player.score++;
            player.hasFlag = false;
            if (player.id == 0) {
                 sharedGameState.map[sharedGameState.p2_flag_y][sharedGameState.p2_flag_x] = P2_FLAG_TILE;
            } else {
                 sharedGameState.map[sharedGameState.p1_flag_y][sharedGameState.p1_flag_x] = P1_FLAG_TILE;
            }
        }

        char display_tile = player.hasFlag ? (player.id == 0 ? P1_WITH_FLAG_TILE : P2_WITH_FLAG_TILE) : player.tile;
        sharedGameState.map[player.y][player.x] = display_tile;
    }
}

// NEW: The main game loop that runs on its own thread
void gameLoop() {
    const int TICK_RATE = 15; // Game updates 15 times per second
    const auto tick_period = std::chrono::milliseconds(1000 / TICK_RATE);

    while (true) {
        // Process all queued commands
        {
            std::lock_guard<std::mutex> lock(commandMutex);
            if (!commandQueue.empty()) {
                for (auto const& [player_id, command] : commandQueue) {
                    handleCommand(player_id, command);
                }
                commandQueue.clear();
            }
        }

        // Broadcast the result of the tick
        broadcastGameState();

        // Wait for the next tick
        std::this_thread::sleep_for(tick_period);
    }
}

// MODIFIED: Client handler now just queues commands
void clientHandler(int client_socket, int player_id) {
    std::cout << "Player " << player_id + 1 << " connected." << std::endl;

    {
        std::lock_guard<std::mutex> lock(gameStateMutex);
        Player newPlayer;
        newPlayer.id = player_id;
        newPlayer.sock_fd = client_socket;
        
        if (player_id == 0) {
            newPlayer.x = 2;
            newPlayer.y = GRID_HEIGHT / 2 - 2;
            newPlayer.tile = P1_TILE;
            newPlayer.base_tile = P1_BASE_TILE;
            newPlayer.enemy_flag_tile = P2_FLAG_TILE;
        } else {
            newPlayer.x = GRID_WIDTH - 3;
            newPlayer.y = GRID_HEIGHT / 2 + 2;
            newPlayer.tile = P2_TILE;
            newPlayer.base_tile = P2_BASE_TILE;
            newPlayer.enemy_flag_tile = P1_FLAG_TILE;
        }
        
        sharedGameState.players[player_id] = newPlayer;
        sharedGameState.map[newPlayer.y][newPlayer.x] = newPlayer.tile;
    }
    broadcastGameState(); // Initial broadcast to show player

    char buffer[BUFFER_SIZE] = {0};
    while (read(client_socket, buffer, BUFFER_SIZE) > 0) {
        // Instead of processing, just add the command to the queue
        {
            std::lock_guard<std::mutex> lock(commandMutex);
            if (strlen(buffer) > 0) {
                commandQueue[player_id] = std::string(1, buffer[0]);
            }
        }
        memset(buffer, 0, BUFFER_SIZE);
    }
    
    std::cout << "Player " << player_id + 1 << " disconnected." << std::endl;
    {
        std::lock_guard<std::mutex> lock(gameStateMutex);
        Player& p = sharedGameState.players[player_id];
        sharedGameState.map[p.y][p.x] = EMPTY_TILE;
        sharedGameState.players.erase(player_id);
    }
    broadcastGameState();
    close(client_socket);
}

// MODIFIED: Main function now starts and joins the game loop thread
int main() {
    int server_fd, new_socket;
    struct sockaddr_in address;
    int opt = 1;
    socklen_t addrlen = sizeof(address);

    std::cout << "Starting server..." << std::endl;
    
    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        perror("socket failed");
        exit(EXIT_FAILURE);
    }

    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt))) {
        perror("setsockopt");
        exit(EXIT_FAILURE);
    }
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);

    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("bind failed");
        exit(EXIT_FAILURE);
    }

    if (listen(server_fd, 2) < 0) {
        perror("listen");
        exit(EXIT_FAILURE);
    }
    
    sharedGameState.map = createInitialMap();
    sharedGameState.p1_flag_y = GRID_HEIGHT / 2;
    sharedGameState.p1_flag_x = 3;
    sharedGameState.p2_flag_y = GRID_HEIGHT / 2;
    sharedGameState.p2_flag_x = GRID_WIDTH - 4;

    std::cout << "Server is listening on port " << PORT << std::endl;

    // Start the main game loop on a separate thread
    std::thread game_thread(gameLoop);

    int player_count = 0;
    while (player_count < 2) {
        if ((new_socket = accept(server_fd, (struct sockaddr *)&address, &addrlen)) < 0) {
            perror("accept");
            continue;
        }
        
        std::thread client_thread(clientHandler, new_socket, player_count);
        client_thread.detach();
        player_count++;
    }

    // Keep the main thread alive by joining the game loop thread
    game_thread.join();

    close(server_fd);
    return 0;
}