#include "server.h"
#include <stdatomic.h>

const char *globalPlayerNameKey = "name";
const int maxScore = 1000;

static int lastBalanceTime = 0;
static atomic_flag balanceTeamsFlag = ATOMIC_FLAG_INIT;

typedef void (*ActionFunc)(const char *);

typedef struct {
    const char *actionName;
    ActionFunc handler;
} ActionMap;

typedef struct {
    int playerNum;
    int kills;
    int deaths;
    int score;
    int teamId;
} player_info_t;

void handleUserinfoChanged(const char *line);

void handleKill(const char *line);

void handleExit(const char *line);

ActionMap options[] = {
        {"ClientUserinfoChanged", handleUserinfoChanged},
        {"Kill",                  handleKill},
        {"Exit",                  handleExit},
        {NULL, NULL},
};

void updatePlayerScore(client_t *client, int *scoreField, int increment);

void balanceTeams(void);

void findOptimalSwap(player_info_t *players, int numPlayers, int team1Id, int team2Id,
                     int *bestPlayer1Index, int *bestPlayer2Index);

int comparePlayers(const void *a, const void *b);

void logTeamBalance(player_info_t *players, int numPlayers, int team1Id, int team2Id, const char *balanceType);

client_t *getPlayerByNumber(int playerNum);

void printUserInfo(client_t *client);

qboolean isValidTeamId(int teamId);

const char *getTeamName(int teamId);

void announceTeamsAutobalance();

void announceTeamSwap(const char *player1Name, int originalTeam1Id, int newTeam1Id,
                      const char *player2Name, int originalTeam2Id, int newTeam2Id);

void swapPlayers(const player_info_t *player1, const player_info_t *player2);

void handleUserinfoChanged(const char *line) {
    if (!line || *(line + 1) == '\0') {
        Com_DPrintf("Error: Invalid format. Line is NULL or too short: %s\n", line ? line : "(null)");
        return;
    }

    char *tokens[MAX_STRING_TOKENS];
    char line_copy[MAX_INFO_STRING];
    int numTokens;
    int playerNum;
    int teamId;

    Q_strncpyz(line_copy, line, sizeof(line_copy));
    numTokens = Com_Split(line_copy, tokens, MAX_STRING_TOKENS, '\\');

    if (numTokens < 1) {
        Com_DPrintf("Error: No tokens found in the line: %s\n", line);
        return;
    }

    if (sscanf(tokens[0], "%d", &playerNum) != 1) {
        Com_DPrintf("Error: Invalid player number: %s\n", tokens[0]);
        return;
    }

    teamId = -1;
    for (int i = 1; i < numTokens; i++) {
        if (strcmp(tokens[i], "t") == 0 && i + 1 < numTokens) {
            teamId = atoi(tokens[i + 1]);
            break;
        }
    }

    if (teamId == -1) {
        Com_DPrintf("Error: Invalid or missing team key in the line: %s\n", line);
        return;
    }

    Com_DPrintf("Player number: %d, Team name: %s\n", playerNum, getTeamName(teamId));

    client_t *client = getPlayerByNumber(playerNum);
    if (!client) {
        Com_DPrintf("Error: Could not find player with number: %d\n", playerNum);
        return;
    }

    client->teamId = teamId;
    Com_DPrintf("Team id successfully updated to '%d' for player number %d\n", teamId, playerNum);

    balanceTeams();
}

