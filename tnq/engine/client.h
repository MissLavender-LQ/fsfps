/*
Copyright (C) 1996-1997 Id Software, Inc.

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/
// client.h

#include "nvs_client.h"	// 2000-04-30 NVS COMMON by Maddes

typedef struct
{
	vec3_t	viewangles;

// intended velocities
	float	forwardmove;
	float	sidemove;
	float	upmove;
#ifdef QUAKE2
	byte	lightlevel;
#endif
} usercmd_t;

typedef struct
{
	int		length;
	char	map[MAX_STYLESTRING];
} lightstyle_t;

typedef struct
{
	char	name[MAX_SCOREBOARDNAME];
	float	entertime;
	int		frags;
	int		colors;			// two 4 bit fields
	byte	translations[VID_GRADES*256];
} scoreboard_t;



// tochris


typedef struct
{
	float					msgtime;
	float					startlerp;
	float					deltalerp;
	float					frametime;
	float					syncbase;

	entity_state_t			baseline;		// to fill in defaults in updates
	entity_state_t			prev;
	entity_state_t			current;

	vec3_t					lerp_origin;
} centity_t;


typedef struct
{
	int		destcolor[3];
	int		percent;		// 0-256
} cshift_t;

#define	CSHIFT_CONTENTS	0
#define	CSHIFT_DAMAGE	1
#define	CSHIFT_BONUS	2
#define	CSHIFT_POWERUP	3
#define	CSHIFT_EXTRA	4
#define	CSHIFT_EXTRA2	5
#define	CSHIFT_EXTRA3	6
#define	NUM_CSHIFTS		7

#define	NAME_LENGTH	64



//
// client_state_t should hold all pieces of the client state
//

#define	SIGNONS		4			// signon messages to receive before connected

#ifdef QSB
#define	MAX_DLIGHTS		512	// may slow down 
#else
#define	MAX_DLIGHTS		32 
#endif
#define	MAX_SHADOWS		64		// was 12442

#define	MAX_FLARES		1024		// was not existing
#define	MAX_WLIGHTS		36 
#define	MAX_LIGHTSFROMWORLD		256



typedef struct
{
	vec3_t	origin;
	float	radius;
	float	die;				// stop lighting after this time
	float	decay;				// drop this each second
	float	minlight;			// don't add when contributing less
	int		key;
	
#ifdef QUAKE2
	
#endif
// 2001-09-11 Colored lightning by LordHavoc/Sarcazm/Maddes  start
	vec3_t	color;				// LordHavoc: .lit support
	float	flashcolor[4];		// flashblend color, RGBA, alpha is for screen flash, Quake default: 1.0/0.5/0.0/0.2
	qboolean	dark;			// subtracts light instead of adding
// 2001-09-11 Colored lightning by LordHavoc/Sarcazm/Maddes  end
	qboolean	unmark;			// world dynamic light; don't mark the world.	also useful for muzzleflashes
} dlight_t;

// leilei - shadowhack
typedef struct
{
	vec3_t	origin;
	float	radius;
	float	hard;			// don't add when contributing less
	float	minlight;
	int		key;
	float	die;				
} shadow_t;


typedef struct
{
	vec3_t	origin;
	float	radius;
	int		key;
	vec3_t	color;
	float	flashcolor[4];		// flashblend color, RGBA, alpha is for screen flash, Quake default: 1.0/0.5/0.0/0.2
	float	die;
	float	decay;
	
} wlight_t;



#ifdef QSB
#define	MAX_BEAMS	256
#else
#define	MAX_BEAMS	24
#endif
typedef struct
{
	int		entity;
	struct model_s	*model;
	float	endtime;
	vec3_t	start, end;
} beam_t;


#ifdef QSB
#define	MAX_EFRAGS		2048
#else
#define	MAX_EFRAGS		640
#endif
#define	MAX_MAPSTRING	2048
#define	MAX_DEMOS		8
#define	MAX_DEMONAME	16

typedef enum {
ca_dedicated, 		// a dedicated server with no ability to start a client
ca_disconnected, 	// full screen console with no connection
ca_connected		// valid netcon, talking to a server
} cactive_t;

//
// the client_static_t structure is persistant through an arbitrary number
// of server connections
//
typedef struct
{
	cactive_t	state;

// personalization data sent to server
	char		mapstring[MAX_QPATH];
	char		spawnparms[MAX_MAPSTRING];	// to restart a level

// demo loop control
	int			demonum;		// -1 = don't play demos
	char		demos[MAX_DEMOS][MAX_DEMONAME];		// when not playing

// demo recording info must be here, because record is started before
// entering a map (and clearing client_state_t)
	qboolean	demorecording;
	qboolean	demoplayback;
	qboolean	timedemo;
	int			forcetrack;			// -1 = use normal cd track
	FILE		*demofile;
	int			td_lastframe;		// to meter out one message a frame
	int			td_startframe;		// host_framecount at start
	float		td_starttime;		// realtime at second frame of timedemo


// connection information
	int			signon;			// 0 to SIGNONS
	struct qsocket_s	*netcon;
	sizebuf_t	message;		// writing buffer to send to server

} client_static_t;

extern client_static_t	cls;

//
// the client_state_t structure is wiped completely at every
// server signon
//
typedef struct
{
	int			movemessages;	// since connecting to this server
								// throw out the first couple, so the player
								// doesn't accidentally do something the
								// first frame
	usercmd_t	cmd;			// last command sent to the server

// information for local display
	int			stats[MAX_CL_STATS];	// health, etc
	int			items;			// inventory bit flags
	float	item_gettime[32];	// cl.time of aquiring item, for blinking
	float		faceanimtime;	// use anim frame if cl.time < this

	cshift_t	cshifts[NUM_CSHIFTS];	// color shifts for damage, powerups
	cshift_t	prev_cshifts[NUM_CSHIFTS];	// and content types

// the client maintains its own idea of view angles, which are
// sent to the server each frame.  The server sets punchangle when
// the view is temporarliy offset, and an angle reset commands at the start
// of each level and after teleporting.
	vec3_t		mviewangles[2];	// during demo playback viewangles is lerped
								// between these
	vec3_t		viewangles;

	vec3_t		mvelocity[2];	// update by server, used for lean+bob
								// (0 is newest)
	vec3_t		velocity;		// lerped between mvelocity[0] and [1]

	vec3_t		punchangle;		// temporary offset

// pitch drifting vars
	float		idealpitch;
	float		pitchvel;
	qboolean	nodrift;
	float		driftmove;
	double		laststop;

	float		viewheight;
	float		crouch;			// local amount for smoothing stepups

	qboolean	paused;			// send over by server
	qboolean	onground;
	qboolean	inwater;

	int			intermission;	// don't change view angle, full screen, etc
	int			completed_time;	// latched at intermission start

	double		mtime[2];		// the timestamp of last two messages
	double		time;			// clients view of time, should be between
								// servertime and oldservertime to generate
								// a lerp point for other data
	double		oldtime;		// previous cl.time, time-oldtime is used
								// to decay light values and smooth step ups

	// how long it has been since the previous client frame in real time
	// (not game time, for that use cl.time - cl.oldtime)
	double realframetime;
	float		last_received_message;	// (realtime) for net trouble icon
	double		gundrawtime;
//
// information that is static for the entire time connected to a server
//
	struct model_s		*model_precache[MAX_MODELS];
	struct sfx_s		*sound_precache[MAX_SOUNDS];

	char		levelname[40];	// for display on solo scoreboard
	int			viewentity;		// cl_entitites[cl.viewentity] = player
	int			maxclients;
	int			gametype;

// refresh related state
	struct model_s	*worldmodel;	// cl_entitites[0].model
	struct efrag_s	*free_efrags;
// 2001-09-20 Configurable entity limits by Maddes  start
	int			max_edicts;
	int			max_static_edicts;
	int			max_temp_edicts;
// 2001-09-20 Configurable entity limits by Maddes  end
	int			num_entities;	// held in cl_entities array
	int			num_statics;	// held in cl_staticentities array
	entity_t	viewent;			// the gun model

	int			cdtrack, looptrack;	// cd audio

// frag scoreboard
	scoreboard_t	*scores;		// [cl.maxclients]

#ifdef QUAKE2
// light level at player's position including dlights
// this is sent back to the server each frame
// architectually ugly but it works
	int			light_level;
#endif

//#ifdef PROTO
	// used by bob
	qboolean oldonground;
	double lastongroundtime;
	double hitgroundtime;
	float bob2_smooth;
	float bobfall_speed;
	float bobfall_swing;

	// previous gun angles (for leaning effects)
	vec3_t gunangles_prev;
	vec3_t gunangles_highpass;
	vec3_t gunangles_adjustment_lowpass;
	vec3_t gunangles_adjustment_highpass;
	// previous gun angles (for leaning effects)
	vec3_t gunorg_prev;
	vec3_t gunorg_highpass;
	vec3_t gunorg_adjustment_lowpass;
	vec3_t gunorg_adjustment_highpass;

//#endif
#ifdef VMTOC
	// Sajt: added this stuff
	vec3_t weapon_origin;
	vec3_t weapon_angles;
#endif

#ifdef LOOKANGLE
		vec3_t		aimangle;		// temporary offset
		vec3_t		lookangle;		// temporary offset
#endif
} client_state_t;


//
// cvars
//
extern	cvar_t	*cl_name;
extern	cvar_t	*cl_color;

extern	cvar_t	*cl_upspeed;
extern	cvar_t	*cl_forwardspeed;
extern	cvar_t	*cl_backspeed;
extern	cvar_t	*cl_sidespeed;

extern	cvar_t	*cl_movespeedkey;

extern	cvar_t	*cl_yawspeed;
extern	cvar_t	*cl_pitchspeed;

extern	cvar_t	*cl_anglespeedkey;

extern	cvar_t	*cl_autofire;

extern	cvar_t	*cl_shownet;
extern	cvar_t	*cl_nolerp;

extern	cvar_t	*cl_sbar;
extern	cvar_t	*cl_hudswap;

extern	cvar_t	*cl_pitchdriftspeed;
extern	cvar_t	*lookspring;
extern	cvar_t	*lookstrafe;
extern	cvar_t	*sensitivity;

extern	cvar_t	*m_pitch;
extern	cvar_t	*m_yaw;
extern	cvar_t	*m_forward;
extern	cvar_t	*m_side;
extern	cvar_t	*m_look;	// 2001-12-16 M_LOOK cvar by Heffo/Maddes

// 2001-11-31 FPS display by QuakeForge/Muff  start
extern cvar_t	*cl_showfps;
extern int		fps_count;
// 2001-11-31 FPS display by QuakeForge/Muff  end

extern cvar_t	*cl_compatibility;	// 2001-12-24 Keeping full backwards compatibility by Maddes

// 2001-09-20 Configurable entity limits by Maddes  start
extern cvar_t	*cl_entities_min;
extern cvar_t	*cl_entities_min_static;
extern cvar_t	*cl_entities_min_temp;

/*
#define	MAX_TEMP_ENTITIES	64			// lightning bolts, etc
#define	MAX_STATIC_ENTITIES	128			// torches, etc
*/
// 2001-09-20 Configurable entity limits by Maddes  end

