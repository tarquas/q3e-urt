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

void testHandleExitWithInvalidState(void) {
    clearState();
    char *invalidExitInput = "Exit: invalid_exit_format";
    handlePrintLine(invalidExitInput);

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

void testHandleExitWithValidInput(void) {
    const char *playerId1 = "1";
    const char *playerId2 = "2";

    connectPlayerId(playerId1);
    connectPlayerId(playerId2);

    char userinfo1[MAX_INFO_VALUE];
    snprintf(userinfo1, sizeof(userinfo1),
             "ClientUserinfoChanged: %s n\\Player1\\t\\1", playerId1);
    handlePrintLine(userinfo1);

    char userinfo2[MAX_INFO_VALUE];
    snprintf(userinfo2, sizeof(userinfo2),
             "ClientUserinfoChanged: %s n\\Player2\\t\\2", playerId2);
    handlePrintLine(userinfo2);

    char exit_info[MAX_INFO_VALUE];
    snprintf(exit_info, sizeof(exit_info), "Exit: %s", playerId1);
    handlePrintLine(exit_info);

    client_t *client1 = getPlayerByNumber(atoi(playerId1));
    client_t *client2 = getPlayerByNumber(atoi(playerId2));

    mu_assert("Client1 should not be NULL", client1 != NULL);
    mu_assert("Client2 should not be NULL", client2 != NULL);

    mu_assert("Client1 userinfo should not be NULL", client1->userinfo != NULL);
    mu_assert("Client2 userinfo should not be NULL", client2->userinfo != NULL);

    mu_assert_int_eq("Client1 state should be reset", client1->state, CS_FREE);
    mu_assert_int_eq("Client1 team ID should be reset", client1->teamId, -1);
    mu_assert_int_eq("Client1 kills should be reset", client1->kills, 0);
    mu_assert_int_eq("Client1 deaths should be reset", client1->deaths, 0);
    mu_assert_str_eq("Client1 name should be reset", Info_ValueForKey(client1->userinfo, "name"), "");

    mu_assert_int_eq("Client2 state should be reset", client2->state, CS_FREE);
    mu_assert_int_eq("Client2 team ID should be reset", client2->teamId, -1);
    mu_assert_int_eq("Client2 kills should be reset", client2->kills, 0);
    mu_assert_int_eq("Client2 deaths should be reset", client2->deaths, 0);
    mu_assert_str_eq("Client2 name should be reset", Info_ValueForKey(client2->userinfo, "name"), "");

    disconnectPlayerId(playerId1);
    disconnectPlayerId(playerId2);
}

void testHandleExitWithInvalidInput(void) {
    clearState();
    char *invalidExitInput = "Exit: invalid_exit_format";
    handlePrintLine(invalidExitInput);

    for (int i = 0; i < sv_maxclients->integer; i++) {
        client_t *client = getPlayerByNumber(i);
        mu_assert_int_eq("Client state should remain free", client->state, CS_FREE);
        mu_assert_int_eq("Client team ID should remain invalid", client->teamId, -1);
    }
}

void testHandleExitWithMissingPlayerID(void) {
    clearState();
    char *invalidExitInput = "Exit:";
    handlePrintLine(invalidExitInput);

    for (int i = 0; i < sv_maxclients->integer; i++) {
        client_t *client = getPlayerByNumber(i);
        mu_assert_int_eq("Client state should remain free", client->state, CS_FREE);
        mu_assert_int_eq("Client team ID should remain invalid", client->teamId, -1);
    }
}

void testHandleExitWithNonexistentPlayerID(void) {
    clearState();
    char *invalidExitInput = "Exit: 999";
    handlePrintLine(invalidExitInput);

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

void testPrint(void) {
    initializeServerState();

    mu_run_test(testRegisterBotWithNoLeadingSlashOk);
    mu_run_test(testRegisterUserWithLeadingSlashOk);
    mu_run_test(testHandleKillWithValidInput);
    mu_run_test(testHandleUserinfoChangedWithInvalidInput);
    mu_run_test(testHandleKillWithInvalidInput);
    mu_run_test(testHandleExitWithInvalidState);
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
    mu_run_test(testHandleExitWithValidInput);
    mu_run_test(testHandleExitWithInvalidInput);
    mu_run_test(testHandleExitWithMissingPlayerID);
    mu_run_test(testHandleExitWithNonexistentPlayerID);
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