void handleKill(const char *line) {
    if (!line || *(line + 1) == '\0') {
        Com_DPrintf("Error: Invalid format. Line is NULL or too short: %s\n", line ? line : "(null)");
        return;
    }

    int killer_id, victim_id, death_cause_index;
    char killer_name[MAX_INFO_VALUE], victim_name[MAX_INFO_VALUE];

    const char *colonPos = strchr(line, ':');
    if (!colonPos) {
        Com_DPrintf("Error: Invalid format, no colon found: %s\n", line);
        return;
    }
    colonPos++;
    if (*colonPos == '\0') {
        Com_DPrintf("Error: Nothing found after the colon in the line: %s\n", line);
        return;
    }

    int count = sscanf(line, "%d %d %d:", &killer_id, &victim_id, &death_cause_index);
    if (count != 3) {
        Com_DPrintf("Error: Invalid kill format, expected 3 numbers before the colon (%d): %s\n", count, line);
        return;
    }

    while (*colonPos == ' ') colonPos++;
    count = sscanf(colonPos, "%[^ ] killed %[^ ]", killer_name, victim_name);
    if (count != 2) {
        Com_DPrintf("Error: Invalid kill description format (%d): %s\n", count, colonPos);
        return;
    }

    Com_DPrintf("Processing kill event: Killer ID=%d, Victim ID=%d, Death Cause Index=%d\n",
                killer_id, victim_id, death_cause_index);

    client_t *killer = getPlayerByNumber(killer_id);
    client_t *victim = getPlayerByNumber(victim_id);

    if (!victim) {
        Com_DPrintf("Error: Invalid victim ID: %d\n", victim_id);
        return;
    }

    if (strcmp(killer_name, "<world>") == 0) {
        Com_DPrintf("Info: World caused death, no score updated for killer. Victim ID: %d\n", victim_id);
        updatePlayerScore(victim, &victim->deaths, 1);
        return;
    }

    if (!killer) {
        Com_DPrintf("Error: Invalid killer ID: %d\n", killer_id);
        return;
    }

    Com_DPrintf("Info: Kill event - Killer: %s, Victim: %s, Death Cause: %d\n",
                killer_name, victim_name, death_cause_index);
    Com_DPrintf("Killer userinfo:\n");
    printUserInfo(killer);

    Com_DPrintf("Victim userinfo:\n");
    printUserInfo(victim);

    int killerTeamId = killer->teamId;
    int victimTeamId = victim->teamId;

    if (!isValidTeamId(killerTeamId)) {
        Com_DPrintf("Error: Killer's team info not found. Killer ID: %d\n", killer_id);
    }
    if (!isValidTeamId(victimTeamId)) {
        Com_DPrintf("Error: Victim's team info not found. Victim ID: %d\n", victim_id);
    }

    if (killer_id == victim_id) {
        Com_DPrintf("Info: Suicide detected for player %d.\n", victim_id);
        updatePlayerScore(victim, &victim->deaths, 1);
    } else if (killerTeamId == victimTeamId) {
        Com_DPrintf("Info: Friendly fire detected. Killer ID: %d, Victim ID: %d\n", killer_id, victim_id);
        updatePlayerScore(killer, &killer->kills, -1);
    } else {
        Com_DPrintf("Info: Enemy kill detected. Killer ID: %d, Victim ID: %d\n", killer_id, victim_id);
        updatePlayerScore(killer, &killer->kills, 1);
        updatePlayerScore(victim, &victim->deaths, 1);
    }

    Com_DPrintf("Kill event processing completed for Killer ID: %d, Victim ID: %d\n", killer_id, victim_id);

    balanceTeams();
}

void handleExit(const char *line) {
    Com_DPrintf("Handling exit: Resetting player stats...\n");
    for (int i = 0; i < sv_maxclients->integer; i++) {
        client_t *client = &svs.clients[i];

        if (!client) {
            Com_DPrintf("Client %d is NULL, skipping...\n", i);
            continue;
        }

        Com_DPrintf("Checking client %d (state: %d)...\n", i, client->state);
        if (client->state >= CS_CONNECTED) {
            Com_DPrintf("Resetting stats for connected client (ID: %d):\n", i);
            printUserInfo(client);

            client->teamId = -1;
            client->kills = 0;
            client->deaths = 0;

            Com_DPrintf("Client %d stats reset successfully.\n", i);
        } else {
            Com_DPrintf("Client %d is not connected (state: %d), skipping reset...\n", i, client->state);
        }
    }

    Com_DPrintf("Finished resetting player stats.\n");
}