extern	client_state_t	cl;

// FIXME, allocate dynamically
extern	efrag_t			cl_efrags[MAX_EFRAGS];
// 2001-09-20 Configurable entity limits by Maddes  start
/*
extern	entity_t		cl_entities[MAX_EDICTS];
extern	entity_t		cl_static_entities[MAX_STATIC_ENTITIES];
*/
extern	entity_t		*cl_entities;
extern	entity_t		*cl_static_entities;
// 2001-09-20 Configurable entity limits by Maddes  end
extern	lightstyle_t	cl_lightstyle[MAX_LIGHTSTYLES];
extern	dlight_t		cl_dlights[MAX_DLIGHTS];
extern	wlight_t		cl_wlights[MAX_WLIGHTS];
extern	shadow_t		cl_shadows[MAX_SHADOWS];
// 2001-09-20 Configurable entity limits by Maddes  start
//extern	entity_t		cl_temp_entities[MAX_TEMP_ENTITIES];
extern	entity_t		*cl_temp_entities;
// 2001-09-20 Configurable entity limits by Maddes  end
extern	beam_t			cl_beams[MAX_BEAMS];

//=============================================================================

//
// cl_main
//
dlight_t *CL_AllocDlight (int key);
void	CL_DecayLights (void);




shadow_t *CL_AllocShadow (int key);

