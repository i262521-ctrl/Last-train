#include "simulator.h"
#include "physics.h"
#include "switches.h"
#include "logger.h"
#include "viewer_iface.h"
#include "util.h" 

// Modular simulation loop using primitive variables
  
#include "level.h"
#include <iostream>
#include <cstring>

bool handleMoveCommand(int targetFloor, int targetRoom,
                       int& currentFloor, int& currentRoom,
                       int& energy, int& time,
                       const char horizontalConn[NUM_FLOORS][ROOMS_PER_FLOOR - 1],
                       const char inventory[MAX_INVENTORY_CAPACITY], int inventoryCount,
                       int& emergencyTripsLeft, bool lift2Operational) {
    
    char connType = CONN_NONE;
    if (!isDirectlyConnected(currentFloor, currentRoom, targetFloor, targetRoom, horizontalConn, connType)) {
        std::cout << "Invalid move: No direct connection.\n";
        return false;
    }
    if (!canTraverse(targetFloor, targetRoom, connType, inventory, inventoryCount, emergencyTripsLeft, lift2Operational)) {
        std::cout << "Invalid move: Access denied (locked/need key).\n";
        return false;
    }

    int dE = COST_MOVE_ORDINARY_ENERGY;
    int dT = COST_MOVE_ORDINARY_TIME;
    if (connType == CONN_WINDOW) { dE = COST_MOVE_WINDOW_ENERGY; dT = COST_MOVE_WINDOW_TIME; }
    else if (connType == '1' || connType == '2') { 
        dE = COST_MOVE_LIFT_ENERGY; dT = COST_MOVE_LIFT_TIME; 
        if (connType == '2' && !lift2Operational) emergencyTripsLeft--;
    }

    applyResourceDelta(energy, time, dE, dT);
    currentFloor = targetFloor;
    currentRoom = targetRoom;
    return true;
}

bool handleInspectCommand(int currentFloor, int currentRoom,
                          int& energy, int& time,
                          const char roomEntities[NUM_FLOORS][ROOMS_PER_FLOOR][MAX_ENTITIES_PER_ROOM],
                          int entityCount,
                          bool objectives[NUM_OBJECTIVES], int& score) {
    applyResourceDelta(energy, time, COST_INSPECT_ENERGY, COST_INSPECT_TIME);
    bool foundSomething = false;
    for (int i = 0; i < entityCount; i++) {
        char e = roomEntities[currentFloor-1][currentRoom-1][i];
        if (e == ENT_CLUE_4 && !objectives[OBJ_CLUE_4]) { objectives[OBJ_CLUE_4] = true; applyScoreDelta(score, SCORE_CLUE_FOUND); std::cout << ">> Found Clue 4!\n"; foundSomething = true; }
        if (e == ENT_CLUE_2 && !objectives[OBJ_CLUE_2]) { objectives[OBJ_CLUE_2] = true; applyScoreDelta(score, SCORE_CLUE_FOUND); std::cout << ">> Found Clue 2!\n"; foundSomething = true; }
        if (e == ENT_CLUE_7 && !objectives[OBJ_CLUE_7]) { objectives[OBJ_CLUE_7] = true; applyScoreDelta(score, SCORE_CLUE_FOUND); std::cout << ">> Found Clue 7!\n"; foundSomething = true; }
    }
    if (!foundSomething) std::cout << "Nothing new found.\n";
    return true;
}