void handlePrintLine(char *string) {
    if (!string) {
        Com_DPrintf("Error: Input string is NULL\n");
        return;
    }

    char *colon = strchr(string, ':');
    if (!colon) {
        Com_DPrintf("Error: No colon found in the input string: %s\n", string);
        return;
    }

    *colon = '\0';
    char *actionName = string;
    char *line = colon + 1;
    while (*line == ' ') line++;

    if (*actionName == '\0') {
        Com_DPrintf("Error: Action name is empty in the input string: %s\n", string);
        return;
    }

    if (*line == '\0') {
        Com_DPrintf("Error: Line content is empty after action name: %s\n", actionName);
        return;
    }

    ActionMap *option = options;
    while (option->actionName != NULL) {
        if (strcmp(option->actionName, actionName) == 0) {
            if (option->handler != NULL) {
                option->handler(line);
            } else {
                Com_DPrintf("Error: Handler function is NULL for action '%s'.\n", actionName);
            }
            return;
        }
        option++;
    }
}

void updatePlayerScore(client_t *client, int *scoreField, int increment) {
    if (!client) {
        Com_DPrintf("Error: Client is NULL\n");
        return;
    }

    int clientId = (int) (client - svs.clients);

    if (!scoreField) {
        Com_DPrintf("Error: scoreField is NULL for client %d\n", clientId);
        return;
    }

    int oldScore = *scoreField;
    *scoreField += increment;
    int newScore = *scoreField;

    Com_DPrintf("Updating score for client %d:\n", clientId);
    Com_DPrintf("  Previous score: %d\n", oldScore);
    Com_DPrintf("  Increment: %d\n", increment);
    Com_DPrintf("  New score: %d\n", newScore);

    if (*scoreField == newScore) {
        Com_DPrintf("Score successfully updated for client %d\n", clientId);
    } else {
        Com_DPrintf("Error: Score update failed for client %d\n", clientId);
    }
}