wlight_t *CL_AllocWlight (int key);

void CL_Init (void);

void CL_EstablishConnection (char *host);
void CL_Signon1 (void);
void CL_Signon2 (void);
void CL_Signon3 (void);
void CL_Signon4 (void);

void CL_Disconnect (void);
void CL_Disconnect_f (void);
void CL_NextDemo (void);
#ifdef QSB
#define			MAX_VISEDICTS	MAX_EDICTS
#else
#define			MAX_VISEDICTS	256
#endif
extern	int				cl_numvisedicts;
extern	entity_t		*cl_visedicts[MAX_VISEDICTS];

//
// cl_input
//
typedef struct
{
	int		down[2];		// key nums holding it down
	int		state;			// low bit is down state
} kbutton_t;

extern	kbutton_t	in_mlook, in_klook;
extern 	kbutton_t 	in_strafe;
extern 	kbutton_t 	in_speed;

void CL_InitInput (void);
void CL_SendCmd (void);
void CL_SendMove (usercmd_t *cmd);

void CL_ParseTEnt (void);
void CL_UpdateTEnts (void);

void CL_ClearState (void);


int  CL_ReadFromServer (void);
void CL_WriteToServer (usercmd_t *cmd);
void CL_BaseMove (usercmd_t *cmd);


float CL_KeyState (kbutton_t *key);
char *Key_KeynumToString (int keynum);

//
// cl_demo.c
//
void CL_StopPlayback (void);
int CL_GetMessage (void);

void CL_Stop_f (void);
void CL_Record_f (void);
void CL_PlayDemo_f (void);
void CL_TimeDemo_f (void);

//
// cl_parse.c
//
void CL_ParseServerMessage (void);
void CL_NewTranslation (int slot);

//
// view
//
void V_StartPitchDrift (void);
void V_StopPitchDrift (void);

void V_RenderView (void);
void V_UpdatePalette (void);
void V_Register (void);
void V_ParseDamage (void);
void V_SetContentsColor (int contents);


//
// cl_tent
//
void CL_InitTEnts (void);
void CL_SignonReply (void);
