#include "tests.h"
#include "../server/server.h"

static cvar_t sv_maxclients_val = {.integer = MAX_CLIENTS, .value=MAX_CLIENTS};
static cvar_t sv_scoreThreshold_val = {.integer = 0, .value = 0};
static cvar_t sv_minBalanceInterval_val = {.integer = 0, .value = 0};
static cvar_t com_developer_val = {.integer = 1, .value = 1};

extern char *globalPlayerNameKey;
extern char *globalPlayerTeamNameKey;

void balanceTeams(void);

client_t *getPlayerByNumber(int playerNum);

qboolean isValidTeamId(int teamId);

void printUserInfo(client_t *client);

const char *getTeamName(int teamId);

void initializeServerState(void);

void finalizeServerState(void);

void clearState(void);

void connectPlayerId(const char *playerNum);

void disconnectPlayerId(const char *playerNum);

void resetPlayerDetails(client_t *client);

void populateTeams(int team1Size, int team2Size);

void assertTeamBalance(int expectedTeam1Size, int expectedTeam2Size, int expectedTotalPlayers);

void printPlayerDetails(void);

void populateTeamsWithScores(int team1Size, int team2Size, int team1Kills, int team1Deaths,
                             int team2Kills, int team2Deaths);

void assertTeamBalanceByScores(int expectedTeam1Size, int expectedTeam2Size, int expectedTotalPlayers,
                               int expectedTeam1Score, int expectedTeam2Score);

void testRegisterBotWithNoLeadingSlashOk(void) {
    clearState();
    const char *playerId = "2";
    const char *playerName = "Dre";
    const int playerTeamId = 2;
    connectPlayerId(playerId);

    char userinfo[MAX_INFO_VALUE];
    snprintf(userinfo, sizeof(userinfo),
             "ClientUserinfoChanged: %s n\\%s\\t\\%d\\r\\2\\tl\\0\\f0\\\\f1\\\\f2\\\\a0\\0\\a1\\0\\a2\\0",
             playerId, playerName, playerTeamId);
    handlePrintLine(userinfo);

    client_t *client = getPlayerByNumber(atoi(playerId));
    mu_assert_int_eq("Player team ID", client->teamId, playerTeamId);
    mu_assert_str_eq("Player name", Info_ValueForKey(client->userinfo, "name"), playerName);

    disconnectPlayerId(playerId);
}

void testRegisterUserWithLeadingSlashOk(void) {
    clearState();
    const char *playerId = "14";
    const char *playerName = "spectreX";
    const int playerTeamId = 1;
    connectPlayerId(playerId);

    char userinfo[MAX_INFO_VALUE];
    snprintf(userinfo, sizeof(userinfo),
             "ClientUserinfoChanged: %s \\n\\%s\\t\\%d\\r\\1\\tl\\0\\f0\\\\f1\\\\f2\\\\a0\\255\\a1\\255\\a2\\255",
             playerId, playerName, playerTeamId);
    handlePrintLine(userinfo);

    client_t *client = getPlayerByNumber(atoi(playerId));
    mu_assert_int_eq("Player team ID", client->teamId, playerTeamId);
    mu_assert_str_eq("Player name", Info_ValueForKey(client->userinfo, "name"), playerName);

    disconnectPlayerId(playerId);
}

void testHandleKillWithValidInput(void) {
    clearState();
    const char *killerId = "12";
    const char *victimId = "18";
    const char *killerName = "spectreX";
    const char *victimName = "Dre";
    const int killerTeam = 1;
    const int victimTeam = 2;
    connectPlayerId(killerId);
    connectPlayerId(victimId);

    char userinfo_killer[MAX_INFO_VALUE];
    snprintf(userinfo_killer, sizeof(userinfo_killer),
             "ClientUserinfoChanged: %s n\\%s\\t\\%d\\r\\1\\tl\\0\\f0\\\\f1\\\\f2\\\\a0\\255\\a1\\255\\a2\\255",
             killerId, killerName, killerTeam);
    handlePrintLine(userinfo_killer);

    char userinfo_victim[MAX_INFO_VALUE];
    snprintf(userinfo_victim, sizeof(userinfo_victim),
             "ClientUserinfoChanged: %s \\n\\%s\\t\\%d\\r\\1\\tl\\0\\f0\\\\f1\\\\f2\\\\a0\\255\\a1\\255\\a2\\255",
             victimId, victimName, victimTeam);
    handlePrintLine(userinfo_victim);

    char kill_info[MAX_INFO_VALUE];
    snprintf(kill_info, sizeof(kill_info),
             "Kill: %s %s 19: %s killed %s by UT_MOD_LR300", killerId, victimId, killerName, victimName);
    handlePrintLine(kill_info);

    client_t *killer = getPlayerByNumber(atoi(killerId));
    client_t *victim = getPlayerByNumber(atoi(victimId));

    mu_assert_int_eq("Player kills", killer->kills, 1);
    mu_assert_int_eq("Player deaths", killer->deaths, 0);
    mu_assert_int_eq("Bot kills", victim->kills, 0);
    mu_assert_int_eq("Bot deaths", victim->deaths, 1);

    disconnectPlayerId(killerId);
    disconnectPlayerId(victimId);
}

void testHandleUserinfoChangedWithInvalidInput(void) {
    clearState();
    char *invalidInput = "ClientUserinfoChanged: invalid_input_format";
    handlePrintLine(invalidInput);

    for (int i = 0; i < sv_maxclients->integer; i++) {
        client_t *client = getPlayerByNumber(i);
        mu_assert_int_eq("Client state should remain free", client->state, CS_FREE);
        mu_assert_int_eq("Client team ID should remain invalid", client->teamId, -1);
    }
}

void testHandleKillWithInvalidInput(void) {
    clearState();
    char *invalidKillInput = "Kill: invalid_kill_format";
    handlePrintLine(invalidKillInput);

    for (int i = 0; i < sv_maxclients->integer; i++) {
        client_t *client = getPlayerByNumber(i);
        mu_assert_int_eq("Client state should remain free", client->state, CS_FREE);
        mu_assert_int_eq("Client team ID should remain invalid", client->teamId, -1);
    }
}

void testHandleUserinfoChangedWithMissingPlayerID(void) {
    clearState();
    char *invalidInput = "ClientUserinfoChanged: n\\Player\\t\\1";
    handlePrintLine(invalidInput);

    for (int i = 0; i < sv_maxclients->integer; i++) {
        client_t *client = getPlayerByNumber(i);
        mu_assert_int_eq("Client state should remain free", client->state, CS_FREE);
        mu_assert_int_eq("Client team ID should remain invalid", client->teamId, -1);
    }
}

void testHandleUserinfoChangedWithInvalidPlayerNumber(void) {
    clearState();
    char *invalidInput = "ClientUserinfoChanged: -1 n\\Player\\t\\1";
    handlePrintLine(invalidInput);

    for (int i = 0; i < sv_maxclients->integer; i++) {
        client_t *client = getPlayerByNumber(i);
        mu_assert_int_eq("Client state should remain free", client->state, CS_FREE);
        mu_assert_int_eq("Client team ID should remain invalid", client->teamId, -1);
    }
}

