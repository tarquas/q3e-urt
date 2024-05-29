#include "server.h"
#include <stdatomic.h>
#include <stdbool.h>

const char *globalPlayerNameKey = "name";
const char *globalPlayerTeamNameKey = "team";

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
    int assists;
    int score;
    int teamId;
    int newTeamId;
    int flagPickups;
    int bombPickups;
    int flagDropped;
    int flagReturned;
    int flagCaptured;
    int minCapTime;
    int maxCapTime;
} PlayerInfo;

typedef enum {
    SCORE_KILLS,
    SCORE_DEATHS,
    SCORE_ASSISTS,
    SCORE_FLAG_PICKUPS,
    SCORE_BOMB_PICKUPS,
    SCORE_FLAG_DROPPED,
    SCORE_FLAG_RETURNED,
    SCORE_FLAG_CAPTURED,
    SCORE_MIN_CAPTURE_TIME,
    SCORE_MAX_CAPTURE_TIME
} ScoreFieldType;

void handleInitGame(const char *line);

void handleUserinfoChanged(const char *line);

void handleKill(const char *line);

void handleItem(const char *line);

void handleFlag(const char *line);

void handleFlagCaptureTime(const char *line);

void handleAssist(const char *line);

ActionMap options[] = {
        {"InitGame",              handleInitGame},
        {"ClientUserinfoChanged", handleUserinfoChanged},
        {"Kill",                  handleKill},
        {"Item",                  handleItem},
        {"Flag",                  handleFlag},
        {"FlagCaptureTime",       handleFlagCaptureTime},
        {"Assist",                handleAssist},
        {NULL, NULL},
};

int tokenizeUserinfoString(char *str, char *tokens[], int maxTokens);

void updatePlayerScore(client_t *client, ScoreFieldType field, int increment);

void balanceTeams(void);

void equalizeTeamSizes(PlayerInfo *players, int numPlayers, int team1Id, int team2Id);

void findMinimalScoreSwaps(PlayerInfo *players, int numPlayers, int team1Id, int team2Id);

int comparePlayers(const void *a, const void *b);

void logTeamBalance(PlayerInfo *players, int numPlayers, int team1Id, int team2Id, const char *balanceType);

client_t *getPlayerByNumber(int playerNum);

void printUserInfo(client_t *client);

qboolean isValidTeamId(int teamId);

const char *getStateName(clientState_t state);

const char *getTeamName(int teamId);

void announceTeamsAutobalance();

void announcePlayerMove(const char *playerName, int originalTeamId, int newTeamId);

void movePlayer(const char *playerName, int newTeamId);

void resetAllPlayerStatsAndSettings(void);

void printTokens(char *tokens[], int numTokens);

void handleInitGame(const char *line) {
    Com_DPrintf("Resetting all player stats and settings.\n");
    resetAllPlayerStatsAndSettings();

    char *tokens[MAX_STRING_TOKENS];
    int numTokens = tokenizeUserinfoString((char *) line, tokens, MAX_STRING_TOKENS);
    printTokens(tokens, numTokens);

    bool mapNameFound = false;
    bool gameTypeFound = false;

    for (int i = 0; i < numTokens; i++) {
        if (strcmp(tokens[i], "mapname") == 0 && (i + 1) < numTokens) {
            strncpy(svs.gameSettings.mapName, tokens[i + 1], sizeof(svs.gameSettings.mapName) - 1);
            svs.gameSettings.mapName[sizeof(svs.gameSettings.mapName) - 1] = '\0';
            Com_DPrintf("Parsed map name: %s\n", svs.gameSettings.mapName);
            mapNameFound = true;
        } else if (strcmp(tokens[i], "g_gametype") == 0 && (i + 1) < numTokens) {
            svs.gameSettings.gameType = atoi(tokens[i + 1]);
            Com_DPrintf("Parsed game type: %d\n", svs.gameSettings.gameType);
            gameTypeFound = true;
            switch (svs.gameSettings.gameType) {
                case 0:
                case 1:
                case 9:
                case 11:
                    svs.gameSettings.ffa_lms_gametype = qtrue;
                    Com_DPrintf("Game type set to Free for All or Last Man Standing\n");
                    break;
                case 7:
                    svs.gameSettings.ctf_gametype = qtrue;
                    Com_DPrintf("Game type set to Capture the Flag\n");
                    break;
                case 4:
                case 5:
                    svs.gameSettings.ts_gametype = qtrue;
                    Com_DPrintf("Game type set to Team Survivor\n");
                    break;
                case 3:
                    svs.gameSettings.tdm_gametype = qtrue;
                    Com_DPrintf("Game type set to Team Deathmatch\n");
                    break;
                case 8:
                    svs.gameSettings.bomb_gametype = qtrue;
                    Com_DPrintf("Game type set to Bomb Mode\n");
                    break;
                case 10:
                    svs.gameSettings.freeze_gametype = qtrue;
                    Com_DPrintf("Game type set to Freeze Tag\n");
                    break;
                default:
                    Com_DPrintf("Unknown game type: %d\n", svs.gameSettings.gameType);
                    break;
            }
        }
    }

    if (!mapNameFound) {
        Com_DPrintf("Error: mapname is missing in the InitGame string.\n");
        return;
    }
    if (!gameTypeFound) {
        Com_DPrintf("Error: g_gametype is missing in the InitGame string.\n");
        return;
    }
    Com_DPrintf("Game initialized with map: %s, game type: %d\n",
                svs.gameSettings.mapName, svs.gameSettings.gameType);
}