bool handleCollectCommand(char itemSymbol, int currentFloor, int currentRoom,
                          int& energy, int& time, int& score,
                          char roomEntities[NUM_FLOORS][ROOMS_PER_FLOOR][MAX_ENTITIES_PER_ROOM],
                          int& entityCount,
                          char inventory[MAX_INVENTORY_CAPACITY], int& inventoryCount,
                          bool objectives[NUM_OBJECTIVES]) {
    
    int foundIdx = -1;
    for (int i = 0; i < entityCount; i++) {
        if (roomEntities[currentFloor-1][currentRoom-1][i] == itemSymbol) {
            foundIdx = i; break;
        }
    }
    
    if (foundIdx == -1) { std::cout << "Item not found here.\n"; return false; }
    
    // Prevent collecting things that should be "used" instead
    if (itemSymbol == ENT_CONTROL_PANEL || itemSymbol == ENT_CABINET_TRAIN_KEY || 
        itemSymbol == ENT_PASSENGER || itemSymbol == ENT_CAT) {
        std::cout << "You cannot collect this directly. Try 'use' command.\n"; 
        return false;
    }

    applyResourceDelta(energy, time, COST_ITEM_PICKUP_ENERGY, COST_ITEM_PICKUP_TIME);
    
    if (itemSymbol == ENT_ENERGY_PACK) { energy = clampInt(energy + BONUS_ENERGY_PACK, 0, MAX_ENERGY); }
    else if (itemSymbol == ENT_EMERGENCY_TOOLKIT) { energy = clampInt(energy + BONUS_EMERGENCY_TOOLKIT, 0, MAX_ENERGY); }
    else if (itemSymbol == ENT_OBSERVATION_TOKEN) { time = clampInt(time + BONUS_OBSERVATION_TOKEN, 0, MAX_TIME); }
    else {
        if (!addItem(inventory, inventoryCount, itemSymbol)) {
            std::cout << "Inventory full!\n"; return false;
        }
        if (itemSymbol == ENT_MASTER_KEY) objectives[OBJ_MASTER_KEY] = true;
        if (itemSymbol == ENT_FUSE) objectives[OBJ_FUSE_COLLECTED] = true;
        if (itemSymbol == ENT_LIFT_PASS) objectives[OBJ_LIFT_PASS] = true;
        if (itemSymbol == ENT_CAT_CARRIER) objectives[OBJ_CAT_CARRIER] = true;
    }

    for (int j = foundIdx; j < entityCount - 1; j++) {
        roomEntities[currentFloor-1][currentRoom-1][j] = roomEntities[currentFloor-1][currentRoom-1][j + 1];
    }
    entityCount--;
    std::cout << "Collected: " << getEntityName(itemSymbol) << "\n";
    return true;
}

bool handleUseCommand(char itemSymbol, int currentFloor, int currentRoom,
                      int& energy, int& time, int& score,
                      char inventory[MAX_INVENTORY_CAPACITY], int& inventoryCount,
                      bool objectives[NUM_OBJECTIVES]) {
    
    applyResourceDelta(energy, time, COST_ITEM_USE_ENERGY, COST_ITEM_USE_TIME);

    if (itemSymbol == ENT_FUSE) {
        if (currentFloor == 3 && currentRoom == 4) {
            if (installFuseControlPanel(inventory, inventoryCount, objectives, score)) {
                std::cout << "Fuse installed! Lift 2 is operational.\n";
                return true;
            }
        } else {
            std::cout << "You must be at F3-R4 (Control Panel) to install the fuse.\n";
        }
    }
    else if (itemSymbol == ENT_MEDICAL_KIT) {
        if (currentFloor == 4 && currentRoom == 1 && !objectives[OBJ_PASSENGER_RESCUED]) {
            objectives[OBJ_PASSENGER_RESCUED] = true; applyScoreDelta(score, SCORE_RESCUE_PASSENGER);
            std::cout << "Passenger Rescued!\n"; return true;
        } else {
            std::cout << "You need to be at F4-R1 (Passenger Room) to use the Medical Kit.\n";
        }
    }
    else if (itemSymbol == ENT_CABINET_TRAIN_KEY) {
        // 1. Open Cabinet at F5-R1
        if (currentFloor == 5 && currentRoom == 1) {
            if (objectives[OBJ_TRAIN_KEY]) {
                std::cout << "Cabinet is already opened.\n";
            } else if (verifyCabinetCode("427", objectives)) {
                objectives[OBJ_CABINET_UNLOCKED] = true;
                objectives[OBJ_TRAIN_KEY] = true;
                applyScoreDelta(score, SCORE_CABINET_UNLOCK + SCORE_TRAIN_KEY);
                std::cout << "Cabinet Opened! Train Key Acquired.\n";
            } else {
                std::cout << "Wrong code or missing clues. You need Clues 4, 2, and 7.\n";
            }
            return true;
        }
        // 2. Start Train at F5-R2
        else if (currentFloor == 5 && currentRoom == 2) {
            if (!objectives[OBJ_TRAIN_KEY]) {
                std::cout << "You don't have the Train Key yet.\n";
            } else if (!objectives[OBJ_PASSENGER_RESCUED]) {
                std::cout << "You must rescue the Passenger first.\n";
            } else if (!objectives[OBJ_CAT_SECURED]) {
                std::cout << "You must secure the Cat first.\n";
            } else {
                objectives[OBJ_TRAIN_PREPARED] = true;
                applyScoreDelta(score, SCORE_PREPARE_TRAIN + SCORE_MISSION_VICTORY);
                std::cout << "Train Prepared! YOU WIN!\n";
            }
            return true;
        } else {
            std::cout << "You can only use the Train Key at the Cabinet (F5-R1) or Train Platform (F5-R2).\n";
        }
    }
    else if (itemSymbol == ENT_CAT) {
        if (currentFloor == 3 && currentRoom == 3) {
            if (objectives[OBJ_CAT_SECURED]) {
                std::cout << "Cat is already secured.\n";
            } else if (objectives[OBJ_CAT_CARRIER]) {
                objectives[OBJ_CAT_SECURED] = true; applyScoreDelta(score, SCORE_SECURE_CAT);
                std::cout << "Cat Secured in Carrier!\n"; 
            } else {
                std::cout << "You need the Cat Carrier to secure the cat.\n";
            }
        } else {
            std::cout << "No cat here.\n";
        }
    }
    return false;
}