void testHandleUserinfoChangedWithMissingTeamID(void) {
    clearState();
    char *invalidInput = "ClientUserinfoChanged: 1 n\\Player";
    handlePrintLine(invalidInput);

    client_t *client = getPlayerByNumber(1);
    mu_assert_int_eq("Client state should remain free", client->state, CS_FREE);
    mu_assert_int_eq("Client team ID should remain invalid", client->teamId, -1);
}

void testHandleUserinfoChangedWithMissingPlayerName(void) {
    clearState();
    char *invalidInput = "ClientUserinfoChanged: 1 t\\1";
    handlePrintLine(invalidInput);

    client_t *client = getPlayerByNumber(1);
    mu_assert_int_eq("Client state should remain free", client->state, CS_FREE);
    mu_assert_int_eq("Client team ID should remain invalid", client->teamId, -1);
}

void testHandleUserinfoChangedWithEmptyUserInfo(void) {
    clearState();
    char *invalidInput = "ClientUserinfoChanged:";
    handlePrintLine(invalidInput);

    for (int i = 0; i < sv_maxclients->integer; i++) {
        client_t *client = getPlayerByNumber(i);
        mu_assert_int_eq("Client state should remain free", client->state, CS_FREE);
        mu_assert_int_eq("Client team ID should remain invalid", client->teamId, -1);
    }
}

void testHandleUserinfoChangedWithInvalidTeamID(void) {
    clearState();
    char *invalidInput = "ClientUserinfoChanged: 1 n\\Player\\t\\9999";
    handlePrintLine(invalidInput);

    client_t *client = getPlayerByNumber(1);
    mu_assert_int_eq("Client state should remain free", client->state, CS_FREE);
    mu_assert_int_eq("Client team ID should remain invalid", client->teamId, -1);
}

void testHandleUserinfoChangedWithPlayerIDOutOfRange(void) {
    clearState();
    char *invalidInput = "ClientUserinfoChanged: 999 n\\Player\\t\\1";
    handlePrintLine(invalidInput);

    for (int i = 0; i < sv_maxclients->integer; i++) {
        client_t *client = getPlayerByNumber(i);
        mu_assert_int_eq("Client state should remain free", client->state, CS_FREE);
        mu_assert_int_eq("Client team ID should remain invalid", client->teamId, -1);
    }
}

void testHandleUserinfoChangedWithPartiallyCorrectInput(void) {
    clearState();
    char *invalidInput = "ClientUserinfoChanged: 1 t\\1";
    handlePrintLine(invalidInput);

    client_t *client = getPlayerByNumber(1);
    mu_assert_int_eq("Client state should remain free", client->state, CS_FREE);
    mu_assert_int_eq("Client team ID should remain invalid", client->teamId, -1);
}

void testHandleUserinfoChangedWithMalformedInput(void) {
    clearState();
    char *invalidInput = "ClientUserinfoChanged: This is a malformed input string";
    handlePrintLine(invalidInput);

    for (int i = 0; i < sv_maxclients->integer; i++) {
        client_t *client = getPlayerByNumber(i);
        mu_assert_int_eq("Client state should remain free", client->state, CS_FREE);
        mu_assert_int_eq("Client team ID should remain invalid", client->teamId, -1);
    }
}

void testHandleKillWithMissingKillerID(void) {
    clearState();
    char *invalidKillInput = "Kill: 18 19: Player killed Player by UT_MOD_LR300";
    handlePrintLine(invalidKillInput);

    for (int i = 0; i < sv_maxclients->integer; i++) {
        client_t *client = getPlayerByNumber(i);
        mu_assert_int_eq("Client state should remain free", client->state, CS_FREE);
        mu_assert_int_eq("Client team ID should remain invalid", client->teamId, -1);
    }
}

void testHandleKillWithInvalidKillerID(void) {
    clearState();
    char *invalidKillInput = "Kill: -1 18 19: Player killed Player by UT_MOD_LR300";
    handlePrintLine(invalidKillInput);

    for (int i = 0; i < sv_maxclients->integer; i++) {
        client_t *client = getPlayerByNumber(i);
        mu_assert_int_eq("Client state should remain free", client->state, CS_FREE);
        mu_assert_int_eq("Client team ID should remain invalid", client->teamId, -1);
    }
}

void testHandleKillWithMissingVictimID(void) {
    clearState();
    char *invalidKillInput = "Kill: 12 19: Player killed Player by UT_MOD_LR300";
    handlePrintLine(invalidKillInput);

    for (int i = 0; i < sv_maxclients->integer; i++) {
        client_t *client = getPlayerByNumber(i);
        mu_assert_int_eq("Client state should remain free", client->state, CS_FREE);
        mu_assert_int_eq("Client team ID should remain invalid", client->teamId, -1);
    }
}

void testHandleKillWithInvalidVictimID(void) {
    clearState();
    char *invalidKillInput = "Kill: 12 -1 19: Player killed Player by UT_MOD_LR300";
    handlePrintLine(invalidKillInput);

    for (int i = 0; i < sv_maxclients->integer; i++) {
        client_t *client = getPlayerByNumber(i);
        mu_assert_int_eq("Client state should remain free", client->state, CS_FREE);
        mu_assert_int_eq("Client team ID should remain invalid", client->teamId, -1);
    }
}

void testHandleKillWithMissingDeathCause(void) {
    clearState();
    char *invalidKillInput = "Kill: 12 18: Player killed Player";
    handlePrintLine(invalidKillInput);

    for (int i = 0; i < sv_maxclients->integer; i++) {
        client_t *client = getPlayerByNumber(i);
        mu_assert_int_eq("Client state should remain free", client->state, CS_FREE);
        mu_assert_int_eq("Client team ID should remain invalid", client->teamId, -1);
    }
}

void testHandleKillWithMissingColon(void) {
    clearState();
    char *invalidKillInput = "Kill 12 18 19: Player killed Player by UT_MOD_LR300";
    handlePrintLine(invalidKillInput);

    for (int i = 0; i < sv_maxclients->integer; i++) {
        client_t *client = getPlayerByNumber(i);
        mu_assert_int_eq("Client state should remain free", client->state, CS_FREE);
        mu_assert_int_eq("Client team ID should remain invalid", client->teamId, -1);
    }
}

void testHandleKillWithEmptyKillInfo(void) {
    clearState();
    char *invalidKillInput = "Kill:";
    handlePrintLine(invalidKillInput);

    for (int i = 0; i < sv_maxclients->integer; i++) {
        client_t *client = getPlayerByNumber(i);
        mu_assert_int_eq("Client state should remain free", client->state, CS_FREE);
        mu_assert_int_eq("Client team ID should remain invalid", client->teamId, -1);
    }
}

void testTeamBalanceWithEqualTeams(void) {
    clearState();
    populateTeams(5, 5);

    balanceTeams();

    assertTeamBalance(5, 5, 10);
}

void testTeamBalanceWithUnequalTeams(void) {
    clearState();
    populateTeams(7, 3);

    balanceTeams();

    assertTeamBalance(5, 5, 10);
}