void handleUserinfoChanged(const char *line) {
    if (!line || *line == '\0') {
        Com_DPrintf("Error: Invalid format. Line is NULL or too short: %s\n", line ? line : "(null)");
        return;
    }

    Com_DPrintf("Processing userinfo changed line: %s\n", line);

    char lineCopy[MAX_INFO_STRING];
    Q_strncpyz(lineCopy, line, sizeof(lineCopy));

    char *token = strtok(lineCopy, " ");
    if (!token) {
        Com_DPrintf("Error: Failed to parse player number from line: %s\n", line);
        return;
    }
    int playerNum = atoi(token);
    if (playerNum < 0 || playerNum >= sv_maxclients->integer) {
        Com_DPrintf("Error: Invalid player number: %s\n", token);
        return;
    }

    char *rest = strtok(NULL, "");
    if (!rest) {
        Com_DPrintf("Error: Failed to parse userinfo string from line: %s\n", line);
        return;
    }

    Com_DPrintf("Remaining userinfo string: %s\n", rest);

    char *tokens[MAX_STRING_TOKENS];
    int numTokens = tokenizeUserinfoString(rest, tokens, MAX_STRING_TOKENS);
    printTokens(tokens, numTokens);

    int teamId = -1;
    char playerName[MAX_INFO_VALUE] = {0};

    for (int i = 0; i < numTokens - 1; i++) {
        if (strcmp(tokens[i], "t") == 0) {
            teamId = atoi(tokens[i + 1]);
        } else if (strcmp(tokens[i], "n") == 0) {
            Q_strncpyz(playerName, tokens[i + 1], sizeof(playerName));
        }
    }

    if (!isValidTeamId(teamId)) {
        Com_DPrintf("Error: Invalid or missing team key in the line: %s. Found Team ID: %d, Player Name: %s\n",
                    line, teamId, playerName);
        return;
    }

    if (playerName[0] == '\0') {
        Com_DPrintf("Error: Missing player name in the line: %s. Found Team ID: %d, Player Name: %s\n",
                    line, teamId, playerName);
        return;
    }

    Com_DPrintf("Parsed Player Info - Number: %d, Team: %s, Name: %s\n",
                playerNum, getTeamName(teamId), playerName);

    client_t *client = getPlayerByNumber(playerNum);
    if (!client) {
        Com_DPrintf("Error: Could not find player with number: %d. Found Team ID: %d, Player Name: %s\n",
                    playerNum, teamId, playerName);
        return;
    }

    client->teamId = teamId;
    Com_DPrintf("Team ID successfully updated to '%d' for player number %d\n", teamId, playerNum);

    Info_SetValueForKey(client->userinfo, globalPlayerNameKey, playerName);
    Info_SetValueForKey(client->userinfo, globalPlayerTeamNameKey, getTeamName(teamId));

    const char *newPlayerName = Info_ValueForKey(client->userinfo, globalPlayerNameKey);
    const char *newPlayerTeamName = Info_ValueForKey(client->userinfo, globalPlayerTeamNameKey);

    if (strcmp(newPlayerName, playerName) == 0) {
        Com_DPrintf("Player name successfully updated to '%s' for player number %d\n", playerName, playerNum);
    } else {
        Com_DPrintf("Error: Failed to update player name for player number %d. Expected: '%s', Got: '%s'\n",
                    playerNum, playerName, newPlayerName);
    }

    const char *expectedTeamName = getTeamName(teamId);
    if (strcmp(newPlayerTeamName, expectedTeamName) == 0) {
        Com_DPrintf("Player team successfully updated to '%s' for player number %d\n", expectedTeamName, playerNum);
    } else {
        Com_DPrintf("Error: Failed to update player team for player number %d. Expected: '%s', Got: '%s'\n",
                    playerNum, expectedTeamName, newPlayerTeamName);
    }

    balanceTeams();
}

void handleKill(const char *line) {
    if (!line || *line == '\0') {
        Com_DPrintf("Error: Invalid format. Line is NULL or too short: %s\n", line ? line : "(null)");
        return;
    }

    int killerId, victimId, deathCauseIndex;
    char killerName[MAX_INFO_VALUE] = {0};
    char victimName[MAX_INFO_VALUE] = {0};

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

    int count = sscanf(line, "%d %d %d:", &killerId, &victimId, &deathCauseIndex);
    if (count != 3) {
        Com_DPrintf("Error: Invalid kill format, expected 3 numbers before the colon (%d): %s\n", count, line);
        return;
    }

    while (*colonPos == ' ') colonPos++;
    count = sscanf(colonPos, "%[^ ] killed %[^ ]", killerName, victimName);
    if (count != 2) {
        Com_DPrintf("Error: Invalid kill description format (%d): %s\n", count, colonPos);
        return;
    }

    killerName[MAX_INFO_VALUE - 1] = '\0';
    victimName[MAX_INFO_VALUE - 1] = '\0';

    Com_DPrintf("Processing kill event: Killer ID=%d, Victim ID=%d, Death Cause Index=%d\n",
                killerId, victimId, deathCauseIndex);

    client_t *killer = getPlayerByNumber(killerId);
    client_t *victim = getPlayerByNumber(victimId);

    if (!victim || victim->state < CS_CONNECTED) {
        Com_DPrintf("Error: Invalid or disconnected victim ID: %d\n", victimId);
        return;
    }

    if (strcmp(killerName, "<world>") == 0) {
        Com_DPrintf("Info: World caused death, no score updated for killer. Victim ID: %d\n", victimId);
        updatePlayerScore(victim, SCORE_DEATHS, 1);
        return;
    }

    if (!killer || killer->state < CS_CONNECTED) {
        Com_DPrintf("Error: Invalid or disconnected killer ID: %d\n", killerId);
        return;
    }

    Com_DPrintf("Info: Kill event - Killer: %s, Victim: %s, Death Cause: %d\n",
                killerName, victimName, deathCauseIndex);
    Com_DPrintf("Killer userinfo:\n");
    printUserInfo(killer);

    Com_DPrintf("Victim userinfo:\n");
    printUserInfo(victim);

    int killerTeamId = killer->teamId;
    int victimTeamId = victim->teamId;

    if (!isValidTeamId(killerTeamId)) {
        Com_DPrintf("Error: Killer's team info not found. Killer ID: %d\n", killerId);
    }
    if (!isValidTeamId(victimTeamId)) {
        Com_DPrintf("Error: Victim's team info not found. Victim ID: %d\n", victimId);
    }

    if (killerId == victimId) {
        Com_DPrintf("Info: Suicide detected for player %d.\n", victimId);
        updatePlayerScore(victim, SCORE_DEATHS, 1);
    } else if (killerTeamId == victimTeamId) {
        Com_DPrintf("Info: Friendly fire detected. Killer ID: %d, Victim ID: %d\n", killerId, victimId);
        updatePlayerScore(killer, SCORE_KILLS, -1);
    } else {
        Com_DPrintf("Info: Enemy kill detected. Killer ID: %d, Victim ID: %d\n", killerId, victimId);
        updatePlayerScore(killer, SCORE_KILLS, 1);
        updatePlayerScore(victim, SCORE_DEATHS, 1);
    }

    Com_DPrintf("Kill event processing completed for Killer ID: %d, Victim ID: %d\n", killerId, victimId);

    balanceTeams();
}

