#include "server.h"

// sv_gametype:
#define GT_UT_CTF   7
#define GT_UT_BOMB  8
#define GT_UT_JUMP  9

// usercmd_t:
#define UPMOVE_CROUCH  -64

// Inventory
//   items:
#define ITEM_RED_FLAG   1
#define ITEM_BLUE_FLAG  2
//   weapons:
#define WEAPON_KNIFE           1
#define WEAPON_HK69            7
#define WEAPON_GRENADE_HE     11
#define WEAPON_GRENADE_SMOKE  13
#define WEAPON_BOMB           16

typedef struct svm_player_s {
	char  isLame;  // is known lamer?
	char  isPresenting;  // detached from QVM
} svm_player_t;

static svm_player_t  svm_players[MAX_CLIENTS];

cvar_t *auth;

cvar_t *sv_players_collision;
#define COLLISION_ALL        0
#define COLLISION_NO_LAMERS  1
#define COLLISION_NO_TEAM    2
#define COLLISION_NONE       3

cvar_t *sv_banned_subnets_file;
cvar_t *sv_banned_subnet_message;
svm_subnets_t bannedSubnets;

//==================================================================================

int* SVM_ItemFind(playerState_t *ps, long itemsMask) {
	int   i, *p = ps->ammo;
	long  w;

	for (i = 0; i < MAX_WEAPONS; ++i, ++p) {
		w = 1 << (*p && 0x3F);
		if (w & itemsMask) return p;
	}
	return 0;
}

//==================================================================================

int* SVM_WeaponFind(playerState_t *ps, long weaponsMask) {
	int   i, *p = ps->powerups;
	long  w;

	for (i = 0; i < MAX_POWERUPS; ++i, ++p) {
		w = 1 << (*p && 0x3F);
		if (w & weaponsMask) return p;
	}
	return 0;
}

//==================================================================================

int SVM_ClientThink(client_t *cl) {
	int               i, id, othid;
	int               num;
	int               touch[MAX_GENTITIES];
	static float      rads[] = {45.0f, 45.0f, 110.0f};  // radius to detect collision with other players
	static float      tmins[] = {-10.0f, -10.0f, -10.0f};  // borders around ally player to deny shooting / enemy player to allow shooting
	static float      tmaxs[] = {10.0f, 10.0f, 10.0f};
	vec3_t            mins, maxs;
	sharedEntity_t    *ent;
	sharedEntity_t    *oth;
	playerState_t     *ps, *othps;
	trace_t           trace;
	int               isLame, w, collision;

	id = (int)(cl - svs.clients);

	if (svm_players[id].isPresenting) {
		if (SVMP_ClientThink(cl)) return 1;
	}

	isLame = svm_players[id].isLame;
	collision = sv_players_collision->integer;

	if (!isLame && collision == COLLISION_ALL && sv_gametype->integer != GT_UT_JUMP) return 0;

	// for lamers and jump mode:

	ps = SV_GameClientNum(id);
	if (ps->persistant[PERS_TEAM] == TEAM_SPECTATOR) return 0;

	ent = SV_GentityNum((int)(cl - svs.clients));

	for (i = 0; i < 3; ++i) {
		mins[i] = ent->r.currentOrigin[i] - rads[i];
		maxs[i] = ent->r.currentOrigin[i] + rads[i];
	}

	num = SV_AreaEntities(mins, maxs, touch, MAX_GENTITIES);

	for (i = 0; i < num; ++i) {
		othid = touch[i];
		if (othid < 0 || othid >= sv_maxclients->integer) continue;
		oth = SV_GentityNum(othid);
		if (ent->s.number == oth->s.number) continue;

		// on collision with other player:

		if (isLame || collision > COLLISION_NO_LAMERS) {
			othps = SV_GameClientNum(othid);
			if (othps->pm_type == PM_DEAD) continue;  // no collision with dead players already
			if (collision < COLLISION_NONE && ps->persistant[PERS_TEAM] != othps->persistant[PERS_TEAM]) continue;  // ok to collide with enemy

			if (isLame) {
				if (svm_players[othid].isLame) continue;  // ok to collide with other lamers
				cl->lastUsercmd.buttons &= ~BUTTON_ATTACK; // deny attacking
				cl->lastUsercmd.upmove = UPMOVE_CROUCH; // deny jump not to bootkick; force crouch not to block the vision

				if (
					sv_gametype->integer == GT_UT_CTF &&
					SVM_ItemFind(ps, (1 << ITEM_RED_FLAG) || (1 << ITEM_BLUE_FLAG))
				) { // if holding the flag in CTF mode:
					SV_ExecuteClientCommand(cl, "ut_itemdrop flag");  // drop the flag
				} else if (
					sv_gametype->integer == GT_UT_BOMB &&
					SVM_WeaponFind(ps, (1 << WEAPON_BOMB))
				) { // if holding the bomb in BOMB mode:
					SV_ExecuteClientCommand(cl, "ut_itemdrop bomb");  // drop the bomb
				}
			}
		}

		// make ghost
		ent->r.contents &= ~CONTENTS_BODY;
		ent->r.contents |= CONTENTS_CORPSE;
		return 0;
	}

	if (ps->pm_type != PM_DEAD) {
		// unghost if no collisions:
		ent->r.contents &= ~CONTENTS_CORPSE;
		ent->r.contents |= CONTENTS_BODY;
	}

	if (isLame && cl->lastUsercmd.buttons & BUTTON_ATTACK) { // if lamer attacking:
		w = cl->lastUsercmd.weapon;
		if (
			w == WEAPON_KNIFE ||
			w == WEAPON_HK69 ||
			w == WEAPON_GRENADE_HE ||
			w == WEAPON_GRENADE_SMOKE
		) {  // for the weapons above:
			cl->lastUsercmd.buttons &= ~BUTTON_ATTACK; // deny attacking by default
		}

		SV_TraceAtCrosshair(&trace, ps, tmins, tmaxs, CONTENTS_BODY, 0);
		i = trace.entityNum;
		if (i < 0 || i >= sv_maxclients->integer) return 0;
		othps = SV_GameClientNum(i);

		if (ps->persistant[PERS_TEAM] != othps->persistant[PERS_TEAM]) {
			cl->lastUsercmd.buttons |= BUTTON_ATTACK; // allow attacking enemy
		} else {
			cl->lastUsercmd.buttons &= ~BUTTON_ATTACK; // deny attacking ally
		}
	}
	return 0;
}