void testTeamBalanceWithSinglePlayer(void) {
    clearState();
    populateTeams(1, 0);

    balanceTeams();

    assertTeamBalance(1, 0, 1);
}

void testTeamBalanceWhenAlreadyBalanced(void) {
    clearState();
    populateTeams(4, 4);

    balanceTeams();

    assertTeamBalance(4, 4, 8);
}

void testTeamBalanceWithMixedValidAndInvalidTeams(void) {
    clearState();
    connectPlayerId("1");
    svs.clients[1].teamId = 0;
    connectPlayerId("2");
    svs.clients[2].teamId = 9999;

    balanceTeams();

    mu_assert_int_eq("Valid team player should remain in team", svs.clients[1].teamId, 0);
    mu_assert_int_eq("Invalid team player should remain unchanged", svs.clients[2].teamId, 9999);
}

void testTeamBalanceWithSpectators(void) {
    clearState();
    connectPlayerId("1");
    svs.clients[1].teamId = 3;
    connectPlayerId("2");
    svs.clients[2].teamId = 1;

    balanceTeams();

    mu_assert_int_eq("Spectator should not be moved", svs.clients[1].teamId, 3);
    mu_assert_int_eq("Player in team should remain in team", svs.clients[2].teamId, 1);
}

void testTeamBalanceWithEdgeCaseTeams(void) {
    clearState();
    populateTeams(MAX_CLIENTS / 2, MAX_CLIENTS / 2);

    balanceTeams();

    assertTeamBalance(MAX_CLIENTS / 2,
                      MAX_CLIENTS / 2,
                      MAX_CLIENTS);
}

void testTeamBalanceWithMorePlayersInOneTeam(void) {
    clearState();
    populateTeams(MAX_CLIENTS - 1, 1);

    balanceTeams();

    assertTeamBalance(MAX_CLIENTS / 2,
                      MAX_CLIENTS / 2,
                      MAX_CLIENTS);
}

void testTeamBalanceWithBalancedTeams(void) {
    clearState();
    populateTeams(1, 2);

    balanceTeams();

    assertTeamBalance(1, 2, MAX_CLIENTS);
}

void testTeamBalanceWithNoPlayers(void) {
    clearState();

    balanceTeams();

    assertTeamBalance(0, 0, 0);
}

void testTeamBalanceByKillsAndDeaths(void) {
    clearState();
    // Team 1 has 5 players with 10 kills and 5 deaths each, Team 2 has 3 players with 5 kills and 10 deaths each
    populateTeamsWithScores(5, 3, 10, 5, 5, 10);

    balanceTeams();

    assertTeamBalanceByScores(4, 4, 8,
                              10, 0);
}

void testTeamBalanceWithMixedScores(void) {
    clearState();
    // Team 1: 4 players (20 kills, 10 deaths), Team 2: 6 players (10 kills, 5 deaths)
    populateTeamsWithScores(4, 6, 20, 10, 10, 5);

    balanceTeams();

    assertTeamBalanceByScores(5, 5, 10,
                              40, 30);
}

void testTeamBalanceWithExtremeScores(void) {
    clearState();
    // Team 1: 1 player (100 kills, 0 deaths), Team 2: 9 players (0 kills, 100 deaths)
    populateTeamsWithScores(1, 9, 100, 0, 0, 100);

    balanceTeams();

    assertTeamBalanceByScores(5, 5, 10,
                              -500, -300);
}

void testTeamBalanceWithBalancedScores(void) {
    clearState();
    // Both teams have 5 players, each with 10 kills and 10 deaths
    populateTeamsWithScores(5, 5, 10, 10, 10, 10);

    balanceTeams();

    assertTeamBalanceByScores(5, 5, 10,
                              0, 0);
}

void testTeamBalanceWithNoPlayersAndScores(void) {
    clearState();

    balanceTeams();

    assertTeamBalanceByScores(0, 0, 0,
                              0, 0);
}

void testTeamBalanceWithOddNumberOfPlayers(void) {
    clearState();
    // Team 1: 5 players (10 kills, 5 deaths), Team 2: 4 players (5 kills, 10 deaths)
    populateTeamsWithScores(5, 4, 10, 5, 5, 10);

    balanceTeams();

    assertTeamBalanceByScores(5, 4, 9,
                              15, -10);
}

void testHandleItemFlagPickup(void) {
    clearState();
    const char *playerId = "5";
    connectPlayerId(playerId);

    char userinfo[MAX_INFO_VALUE];
    snprintf(userinfo, sizeof(userinfo), "ClientUserinfoChanged: %s n\\Player5\\t\\1", playerId);
    handlePrintLine(userinfo);

    char itemInfo[MAX_INFO_VALUE];
    snprintf(itemInfo, sizeof(itemInfo), "Item: %s flag", playerId);
    handlePrintLine(itemInfo);

    client_t *client = getPlayerByNumber(atoi(playerId));
    mu_assert("Client should not be NULL", client != NULL);
    mu_assert_int_eq("Flag pickups should be incremented", client->flagPickups, 1);

    disconnectPlayerId(playerId);
}

void testHandleItemBombPickup(void) {
    clearState();
    const char *playerId = "6";
    connectPlayerId(playerId);

    char userinfo[MAX_INFO_VALUE];
    snprintf(userinfo, sizeof(userinfo), "ClientUserinfoChanged: %s n\\Player6\\t\\2", playerId);
    handlePrintLine(userinfo);

    char itemInfo[MAX_INFO_VALUE];
    snprintf(itemInfo, sizeof(itemInfo), "Item: %s bomb", playerId);
    handlePrintLine(itemInfo);

    client_t *client = getPlayerByNumber(atoi(playerId));
    mu_assert("Client should not be NULL", client != NULL);
    mu_assert_int_eq("Bomb pickups should be incremented", client->bombPickups, 1);

    disconnectPlayerId(playerId);
}

void testHandleItemInvalidItem(void) {
    clearState();
    const char *playerId = "7";
    connectPlayerId(playerId);

    char userinfo[MAX_INFO_VALUE];
    snprintf(userinfo, sizeof(userinfo), "ClientUserinfoChanged: %s n\\Player7\\t\\1", playerId);
    handlePrintLine(userinfo);

    char itemInfo[MAX_INFO_VALUE];
    snprintf(itemInfo, sizeof(itemInfo), "Item: %s health", playerId);
    handlePrintLine(itemInfo);

    client_t *client = getPlayerByNumber(atoi(playerId));
    mu_assert("Client should not be NULL", client != NULL);
    mu_assert_int_eq("Flag pickups should remain 0", client->flagPickups, 0);
    mu_assert_int_eq("Bomb pickups should remain 0", client->bombPickups, 0);

    disconnectPlayerId(playerId);
}

void testHandleItemMissingPlayerID(void) {
    clearState();
    char itemInfo[MAX_INFO_VALUE];
    snprintf(itemInfo, sizeof(itemInfo), "Item: flag");
    handlePrintLine(itemInfo);

    for (int i = 0; i < sv_maxclients->integer; i++) {
        client_t *client = getPlayerByNumber(i);
        mu_assert_int_eq("Client state should remain free", client->state, CS_FREE);
        mu_assert_int_eq("Client flag pickups should remain 0", client->flagPickups, 0);
        mu_assert_int_eq("Client bomb pickups should remain 0", client->bombPickups, 0);
    }
}