void handleItem(const char *line) {
    if (!line || *line == '\0') {
        Com_DPrintf("Error: Invalid format. Line is NULL or too short: %s\n", line ? line : "(null)");
        return;
    }

    char lineCopy[MAX_INFO_STRING];
    Q_strncpyz(lineCopy, line, sizeof(lineCopy));

    char *playerNumStr = strtok(lineCopy, " ");
    char *item = strtok(NULL, "");

    if (!playerNumStr || !item) {
        Com_DPrintf("Error: Failed to parse item pickup line: %s\n", line);
        return;
    }

    int playerNum = atoi(playerNumStr);
    client_t *client = getPlayerByNumber(playerNum);
    if (!client || client->state < CS_CONNECTED) {
        Com_DPrintf("Error: Could not find connected player with number: %d\n", playerNum);
        return;
    }

    if (strstr(item, "flag")) {
        Com_DPrintf("Player %s picked up item: %s\n",
                    Info_ValueForKey(client->userinfo, globalPlayerNameKey), item);
        updatePlayerScore(client, SCORE_FLAG_PICKUPS, 1);
    } else if (strstr(item, "bomb")) {
        Com_DPrintf("Player %s picked up item: %s\n",
                    Info_ValueForKey(client->userinfo, globalPlayerNameKey), item);
        updatePlayerScore(client, SCORE_BOMB_PICKUPS, 1);
    } else {
        Com_DPrintf("Item pickup ignored: %s\n", item);
    }
}

void handleFlag(const char *line) {
    if (!line || *line == '\0') {
        Com_DPrintf("Error: Invalid format. Line is NULL or too short: %s\n", line ? line : "(null)");
        return;
    }

    char lineCopy[MAX_INFO_STRING];
    Q_strncpyz(lineCopy, line, sizeof(lineCopy));

    char *playerNumStr = strtok(lineCopy, " ");
    char *subtypeStr = strtok(NULL, ": ");
    char *data = strtok(NULL, "");

    if (!playerNumStr || !subtypeStr || !data) {
        Com_DPrintf("Error: Failed to parse flag event line: %s\n", line);
        return;
    }

    int playerNum = atoi(playerNumStr);
    int subtype = atoi(subtypeStr);
    client_t *client = getPlayerByNumber(playerNum);
    if (!client || client->state < CS_CONNECTED) {
        Com_DPrintf("Error: Could not find connected player with number: %d\n", playerNum);
        return;
    }

    switch (subtype) {
        case 0:
            Com_DPrintf("Player %s triggered action: flag_dropped with data: %s\n",
                        Info_ValueForKey(client->userinfo, globalPlayerNameKey), data);
            updatePlayerScore(client, SCORE_FLAG_DROPPED, 1);
            break;
        case 1:
            Com_DPrintf("Player %s triggered action: flag_returned with data: %s\n",
                        Info_ValueForKey(client->userinfo, globalPlayerNameKey), data);
            updatePlayerScore(client, SCORE_FLAG_RETURNED, 1);
            break;
        case 2:
            Com_DPrintf("Player %s triggered action: flag_captured with data: %s\n",
                        Info_ValueForKey(client->userinfo, globalPlayerNameKey), data);
            updatePlayerScore(client, SCORE_FLAG_CAPTURED, 1);
            break;
        default:
            Com_DPrintf("Subtype ignored: %d\n", subtype);
            return;
    }
}