//==================================================================================

char* SVM_ClientConnect(client_t *cl) {
	int id;
	//char *value;

	if (
		// if auth works allow to play with it:
		//TODO: !(auth->integer && (value = Info_ValueForKey(cl->userinfo, "authl")) && *value) ||

		SVM_Subnets_FindByAdr(&bannedSubnets, &cl->netchan.remoteAddress)
	) {
		return unescape_string((char *) va("%s", sv_banned_subnet_message->string));
	}

	id = (int)(cl - svs.clients);
	memset(&svm_players[id], 0, sizeof(svm_player_t));
	return NULL;
}

//==================================================================================

static void SVM_SetLamer_f( void ) {
	client_t  *cl;
	int       id, set;

	// make sure server is running
	if ( !com_sv_running->integer ) {
		Com_Printf( "Server is not running.\n" );
		return;
	}

	if ( Cmd_Argc() < 2 ) {
		Com_Printf ("Usage: setlamer <player name> [<0|1>]\n");
		return;
	}

	cl = SV_GetPlayerByHandle();

	if ( !cl ) {
		Com_Printf ("Player not found: %s\n", Cmd_Argv(1));
		return;
	}

	id = (int)(cl - svs.clients);

	if (Cmd_Argc() == 3) {
		set = atoi(Cmd_Argv(2)) != 0;
		svm_players[id].isLame = set;
	} else {
		set = svm_players[id].isLame;
	}

	Com_Printf("player %s has lamer status %s\n", Cmd_Argv(1), set ? "set" : "unset");
}

static void SVM_SetPresent_f( void ) {
	client_t  *cl;
	int       id, set;

	// make sure server is running
	if ( !com_sv_running->integer ) {
		Com_Printf( "Server is not running.\n" );
		return;
	}

	if ( Cmd_Argc() < 2 ) {
		Com_Printf ("Usage: setpresent <player name> [<0|1>]\n");
		return;
	}

	cl = SV_GetPlayerByHandle();

	if ( !cl ) {
		Com_Printf ("Player not found: %s\n", Cmd_Argv(1));
		return;
	}

	id = (int)(cl - svs.clients);

	if (Cmd_Argc() == 3) {
		set = atoi(Cmd_Argv(2)) != 0;
		svm_players[id].isPresenting = set;
	} else {
		set = svm_players[id].isPresenting;
	}

	Com_Printf("player %s has presentation mode %s\n", Cmd_Argv(1), set ? "on" : "off");
}

//==================================================================================

/**
 * Log in the same file as the game module.
 */
void QDECL SV_LogPrintf(const char *fmt, ...) {
	va_list argptr;
	char buffer[MAX_STRING_CHARS];
	int min, tens, sec;

	sec  = sv.time / 1000;
	min  = sec / 60;
	sec -= min * 60;
	tens = sec / 10;
	sec -= tens * 10;

	Com_sprintf(buffer, sizeof(buffer), "%3i:%i%i ", min, tens, sec);

	va_start(argptr, fmt);
	vsprintf(buffer + 7, fmt, argptr);
	va_end(argptr);

	if (g_log_fileHandle >= 0) {
		FS_Write(buffer, strlen(buffer), g_log_fileHandle);
	}
}

uint32_t get_prefix4(char *string) {
	uint32_t	pfx;
	pfx = * (uint32_t *) string;
	#ifdef Q3_LITTLE_ENDIAN
	pfx = __builtin_bswap32(pfx);
	#endif
	return pfx;
}