void testHandleItemInvalidPlayerID(void) {
    clearState();
    char itemInfo[MAX_INFO_VALUE];
    snprintf(itemInfo, sizeof(itemInfo), "Item: -1 flag");
    handlePrintLine(itemInfo);

    for (int i = 0; i < sv_maxclients->integer; i++) {
        client_t *client = getPlayerByNumber(i);
        mu_assert_int_eq("Client state should remain free", client->state, CS_FREE);
        mu_assert_int_eq("Client flag pickups should remain 0", client->flagPickups, 0);
        mu_assert_int_eq("Client bomb pickups should remain 0", client->bombPickups, 0);
    }
}

void testHandleFlagDropped(void) {
    clearState();
    const char *playerId = "8";
    connectPlayerId(playerId);

    char userinfo[MAX_INFO_VALUE];
    snprintf(userinfo, sizeof(userinfo), "ClientUserinfoChanged: %s n\\Player8\\t\\1", playerId);
    handlePrintLine(userinfo);

    char flagInfo[MAX_INFO_VALUE];
    snprintf(flagInfo, sizeof(flagInfo), "Flag: %s 0: Dropped flag", playerId);
    handlePrintLine(flagInfo);

    client_t *client = getPlayerByNumber(atoi(playerId));
    mu_assert("Client should not be NULL", client != NULL);
    mu_assert_int_eq("Flag dropped should be incremented", client->flagDropped, 1);

    disconnectPlayerId(playerId);
}

void testHandleFlagReturned(void) {
    clearState();
    const char *playerId = "9";
    connectPlayerId(playerId);

    char userinfo[MAX_INFO_VALUE];
    snprintf(userinfo, sizeof(userinfo), "ClientUserinfoChanged: %s n\\Player9\\t\\2", playerId);
    handlePrintLine(userinfo);

    char flagInfo[MAX_INFO_VALUE];
    snprintf(flagInfo, sizeof(flagInfo), "Flag: %s 1: Returned flag", playerId);
    handlePrintLine(flagInfo);

    client_t *client = getPlayerByNumber(atoi(playerId));
    mu_assert("Client should not be NULL", client != NULL);
    mu_assert_int_eq("Flag returned should be incremented", client->flagReturned, 1);

    disconnectPlayerId(playerId);
}

void testHandleFlagCaptured(void) {
    clearState();
    const char *playerId = "10";
    connectPlayerId(playerId);

    char userinfo[MAX_INFO_VALUE];
    snprintf(userinfo, sizeof(userinfo), "ClientUserinfoChanged: %s n\\Player10\\t\\1", playerId);
    handlePrintLine(userinfo);

    char flagInfo[MAX_INFO_VALUE];
    snprintf(flagInfo, sizeof(flagInfo), "Flag: %s 2: Captured flag", playerId);
    handlePrintLine(flagInfo);

    client_t *client = getPlayerByNumber(atoi(playerId));
    mu_assert("Client should not be NULL", client != NULL);
    mu_assert_int_eq("Flag captured should be incremented", client->flagCaptured, 1);

    disconnectPlayerId(playerId);
}

void testHandleFlagInvalidSubtype(void) {
    clearState();
    const char *playerId = "11";
    connectPlayerId(playerId);

    char userinfo[MAX_INFO_VALUE];
    snprintf(userinfo, sizeof(userinfo), "ClientUserinfoChanged: %s n\\Player11\\t\\2", playerId);
    handlePrintLine(userinfo);

    char flagInfo[MAX_INFO_VALUE];
    snprintf(flagInfo, sizeof(flagInfo), "Flag: %s 3: Invalid subtype", playerId);
    handlePrintLine(flagInfo);

    client_t *client = getPlayerByNumber(atoi(playerId));
    mu_assert("Client should not be NULL", client != NULL);
    mu_assert_int_eq("Flag dropped should remain 0", client->flagDropped, 0);
    mu_assert_int_eq("Flag returned should remain 0", client->flagReturned, 0);
    mu_assert_int_eq("Flag captured should remain 0", client->flagCaptured, 0);

    disconnectPlayerId(playerId);
}

void testHandleFlagMissingPlayerID(void) {
    clearState();
    char flagInfo[MAX_INFO_VALUE];
    snprintf(flagInfo, sizeof(flagInfo), "Flag: 0: Dropped flag");
    handlePrintLine(flagInfo);

    for (int i = 0; i < sv_maxclients->integer; i++) {
        client_t *client = getPlayerByNumber(i);
        mu_assert_int_eq("Client state should remain free", client->state, CS_FREE);
        mu_assert_int_eq("Client flag dropped should remain 0", client->flagDropped, 0);
        mu_assert_int_eq("Client flag returned should remain 0", client->flagReturned, 0);
        mu_assert_int_eq("Client flag captured should remain 0", client->flagCaptured, 0);
    }
}

void testHandleFlagInvalidPlayerID(void) {
    clearState();
    char flagInfo[MAX_INFO_VALUE];
    snprintf(flagInfo, sizeof(flagInfo), "Flag: -1 0: Dropped flag");
    handlePrintLine(flagInfo);

    for (int i = 0; i < sv_maxclients->integer; i++) {
        client_t *client = getPlayerByNumber(i);
        mu_assert_int_eq("Client state should remain free", client->state, CS_FREE);
        mu_assert_int_eq("Client flag dropped should remain 0", client->flagDropped, 0);
        mu_assert_int_eq("Client flag returned should remain 0", client->flagReturned, 0);
        mu_assert_int_eq("Client flag captured should remain 0", client->flagCaptured, 0);
    }
}

void testHandleFlagCaptureTimeWithValidInput(void) {
    clearState();
    const char *playerId = "10";
    connectPlayerId(playerId);

    char userinfo[MAX_INFO_VALUE];
    snprintf(userinfo, sizeof(userinfo), "ClientUserinfoChanged: %s n\\Player10\\t\\1", playerId);
    handlePrintLine(userinfo);

    char captureTimeInfo[MAX_INFO_VALUE];
    snprintf(captureTimeInfo, sizeof(captureTimeInfo), "FlagCaptureTime: %s: 15000", playerId);
    handlePrintLine(captureTimeInfo);

    client_t *client = getPlayerByNumber(atoi(playerId));
    mu_assert("Client should not be NULL", client != NULL);
    mu_assert_int_eq("Min capture time should be set", client->minCapTime, 15);
    mu_assert_int_eq("Max capture time should be set", client->maxCapTime, 15);

    disconnectPlayerId(playerId);
}