void balanceTeams(void) {
    if (atomic_flag_test_and_set(&balanceTeamsFlag)) {
        Com_DPrintf("Skipping balanceTeams: another instance is running.\n");
        return;
    }

    int currentTime = svs.time;
    int timeSinceLastBalance = currentTime - lastBalanceTime;
    int minBalanceIntervalMs = sv_minBalanceInterval->integer * 1000;

    if (timeSinceLastBalance < minBalanceIntervalMs) {
        Com_DPrintf(
                "Auto-balance skipped: waiting for minimum interval (%d seconds). Current time: %d, Last balance time: %d, Time since last balance: %d ms, Minimum interval: %d ms\n",
                sv_minBalanceInterval->integer, currentTime, lastBalanceTime, timeSinceLastBalance,
                minBalanceIntervalMs);
        atomic_flag_clear(&balanceTeamsFlag);
        return;
    } else {
        Com_DPrintf(
                "Proceeding with team balance. Current time: %d, Last balance time: %d, Time since last balance: %d ms, Minimum interval: %d ms\n",
                currentTime, lastBalanceTime, timeSinceLastBalance, minBalanceIntervalMs);
    }

    Com_DPrintf("Initiating team balance process at time: %d\n", currentTime);

    player_info_t players[MAX_CLIENTS];
    int numPlayers = 0;
    int team1Id = -1;
    int team2Id = -1;

    Com_DPrintf("Starting team balance process...\n");

    for (int i = 0; i < sv_maxclients->integer; i++) {
        client_t *client = &svs.clients[i];
        if (client->state >= CS_CONNECTED) {
            int kills = client->kills;
            int deaths = client->deaths;
            int teamId = client->teamId;

            if (!isValidTeamId(teamId)) {
                Com_DPrintf("Player %d has invalid team ID: %d\n", i, teamId);
                continue;
            }

            int score = (kills * maxScore) / (deaths + 1);

            players[numPlayers].playerNum = i;
            players[numPlayers].kills = kills;
            players[numPlayers].deaths = deaths;
            players[numPlayers].score = score;
            players[numPlayers].teamId = teamId;

            if (team1Id == -1) {
                team1Id = teamId;
            } else if (team2Id == -1 && team1Id != teamId) {
                team2Id = teamId;
            }

            Com_DPrintf("Player %d: Team=%s, Kills=%d, Deaths=%d, Score=%d\n",
                        players[numPlayers].playerNum, getTeamName(players[numPlayers].teamId),
                        players[numPlayers].kills, players[numPlayers].deaths, players[numPlayers].score);

            numPlayers++;
        }
    }

    if (numPlayers < 2 || team1Id == -1 || team2Id == -1) {
        Com_DPrintf("Error: Not enough players or teams to balance.\n");
        atomic_flag_clear(&balanceTeamsFlag);
        return;
    }

    Com_DPrintf("Teams identified: Team1=%s, Team2=%s\n", getTeamName(team1Id), getTeamName(team2Id));
    logTeamBalance(players, numPlayers, team1Id, team2Id, "Current");

    Com_DPrintf("Sorting players by score...\n");
    qsort(players, numPlayers, sizeof(player_info_t), comparePlayers);

    int team1Score = 0, team2Score = 0;
    int team1Count = 0, team2Count = 0;

    for (int i = 0; i < numPlayers; i++) {
        if (players[i].teamId == team1Id) {
            team1Score += players[i].score;
            team1Count++;
        } else if (players[i].teamId == team2Id) {
            team2Score += players[i].score;
            team2Count++;
        }
    }

    float team1Average = (team1Count > 0) ? (float) team1Score / (float) team1Count : 0;
    float team2Average = (team2Count > 0) ? (float) team2Score / (float) team2Count : 0;

    float totalAverageScore = team1Average + team2Average;

    float team1Percentage = (totalAverageScore > 0) ? (team1Average / totalAverageScore) * 100 : 0;
    float team2Percentage = (totalAverageScore > 0) ? (team2Average / totalAverageScore) * 100 : 0;

    Com_DPrintf("Current scores - %s: %.2f%%, %s: %.2f%%\n",
                getTeamName(team1Id), team1Percentage, getTeamName(team2Id), team2Percentage);

    float scoreDifference = fabs(team1Percentage - team2Percentage);

    if (scoreDifference <= sv_scoreThreshold->value) {
        Com_DPrintf("Teams are balanced within the threshold (%.2f). No shuffling required.\n",
                    sv_scoreThreshold->value);
        atomic_flag_clear(&balanceTeamsFlag);
        return;
    }

    Com_DPrintf("Finding optimal player swap to balance teams...\n");
    int bestPlayer1Index = -1;
    int bestPlayer2Index = -1;
    findOptimalSwap(players, numPlayers, team1Id, team2Id, &bestPlayer1Index, &bestPlayer2Index);

    if (bestPlayer1Index != -1 && bestPlayer2Index != -1) {
        int originalTeam1Id = players[bestPlayer1Index].teamId;
        int originalTeam2Id = players[bestPlayer2Index].teamId;
        int newTeam1Id = team2Id;
        int newTeam2Id = team1Id;

        int newTeam1Score = team1Score - players[bestPlayer1Index].score + players[bestPlayer2Index].score;
        int newTeam2Score = team2Score - players[bestPlayer2Index].score + players[bestPlayer1Index].score;

        float newTeam1Average = (team1Count > 0) ? (float) newTeam1Score / (float) team1Count : 0;
        float newTeam2Average = (team2Count > 0) ? (float) newTeam2Score / (float) team2Count : 0;

        float newTotalAverageScore = newTeam1Average + newTeam2Average;

        float newTeam1Percentage = (newTotalAverageScore > 0) ? (newTeam1Average / newTotalAverageScore) * 100 : 0;
        float newTeam2Percentage = (newTotalAverageScore > 0) ? (newTeam2Average / newTotalAverageScore) * 100 : 0;

        float newScoreDifference = fabs(newTeam1Percentage - newTeam2Percentage);

        client_t *client1 = getPlayerByNumber(players[bestPlayer1Index].playerNum);
        client_t *client2 = getPlayerByNumber(players[bestPlayer2Index].playerNum);

        const char *player1Name = Info_ValueForKey(client1->userinfo, globalPlayerNameKey);
        const char *player2Name = Info_ValueForKey(client2->userinfo, globalPlayerNameKey);

        Com_DPrintf("Swapping player %s (team %s) with player %s (team %s)\n",
                    player1Name, getTeamName(originalTeam1Id), player2Name, getTeamName(originalTeam2Id));
        Com_DPrintf("Estimated balance score after swap - %s: %.2f%%, %s: %.2f%%. New score difference: %.2f\n",
                    getTeamName(newTeam1Id), newTeam1Percentage,
                    getTeamName(newTeam2Id), newTeam2Percentage, newScoreDifference);

        announceTeamsAutobalance();
        announceTeamSwap(player1Name, originalTeam1Id, newTeam1Id,
                         player2Name, originalTeam2Id, newTeam2Id);
        swapPlayers(&players[bestPlayer1Index], &players[bestPlayer2Index]);

        lastBalanceTime = svs.time;
    } else {
        Com_DPrintf("No optimal swap found to improve team balance.\n");
    }

    logTeamBalance(players, numPlayers, team1Id, team2Id, "Final");
    Com_DPrintf("Team balancing process completed.\n");

    atomic_flag_clear(&balanceTeamsFlag);
}

