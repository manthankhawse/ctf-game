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
#include <chrono>

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
    char previous_tile = EMPTY_TILE; // Remember what was on the tile before the player moved there
};

struct GameState {
    std::vector<std::string> map;
    std::map<int, Player> players;
    int p1_flag_x, p1_flag_y;
    int p2_flag_x, p2_flag_y;
};

GameState sharedGameState;
std::mutex gameStateMutex;

std::map<int, std::string> commandQueue;
std::mutex commandMutex;

void broadcastGameState() {
    std::lock_guard<std::mutex> lock(gameStateMutex);
    std::string serialized_state;
    for (const auto& row : sharedGameState.map) {
        serialized_state += row;
    }
    
    // *** FIX: Use a single newline as a clean delimiter between map and score info ***
    serialized_state += "\n";

    int p1_score = sharedGameState.players.count(0) ? sharedGameState.players[0].score : 0;
    int p2_score = sharedGameState.players.count(1) ? sharedGameState.players[1].score : 0;
    
    std::string score_info = "Player 1 Score: " + std::to_string(p1_score) + " | Player 2 Score: " + std::to_string(p2_score);
    if (p1_score >= 3) score_info += "\nPLAYER 1 WINS!";
    if (p2_score >= 3) score_info += "\nPLAYER 2 WINS!";
    
    serialized_state += score_info;

    auto it = sharedGameState.players.begin();
    while (it != sharedGameState.players.end()) {
        ssize_t sent = send(it->second.sock_fd, serialized_state.c_str(), serialized_state.length(), MSG_NOSIGNAL);
        if (sent <= 0) { // Handle error or graceful close
            std::cout << "Player " << it->second.id + 1 << " disconnected." << std::endl;
            sharedGameState.map[it->second.y][it->second.x] = it->second.previous_tile;
            close(it->second.sock_fd);
            it = sharedGameState.players.erase(it);
        } else {
            ++it;
        }
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
    else return;

    if (new_x > 0 && new_x < GRID_WIDTH - 1 && new_y > 0 && new_y < GRID_HEIGHT - 1 && sharedGameState.map[new_y][new_x] != WALL_TILE) {
        bool position_occupied = false;
        for (const auto& [other_id, other_player] : sharedGameState.players) {
            if (other_id != player_id && other_player.x == new_x && other_player.y == new_y) {
                position_occupied = true;
                break;
            }
        }
        
        if (!position_occupied) {
            sharedGameState.map[player.y][player.x] = player.previous_tile;
            player.previous_tile = sharedGameState.map[new_y][new_x];
            player.x = new_x;
            player.y = new_y;

            if (player.previous_tile == player.enemy_flag_tile) {
                player.hasFlag = true;
                player.previous_tile = EMPTY_TILE;
            }

            if (player.hasFlag && player.previous_tile == player.base_tile) {
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
}

void gameLoop() {
    const int TICK_RATE = 15;
    const auto tick_period = std::chrono::milliseconds(1000 / TICK_RATE);

    while (true) {
        {
            std::lock_guard<std::mutex> lock(commandMutex);
            for (auto const& [player_id, command] : commandQueue) {
                handleCommand(player_id, command);
            }
            commandQueue.clear();
        }
        broadcastGameState();
        std::this_thread::sleep_for(tick_period);
    }
}

void clientHandler(int client_socket, int player_id) {
    {
        std::lock_guard<std::mutex> lock(gameStateMutex);
        Player newPlayer;
        newPlayer.id = player_id;
        newPlayer.sock_fd = client_socket;
        
        if (player_id == 0) {
            newPlayer.x = 1; newPlayer.y = GRID_HEIGHT / 2;
            newPlayer.tile = P1_TILE; newPlayer.base_tile = P1_BASE_TILE; newPlayer.enemy_flag_tile = P2_FLAG_TILE;
        } else {
            newPlayer.x = GRID_WIDTH - 2; newPlayer.y = GRID_HEIGHT / 2;
            newPlayer.tile = P2_TILE; newPlayer.base_tile = P2_BASE_TILE; newPlayer.enemy_flag_tile = P1_FLAG_TILE;
        }
        
        newPlayer.previous_tile = sharedGameState.map[newPlayer.y][newPlayer.x];
        sharedGameState.map[newPlayer.y][newPlayer.x] = newPlayer.tile;
        sharedGameState.players[player_id] = newPlayer;
    }

    std::cout << "Player " << player_id + 1 << " connected." << std::endl;
    broadcastGameState();

    char buffer[10] = {0};
    while (read(client_socket, buffer, sizeof(buffer) - 1) > 0) {
        {
            std::lock_guard<std::mutex> lock(commandMutex);
            if (strlen(buffer) > 0) {
                commandQueue[player_id] = std::string(1, buffer[0]);
            }
        }
        memset(buffer, 0, sizeof(buffer));
    }
    
    // Note: Disconnect is now handled gracefully inside the broadcastGameState loop.
    // This ensures cleanup happens even if the client doesn't send a quit message.
    {
        std::lock_guard<std::mutex> cmd_lock(commandMutex);
        commandQueue.erase(player_id);
    }
}

int main() {
    int server_fd, new_socket;
    struct sockaddr_in address;
    int opt = 1;
    socklen_t addrlen = sizeof(address);

    std::cout << "Starting server..." << std::endl;
    
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);
    bind(server_fd, (struct sockaddr *)&address, sizeof(address));
    listen(server_fd, 2);
    
    sharedGameState.map = createInitialMap();
    sharedGameState.p1_flag_y = GRID_HEIGHT / 2;
    sharedGameState.p1_flag_x = 3;
    sharedGameState.p2_flag_y = GRID_HEIGHT / 2;
    sharedGameState.p2_flag_x = GRID_WIDTH - 4;

    std::cout << "Server is listening on port " << PORT << std::endl;
    std::thread game_thread(gameLoop);

    int player_count = 0;
    while (player_count < 2) {
        new_socket = accept(server_fd, (struct sockaddr *)&address, &addrlen);
        std::thread client_thread(clientHandler, new_socket, player_count);
        client_thread.detach();
        player_count++;
    }

    game_thread.join();
    close(server_fd);
    return 0;
}