void testHandleFlagCaptureTimeWithImprovedCaptureTime(void) {
    clearState();
    const char *playerId = "11";
    connectPlayerId(playerId);

    char userinfo[MAX_INFO_VALUE];
    snprintf(userinfo, sizeof(userinfo), "ClientUserinfoChanged: %s n\\Player11\\t\\1", playerId);
    handlePrintLine(userinfo);

    char captureTimeInfoInitial[MAX_INFO_VALUE];
    snprintf(captureTimeInfoInitial, sizeof(captureTimeInfoInitial), "FlagCaptureTime: %s: 20000",
             playerId);
    handlePrintLine(captureTimeInfoInitial);

    char captureTimeInfoImproved[MAX_INFO_VALUE];
    snprintf(captureTimeInfoImproved, sizeof(captureTimeInfoImproved), "FlagCaptureTime: %s: 10000",
             playerId);
    handlePrintLine(captureTimeInfoImproved);

    client_t *client = getPlayerByNumber(atoi(playerId));
    mu_assert("Client should not be NULL", client != NULL);
    mu_assert_int_eq("Min capture time should be updated", client->minCapTime, 10);
    mu_assert_int_eq("Max capture time should remain the same", client->maxCapTime, 20);

    disconnectPlayerId(playerId);
}

void testHandleFlagCaptureTimeWithWorseCaptureTime(void) {
    clearState();
    const char *playerId = "12";
    connectPlayerId(playerId);

    char userinfo[MAX_INFO_VALUE];
    snprintf(userinfo, sizeof(userinfo), "ClientUserinfoChanged: %s n\\Player12\\t\\1", playerId);
    handlePrintLine(userinfo);

    char captureTimeInfoInitial[MAX_INFO_VALUE];
    snprintf(captureTimeInfoInitial, sizeof(captureTimeInfoInitial), "FlagCaptureTime: %s: 5000",
             playerId);
    handlePrintLine(captureTimeInfoInitial);

    char captureTimeInfoWorse[MAX_INFO_VALUE];
    snprintf(captureTimeInfoWorse, sizeof(captureTimeInfoWorse), "FlagCaptureTime: %s: 30000", playerId);
    handlePrintLine(captureTimeInfoWorse);

    client_t *client = getPlayerByNumber(atoi(playerId));
    mu_assert("Client should not be NULL", client != NULL);
    mu_assert_int_eq("Min capture time should remain the same", client->minCapTime, 5);
    mu_assert_int_eq("Max capture time should be updated", client->maxCapTime, 30);

    disconnectPlayerId(playerId);
}

void testHandleFlagCaptureTimeWithInvalidPlayerID(void) {
    clearState();
    char captureTimeInfo[MAX_INFO_VALUE];
    snprintf(captureTimeInfo, sizeof(captureTimeInfo), "FlagCaptureTime: -1: 15000");
    handlePrintLine(captureTimeInfo);

    for (int i = 0; i < sv_maxclients->integer; i++) {
        client_t *client = getPlayerByNumber(i);
        mu_assert_int_eq("Client min capture time should remain 0", client->minCapTime, 0);
        mu_assert_int_eq("Client max capture time should remain 0", client->maxCapTime, 0);
    }
}

void testHandleFlagCaptureTimeWithMissingPlayerID(void) {
    clearState();
    char captureTimeInfo[MAX_INFO_VALUE];
    snprintf(captureTimeInfo, sizeof(captureTimeInfo), "FlagCaptureTime: : 15000");
    handlePrintLine(captureTimeInfo);

    for (int i = 0; i < sv_maxclients->integer; i++) {
        client_t *client = getPlayerByNumber(i);
        mu_assert_int_eq("Client min capture time should remain 0", client->minCapTime, 0);
        mu_assert_int_eq("Client max capture time should remain 0", client->maxCapTime, 0);
    }
}

void testHandleFlagCaptureTimeWithInvalidCaptureTime(void) {
    clearState();
    const char *playerId = "13";
    connectPlayerId(playerId);

    char userinfo[MAX_INFO_VALUE];
    snprintf(userinfo, sizeof(userinfo), "ClientUserinfoChanged: %s n\\Player13\\t\\1", playerId);
    handlePrintLine(userinfo);

    char captureTimeInfo[MAX_INFO_VALUE];
    snprintf(captureTimeInfo, sizeof(captureTimeInfo), "FlagCaptureTime: %s: invalid_time",
             playerId);
    handlePrintLine(captureTimeInfo);

    client_t *client = getPlayerByNumber(atoi(playerId));
    mu_assert("Client should not be NULL", client != NULL);
    mu_assert_int_eq("Min capture time should remain 0", client->minCapTime, 0);
    mu_assert_int_eq("Max capture time should remain 0", client->maxCapTime, 0);

    disconnectPlayerId(playerId);
}

void testHandleFlagCaptureTimeWithZeroCaptureTime(void) {
    clearState();
    const char *playerId = "14";
    connectPlayerId(playerId);

    char userinfo[MAX_INFO_VALUE];
    snprintf(userinfo, sizeof(userinfo), "ClientUserinfoChanged: %s n\\Player14\\t\\1", playerId);
    handlePrintLine(userinfo);

    char captureTimeInfo[MAX_INFO_VALUE];
    snprintf(captureTimeInfo, sizeof(captureTimeInfo), "FlagCaptureTime: %s: 0", playerId);
    handlePrintLine(captureTimeInfo);

    client_t *client = getPlayerByNumber(atoi(playerId));
    mu_assert("Client should not be NULL", client != NULL);
    mu_assert_int_eq("Min capture time should remain 0", client->minCapTime, 0);
    mu_assert_int_eq("Max capture time should remain 0", client->maxCapTime, 0);

    disconnectPlayerId(playerId);
}

void testHandleAssistWithValidInput(void) {
    clearState();
    const char *playerId = "10";
    const char *victimId = "11";
    const char *attackerId = "12";
    connectPlayerId(playerId);
    connectPlayerId(victimId);
    connectPlayerId(attackerId);

    char userinfo[MAX_INFO_VALUE];
    snprintf(userinfo, sizeof(userinfo), "ClientUserinfoChanged: %s n\\Player10\\t\\1", playerId);
    handlePrintLine(userinfo);

    snprintf(userinfo, sizeof(userinfo), "ClientUserinfoChanged: %s n\\Player11\\t\\2", victimId);
    handlePrintLine(userinfo);

    snprintf(userinfo, sizeof(userinfo), "ClientUserinfoChanged: %s n\\Player12\\t\\1", attackerId);
    handlePrintLine(userinfo);

    char assistInfo[MAX_INFO_VALUE];
    snprintf(assistInfo, sizeof(assistInfo), "Assist: %s %s %s: Player10 assisted Player12 to kill Player11", playerId,
             victimId, attackerId);
    handlePrintLine(assistInfo);

    client_t *client = getPlayerByNumber(atoi(playerId));
    mu_assert("Client should not be NULL", client != NULL);
    mu_assert_int_eq("Assists should be incremented", client->assists, 1);

    disconnectPlayerId(playerId);
    disconnectPlayerId(victimId);
    disconnectPlayerId(attackerId);
}

void testHandleAssistWithInvalidPlayerID(void) {
    clearState();
    char assistInfo[MAX_INFO_VALUE];
    snprintf(assistInfo, sizeof(assistInfo), "Assist: -1 11 12: Player10 assisted Player12 to kill Player11");
    handlePrintLine(assistInfo);

    for (int i = 0; i < sv_maxclients->integer; i++) {
        client_t *client = getPlayerByNumber(i);
        mu_assert_int_eq("Assists should remain 0", client->assists, 0);
    }
}