void findOptimalSwap(player_info_t *players, int numPlayers, int team1Id, int team2Id,
                     int *bestPlayer1Index, int *bestPlayer2Index) {
    int team1Score = 0, team2Score = 0;
    int team1Count = 0, team2Count = 0;

    for (int i = 0; i < numPlayers; i++) {
        if (players[i].teamId == team1Id) {
            team1Score += players[i].score;
            team1Count++;
        } else if (players[i].teamId == team2Id) {
            team2Score += players[i].score;
            team2Count++;
        }
    }

    float team1Average = (team1Count > 0) ? (float) team1Score / (float) team1Count : 0;
    float team2Average = (team2Count > 0) ? (float) team2Score / (float) team2Count : 0;

    float totalAverageScore = team1Average + team2Average;

    float team1Percentage = (totalAverageScore > 0) ? (team1Average / totalAverageScore) * 100 : 0;
    float team2Percentage = (totalAverageScore > 0) ? (team2Average / totalAverageScore) * 100 : 0;

    float initialScoreDifference = fabs(team1Percentage - team2Percentage);
    Com_DPrintf("Initial score difference: %.2f\n", initialScoreDifference);

    float bestScoreDifference = initialScoreDifference;

    for (int i = 0; i < numPlayers; i++) {
        for (int j = 0; j < numPlayers; j++) {
            if (players[i].teamId == team1Id && players[j].teamId == team2Id) {
                int newTeam1Score = team1Score - players[i].score + players[j].score;
                int newTeam2Score = team2Score - players[j].score + players[i].score;

                float newTeam1Average = (team1Count > 0) ? (float) newTeam1Score / (float) team1Count : 0;
                float newTeam2Average = (team2Count > 0) ? (float) newTeam2Score / (float) team2Count : 0;

                float newTotalAverageScore = newTeam1Average + newTeam2Average;

                float newTeam1Percentage = (newTotalAverageScore > 0)
                                           ? (newTeam1Average / newTotalAverageScore) * 100
                                           : 0;
                float newTeam2Percentage = (newTotalAverageScore > 0)
                                           ? (newTeam2Average / newTotalAverageScore) * 100
                                           : 0;

                float newScoreDifference = fabs(newTeam1Percentage - newTeam2Percentage);

                if (newScoreDifference < bestScoreDifference) {
                    bestScoreDifference = newScoreDifference;
                    *bestPlayer1Index = i;
                    *bestPlayer2Index = j;
                }
            }
        }
    }

    Com_DPrintf("Best score difference after evaluation: %.2f\n", bestScoreDifference);
}

int comparePlayers(const void *a, const void *b) {
    return ((player_info_t *) b)->score - ((player_info_t *) a)->score;
}

void logTeamBalance(player_info_t *players, int numPlayers, int team1Id, int team2Id, const char *balanceType) {
    int team1Score = 0, team2Score = 0;
    int team1Kills = 0, team2Kills = 0;
    int team1Deaths = 0, team2Deaths = 0;
    int team1Count = 0, team2Count = 0;

    for (int i = 0; i < numPlayers; i++) {
        if (players[i].teamId == team1Id) {
            team1Score += players[i].score;
            team1Kills += players[i].kills;
            team1Deaths += players[i].deaths;
            team1Count++;
        } else if (players[i].teamId == team2Id) {
            team2Score += players[i].score;
            team2Kills += players[i].kills;
            team2Deaths += players[i].deaths;
            team2Count++;
        }
    }

    float team1Average = (team1Count > 0) ? (float) team1Score / (float) team1Count : 0;
    float team2Average = (team2Count > 0) ? (float) team2Score / (float) team2Count : 0;

    float totalAverageScore = team1Average + team2Average;

    float team1Percentage = (totalAverageScore > 0) ? (team1Average / totalAverageScore) * 100 : 0;
    float team2Percentage = (totalAverageScore > 0) ? (team2Average / totalAverageScore) * 100 : 0;

    Com_DPrintf("%s team balance:\n", balanceType);
    Com_DPrintf("%s: %d players, Total score: %d, Average score: %.2f%%, Kills: %d, Deaths: %d\n",
                getTeamName(team1Id), team1Count, team1Score, team1Percentage, team1Kills, team1Deaths);
    Com_DPrintf("%s: %d players, Total score: %d, Average score: %.2f%%, Kills: %d, Deaths: %d\n",
                getTeamName(team2Id), team2Count, team2Score, team2Percentage, team2Kills, team2Deaths);
}

