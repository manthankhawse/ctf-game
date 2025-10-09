// game_common.h
#ifndef GAME_COMMON_H
#define GAME_COMMON_H

#include <string>
#include <vector>

// --- Network Configuration ---
#define PORT 8080
#define BUFFER_SIZE 2048

// --- Game Grid Configuration ---
const int GRID_WIDTH = 40;
const int GRID_HEIGHT = 15;

// --- Game Entities ---
const char EMPTY_TILE = '.';
const char WALL_TILE = '#';
const char P1_TILE = '1';
const char P2_TILE = '2';
const char P1_FLAG_TILE = 'A';
const char P2_FLAG_TILE = 'B';
const char P1_BASE_TILE = 'a';
const char P2_BASE_TILE = 'b';
const char P1_WITH_FLAG_TILE = '!';
const char P2_WITH_FLAG_TILE = '@';


// Helper function to create the initial game map
std::vector<std::string> createInitialMap() {
    std::vector<std::string> map(GRID_HEIGHT, std::string(GRID_WIDTH, EMPTY_TILE));

    // Create a border
    for (int i = 0; i < GRID_WIDTH; ++i) {
        map[0][i] = WALL_TILE;
        map[GRID_HEIGHT - 1][i] = WALL_TILE;
    }
    for (int i = 0; i < GRID_HEIGHT; ++i) {
        map[i][0] = WALL_TILE;
        map[i][GRID_WIDTH - 1] = WALL_TILE;
    }

    // Add some internal obstacles
    for (int i = 5; i < GRID_HEIGHT - 5; ++i) {
        map[i][GRID_WIDTH / 2] = WALL_TILE;
    }

    // Place bases and flags
    map[GRID_HEIGHT / 2][2] = P1_BASE_TILE;
    map[GRID_HEIGHT / 2][3] = P1_FLAG_TILE;

    map[GRID_HEIGHT / 2][GRID_WIDTH - 3] = P2_BASE_TILE;
    map[GRID_HEIGHT / 2][GRID_WIDTH - 4] = P2_FLAG_TILE;

    return map;
}

#endif // GAME_COMMON_H