void testHandleAssistWithMissingPlayerID(void) {
    clearState();
    char assistInfo[MAX_INFO_VALUE];
    snprintf(assistInfo, sizeof(assistInfo), "Assist: 11 12: Player10 assisted Player12 to kill Player11");
    handlePrintLine(assistInfo);

    for (int i = 0; i < sv_maxclients->integer; i++) {
        client_t *client = getPlayerByNumber(i);
        mu_assert_int_eq("Assists should remain 0", client->assists, 0);
    }
}

void testHandleAssistWithInvalidVictimID(void) {
    clearState();
    const char *playerId = "10";
    const char *attackerId = "12";
    connectPlayerId(playerId);
    connectPlayerId(attackerId);

    char userinfo[MAX_INFO_VALUE];
    snprintf(userinfo, sizeof(userinfo), "ClientUserinfoChanged: %s n\\Player10\\t\\1", playerId);
    handlePrintLine(userinfo);

    snprintf(userinfo, sizeof(userinfo), "ClientUserinfoChanged: %s n\\Player12\\t\\1", attackerId);
    handlePrintLine(userinfo);

    char assistInfo[MAX_INFO_VALUE];
    snprintf(assistInfo, sizeof(assistInfo), "Assist: %s -1 %s: Player10 assisted Player12 to kill invalid player",
             playerId, attackerId);
    handlePrintLine(assistInfo);

    client_t *client = getPlayerByNumber(atoi(playerId));
    mu_assert("Client should not be NULL", client != NULL);
    mu_assert_int_eq("Assists should remain 0", client->assists, 0);

    disconnectPlayerId(playerId);
    disconnectPlayerId(attackerId);
}

void testHandleAssistWithMissingVictimID(void) {
    clearState();
    const char *playerId = "10";
    const char *attackerId = "12";
    connectPlayerId(playerId);
    connectPlayerId(attackerId);

    char userinfo[MAX_INFO_VALUE];
    snprintf(userinfo, sizeof(userinfo), "ClientUserinfoChanged: %s n\\Player10\\t\\1", playerId);
    handlePrintLine(userinfo);

    snprintf(userinfo, sizeof(userinfo), "ClientUserinfoChanged: %s n\\Player12\\t\\1", attackerId);
    handlePrintLine(userinfo);

    char assistInfo[MAX_INFO_VALUE];
    snprintf(assistInfo, sizeof(assistInfo), "Assist: %s %s: Player10 assisted Player12 to kill missing player",
             playerId, attackerId);
    handlePrintLine(assistInfo);

    client_t *client = getPlayerByNumber(atoi(playerId));
    mu_assert("Client should not be NULL", client != NULL);
    mu_assert_int_eq("Assists should remain 0", client->assists, 0);

    disconnectPlayerId(playerId);
    disconnectPlayerId(attackerId);
}

void testHandleAssistWithInvalidAttackerID(void) {
    clearState();
    const char *playerId = "10";
    const char *victimId = "11";
    connectPlayerId(playerId);
    connectPlayerId(victimId);

    char userinfo[MAX_INFO_VALUE];
    snprintf(userinfo, sizeof(userinfo), "ClientUserinfoChanged: %s n\\Player10\\t\\1", playerId);
    handlePrintLine(userinfo);

    snprintf(userinfo, sizeof(userinfo), "ClientUserinfoChanged: %s n\\Player11\\t\\2", victimId);
    handlePrintLine(userinfo);

    char assistInfo[MAX_INFO_VALUE];
    snprintf(assistInfo, sizeof(assistInfo), "Assist: %s %s -1: Player10 assisted invalid player to kill Player11",
             playerId, victimId);
    handlePrintLine(assistInfo);

    client_t *client = getPlayerByNumber(atoi(playerId));
    mu_assert("Client should not be NULL", client != NULL);
    mu_assert_int_eq("Assists should remain 0", client->assists, 0);

    disconnectPlayerId(playerId);
    disconnectPlayerId(victimId);
}

void testHandleAssistWithMissingAttackerID(void) {
    clearState();
    const char *playerId = "10";
    const char *victimId = "11";
    connectPlayerId(playerId);
    connectPlayerId(victimId);

    char userinfo[MAX_INFO_VALUE];
    snprintf(userinfo, sizeof(userinfo), "ClientUserinfoChanged: %s n\\Player10\\t\\1", playerId);
    handlePrintLine(userinfo);

    snprintf(userinfo, sizeof(userinfo), "ClientUserinfoChanged: %s n\\Player11\\t\\2", victimId);
    handlePrintLine(userinfo);

    char assistInfo[MAX_INFO_VALUE];
    snprintf(assistInfo, sizeof(assistInfo), "Assist: %s %s: Player10 assisted missing player to kill Player11",
             playerId, victimId);
    handlePrintLine(assistInfo);

    client_t *client = getPlayerByNumber(atoi(playerId));
    mu_assert("Client should not be NULL", client != NULL);
    mu_assert_int_eq("Assists should remain 0", client->assists, 0);

    disconnectPlayerId(playerId);
    disconnectPlayerId(victimId);
}

void testHandleUserinfoChangedWithInvalidFormat(void) {
    clearState();
    char *invalidInput = "ClientUserinfoChanged: invalid\\format\\string";
    handlePrintLine(invalidInput);

    for (int i = 0; i < sv_maxclients->integer; i++) {
        client_t *client = getPlayerByNumber(i);
        mu_assert_int_eq("Client state should remain free with invalid format", client->state, CS_FREE);
        mu_assert_int_eq("Client team ID should remain invalid with invalid format", client->teamId, -1);
    }
}

void testHandleAssistComprehensive(void) {
    clearState();
    const char *playerId = "10";
    const char *victimId = "11";
    const char *attackerId = "12";
    connectPlayerId(playerId);
    connectPlayerId(victimId);
    connectPlayerId(attackerId);

    char userinfo[MAX_INFO_VALUE];
    snprintf(userinfo, sizeof(userinfo), "ClientUserinfoChanged: %s n\\Player10\\t\\1", playerId);
    handlePrintLine(userinfo);

    snprintf(userinfo, sizeof(userinfo), "ClientUserinfoChanged: %s n\\Player11\\t\\2", victimId);
    handlePrintLine(userinfo);

    snprintf(userinfo, sizeof(userinfo), "ClientUserinfoChanged: %s n\\Player12\\t\\1", attackerId);
    handlePrintLine(userinfo);

    char assistInfo[MAX_INFO_VALUE];
    snprintf(assistInfo, sizeof(assistInfo), "Assist: %s %s %s: Player10 assisted Player12 to kill Player11",
             playerId, victimId, attackerId);
    handlePrintLine(assistInfo);

    client_t *client = getPlayerByNumber(atoi(playerId));
    client_t *attacker = getPlayerByNumber(atoi(attackerId));
    client_t *victim = getPlayerByNumber(atoi(victimId));

    mu_assert("Client should not be NULL", client != NULL);
    mu_assert_int_eq("Assists should be incremented", client->assists, 1);
    mu_assert_int_eq("Attacker kills should remain unchanged", attacker->kills, 0);
    mu_assert_int_eq("Victim deaths should remain unchanged", victim->deaths, 0);

    disconnectPlayerId(playerId);
    disconnectPlayerId(victimId);
    disconnectPlayerId(attackerId);
}