void handleFlagCaptureTime(const char *line) {
    if (!line || *line == '\0') {
        Com_DPrintf("Error: Invalid format. Line is NULL or too short: %s\n", line ? line : "(null)");
        return;
    }

    char lineCopy[MAX_INFO_STRING];
    Q_strncpyz(lineCopy, line, sizeof(lineCopy));

    char *playerNumStr = strtok(lineCopy, ": ");
    char *capTimeStr = strtok(NULL, "");

    if (!playerNumStr || !capTimeStr) {
        Com_DPrintf("Error: Failed to parse flag capture time line: %s\n", line);
        return;
    }

    int playerNum = atoi(playerNumStr);
    int capTime = atoi(capTimeStr);
    int capTimeSeconds = (int) round((double) capTime / 1000 * 100) / 100;

    client_t *client = getPlayerByNumber(playerNum);
    if (!client || client->state < CS_CONNECTED) {
        Com_DPrintf("Error: Could not find connected player with number: %d\n", playerNum);
        return;
    }

    Com_DPrintf("Player %s (PlayerNum: %d) captured the flag in %d seconds\n",
                Info_ValueForKey(client->userinfo, globalPlayerNameKey), playerNum, capTimeSeconds);
    if (client->minCapTime == 0 || capTimeSeconds < client->minCapTime) {
        updatePlayerScore(client, SCORE_MIN_CAPTURE_TIME, capTimeSeconds);
    }
    if (capTimeSeconds > client->maxCapTime) {
        updatePlayerScore(client, SCORE_MAX_CAPTURE_TIME, capTimeSeconds);
    }
}

void handleAssist(const char *line) {
    if (!line || *line == '\0') {
        Com_DPrintf("Error: Invalid format. Line is NULL or too short: %s\n", line ? line : "(null)");
        return;
    }

    char lineCopy[MAX_INFO_STRING];
    Q_strncpyz(lineCopy, line, sizeof(lineCopy));

    char *playerNumStr = strtok(lineCopy, " ");
    char *victimNumStr = strtok(NULL, " ");
    char *attackerNumStr = strtok(NULL, " ");
    char *msg = strtok(NULL, "");

    if (!playerNumStr || !victimNumStr || !attackerNumStr || !msg) {
        Com_DPrintf("Error: Failed to parse assist line: %s\n", line);
        return;
    }

    int playerNum = atoi(playerNumStr);
    int victimNum = atoi(victimNumStr);
    int attackerNum = atoi(attackerNumStr);

    client_t *client = getPlayerByNumber(playerNum);
    client_t *victim = getPlayerByNumber(victimNum);
    client_t *attacker = getPlayerByNumber(attackerNum);

    if (!client || client->state < CS_CONNECTED) {
        Com_DPrintf("Error: Could not find connected assistant with number: %d\n", playerNum);
        return;
    }

    if (!victim || victim->state < CS_CONNECTED) {
        Com_DPrintf("Error: Could not find connected victim with number: %d\n", victimNum);
        return;
    }

    if (!attacker || attacker->state < CS_CONNECTED) {
        Com_DPrintf("Error: Could not find connected attacker with number: %d\n", attackerNum);
        return;
    }

    Com_DPrintf("Assist event: %s assisted %s to kill %s\n",
                Info_ValueForKey(client->userinfo, globalPlayerNameKey),
                Info_ValueForKey(attacker->userinfo, globalPlayerNameKey),
                Info_ValueForKey(victim->userinfo, globalPlayerNameKey));

    updatePlayerScore(client, SCORE_ASSISTS, 1);
}

void handlePrintLine(char *inputString) {
    if (!inputString) {
        Com_DPrintf("Error: Input string is NULL\n");
        return;
    }

    if (*inputString == '\0') {
        Com_DPrintf("Error: Input string is empty\n");
        return;
    }

    Com_DPrintf("Processing input string: %s\n", inputString);

    char string[MAX_INFO_STRING];
    Q_strncpyz(string, inputString, sizeof(string));

    char *colon = strchr(string, ':');
    if (!colon) {
        Com_DPrintf("Error: No colon found in the input string: %s\n", string);
        return;
    }

    *colon = '\0';
    char *actionName = string;
    char *line = colon + 1;
    while (*line == ' ') line++;

    Com_DPrintf("Parsed action name: %s\n", actionName);
    Com_DPrintf("Parsed line content: %s\n", line);

    if (*actionName == '\0') {
        Com_DPrintf("Error: Action name is empty in the input string: %s\n", inputString);
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
                Com_DPrintf("Found handler for action: %s, executing handler.\n", actionName);
                option->handler(line);
            } else {
                Com_DPrintf("Error: Handler function is NULL for action '%s'.\n", actionName);
            }
            return;
        }
        option++;
    }

    Com_DPrintf("Error: No handler found for action '%s'.\n", actionName);
}

void resetAllPlayerStatsAndSettings(void) {
    memset(&svs.gameSettings, 0, sizeof(svs.gameSettings));
    Com_DPrintf("Initialized gameSettings to zero.\n");
    for (int i = 0; i < sv_maxclients->integer; i++) {
        client_t *client = &svs.clients[i];
        if (!client) {
            Com_DPrintf("Client %d is NULL, skipping...\n", i);
            continue;
        }

        if (client->userinfo[0] == '\0') {
            continue;
        }

        client->kills = 0;
        client->deaths = 0;
        client->assists = 0;
        client->flagPickups = 0;
        client->bombPickups = 0;
        client->flagDropped = 0;
        client->flagReturned = 0;
        client->flagCaptured = 0;
        client->minCapTime = 0;
        client->maxCapTime = 0;

        Com_DPrintf("Reset stats and settings for client %d\n", i);
    }
}

int tokenizeUserinfoString(char *str, char *tokens[], int maxTokens) {
    if (!str || !tokens || maxTokens <= 0) {
        Com_DPrintf("Error: Invalid input to tokenizeUserinfoString. str: %s, tokens: %p, maxTokens: %d\n",
                    str ? str : "(null)", (void *) tokens, maxTokens);
        return 0;
    }

    int numTokens = 0;
    char *ptr = str;

    while (*ptr && numTokens < maxTokens) {
        if (numTokens >= maxTokens) {
            Com_DPrintf("Warning: Maximum number of tokens reached: %d\n", maxTokens);
            break;
        }

        tokens[numTokens++] = ptr;

        while (*ptr && *ptr != '\\') {
            ptr++;
        }

        if (*ptr) {
            *ptr++ = '\0';
        }
    }

    if (numTokens < maxTokens) {
        tokens[numTokens] = NULL;
    }

    return numTokens;
}

