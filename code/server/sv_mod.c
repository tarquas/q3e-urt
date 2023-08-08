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
} svm_player_t;

static svm_player_t  svm_players[MAX_CLIENTS];

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

void SVM_ClientThink(client_t *cl) {
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
	int               isLame, w;

	id = (int)(cl - svs.clients);

	isLame = svm_players[id].isLame;

	if (!isLame && sv_gametype->integer != GT_UT_JUMP) return;

	// for lamers and jump mode:

	ps = SV_GameClientNum(id);
	if (ps->persistant[PERS_TEAM] == TEAM_SPECTATOR) return;

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

		if (isLame) {
			othps = SV_GameClientNum(othid);
			if (ps->persistant[PERS_TEAM] != othps->persistant[PERS_TEAM]) continue;  // ok to collide with enemy
			if (svm_players[othid].isLame) continue;  // ok to collide with other lamers
			cl->lastUsercmd.buttons &= ~BUTTON_ATTACK; // deny attacking
			cl->lastUsercmd.upmove = UPMOVE_CROUCH; // deny jump not to bootkick; force crouch not to block the vision

			if (
				sv_gametype->integer == GT_UT_CTF &&
				SVM_ItemFind(ps, (1 << ITEM_RED_FLAG) || (1 << ITEM_BLUE_FLAG))
			) { // if holding the flag in CTF mode:
				SV_ExecuteClientCommand(cl, "ut_itemdrop flag");  // drop the flag
			} else if (
				sv_gametype->integer != GT_UT_BOMB &&
				SVM_WeaponFind(ps, (1 << WEAPON_BOMB))
			) { // if holding the bomb in BOMB mode:
				SV_ExecuteClientCommand(cl, "ut_itemdrop bomb");  // drop the bomb
			}
		}

		// make ghost
		ent->r.contents &= ~CONTENTS_BODY;
		ent->r.contents |= CONTENTS_CORPSE;
		return;
	}

	// unghost if no collisions:
	ent->r.contents &= ~CONTENTS_CORPSE;
	ent->r.contents |= CONTENTS_BODY;

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
		if (i < 0 || i >= sv_maxclients->integer) return;
		othps = SV_GameClientNum(i);

		if (ps->persistant[PERS_TEAM] != othps->persistant[PERS_TEAM]) {
			cl->lastUsercmd.buttons |= BUTTON_ATTACK; // allow attacking enemy
		} else {
			cl->lastUsercmd.buttons &= ~BUTTON_ATTACK; // deny attacking ally
		}
	}
}

//==================================================================================

char* SVM_ClientConnect(client_t *cl) {
	int id;

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

	if ( Cmd_Argc() != 3 ) {
		Com_Printf ("Usage: setlamer <player name> <0|1>\n");
		return;
	}

	cl = SV_GetPlayerByHandle();

	if ( !cl ) {
		Com_Printf ("Player not found: %s\n", Cmd_Argv(1));
		return;
	}

	id = (int)(cl - svs.clients);
	set = atoi(Cmd_Argv(2)) != 0;
	svm_players[id].isLame = set;
	Com_Printf("player %s has lamer status %s\n", Cmd_Argv(1), set ? "set" : "unset");
}


//==================================================================================

void SVM_Init( void ) {
	Cmd_AddCommand(     "setlamer", SVM_SetLamer_f );
	Cmd_SetDescription( "setlamer", "Define if player is a lamer\nusage: setlamer <player> <0|1>" );
}