bool checkEndConditions(int energy, int time, const bool objectives[NUM_OBJECTIVES]) {
    if (energy <= 0 || time <= 0) return true; // Defeat
    if (objectives[OBJ_TRAIN_PREPARED]) return true; // Victory
    return false;
}

bool handleUnlockCabinetCommand(const char* code, int currentFloor, int currentRoom,
                                bool objectives[NUM_OBJECTIVES], int& score,
                                char roomEntities[NUM_FLOORS][ROOMS_PER_FLOOR][MAX_ENTITIES_PER_ROOM],
                                int& entityCount) {
    return true; // Not used directly, handled by use command
}

void runSimulation(int& playerFloor, int& playerRoom,
                   int& energy, int& time, int& score,
                   char inventory[MAX_INVENTORY_CAPACITY], int& inventoryCount,
                   bool objectives[NUM_OBJECTIVES], int& emergencyTripsLeft,
                   char horizontalConn[NUM_FLOORS][ROOMS_PER_FLOOR - 1],
                   char roomEntities[NUM_FLOORS][ROOMS_PER_FLOOR][MAX_ENTITIES_PER_ROOM],
                   int entityCounts[NUM_FLOORS][ROOMS_PER_FLOOR]) {
    
    char cmd[64];
    while (true) {
        if (checkEndConditions(energy, time, objectives)) break;

        renderDashboard(playerFloor, playerRoom, energy, time, score, inventory, inventoryCount, emergencyTripsLeft);
        renderRoomDetails(playerFloor, playerRoom, roomEntities, entityCounts[playerFloor-1][playerRoom-1]);
        std::cout << "> ";
        std::cin >> cmd;

        if (stringEquals(cmd, "quit")) break;
        else if (stringEquals(cmd, "map")) renderAsciiBuilding(playerFloor, playerRoom);
        else if (stringEquals(cmd, "inspect")) {
            handleInspectCommand(playerFloor, playerRoom, energy, time, roomEntities, entityCounts[playerFloor-1][playerRoom-1], objectives, score);
        }
        else if (stringEquals(cmd, "collect")) {
            char sym; std::cout << "Enter item symbol: "; std::cin >> sym;
            handleCollectCommand(sym, playerFloor, playerRoom, energy, time, score, roomEntities, entityCounts[playerFloor-1][playerRoom-1], inventory, inventoryCount, objectives);
        }
        else if (stringEquals(cmd, "use")) {
            char sym; std::cout << "Enter item symbol: "; std::cin >> sym;
            handleUseCommand(sym, playerFloor, playerRoom, energy, time, score, inventory, inventoryCount, objectives);
        }
        else if (stringEquals(cmd, "move")) {
            int f, r; std::cout << "Enter target floor and room (e.g. 1 2): "; std::cin >> f >> r;
            handleMoveCommand(f, r, playerFloor, playerRoom, energy, time, horizontalConn, inventory, inventoryCount, emergencyTripsLeft, objectives[OBJ_LIFT2_ACTIVE]);
        }
        else {
            std::cout << "Unknown command.\n";
        }
    }
    
    bool victory = objectives[OBJ_TRAIN_PREPARED] && energy > 0 && time > 0;
    writeGameSummaryReport("out/game_summary.txt", score, energy, time, objectives, victory);
    if (victory) std::cout << "VICTORY!\n";
    else std::cout << "GAME OVER.\n";
}