void updatePlayerScore(client_t *client, ScoreFieldType field, int increment) {
    if (!client) {
        Com_DPrintf("Error: Client is NULL\n");
        return;
    }

    int clientId = (int) (client - svs.clients);
    int *scoreField;
    const char *scoreFieldName;

    switch (field) {
        case SCORE_KILLS:
            scoreField = &client->kills;
            scoreFieldName = "kills";
            break;
        case SCORE_DEATHS:
            scoreField = &client->deaths;
            scoreFieldName = "deaths";
            break;
        case SCORE_ASSISTS:
            scoreField = &client->assists;
            scoreFieldName = "assists";
            break;
        case SCORE_FLAG_DROPPED:
            scoreField = &client->flagDropped;
            scoreFieldName = "flag dropped";
            break;
        case SCORE_FLAG_RETURNED:
            scoreField = &client->flagReturned;
            scoreFieldName = "flag returned";
            break;
        case SCORE_FLAG_CAPTURED:
            scoreField = &client->flagCaptured;
            scoreFieldName = "flag captured";
            break;
        case SCORE_FLAG_PICKUPS:
            scoreField = &client->flagPickups;
            scoreFieldName = "flag pickups";
            break;
        case SCORE_BOMB_PICKUPS:
            scoreField = &client->bombPickups;
            scoreFieldName = "bomb pickups";
            break;
        case SCORE_MIN_CAPTURE_TIME:
            scoreField = &client->minCapTime;
            scoreFieldName = "min capture time";
            break;
        case SCORE_MAX_CAPTURE_TIME:
            scoreField = &client->maxCapTime;
            scoreFieldName = "max capture time";
            break;
        default:
            Com_DPrintf("Error: Invalid score field for client %d\n", clientId);
            return;
    }

    if (field == SCORE_MIN_CAPTURE_TIME || field == SCORE_MAX_CAPTURE_TIME) {
        Com_DPrintf("Updating %s for client %d:\n", scoreFieldName, clientId);
        Com_DPrintf("  Previous %s: %d\n", scoreFieldName, *scoreField);
        Com_DPrintf("  New %s: %d\n", scoreFieldName, increment);
        *scoreField = increment;
    } else {
        Com_DPrintf("Updating %s for client %d:\n", scoreFieldName, clientId);
        Com_DPrintf("  Previous %s: %d\n", scoreFieldName, *scoreField);
        Com_DPrintf("  Increment: %d\n", increment);

        int oldScore = *scoreField;
        *scoreField += increment;
        int newScore = *scoreField;

        Com_DPrintf("  New %s: %d\n", scoreFieldName, newScore);

        if (newScore == oldScore + increment) {
            Com_DPrintf("%s successfully updated for client %d\n", scoreFieldName, clientId);
        } else {
            Com_DPrintf("Error: %s update failed for client %d. Expected: %d, Got: %d\n",
                        scoreFieldName, clientId, oldScore + increment, newScore);
        }
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
                sv_minBalanceInterval->integer, currentTime, lastBalanceTime, timeSinceLastBalance, minBalanceIntervalMs
        );
        atomic_flag_clear(&balanceTeamsFlag);
        return;
    }

    Com_DPrintf(
            "Proceeding with team balance. Current time: %d, Last balance time: %d, Time since last balance: %d ms, Minimum interval: %d ms\n",
            currentTime, lastBalanceTime, timeSinceLastBalance, minBalanceIntervalMs
    );
    Com_DPrintf("Initiating team balance process at time: %d\n", currentTime);

    PlayerInfo players[MAX_CLIENTS];
    int numPlayers = 0;
    int team1Id = -1;
    int team2Id = -1;

    Com_DPrintf("Starting team balance process...\n");

    for (int i = 0; i < sv_maxclients->integer; i++) {
        client_t *client = &svs.clients[i];
        if (client->state >= CS_CONNECTED) {
            int kills = client->kills;
            int deaths = client->deaths;
            int assists = client->assists;
            int teamId = client->teamId;

            if (!isValidTeamId(teamId)) {
                Com_DPrintf("Player %d has invalid team ID: %d\n", i, teamId);
                continue;
            }

            players[numPlayers].playerNum = i;
            players[numPlayers].kills = kills;
            players[numPlayers].deaths = deaths;
            players[numPlayers].assists = assists;
            players[numPlayers].score = kills - deaths;
            players[numPlayers].teamId = teamId;
            players[numPlayers].newTeamId = teamId;
            players[numPlayers].flagDropped = client->flagDropped;
            players[numPlayers].flagReturned = client->flagReturned;
            players[numPlayers].flagCaptured = client->flagCaptured;
            players[numPlayers].flagPickups = client->flagPickups;
            players[numPlayers].bombPickups = client->bombPickups;
            players[numPlayers].minCapTime = client->minCapTime;
            players[numPlayers].maxCapTime = client->maxCapTime;

            if (team1Id == -1) {
                team1Id = teamId;
            } else if (team2Id == -1 && team1Id != teamId) {
                team2Id = teamId;
            }

            Com_DPrintf(
                    "Player %d: Team=%s, Kills=%d, Deaths=%d, Assists=%d, Score=%d, Flag Dropped=%d, Flag Returned=%d, Flag Captured=%d, Flag Pickups=%d, Bomb Pickups=%d, Min Capture Time=%d, Max Capture Time=%d, Map Name=%s, Game Type=%d\n",
                    players[numPlayers].playerNum, getTeamName(players[numPlayers].teamId),
                    players[numPlayers].kills, players[numPlayers].deaths, players[numPlayers].assists,
                    players[numPlayers].score, players[numPlayers].flagDropped, players[numPlayers].flagReturned,
                    players[numPlayers].flagCaptured, players[numPlayers].flagPickups, players[numPlayers].bombPickups,
                    players[numPlayers].minCapTime, players[numPlayers].maxCapTime,
                    svs.gameSettings.mapName, svs.gameSettings.gameType
            );

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

    qsort(players, numPlayers, sizeof(PlayerInfo), comparePlayers);

    equalizeTeamSizes(players, numPlayers, team1Id, team2Id);
    findMinimalScoreSwaps(players, numPlayers, team1Id, team2Id);

    int movesMade = 0;

    for (int i = 0; i < numPlayers; i++) {
        if (players[i].teamId != players[i].newTeamId) {
            movesMade++;
        }
    }

    if (movesMade > 0) {
        announceTeamsAutobalance();
        Com_DPrintf("Number of players to be moved: %d\n", movesMade);

        for (int i = 0; i < numPlayers; i++) {
            if (players[i].teamId != players[i].newTeamId) {
                PlayerInfo *player = &players[i];
                int originalTeamId = player->teamId;
                int newTeamId = player->newTeamId;

                client_t *client = getPlayerByNumber(player->playerNum);
                const char *playerName = Info_ValueForKey(client->userinfo, globalPlayerNameKey);

                Com_DPrintf("Preparing to move player '%s' (PlayerNum: %d) from team '%s' to team '%s'.\n",
                            playerName, player->playerNum, getTeamName(originalTeamId), getTeamName(newTeamId));

                announcePlayerMove(playerName, originalTeamId, newTeamId);
                movePlayer(playerName, newTeamId);

                client->teamId = newTeamId;
                player->teamId = newTeamId;
                Info_SetValueForKey(client->userinfo, globalPlayerTeamNameKey, getTeamName(newTeamId));
                const char *updatedTeamName = Info_ValueForKey(client->userinfo, globalPlayerTeamNameKey);
                if (strcmp(updatedTeamName, getTeamName(newTeamId)) == 0) {
                    Com_DPrintf("Successfully moved player '%s' to team '%s'.\n", playerName, updatedTeamName);
                } else {
                    Com_DPrintf("Error: Failed to update team for player '%s'. Expected: '%s', Got: '%s'\n",
                                playerName, getTeamName(newTeamId), updatedTeamName);
                }
            }
        }
    } else {
        Com_DPrintf("No players needed to be moved.\n");
    }

    lastBalanceTime = svs.time;
    logTeamBalance(players, numPlayers, team1Id, team2Id, "Final");
    Com_DPrintf("Team balancing process completed.\n");

    atomic_flag_clear(&balanceTeamsFlag);
}

void equalizeTeamSizes(PlayerInfo *players, int numPlayers, int team1Id, int team2Id) {
    int team1Count = 0, team2Count = 0;

    for (int i = 0; i < numPlayers; i++) {
        if (players[i].teamId == team1Id) {
            team1Count++;
        } else if (players[i].teamId == team2Id) {
            team2Count++;
        }
    }

    Com_DPrintf("Initial team sizes: Team1=%d, Team2=%d\n", team1Count, team2Count);

    int sizeDifference = abs(team1Count - team2Count);
    if (sizeDifference <= 1) {
        Com_DPrintf("Team sizes are already balanced or nearly balanced. No swaps needed.\n");
        return;
    }

    int targetSize = (team1Count + team2Count) / 2;

    bool movedPlayers[MAX_CLIENTS] = {false};

    while (team1Count > targetSize) {
        bool moved = false;
        for (int i = numPlayers - 1; i >= 0; i--) {
            if (players[i].teamId == team1Id && !movedPlayers[i]) {
                Com_DPrintf("Equalizing size: Moving player %d from Team1 to Team2\n", players[i].playerNum);
                players[i].newTeamId = team2Id;
                movedPlayers[i] = true;
                team1Count--;
                team2Count++;
                moved = true;
                break;
            }
        }
        if (!moved) break;
    }

    while (team2Count > targetSize) {
        bool moved = false;
        for (int i = numPlayers - 1; i >= 0; i--) {
            if (players[i].teamId == team2Id && !movedPlayers[i]) {
                Com_DPrintf("Equalizing size: Moving player %d from Team2 to Team1\n", players[i].playerNum);
                players[i].newTeamId = team1Id;
                movedPlayers[i] = true;
                team2Count--;
                team1Count++;
                moved = true;
                break;
            }
        }
        if (!moved) break;
    }

    Com_DPrintf("Final team sizes after equalizing: Team1=%d, Team2=%d\n", team1Count, team2Count);
}

void findMinimalScoreSwaps(PlayerInfo *players, int numPlayers, int team1Id, int team2Id) {
    int team1Score = 0, team2Score = 0;
    for (int i = 0; i < numPlayers; i++) {
        if (players[i].teamId == team1Id) {
            team1Score += players[i].score;
        } else if (players[i].teamId == team2Id) {
            team2Score += players[i].score;
        }
    }

    Com_DPrintf("Initial team scores: Team1=%d, Team2=%d\n", team1Score, team2Score);

    float bestScoreDifference = fabs(team1Score - team2Score);
    while (bestScoreDifference > sv_scoreThreshold->value) {
        int bestPlayer1Index = -1;
        int bestPlayer2Index = -1;

        for (int i = 0; i < numPlayers; i++) {
            for (int j = 0; j < numPlayers; j++) {
                if (players[i].teamId == team1Id && players[j].teamId == team2Id) {
                    int newTeam1Score = team1Score - players[i].score + players[j].score;
                    int newTeam2Score = team2Score - players[j].score + players[i].score;
                    float newScoreDifference = fabs(newTeam1Score - newTeam2Score);

                    if (newScoreDifference < bestScoreDifference) {
                        bestScoreDifference = newScoreDifference;
                        bestPlayer1Index = i;
                        bestPlayer2Index = j;
                    }
                }
            }
        }

        if (bestPlayer1Index == -1 || bestPlayer2Index == -1) {
            break;
        }

        Com_DPrintf("Balancing score: Swapping player %d (Team1) with player %d (Team2)\n",
                    players[bestPlayer1Index].playerNum, players[bestPlayer2Index].playerNum);

        players[bestPlayer1Index].newTeamId = team2Id;
        players[bestPlayer2Index].newTeamId = team1Id;

        team1Score = team1Score - players[bestPlayer1Index].score + players[bestPlayer2Index].score;
        team2Score = team2Score - players[bestPlayer2Index].score + players[bestPlayer1Index].score;

        bestScoreDifference = fabs(team1Score - team2Score);
    }

    Com_DPrintf("Final team scores after balancing: Team1=%d, Team2=%d\n", team1Score, team2Score);
}

int comparePlayers(const void *a, const void *b) {
    return ((PlayerInfo *) b)->score - ((PlayerInfo *) a)->score;
}

void logTeamBalance(PlayerInfo *players, int numPlayers, int team1Id, int team2Id, const char *balanceType) {
    int team1Score = 0, team2Score = 0;
    int team1Kills = 0, team2Kills = 0;
    int team1Deaths = 0, team2Deaths = 0;
    int team1Assists = 0, team2Assists = 0;
    int team1FlagDropped = 0, team2FlagDropped = 0;
    int team1FlagReturned = 0, team2FlagReturned = 0;
    int team1FlagCaptured = 0, team2FlagCaptured = 0;
    int team1FlagPickups = 0, team2FlagPickups = 0;
    int team1BombPickups = 0, team2BombPickups = 0;
    int team1MinCapTime = INT_MAX, team2MinCapTime = INT_MAX;
    int team1MaxCapTime = 0, team2MaxCapTime = 0;
    int team1Count = 0, team2Count = 0;

    Com_DPrintf("%s team balance:\n", balanceType);

    for (int i = 0; i < numPlayers; i++) {
        if (svs.clients[players[i].playerNum].state >= CS_CONNECTED) {
            Com_DPrintf(
                    "Player %d: Team=%s, Kills=%d, Deaths=%d, Assists=%d, Score=%d, Flag Dropped=%d, Flag Returned=%d, Flag Captured=%d, Flag Pickups=%d, Bomb Pickups=%d, Min Capture Time=%d, Max Capture Time=%d, Map Name=%s, Game Type=%d\n",
                    players[i].playerNum, getTeamName(players[i].teamId),
                    players[i].kills, players[i].deaths, players[i].assists, players[i].score,
                    players[i].flagDropped, players[i].flagReturned, players[i].flagCaptured,
                    players[i].flagPickups, players[i].bombPickups, players[i].minCapTime, players[i].maxCapTime,
                    svs.gameSettings.mapName, svs.gameSettings.gameType
            );

            if (players[i].teamId == team1Id) {
                team1Score += players[i].score;
                team1Kills += players[i].kills;
                team1Deaths += players[i].deaths;
                team1Assists += players[i].assists;
                team1FlagDropped += players[i].flagDropped;
                team1FlagReturned += players[i].flagReturned;
                team1FlagCaptured += players[i].flagCaptured;
                team1FlagPickups += players[i].flagPickups;
                team1BombPickups += players[i].bombPickups;
                if (players[i].minCapTime < team1MinCapTime) team1MinCapTime = players[i].minCapTime;
                if (players[i].maxCapTime > team1MaxCapTime) team1MaxCapTime = players[i].maxCapTime;
                team1Count++;
            } else if (players[i].teamId == team2Id) {
                team2Score += players[i].score;
                team2Kills += players[i].kills;
                team2Deaths += players[i].deaths;
                team2Assists += players[i].assists;
                team2FlagDropped += players[i].flagDropped;
                team2FlagReturned += players[i].flagReturned;
                team2FlagCaptured += players[i].flagCaptured;
                team2FlagPickups += players[i].flagPickups;
                team2BombPickups += players[i].bombPickups;
                if (players[i].minCapTime < team2MinCapTime) team2MinCapTime = players[i].minCapTime;
                if (players[i].maxCapTime > team2MaxCapTime) team2MaxCapTime = players[i].maxCapTime;
                team2Count++;
            }
        }
    }

    Com_DPrintf("%s summary:\n", balanceType);
    Com_DPrintf(
            "%s: %d players, Total score: %d, Kills: %d, Deaths: %d, Assists: %d, Flag Dropped: %d, Flag Returned: %d, Flag Captured: %d, Flag Pickups: %d, Bomb Pickups: %d, Min Capture Time: %d, Max Capture Time: %d\n",
            getTeamName(team1Id), team1Count, team1Score, team1Kills, team1Deaths, team1Assists, team1FlagDropped,
            team1FlagReturned, team1FlagCaptured, team1FlagPickups, team1BombPickups, team1MinCapTime, team1MaxCapTime
    );
    Com_DPrintf(
            "%s: %d players, Total score: %d, Kills: %d, Deaths: %d, Assists: %d, Flag Dropped: %d, Flag Returned: %d, Flag Captured: %d, Flag Pickups: %d, Bomb Pickups: %d, Min Capture Time: %d, Max Capture Time: %d\n",
            getTeamName(team2Id), team2Count, team2Score, team2Kills, team2Deaths, team2Assists, team2FlagDropped,
            team2FlagReturned, team2FlagCaptured, team2FlagPickups, team2BombPickups, team2MinCapTime, team2MaxCapTime
    );
}

client_t *getPlayerByNumber(int playerNum) {
    if (playerNum < 0 || playerNum >= sv_maxclients->integer) {
        Com_DPrintf("Error: Player number %d is out of valid range (0-%d).\n",
                    playerNum, sv_maxclients->integer - 1);
        return NULL;
    }

    return &svs.clients[playerNum];
}

void printUserInfo(client_t *client) {
    if (!client) {
        Com_DPrintf("Error: Client is NULL\n");
        return;
    }
    if (!com_developer || !com_developer->integer) {
        return;
    }

    int playerId = (int) (client - svs.clients);
    const char *playerName = Info_ValueForKey(client->userinfo, globalPlayerNameKey);
    const char *teamName = getTeamName(client->teamId);
    const char *stateName = getStateName(client->state);

    Com_DPrintf("Userinfo for client %d:\n", playerId);

    const char *userinfo = client->userinfo;
    if (*userinfo != '\\') {
        Com_DPrintf("Error: Malformed userinfo string. Userinfo: %s\n", userinfo);
        return;
    }

    char key[BIG_INFO_KEY];
    char value[BIG_INFO_VALUE];

    Com_DPrintf("Parsed userinfo key-value pairs:\n");
    while (*userinfo) {
        userinfo = Info_NextPair(userinfo, key, value);
        if (*key == '\0') {
            break;
        }

        Com_DPrintf("  %s: %s\n", key, value);
    }

    Com_DPrintf("  Player ID: %d\n", playerId);
    Com_DPrintf("  Name: %s\n", playerName);
    Com_DPrintf("  Team ID: %d\n", client->teamId);
    Com_DPrintf("  Team Name: %s\n", teamName);
    Com_DPrintf("  State: %s\n", stateName);
    Com_DPrintf("  Kills: %d\n", client->kills);
    Com_DPrintf("  Deaths: %d\n", client->deaths);
    Com_DPrintf("  Assists: %d\n", client->assists);
    Com_DPrintf("  Flag Dropped: %d\n", client->flagDropped);
    Com_DPrintf("  Flag Returned: %d\n", client->flagReturned);
    Com_DPrintf("  Flag Captured: %d\n", client->flagCaptured);
    Com_DPrintf("  Flag Pickups: %d\n", client->flagPickups);
    Com_DPrintf("  Bomb Pickups: %d\n", client->bombPickups);
    Com_DPrintf("  Min Capture Time: %d\n", client->minCapTime);
    Com_DPrintf("  Max Capture Time: %d\n", client->maxCapTime);
    Com_DPrintf("  Map Name: %s\n", svs.gameSettings.mapName);
    Com_DPrintf("  Game Type: %d\n", svs.gameSettings.gameType);
}

void printTokens(char *tokens[], int numTokens) {
    if (com_developer && com_developer->integer) {
        Com_DPrintf("Tokenized user info string. Number of tokens parsed: %d\n", numTokens);
        for (int i = 0; i < numTokens; i++) {
            Com_DPrintf("Token %d: %s\n", i, tokens[i]);
        }
    }
}

qboolean isValidTeamId(int teamId) {
    return teamId == 0 || teamId == 1 || teamId == 2;
}

const char *getStateName(clientState_t state) {
    switch (state) {
        case CS_FREE:
            return "Free";
        case CS_ZOMBIE:
            return "Zombie";
        case CS_CONNECTED:
            return "Connected";
        case CS_PRIMED:
            return "Primed";
        case CS_ACTIVE:
            return "Active";
        default:
            return "Unknown";
    }
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
    char sayCommand[MAX_CMD_LINE];
    Com_sprintf(sayCommand, sizeof(sayCommand), "bigtext \"Autobalancing Teams!\"\n");
    Cbuf_ExecuteText(EXEC_NOW, sayCommand);
    Cbuf_Execute();
}

void announcePlayerMove(const char *playerName, int originalTeamId, int newTeamId) {
    const char *originalTeamColor = "";
    const char *newTeamColor = "";

    switch (originalTeamId) {
        case 0:
            originalTeamColor = "^2"; // Green
            break;
        case 1:
            originalTeamColor = "^1"; // Red
            break;
        case 2:
            originalTeamColor = "^4"; // Blue
            break;
        default:
            originalTeamColor = "^7"; // Default white color
    }

    switch (newTeamId) {
        case 0:
            newTeamColor = "^2"; // Green
            break;
        case 1:
            newTeamColor = "^1"; // Red
            break;
        case 2:
            newTeamColor = "^4"; // Blue
            break;
        default:
            newTeamColor = "^7"; // Default white color
    }

    const char *originalTeamName = getTeamName(originalTeamId);
    const char *newTeamName = getTeamName(newTeamId);

    char announcement[MAX_CMD_LINE];
    Com_sprintf(announcement, sizeof(announcement),
                "say ^7[^3Team Balance^7] ^7Player ^3%s ^7will be moved from %s%s ^7to %s%s^7.\n",
                playerName, originalTeamColor, originalTeamName, newTeamColor, newTeamName);

    Cbuf_ExecuteText(EXEC_NOW, announcement);
    Cbuf_Execute();
}

void movePlayer(const char *playerName, int newTeamId) {
    char command[MAX_CMD_LINE];
    Com_sprintf(command, sizeof(command), "forceteam %s %s\n", playerName, getTeamName(newTeamId));
    Cbuf_ExecuteText(EXEC_NOW, command);
    Cbuf_Execute();
}