client_t *getPlayerByNumber(int playerNum) {
    Com_DPrintf("Fetching player by number: %d\n", playerNum);

    if (playerNum < 0 || playerNum >= sv_maxclients->integer) {
        Com_DPrintf("Error: Player number %d is out of valid range (0-%d).\n",
                    playerNum, sv_maxclients->integer - 1);
        return NULL;
    }

    client_t *cl = &svs.clients[playerNum];
    if (cl->state >= CS_CONNECTED) {
        return cl;
    } else {
        Com_DPrintf("Error: Player number %d is not connected. State: %d\n",
                    playerNum, cl->state);
        return NULL;
    }
}

void printUserInfo(client_t *client) {
    if (!client) {
        Com_DPrintf("Error: Client is NULL\n");
        return;
    }

    Com_DPrintf("Userinfo for client %d:\n", (int) (client - svs.clients));
    const char *userinfo = client->userinfo;

    if (*userinfo != '\\') {
        Com_DPrintf("Error: Malformed userinfo string\n");
        return;
    }

    char key[BIG_INFO_KEY];
    char value[BIG_INFO_VALUE];

    while (*userinfo) {
        userinfo = Info_NextPair(userinfo, key, value);
        if (*key == '\0') {
            break;
        }

        Com_DPrintf("  %s: %s\n", key, value);
    }

    int teamId = client->teamId;
    int kills = client->kills;
    int deaths = client->deaths;

    Com_DPrintf("  Team ID: %d\n", teamId);
    Com_DPrintf("  Team Name: %s\n", getTeamName(teamId));
    Com_DPrintf("  Kills: %d\n", kills);
    Com_DPrintf("  Deaths: %d\n", deaths);
}

qboolean isValidTeamId(int teamId) {
    return teamId == 0 || teamId == 1 || teamId == 2;
}

const char *getTeamName(int teamId) {
    switch (teamId) {
        case 0:
            return "green";
        case 1:
            return "red";
        case 2:
            return "blue";
        case 3:
            return "spectator";
        default:
            return "unknown";
    }
}

void announceTeamsAutobalance() {
    char sayCommand[MAX_STRING_CHARS];
    Com_sprintf(sayCommand, sizeof(sayCommand), "bigtext \"Autobalancing Teams!\"\n");
    Cbuf_ExecuteText(EXEC_NOW, sayCommand);
    Cbuf_Execute();
}

void announceTeamSwap(const char *player1Name, int originalTeam1Id, int newTeam1Id,
                      const char *player2Name, int originalTeam2Id, int newTeam2Id) {
    char sayCommand[MAX_STRING_CHARS];
    Com_sprintf(sayCommand, sizeof(sayCommand),
                "say ^7[^2Autobalancing^7] ^1%s ^7(^4%s^7) ^7swapped with ^1%s ^7(^4%s^7)\n",
                player1Name, getTeamName(originalTeam1Id),
                player2Name, getTeamName(originalTeam2Id));
    Cbuf_ExecuteText(EXEC_NOW, sayCommand);
    Cbuf_Execute();
}

void swapPlayers(const player_info_t *player1, const player_info_t *player2) {
    char command[MAX_STRING_CHARS];
    Com_sprintf(command, sizeof(command), "swap %d %d\n", player1->playerNum, player2->playerNum);
    Cbuf_ExecuteText(EXEC_NOW, command);
    Cbuf_Execute();
}