void testHandleKillWithInvalidPlayerNumber(void) {
    clearState();
    char *invalidKillInput = "Kill: -1 -1 19: Invalid killer killed Invalid victim";
    handlePrintLine(invalidKillInput);

    for (int i = 0; i < sv_maxclients->integer; i++) {
        client_t *client = getPlayerByNumber(i);
        mu_assert_int_eq("Client state should remain free", client->state, CS_FREE);
        mu_assert_int_eq("Client team ID should remain invalid", client->teamId, -1);
    }
}

void testPlayerStatsResetOnInitGame(void) {
    clearState();
    const char *playerId = "3";
    connectPlayerId(playerId);

    char userinfo[MAX_INFO_VALUE];
    snprintf(userinfo, sizeof(userinfo), "ClientUserinfoChanged: %s n\\Player3\\t\\1", playerId);
    handlePrintLine(userinfo);

    client_t *client = getPlayerByNumber(atoi(playerId));
    client->kills = 5;
    client->deaths = 3;
    client->assists = 2;

    handlePrintLine("InitGame: mapname\\testmap\\g_gametype\\3");

    mu_assert_int_eq("Kills should be reset on InitGame", client->kills, 0);
    mu_assert_int_eq("Deaths should be reset on InitGame", client->deaths, 0);
    mu_assert_int_eq("Assists should be reset on InitGame", client->assists, 0);

    disconnectPlayerId(playerId);
}

void testHandleMultiplePlayersInit(void) {
    clearState();
    const char *playerId1 = "4";
    const char *playerId2 = "5";

    connectPlayerId(playerId1);
    connectPlayerId(playerId2);

    char userinfo1[MAX_INFO_VALUE];
    snprintf(userinfo1, sizeof(userinfo1), "ClientUserinfoChanged: %s n\\Player4\\t\\1", playerId1);
    handlePrintLine(userinfo1);

    char userinfo2[MAX_INFO_VALUE];
    snprintf(userinfo2, sizeof(userinfo2), "ClientUserinfoChanged: %s n\\Player5\\t\\2", playerId2);
    handlePrintLine(userinfo2);

    client_t *client1 = getPlayerByNumber(atoi(playerId1));
    client_t *client2 = getPlayerByNumber(atoi(playerId2));

    mu_assert_int_eq("Client1 state should be connected", client1->state, CS_CONNECTED);
    mu_assert_int_eq("Client2 state should be connected", client2->state, CS_CONNECTED);
    mu_assert_str_eq("Client1 name should be set correctly", Info_ValueForKey(client1->userinfo, "name"), "Player4");
    mu_assert_str_eq("Client2 name should be set correctly", Info_ValueForKey(client2->userinfo, "name"), "Player5");

    disconnectPlayerId(playerId1);
    disconnectPlayerId(playerId2);
}

void testPrint(void) {
    initializeServerState();

    mu_run_test(testRegisterBotWithNoLeadingSlashOk);
    mu_run_test(testRegisterUserWithLeadingSlashOk);
    mu_run_test(testHandleKillWithValidInput);
    mu_run_test(testHandleUserinfoChangedWithInvalidInput);
    mu_run_test(testHandleKillWithInvalidInput);
    mu_run_test(testHandleUserinfoChangedWithMissingPlayerID);
    mu_run_test(testHandleUserinfoChangedWithInvalidPlayerNumber);
    mu_run_test(testHandleUserinfoChangedWithMissingTeamID);
    mu_run_test(testHandleUserinfoChangedWithMissingPlayerName);
    mu_run_test(testHandleUserinfoChangedWithEmptyUserInfo);
    mu_run_test(testHandleUserinfoChangedWithInvalidTeamID);
    mu_run_test(testHandleUserinfoChangedWithPlayerIDOutOfRange);
    mu_run_test(testHandleUserinfoChangedWithPartiallyCorrectInput);
    mu_run_test(testHandleUserinfoChangedWithMalformedInput);
    mu_run_test(testHandleKillWithMissingKillerID);
    mu_run_test(testHandleKillWithInvalidKillerID);
    mu_run_test(testHandleKillWithMissingVictimID);
    mu_run_test(testHandleKillWithInvalidVictimID);
    mu_run_test(testHandleKillWithMissingDeathCause);
    mu_run_test(testHandleKillWithMissingColon);
    mu_run_test(testHandleKillWithEmptyKillInfo);
    mu_run_test(testTeamBalanceWithEqualTeams);
    mu_run_test(testTeamBalanceWithUnequalTeams);
    mu_run_test(testTeamBalanceWithSinglePlayer);
    mu_run_test(testTeamBalanceWhenAlreadyBalanced);
    mu_run_test(testTeamBalanceWithMixedValidAndInvalidTeams);
    mu_run_test(testTeamBalanceWithSpectators);
    mu_run_test(testTeamBalanceWithEdgeCaseTeams);
    mu_run_test(testTeamBalanceWithMorePlayersInOneTeam);
    mu_run_test(testTeamBalanceWithNoPlayers);
    mu_run_test(testTeamBalanceByKillsAndDeaths);
    mu_run_test(testTeamBalanceWithMixedScores);
    mu_run_test(testTeamBalanceWithExtremeScores);
    mu_run_test(testTeamBalanceWithBalancedScores);
    mu_run_test(testTeamBalanceWithNoPlayersAndScores);
    mu_run_test(testTeamBalanceWithOddNumberOfPlayers);
    mu_run_test(testHandleItemFlagPickup);
    mu_run_test(testHandleItemBombPickup);
    mu_run_test(testHandleItemInvalidItem);
    mu_run_test(testHandleItemMissingPlayerID);
    mu_run_test(testHandleItemInvalidPlayerID);
    mu_run_test(testHandleFlagDropped);
    mu_run_test(testHandleFlagReturned);
    mu_run_test(testHandleFlagCaptured);
    mu_run_test(testHandleFlagInvalidSubtype);
    mu_run_test(testHandleFlagMissingPlayerID);
    mu_run_test(testHandleFlagInvalidPlayerID);
    mu_run_test(testHandleFlagCaptureTimeWithValidInput);
    mu_run_test(testHandleFlagCaptureTimeWithImprovedCaptureTime);
    mu_run_test(testHandleFlagCaptureTimeWithWorseCaptureTime);
    mu_run_test(testHandleFlagCaptureTimeWithInvalidPlayerID);
    mu_run_test(testHandleFlagCaptureTimeWithMissingPlayerID);
    mu_run_test(testHandleFlagCaptureTimeWithInvalidCaptureTime);
    mu_run_test(testHandleFlagCaptureTimeWithZeroCaptureTime);
    mu_run_test(testHandleAssistWithValidInput);
    mu_run_test(testHandleAssistWithInvalidPlayerID);
    mu_run_test(testHandleAssistWithMissingPlayerID);
    mu_run_test(testHandleAssistWithInvalidVictimID);
    mu_run_test(testHandleAssistWithMissingVictimID);
    mu_run_test(testHandleAssistWithInvalidAttackerID);
    mu_run_test(testHandleAssistWithMissingAttackerID);
    mu_run_test(testHandleUserinfoChangedWithInvalidFormat);
    mu_run_test(testHandleAssistComprehensive);
    mu_run_test(testHandleKillWithInvalidPlayerNumber);
    mu_run_test(testPlayerStatsResetOnInitGame);
    mu_run_test(testHandleMultiplePlayersInit);

    finalizeServerState();
}