char* SVM_OnGamePrint(char *string) {  // \n -terminated
	int colourName_id, colourNames, index;

	colourNames = sv_colourNames->integer;

	switch (get_prefix4(string)) {
		case 'Kill':
			if (!colourNames || sscanf(string, "Kill: %*d %d ", &colourName_id) != 1) colourName_id = -1;
			break;
		case 'Clie':
			if (!colourNames || sscanf(string, "Client%*5[^:]: %d", &colourName_id) != 1) colourName_id = -1;
			break;
		default:
			colourName_id = -1;
			break;
	}

	if (colourName_id >= 0) {
		index = colourName_id + CS_URT_PLAYERS;
		SV_SetConfigstring(index, sv.configstrings[index]);
	}

	return string;
}

qboolean SVM_OnLogPrint(char *string, int len) {
	return 0;
}

static char bot_prefixes[] = "!@&/";
static client_t *last_chat_client = 0;

int SVM_OnClientCommand( client_t *cl, char *s ) {
	char *p;

	switch (get_prefix4(s)) {
		case 'say_':
			if (get_prefix4(s + 4) != 'team') break;
		case 'say ':
			if (sv_hideChatCmd->integer > 0) {
				p = Cmd_ArgsFrom(1);
				while (*p == ' ') ++p;
				if (memchr(bot_prefixes, *p, sizeof(bot_prefixes) - 1)) {
					SV_LogPrintf("say: %d %s: %s\n", cl - svs.clients, cl->plainName, p);
					SV_SendServerCommand(cl, "chat \"^8[^7hidden^8] ^7%s^7: ^8%s\n\"", cl->colourName, p);
					return 1;
				}
			}
			last_chat_client = cl;
			sv.lastSpecChat[0] = '\0';
			break;
		default:
			break;
	}

	return 0;
}

static char spec_chat_prefix[] = "cchat \"0\" \"(SPEC) ";

int SVM_OnServerCommand(client_t **pcl, char *message) {
	int len1, len2;
	char *p;

	// Com_Printf("SVCMD: %s\n", message);

	switch (get_prefix4(message)) {
		case 'ccha':
			// sv_specChatGlobal
			if (sv_specChatGlobal->integer > 0 && *pcl != NULL) {
				if (!Q_strncmp(message, spec_chat_prefix, sizeof(spec_chat_prefix) - 1)) {
					if (!Q_strncmp(message, sv.lastSpecChat, sizeof(sv.lastSpecChat) - 1)) {
						return 1;
					}
					Q_strncpyz(sv.lastSpecChat, (char *) message, sizeof(sv.lastSpecChat));
					*pcl = NULL;
				}
			}
		//case 'prin':
		//case 'ccpr':
		case 'tcch':
			if (sv_colourNames->integer && last_chat_client) {
				p = strstr(message, last_chat_client->plainName);
				if (p) {
					len1 = strlen(last_chat_client->plainName);
					len2 = strlen(last_chat_client->colourName);
					memmove(p + len2, p + len1, strlen(p + len1) + 1);
					memcpy(p, last_chat_client->colourName, len2);
				}
			}
			break;
		default:
			break;
	}

	return 0;
}

//==================================================================================

void SVM_Init( void ) {
	auth = Cvar_Get( "auth", "", CVAR_ROM );

	sv_players_collision = Cvar_Get( "sv_players_collision", "1", CVAR_ARCHIVE );
	Cvar_SetDescription(sv_players_collision, "Disable collision for: 1=lamers only, 2=team allies, 3=all players");

	Cmd_AddCommand(     "setlamer", SVM_SetLamer_f );
	Cmd_SetDescription( "setlamer", "Define if player is a lamer\nusage: setlamer <player> <0|1>" );

	Cmd_AddCommand(     "setpresent", SVM_SetPresent_f );
	Cmd_SetDescription( "setpresent", "Set presentation mode for a player\nusage: setpresent <player> <0|1>" );

	sv_banned_subnets_file = Cvar_Get( "sv_banned_subnets_file", "banned-subnets.txt", CVAR_ARCHIVE );
	Cvar_SetDescription(sv_banned_subnets_file, "File from gamedir with banned subnets, one in a line, format: N.N.N.N/N\nDefault: banned-subnets.txt");

	sv_banned_subnet_message = Cvar_Get( "sv_banned_subnet_message", "Your subnet is banned.", CVAR_ARCHIVE );
	Cvar_SetDescription(sv_banned_subnet_message, "Display this message when someone connects from a banned subnet\nDefault: Your subnet is banned.");

	SVM_Subnets_Init(&bannedSubnets);
	SVM_Subnets_AddFromFile(&bannedSubnets, sv_banned_subnets_file->string);
	SVM_Subnets_Commit(&bannedSubnets);
	Com_Printf("Subnet blocker: Retrieved %ld VPN subnets\n", bannedSubnets.count);
}