void populateTeams(int team1Size, int team2Size) {
    for (int i = 0; i < team1Size; i++) {
        client_t *client = &svs.clients[i];
        Info_SetValueForKey(client->userinfo, globalPlayerNameKey, va("Player%d", i));
        Info_SetValueForKey(client->userinfo, globalPlayerTeamNameKey, getTeamName(0));
        client->teamId = 0;
        client->state = CS_CONNECTED;
    }

    for (int i = team1Size; i < team1Size + team2Size; i++) {
        client_t *client = &svs.clients[i];
        Info_SetValueForKey(client->userinfo, globalPlayerNameKey, va("Player%d", i));
        Info_SetValueForKey(client->userinfo, globalPlayerTeamNameKey, getTeamName(1));
        client->teamId = 1;
        client->state = CS_CONNECTED;
    }
}

void assertTeamBalance(int expectedTeam1Size, int expectedTeam2Size, int expectedTotalPlayers) {
    int team1Size = 0;
    int team2Size = 0;

    for (int i = 0; i < sv_maxclients->integer; i++) {
        if (svs.clients[i].state >= CS_CONNECTED) {
            if (svs.clients[i].teamId == 0) {
                team1Size++;
            } else if (svs.clients[i].teamId == 1) {
                team2Size++;
            }
        }
    }

    mu_assert_int_eq("Team 1 size should match expected", team1Size, expectedTeam1Size);
    mu_assert_int_eq("Team 2 size should match expected", team2Size, expectedTeam2Size);
    mu_assert_int_eq("Total players should match expected", team1Size + team2Size, expectedTotalPlayers);
}

void populateTeamsWithScores(int team1Size, int team2Size, int team1Kills, int team1Deaths,
                             int team2Kills, int team2Deaths) {
    for (int i = 0; i < team1Size; i++) {
        client_t *client = &svs.clients[i];
        Info_SetValueForKey(client->userinfo, globalPlayerNameKey, va("Player%d", i));
        Info_SetValueForKey(client->userinfo, globalPlayerTeamNameKey, getTeamName(0));
        client->teamId = 0;
        client->state = CS_CONNECTED;
        client->kills = team1Kills;
        client->deaths = team1Deaths;
    }

    for (int i = team1Size; i < team1Size + team2Size; i++) {
        client_t *client = &svs.clients[i];
        Info_SetValueForKey(client->userinfo, globalPlayerNameKey, va("Player%d", i));
        Info_SetValueForKey(client->userinfo, globalPlayerTeamNameKey, getTeamName(1));
        client->teamId = 1;
        client->state = CS_CONNECTED;
        client->kills = team2Kills;
        client->deaths = team2Deaths;
    }
}

void assertTeamBalanceByScores(int expectedTeam1Size, int expectedTeam2Size, int expectedTotalPlayers,
                               int expectedTeam1Score, int expectedTeam2Score) {
    int team1Size = 0;
    int team2Size = 0;
    int team1Score = 0;
    int team2Score = 0;

    for (int i = 0; i < sv_maxclients->integer; i++) {
        if (svs.clients[i].state >= CS_CONNECTED) {
            if (svs.clients[i].teamId == 0) {
                team1Size++;
                team1Score += svs.clients[i].kills - svs.clients[i].deaths;
            } else if (svs.clients[i].teamId == 1) {
                team2Size++;
                team2Score += svs.clients[i].kills - svs.clients[i].deaths;
            }
        }
    }

    mu_assert_int_eq("Team 1 size should match expected", team1Size, expectedTeam1Size);
    mu_assert_int_eq("Team 2 size should match expected", team2Size, expectedTeam2Size);
    mu_assert_int_eq("Total players should match expected", team1Size + team2Size, expectedTotalPlayers);
    mu_assert_int_eq("Team 1 score should match expected", team1Score, expectedTeam1Score);
    mu_assert_int_eq("Team 2 score should match expected", team2Score, expectedTeam2Score);
}

void printPlayerDetails(void) {
    for (int i = 0; i < sv_maxclients->integer; i++) {
        printUserInfo(&svs.clients[i]);
    }
}

void clearState(void) {
    for (int i = 0; i < sv_maxclients->integer; i++) {
        resetPlayerDetails(&svs.clients[i]);
    }
}

void connectPlayerId(const char *playerNum) {
    int num = atoi(playerNum);
    if (num >= 0 && num < sv_maxclients->integer) {
        svs.clients[num].state = CS_CONNECTED;
        client_t *client = getPlayerByNumber(num);
        mu_assert_int_eq("Player state after connect", client->state, CS_CONNECTED);
    } else {
        Com_DPrintf("Error: Player number %s out of valid range (0-%d).\n", playerNum, sv_maxclients->integer - 1);
    }
}

void disconnectPlayerId(const char *playerNum) {
    int num = atoi(playerNum);
    if (num >= 0 && num < sv_maxclients->integer) {
        svs.clients[num].state = CS_FREE;
        client_t *client = getPlayerByNumber(num);
        mu_assert_int_eq("Player state after disconnect", client->state, CS_FREE);
    } else {
        Com_DPrintf("Error: Player number %s out of valid range (0-%d).\n", playerNum, sv_maxclients->integer - 1);
    }
}

void resetPlayerDetails(client_t *client) {
    if (client == NULL) {
        Com_DPrintf("Error: client is NULL\n");
        return;
    }

    Info_SetValueForKey(client->userinfo, "name", "");
    Info_SetValueForKey(client->userinfo, "team", "");

    client->state = CS_FREE;
    client->teamId = -1;

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
    Com_DPrintf("Reset player details for client\n");

    memset(&svs.gameSettings, 0, sizeof(svs.gameSettings));
    Com_DPrintf("Initialized gameSettings to zero.\n");
}

void initializeServerState(void) {
    sv_maxclients = &sv_maxclients_val;
    sv_scoreThreshold = &sv_scoreThreshold_val;
    sv_minBalanceInterval = &sv_minBalanceInterval_val;
    com_developer = &com_developer_val;

    svs.clients = malloc(sizeof(client_t) * sv_maxclients->integer);
    if (!svs.clients) {
        Com_DPrintf("Error: Failed to allocate memory for clients\n");
        exit(EXIT_FAILURE);
    }

    memset(svs.clients, 0, sizeof(client_t) * sv_maxclients->integer);
    for (int i = 0; i < sv_maxclients->integer; i++) {
        resetPlayerDetails(&svs.clients[i]);
    }
    svs.initialized = qtrue;
    svs.time = 0;
}

void finalizeServerState(void) {
    if (svs.clients) {
        free(svs.clients);
        svs.clients = NULL;
    }
    svs.initialized = qfalse;
}
