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
// host.c -- coordinates spawning and killing of local servers

#include "quakedef.h"
#include "r_local.h"

/*

A server can always be started, even if the system started out as a client
to a remote system.

A client can NOT be started if the system started as a dedicated server.

Memory is cleared / released when a server or client begins, not when they end.

*/

// 2001-10-20 TIMESCALE extension by Tomaz/Maddes  start
double	host_cpu_frametime;
double	host_org_frametime;

cvar_t	*host_timescale;
// 2001-10-20 TIMESCALE extension by Tomaz/Maddes  end

float	thestandstill; // leilei - standstill hack

quakeparms_t host_parms;

qboolean	host_initialized;		// true if into command execution

double		host_frametime;
double		host_time;
double		realtime;				// without any filtering or bounding
double		oldrealtime;			// last frame run
int			host_framecount;

int			host_hunklevel;

int			minimum_memory;

client_t	*host_client;			// current client

jmp_buf 	host_abortserver;

byte		*host_basepal;
byte		*host_otherpal;		// NEW palette only!
byte		*host_origpal;		// QUAKE palette only!
byte		*host_palremap;		// byte table to remap one pal to the other on the fly
byte		*host_colormap;
byte		*host_colormap_red;
byte		*host_colormap_green;
byte		*host_colormap_blue;
byte		*host_colormap_buffer;
byte		*host_colormap_nofb;
byte		*host_fogmap;
byte		*glcolormap;



int			host_fullbrights;   // for preserving fullbrights in color operations



#ifdef EGA
byte		*host_egamap;
#endif
cvar_t	*host_framerate;	// set for slow motion
cvar_t	*host_speeds;		// set for running times

cvar_t	*sys_ticrate;
cvar_t	*serverprofile;

cvar_t	*fraglimit;
cvar_t	*timelimit;
cvar_t	*teamplay;

cvar_t	*samelevel;
cvar_t	*noexit;

cvar_t	*developer;

cvar_t	*skill;					// 0 - 3
cvar_t	*deathmatch;			// 0, 1, or 2
cvar_t	*coop;					// 0 or 1
cvar_t	*autosaver;				// leilei - autosaver
cvar_t	*loadscreen;				// leilei - loadscreen

cvar_t	*pausable;

cvar_t	*temp1;
cvar_t	*temp2;
cvar_t	*temp3;

cvar_t	*contact;		// 2000-01-31 Contact cvar by Maddes

cvar_t	*max_fps;		// 2001-12-16 MAX_FPS cvar by MrG

int		fps_count;	// 2001-11-31 FPS display by QuakeForge/Muff  end


#ifdef EGAHACK


const byte ega_palette[256 * 3] =
{
    0,  0,  0, 
	0,  0,170,
	0,170,	0,
	0,170,170,
  170,	0,	0,
  170,	0,170,
  170, 85,	0,
  170,170,170,
   85, 85, 85,
   85, 85,255, // highlights
   85,255, 85,
   85,255,255,
  255, 85, 85,
  255, 85,255,
  255,255, 85,
  255,255,255
  		// Nah, you don't need the rest of the colors :)
};



const byte w_palette[256 * 3] =
{
    0,  0,  0, 
  128,  0,	0,
	0,128,	0,
  128,128,	0,
    0,	0,128,
  128,	0,128,
    0,128,128,
  192,192,192,
  128,128,128,
    0,  0,255, // highlights
    0,255,  0,
  255,255,  0,
    0,  0,255,
  255,  0,255,
    0,255,255,
  255,255,255
  		// Nah, you don't need the rest of the colors :)
};


#endif
/*
================
Host_EndGame
================
*/
void Host_EndGame (char *message, ...)
{
	va_list		argptr;
	char		string[1024];

	va_start (argptr,message);
	vsprintf (string,message,argptr);
	va_end (argptr);
	Con_DPrintf ("Host_EndGame: %s\n",string);

	if (sv.active)
		Host_ShutdownServer (false);

	if (cls.state == ca_dedicated)
		Sys_Error ("Host_EndGame: %s\n",string);	// dedicated servers exit

	if (cls.demonum != -1)
		CL_NextDemo ();
	else
		CL_Disconnect ();

	longjmp (host_abortserver, 1);
}

/*
================
Host_Error

This shuts down both the client and server
================
*/
void Host_Error (char *error, ...)
{
	va_list		argptr;
	char		string[1024];
	static	qboolean inerror = false;

	if (inerror)
		Sys_Error ("Host_Error: recursively entered");
	inerror = true;

	SCR_EndLoadingPlaque ();		// reenable screen updates

	va_start (argptr,error);
	vsprintf (string,error,argptr);
	va_end (argptr);
	Con_Printf ("Host_Error: %s\n",string);

	if (sv.active)
		Host_ShutdownServer (false);

	if (cls.state == ca_dedicated)
		Sys_Error ("Host_Error: %s",string);	// dedicated servers exit

	CL_Disconnect ();
	cls.demonum = -1;

	inerror = false;

	longjmp (host_abortserver, 1);
}

/*
================
Host_FindMaxClients
================
*/
void	Host_FindMaxClients (void)
{
	int		i;

	svs.maxclients = 1;
	svs.maxclientslimit = MAX_SCOREBOARD;	// 2000-01-11 Set default maximum clients to 16 instead of 4 by Maddes

	i = COM_CheckParm ("-dedicated");
	if (i)
	{
		cls.state = ca_dedicated;
		if (i != (com_argc - 1))
		{
			svs.maxclients = Q_atoi (com_argv[i+1]);
		}
		else
			svs.maxclients = 8;
	}
	else
		cls.state = ca_disconnected;

	i = COM_CheckParm ("-listen");
	if (i)
	{
		if (cls.state == ca_dedicated)
			Sys_Error ("Only one of -dedicated or -listen can be specified");
		if (i != (com_argc - 1))
			svs.maxclients = Q_atoi (com_argv[i+1]);
		else
			svs.maxclients = 8;
	}
	if (svs.maxclients < 1)
		svs.maxclients = 8;
// 2000-01-11 Set default maximum clients to 16 instead of 4 by Maddes  end
	if (svs.maxclientslimit < 4)
		svs.maxclientslimit = 4;

// 2000-01-11 Set default maximum clients to 16 instead of 4 by Maddes  start
	if (svs.maxclientslimit > MAX_SCOREBOARD)
		svs.maxclientslimit = MAX_SCOREBOARD;

	if (svs.maxclients > svs.maxclientslimit)
		svs.maxclients = svs.maxclientslimit;
// 2000-01-11 Set default maximum clients to 16 instead of 4 by Maddes  end
	svs.clients = Hunk_AllocName (svs.maxclientslimit*sizeof(client_t), "clients");

	if (svs.maxclients > 1)
		Cvar_Set (deathmatch, "1");
	else
		Cvar_Set (deathmatch, "0");
}

// 1999-09-06 deathmatch/coop not at the same time fix by Maddes  start
void Callback_Deathmatch (cvar_t *var)
{
	if (var->value)
	{
		Cvar_Set (coop, "0");
	}
}

void Callback_Coop (cvar_t *var)
{
	if (var->value)
	{
		Cvar_Set (deathmatch, "0");
	}
}
// 1999-09-06 deathmatch/coop not at the same time fix by Maddes  end

// 2001-09-18 New cvar system by Maddes (Init)  start
/*
=======================
Host_InitLocal_Cvars
======================
*/
void Host_InitLocal_Cvars (void)
{
// 2001-10-20 TIMESCALE extension by Tomaz/Maddes  start
	host_timescale = Cvar_Get ("host_timescale", "1", CVAR_NONE);
	Cvar_SetRangecheck (host_timescale, Cvar_RangecheckFloat, 0.0, 10.0);
	Cvar_Set(host_timescale, host_timescale->string);	// do rangecheck
// 2001-10-20 TIMESCALE extension by Tomaz/Maddes  end

	host_framerate = Cvar_Get ("host_framerate", "0", CVAR_ORIGINAL);
	host_speeds = Cvar_Get ("host_speeds", "0", CVAR_ORIGINAL);

	sys_ticrate = Cvar_Get ("sys_ticrate", "0.05", CVAR_ORIGINAL);
	serverprofile = Cvar_Get ("serverprofile", "0", CVAR_ORIGINAL);

	fraglimit = Cvar_Get ("fraglimit", "0", CVAR_NOTIFY|CVAR_SERVERINFO|CVAR_ORIGINAL);
	timelimit = Cvar_Get ("timelimit", "0", CVAR_NOTIFY|CVAR_SERVERINFO|CVAR_ORIGINAL);
	teamplay = Cvar_Get ("teamplay", "0", CVAR_NOTIFY|CVAR_SERVERINFO|CVAR_ORIGINAL);

 	samelevel = Cvar_Get ("samelevel", "0", CVAR_ORIGINAL);
	noexit = Cvar_Get ("noexit", "0", CVAR_NOTIFY|CVAR_SERVERINFO|CVAR_ORIGINAL);
	skill = Cvar_Get ("skill", "1", CVAR_ORIGINAL);

	autosaver = Cvar_Get ("autosaver", "0", CVAR_ARCHIVE);
	loadscreen = Cvar_Get ("loadscreen", "1", CVAR_ARCHIVE);

	developer = Cvar_Get ("developer", "0", CVAR_ORIGINAL);

	deathmatch = Cvar_Get ("deathmatch", "0", CVAR_ORIGINAL);
	Cvar_SetCallback (deathmatch, Callback_Deathmatch);	// 1999-09-06 deathmatch/coop not at the same time fix by Maddes

	coop = Cvar_Get ("coop", "0", CVAR_ORIGINAL);
	Cvar_SetCallback (coop, Callback_Coop);	// 1999-09-06 deathmatch/coop not at the same time fix by Maddes

	pausable = Cvar_Get ("pausable", "1", CVAR_ORIGINAL);

	temp1 = Cvar_Get ("temp1", "0", CVAR_ORIGINAL);
	temp2 = Cvar_Get ("temp2", "0", CVAR_ORIGINAL);
	temp3 = Cvar_Get ("temp3", "0", CVAR_ORIGINAL);

	contact = Cvar_Get ("contact", "", CVAR_ARCHIVE);	// 2000-01-31 Contact cvar by Maddes

// 2001-12-16 MAX_FPS cvar by MrG  start
	max_fps = Cvar_Get ("max_fps", "72", CVAR_ARCHIVE);
	Cvar_SetRangecheck (max_fps, Cvar_RangecheckInt, 10, 200);
	Cvar_Set(max_fps, max_fps->string);	// do rangecheck
// 2001-12-16 MAX_FPS cvar by MrG  end
}
// 2001-09-18 New cvar system by Maddes (Init)  end

/*
=======================
Host_InitLocal
======================
*/
void Host_InitLocal (void)
{
	Host_InitCommands ();

// 2001-09-18 New cvar system by Maddes (Init)  end

	Host_FindMaxClients ();

	host_time = 1.0;		// so a think at time 0 won't get called
}


/*
===============
Host_WriteConfiguration

Writes key bindings and archived cvars to config.cfg
===============
*/
void Host_WriteConfiguration (void)
{
	FILE	*f;

// dedicated servers initialize the host but don't parse and set the
// config.cfg cvars
//	if (host_initialized & !isDedicated)
	if (host_initialized && !isDedicated)	// 1999-12-24 logical correction by Maddes
	{
		f = fopen (va("%s/config.cfg",com_gamedir), "w");
		if (!f)
		{
			Con_Printf ("Couldn't write config.cfg.\n");
			return;
		}

		Key_WriteBindings (f);
		Cvar_WriteVariables (f, false);	// 2001-09-18 New cvar system by Maddes

		fclose (f);

// 2001-09-18 New cvar system by Maddes  start
		f = fopen (va("%s/config.rc",com_gamedir), "w");
		if (!f)
		{
			Con_Printf ("Couldn't write config.rc.\n");
			return;
		}

		Cvar_WriteVariables (f, true);	// 2001-09-18 New cvar system by Maddes

		fclose (f);
// 2001-09-18 New cvar system by Maddes  end
	}
}


/*
=================
SV_ClientPrintf

Sends text across to be displayed
FIXME: make this just a stuffed echo?
=================
*/
void SV_ClientPrintf (char *fmt, ...)
{
	va_list		argptr;
	char		string[1024];

	va_start (argptr,fmt);
	vsprintf (string, fmt,argptr);
	va_end (argptr);

	MSG_WriteByte (&host_client->message, svc_print);
	MSG_WriteString (&host_client->message, string);
}

/*
=================
SV_BroadcastPrintf

Sends text to all active clients
=================
*/
void SV_BroadcastPrintf (char *fmt, ...)
{
	va_list		argptr;
	char		string[1024];
	int			i;

	va_start (argptr,fmt);
	vsprintf (string, fmt,argptr);
	va_end (argptr);

	for (i=0 ; i<svs.maxclients ; i++)
		if (svs.clients[i].active && svs.clients[i].spawned)
		{
			MSG_WriteByte (&svs.clients[i].message, svc_print);
			MSG_WriteString (&svs.clients[i].message, string);
		}
}

/*
=================
Host_ClientCommands

Send text over to the client to be executed
=================
*/
void Host_ClientCommands (char *fmt, ...)
{
	va_list		argptr;
	char		string[1024];

	va_start (argptr,fmt);
	vsprintf (string, fmt,argptr);
	va_end (argptr);

	MSG_WriteByte (&host_client->message, svc_stufftext);
	MSG_WriteString (&host_client->message, string);
}

/*
=====================
SV_DropClient

Called when the player is getting totally kicked off the host
if (crash = true), don't bother sending signofs
=====================
*/
void SV_DropClient (qboolean crash)
{
	int		saveSelf;
	int		i;
	client_t *client;

	if (!crash)
	{
		// send any final messages (don't check for errors)
		if (NET_CanSendMessage (host_client->netconnection))
		{
			MSG_WriteByte (&host_client->message, svc_disconnect);
			NET_SendMessage (host_client->netconnection, &host_client->message);
		}

		if (host_client->edict && host_client->spawned)
		{
		// call the prog function for removing a client
		// this will set the body to a dead frame, among other things
			saveSelf = pr_global_struct->self;
			pr_global_struct->self = EDICT_TO_PROG(host_client->edict);
			PR_ExecuteProgram (pr_global_struct->ClientDisconnect);
			pr_global_struct->self = saveSelf;
		}

		Sys_Printf ("Client %s removed\n",host_client->name);
	}

// break the net connection
	NET_Close (host_client->netconnection);
	host_client->netconnection = NULL;

// free the client (the body stays around)
	host_client->active = false;
	host_client->name[0] = 0;
	host_client->old_frags = -999999;
	net_activeconnections--;

// send notification to all clients
	for (i=0, client = svs.clients ; i<svs.maxclients ; i++, client++)
	{
		if (!client->active)
			continue;
		MSG_WriteByte (&client->message, svc_updatename);
		MSG_WriteByte (&client->message, host_client - svs.clients);
		MSG_WriteString (&client->message, "");
		MSG_WriteByte (&client->message, svc_updatefrags);
		MSG_WriteByte (&client->message, host_client - svs.clients);
		MSG_WriteShort (&client->message, 0);
		MSG_WriteByte (&client->message, svc_updatecolors);
		MSG_WriteByte (&client->message, host_client - svs.clients);
		MSG_WriteByte (&client->message, 0);
	}
}

/*
==================
Host_ShutdownServer

This only happens at the end of a game, not between levels
==================
*/
void Host_ShutdownServer(qboolean crash)
{
	int		i;
	int		count;
	sizebuf_t	buf;
	char		message[4];
	double	start;

	if (!sv.active)
		return;

	sv.active = false;

// stop all client sounds immediately
	if (cls.state == ca_connected)
		CL_Disconnect ();

// flush any pending messages - like the score!!!
	start = Sys_FloatTime();
	do
	{
		count = 0;
		for (i=0, host_client = svs.clients ; i<svs.maxclients ; i++, host_client++)
		{
			if (host_client->active && host_client->message.cursize)
			{
				if (NET_CanSendMessage (host_client->netconnection))
				{
					NET_SendMessage(host_client->netconnection, &host_client->message);
					SZ_Clear (&host_client->message);
				}
				else
				{
					NET_GetMessage(host_client->netconnection);
					count++;
				}
			}
		}
		if ((Sys_FloatTime() - start) > 3.0)
			break;
	}
	while (count);

// make sure all the clients know we're disconnecting
	buf.data = message;
	buf.maxsize = 4;
	buf.cursize = 0;
	MSG_WriteByte(&buf, svc_disconnect);
	count = NET_SendToAll(&buf, 5);
	if (count)
		Con_Printf("Host_ShutdownServer: NET_SendToAll failed for %u clients\n", count);

	for (i=0, host_client = svs.clients ; i<svs.maxclients ; i++, host_client++)
		if (host_client->active)
			SV_DropClient(crash);

//
// clear structures
//
	memset (&sv, 0, sizeof(sv));
	memset (svs.clients, 0, svs.maxclientslimit*sizeof(client_t));
}


/*
================
Host_ClearMemory

This clears all the memory used by both the client and server, but does
not reinitialize anything.
================
*/
void Host_ClearMemory (void)
{
	Con_DPrintf ("Clearing memory\n");
	D_FlushCaches ();
	Mod_ClearAll ();
	if (host_hunklevel)
		Hunk_FreeToLowMark (host_hunklevel);

	cls.signon = 0;
	memset (&sv, 0, sizeof(sv));
	memset (&cl, 0, sizeof(cl));
}


//============================================================================


/*
===================
Host_FilterTime

Returns false if the time is too short to run a frame
===================
*/
qboolean Host_FilterTime (float time)
{
	realtime += time;

// 2001-12-16 MAX_FPS cvar by MrG  start
//	if (!cls.timedemo && realtime - oldrealtime < 1.0/72.0)
	if (max_fps->value < 10) Cvar_Set(max_fps, "72");
	if (!cls.timedemo && realtime - oldrealtime < 1.0/max_fps->value)
// 2001-12-16 MAX_FPS cvar by MrG  end
		return false;		// framerate is too high

// 2001-10-20 TIMESCALE extension by Tomaz/Maddes  start
//	host_frametime = realtime - oldrealtime;
	host_cpu_frametime = realtime - oldrealtime;
	host_org_frametime = host_cpu_frametime;
// 2001-10-20 TIMESCALE extension by Tomaz/Maddes  end
	oldrealtime = realtime;

	if (host_framerate->value > 0)
// 2001-10-20 TIMESCALE extension by Tomaz/Maddes  start
	{
//		host_frametime = host_framerate->value;
		host_org_frametime = host_framerate->value;
	}
// 2001-10-20 TIMESCALE extension by Tomaz/Maddes  end
	else
	{	// don't allow really long or short frames
// 2001-10-20 TIMESCALE extension by Tomaz/Maddes  start
/*
		if (host_frametime > 0.1)
			host_frametime = 0.1;
		if (host_frametime < 0.001)
			host_frametime = 0.001;
*/
		if (host_org_frametime > 0.1)
			host_org_frametime = 0.1;
		if (host_org_frametime < 0.001)
			host_org_frametime = 0.001;
// 2001-10-20 TIMESCALE extension by Tomaz/Maddes  end
	}

	host_frametime = host_org_frametime * host_timescale->value;	// 2001-10-20 TIMESCALE extension by Tomaz/Maddes

	return true;
}


/*
===================
Host_GetConsoleCommands

Add them exactly as if they had been typed at the console
===================
*/
void Host_GetConsoleCommands (void)
{
	char	*cmd;

	while (1)
	{
		cmd = Sys_ConsoleInput ();
		if (!cmd)
			break;
		Cbuf_AddText (cmd);
	}
}


/*
==================
Host_ServerFrame

==================
*/
#ifdef FPS_20

void _Host_ServerFrame (void)
{
// run the world state
	pr_global_struct->frametime = host_frametime;
// 2001-10-20 TIMESCALE extension by Tomaz/Maddes  start
	if (pr_global_cpu_frametime)
	{
		G_FLOAT(pr_global_cpu_frametime->ofs) = host_cpu_frametime;
	}

	if (pr_global_org_frametime)
	{
		G_FLOAT(pr_global_org_frametime->ofs) = host_org_frametime;
	}
// 2001-10-20 TIMESCALE extension by Tomaz/Maddes  end

// read client messages
	SV_RunClients ();

// move things around and think
// always pause in single player if in console or menus
	if (!sv.paused && (svs.maxclients > 1 || key_dest == key_game) )
		SV_Physics ();
}

void Host_ServerFrame (void)
{
	float	save_host_frametime;
	float	temp_host_frametime;
	int	i;		// 2000-05-02 NVS SVC by Maddes

// run the world state
	pr_global_struct->frametime = host_frametime;
// 2001-10-20 TIMESCALE extension by Tomaz/Maddes  start
	if (pr_global_cpu_frametime)
	{
		G_FLOAT(pr_global_cpu_frametime->ofs) = host_cpu_frametime;
	}

	if (pr_global_org_frametime)
	{
		G_FLOAT(pr_global_org_frametime->ofs) = host_org_frametime;
	}
// 2001-10-20 TIMESCALE extension by Tomaz/Maddes  end

// set the time and clear the general datagram
	SV_ClearDatagram ();
// 2000-05-02 NVS SVC by Maddes  start
	// clear existing clients
	for (i=0 ; i<svs.maxclients ; i++)
	{
		if (svs.clients[i].active)
		{
			SZ_Clear (&svs.clients[i].datagram);
		}
	}
// 2000-05-02 NVS SVC by Maddes  end

// check for new clients
	SV_CheckForNewClients ();

	temp_host_frametime = save_host_frametime = host_frametime;
	while(temp_host_frametime > (1.0/72.0))
	{
		if (temp_host_frametime > 0.05)
			host_frametime = 0.05;
		else
			host_frametime = temp_host_frametime;
		temp_host_frametime -= host_frametime;
		_Host_ServerFrame ();
	}
	host_frametime = save_host_frametime;

// send all messages to the clients
	SV_SendClientMessages ();
}

#else

void Host_ServerFrame (void)
{
	int	i;		// 2000-05-02 NVS SVC by Maddes

// run the world state
	pr_global_struct->frametime = host_frametime;
// 2001-10-20 TIMESCALE extension by Tomaz/Maddes  start
	if (pr_global_cpu_frametime)
	{
		G_FLOAT(pr_global_cpu_frametime->ofs) = host_cpu_frametime;
	}

	if (pr_global_org_frametime)
	{
		G_FLOAT(pr_global_org_frametime->ofs) = host_org_frametime;
	}
// 2001-10-20 TIMESCALE extension by Tomaz/Maddes  end

// set the time and clear the general datagram
	SV_ClearDatagram ();
// 2000-05-02 NVS SVC by Maddes  start
	// clear existing clients
	for (i=0 ; i<svs.maxclients ; i++)
	{
		if (svs.clients[i].active)
		{
			SZ_Clear (&svs.clients[i].datagram);
		}
	}
// 2000-05-02 NVS SVC by Maddes  end

// check for new clients
	SV_CheckForNewClients ();

// read client messages
	SV_RunClients ();

// move things around and think
// always pause in single player if in console or menus
	if (!sv.paused && (svs.maxclients > 1 || key_dest == key_game) )
		SV_Physics ();

// send all messages to the clients
	SV_SendClientMessages ();
}

#endif


/*
==================
Host_Frame

Runs all active servers
==================
*/
void _Host_Frame (float time)
{
	static double		time1 = 0;
	static double		time2 = 0;
	static double		time3 = 0;
	int			pass1, pass2, pass3;

	if (setjmp (host_abortserver) )
		return;			// something bad happened, or the server disconnected

// keep the random time dependent
	rand ();

// decide the simulation time
	if (!Host_FilterTime (time))
		return;			// don't run too fast, or packets will flood out

// get new key events
	Sys_SendKeyEvents ();

// allow mice or other external controllers to add commands
	IN_Commands ();

// process console commands
	Cbuf_Execute ();

	NET_Poll();

// if running the server locally, make intentions now
	if (sv.active)
		CL_SendCmd ();

//-------------------
//
// server operations
//
//-------------------

// check for commands typed to the host
	Host_GetConsoleCommands ();

	if (sv.active)
		Host_ServerFrame ();

//-------------------
//
// client operations
//
//-------------------

// if running the server remotely, send intentions now after
// the incoming messages have been read
	if (!sv.active)
		CL_SendCmd ();

	host_time += host_frametime;

// fetch results from server
	if (cls.state == ca_connected)
	{
		CL_ReadFromServer ();
	}

// update video
	if (host_speeds->value)
		time1 = Sys_FloatTime ();

	SCR_UpdateScreen ();

	if (host_speeds->value)
		time2 = Sys_FloatTime ();




// update audio
	if (cls.signon == SIGNONS)
	{
		S_Update (r_origin, vpn, vright, vup);
		CL_DecayLights ();
	}
	else
		S_Update (vec3_origin, vec3_origin, vec3_origin, vec3_origin);

#ifdef	ASS_MIDI
	MIDI_Update();	// leilei - update our midi
#endif
	CDAudio_Update();

	if (host_speeds->value)
	{
		pass1 = (time1 - time3)*1000;
		time3 = Sys_FloatTime ();
		pass2 = (time2 - time1)*1000;
		pass3 = (time3 - time2)*1000;
		Con_Printf ("%3i tot %3i server %3i gfx %3i snd\n",
					pass1+pass2+pass3, pass1, pass2, pass3);
	}

	host_framecount++;

	fps_count++;	// 2001-11-31 FPS display by QuakeForge/Muff
}

void Host_Frame (float time)
{
	double	time1, time2;
	static double	timetotal;
	static int		timecount;
	int		i, c, m;

	if (!serverprofile->value)
	{
		_Host_Frame (time);
		return;
	}

	time1 = Sys_FloatTime ();
	_Host_Frame (time);
	time2 = Sys_FloatTime ();

	timetotal += time2 - time1;
	timecount++;

	if (timecount < 1000)
		return;

	m = timetotal*1000/timecount;
	timecount = 0;
	timetotal = 0;
	c = 0;
	for (i=0 ; i<svs.maxclients ; i++)
	{
		if (svs.clients[i].active)
			c++;
	}

	Con_Printf ("serverprofile: %2i clients %2i msec\n",  c,  m);
}

//============================================================================


extern int vcrFile;
#define	VCR_SIGNATURE	0x56435231
// "VCR1"

void Host_InitVCR (quakeparms_t *parms)
{
	int		i, len, n;
	char	*p;

	if (COM_CheckParm("-playback"))
	{
		if (com_argc != 2)
			Sys_Error("No other parameters allowed with -playback\n");

		Sys_FileOpenRead("quake.vcr", &vcrFile);
		if (vcrFile == -1)
			Sys_Error("playback file not found\n");

		Sys_FileRead (vcrFile, &i, sizeof(int));
		if (i != VCR_SIGNATURE)
			Sys_Error("Invalid signature in vcr file\n");

		Sys_FileRead (vcrFile, &com_argc, sizeof(int));
		com_argv = malloc(com_argc * sizeof(char *));
		com_argv[0] = parms->argv[0];
		for (i = 0; i < com_argc; i++)
		{
			Sys_FileRead (vcrFile, &len, sizeof(int));
			p = malloc(len);
			Sys_FileRead (vcrFile, p, len);
			com_argv[i+1] = p;
		}
		com_argc++; /* add one for arg[0] */
		parms->argc = com_argc;
		parms->argv = com_argv;
	}

	if ( (n = COM_CheckParm("-record")) != 0)
	{
		vcrFile = Sys_FileOpenWrite("quake.vcr");

		i = VCR_SIGNATURE;
		Sys_FileWrite(vcrFile, &i, sizeof(int));
		i = com_argc - 1;
		Sys_FileWrite(vcrFile, &i, sizeof(int));
		for (i = 1; i < com_argc; i++)
		{
			if (i == n)
			{
				len = 10;
				Sys_FileWrite(vcrFile, &len, sizeof(int));
				Sys_FileWrite(vcrFile, "-playback", len);
				continue;
			}
			len = strlen(com_argv[i]) + 1;
			Sys_FileWrite(vcrFile, &len, sizeof(int));
			Sys_FileWrite(vcrFile, com_argv[i], len);
		}
	}

}



// 2001-09-18 New cvar system by Maddes (Init)  start
void COM_Init_Cvars ();
void Con_Init_Cvars ();
//TW	void Key_Init_Cvars ();
void Mod_Init_Cvars();
void Chase_Init_Cvars ();
void SCR_Init_Cvars ();
void VID_Init_Cvars();
void V_Init_Cvars();
//TW	void M_Init_Cvars ();
void R_Init_Cvars ();
void R_Presets ();
//TW	void Sbar_Init_Cvars ();
void CL_Init_Cvars ();
void S_Init_Cvars ();
void IN_Init_Cvars ();
void NET_Init_Cvars ();
void Host_InitLocal_Cvars ();
void PR_Init_Cvars();
void Draw_Init_Cvars();
//TW	void CDAudio_Init_Cvars();
// 2001-09-18 New cvar system by Maddes (Init)  end



void	VID_SetPalette2 (unsigned char *palette)
{
	byte	*pal;
	unsigned r,g,b;
	unsigned v;
	int		r1,g1,b1;
	int		j,k,l,m,ind;
	unsigned short i;
	unsigned	*table;
	FILE *f;
	char s[255];
	float gamma = 0;
	Con_Printf ("Making 8to24 lookup tables.");
//
// 8 8 8 encoding
//
	pal = palette;
	table = d_8to24table;
	for (i=0 ; i<256 ; i++)
	{
		Con_Printf (".");	// loop an indicator
		r = pal[0];
		g = pal[1];
		b = pal[2];
		if (r>255) r = 255;
		if (g>255) g = 255;
		if (b>255) b = 255;
		pal += 3;
		v = (255<<24) + (r<<0) + (g<<8) + (b<<16);
		*table++ = v;
		
	}

	
	// The 15-bit table we use is actually made elsewhere (it's palmap)

	d_8to24table[255] &= 0xffffff;	// 255 is transparent
	d_8to24table[0] &= 0x000000;	// black is black


//		if (!d_8to24table)
//		Con_Printf ("FAILED!\n");
//	else
//		Con_Printf ("!\n");
}
#ifdef GLOBOT
void Bot_Init (void);
#endif
void ColormapForceLoad (void)
{
	loadedfile_t	*fileinfo;	// 2001-09-12 Returning information about loaded file by Maddes
			fileinfo = COM_LoadHunkFile ("gfx/colormap.lmp");
		if (!fileinfo)

			Sys_Error ("Couldn't load gfx/colormap.lmp");
		host_colormap = fileinfo->data;


};



int		translate_bsp;
int		translate_mdl;
int		translate_gfx;
int		translate_spr;
byte	colorthis;
void Palette_Init (void)
{
	loadedfile_t	*fileinfo;	// 2001-09-12 Returning information about loaded file by Maddes
	int		pre100;



	overbrights = 1;
	if (COM_CheckParm ("-beta")){
		pre100 = 1;
		}
#ifdef EGAHACK

		fileinfo = COM_LoadHunkFile ("gfx/palette.lmp");
		if (!fileinfo)
			Sys_Error ("Couldn't load gfx/palette.lmp");
		host_basepal = fileinfo->data;
		host_origpal = fileinfo->data;
		host_otherpal = fileinfo->data;

		host_basepal = (unsigned char *)ega_palette; // go EGA!

#else
	
		fileinfo = COM_LoadHunkFile ("gfx/palette.lmp");
		if (!fileinfo){
			// quake pre-rel doesn't have a palette.lmp
				// instead it's in the gfx.wad
				// we can determine that we're running 0.8 this way
		//	if (W_GetLumpName ("palette")){
		//			host_basepal = W_GetLumpName ("palette");
		//			pre100 = 1;
		//	}
			//	else
				Sys_Error ("Couldn't load gfx/palette.lmp");
		}
	//	if (!W_GetLumpName ("palette")){
				host_basepal = fileinfo->data;
				host_origpal = fileinfo->data;
	//	}
		fileinfo = COM_LoadHunkFile ("gfx/tranfrom.lmp");
		if (!fileinfo)
				host_otherpal = host_basepal;	// nothing happened let's move on.
		else
		host_otherpal = fileinfo->data;


		fileinfo = COM_LoadHunkFile ("gfx/tranto.lmp");
		if (!fileinfo)
				host_otherpal = host_basepal;	// nothing happened let's move on.
		else
		{
			host_basepal = fileinfo->data;
			fileinfo = COM_LoadHunkFile ("gfx/palette.lmp");
			host_otherpal = fileinfo->data;
		}
				


#endif
		if(pre100 == 1){
	/*	fileinfo = COM_LoadHunkFile ("gfx/colormap.lmp");
		if (!fileinfo)

			Sys_Error ("Couldn't load gfx/colormap.lmp");
			*/
		host_colormap = malloc(16384);
		
	
		host_fullbrights = 256-host_colormap[16384]; // leilei - variable our fullbright counts if available
		}
		else
		{
		fileinfo = COM_LoadHunkFile ("gfx/colormap.lmp");
		if (!fileinfo)

			Sys_Error ("Couldnf't load gfx/colormap.lmp");
		host_colormap = fileinfo->data;
		fileinfo = COM_LoadHunkFile ("gfx/colormap.lmp");
		host_colormap_red = fileinfo->data;
		fileinfo = COM_LoadHunkFile ("gfx/colormap.lmp");
		host_colormap_green = fileinfo->data;
		fileinfo = COM_LoadHunkFile ("gfx/colormap.lmp");
		host_colormap_blue = fileinfo->data;


		host_fullbrights = 256-host_colormap[16384]; // leilei - variable our fullbright counts if available
		}

		

	if (COM_CheckParm ("-glsuck")){
		overbrights = 0;
		fullbrights = 0;
		host_fullbrights = 255; 
		GrabColorMap();
	}

	if (COM_CheckParm ("-nooverbright")){
		overbrights = 0;
		GrabColorMap();
	}

	if (pre100 == 1){
		overbrights = 0;
		fullbrights = 32;	// pre 1.00 didn't have the number of brights stored. :(
		GrabColorMap();
	}


	if (COM_CheckParm ("-nofb")){
		host_fullbrights = 0; 
		fullbrights = 0;
		GrabColorMap();
	}
		if (host_basepal != host_otherpal){
		// Make a translation table for converting stuff that uses otherpal to our new pal
		translate_bsp = 1;
#ifdef EGAHACK
		InitRemap(host_origpal);
		//GrabColorMapEGA();
		TranslateColorMapEGA();
		
#else
		InitRemap(host_otherpal);
		GrabColorMap();
#endif
		}
	
	// Fog

		host_fogmap = malloc(16384);//fileinfo->data;


		GrabColorMapNoFB();

		

		MakeMy15to8(host_basepal);


}
void MassiveLookupTablesInit (void);
/*
====================
Host_Init
====================
*/
void Host_Init (quakeparms_t *parms)
{
	loadedfile_t	*fileinfo;	// 2001-09-12 Returning information about loaded file by Maddes

	if (standard_quake)
		minimum_memory = MINIMUM_MEMORY;
	else
		minimum_memory = MINIMUM_MEMORY_LEVELPAK;

	if (COM_CheckParm ("-minmemory"))
		parms->memsize = minimum_memory;

	host_parms = *parms;

	if (parms->memsize < minimum_memory)
		Sys_Error ("Only %4.1f megs of memory available, can't execute game", parms->memsize / (float)0x100000);

	com_argc = parms->argc;
	com_argv = parms->argv;

	Memory_Init (parms->membase, parms->memsize);
	Cvar_Init ();		// 2001-09-18 New cvar system by Maddes
	Cbuf_Init ();
	Cmd_Init ();
	
// 2001-09-18 New cvar system by Maddes (Init)  start
	COM_Init_Cvars ();				// initialize all filesystem related variables
	Con_Init_Cvars ();				// initialize all console related cvars
//TW	Key_Init_Cvars ();				// initialize all key related cvars
	Mod_Init_Cvars();				// initialize all model related cvars
	Chase_Init_Cvars ();			// initialize all chase camera related cvars
	SCR_Init_Cvars ();				// initialize all screen(?) related cvars
	VID_Init_Cvars();				// initialize all video related cvars
	V_Init_Cvars();					// initialize all view related cvars
//TW	M_Init_Cvars ();				// initialize all menu related cvars
	R_Init_Cvars ();				// initialize all rendering system related cvars
//TW	Sbar_Init_Cvars ();				// initialize all statusbar related cvars
	CL_Init_Cvars ();				// initialize all cl_* related cvars
	S_Init_Cvars ();				// initialize all sound system related cvars
	IN_Init_Cvars ();				// initialize all input related cvars
	NET_Init_Cvars ();				// initialize all net related cvars
	Host_InitLocal_Cvars ();		// initialize all local host related cvars
	PR_Init_Cvars();				// initialize all pr_* related cvars
	NVS_Init_Cvars ();				// 2000-04-30 NVS COMMON by Maddes
	NVS_Init_Server_Cvars ();		// 2000-04-30 NVS HANDSHAKE SRV<->QC/SRV<->CL by Maddes
	NVS_Init_Client_Cvars ();		// 2000-04-30 NVS HANDSHAKE SRV<->QC/SRV<->CL by Maddes
// 2001-09-18 New cvar system by Maddes (Init)  end
	


	V_Init ();
	Chase_Init ();
	Host_InitVCR (parms);
	COM_Init (parms->basedir);


	Host_InitLocal ();
//	W_LoadWadFileExtra ("extra.wad");
	W_LoadWadFile ("gfx.wad");

	Key_Init ();
	Con_Init ();
	M_Init ();
	PR_Init ();
	Mod_Init ();
#ifndef BENCH
	NET_Init ();
#endif
	SV_Init ();
#ifdef GLOBOT
		Bot_Init ();
#endif
	NVS_Init ();	// 2000-04-30 NVS COMMON by Maddes
	NVS_Init_Server ();		// 2000-04-30 NVS HANDSHAKE SRV<->QC/SRV<->CL by Maddes
	NVS_Init_Client ();		// 2000-04-30 NVS HANDSHAKE SRV<->QC/SRV<->CL by Maddes
//#ifndef	_WIN32
//	S_Init (); // moved sound init way back here. so we can see it in text
//#endif
	Con_Printf ("Exe: "__TIME__" "__DATE__"\n");
	Con_Printf ("%4.1f megabyte heap\n",parms->memsize/ (1024*1024.0));

	R_InitTextures ();		// needed even for dedicated servers

	if (cls.state != ca_dedicated)
	{

		Palette_Init();
#ifndef BENCH
	// don't do lookup stuff in benchmark
		MassiveLookupTablesInit();


		if (host_basepal != host_origpal){
		//	InitColorColormaps();
		//	GrabColorMap();
		//	InitRemap(host_origpal);
		}
#endif
// 2000-07-28 DOSQuake input init before video init fix by Norberto Alfredo Bensa  start
//#ifndef _WIN32 // on non win32, mouse comes before video for security reasons
#if !defined(_WIN32) && !defined(DOSQUAKE)	// on non dos/win32, mouse comes before video for security reasons
// 2000-07-28 DOSQuake input init before video init fix by Norberto Alfredo Bensa  end
		IN_Init ();
#endif
		VID_Init (host_basepal);

		Draw_Init_Cvars();	// 2001-09-18 New cvar system by Maddes (Init)
		Draw_Init ();
//		RemapMenuMap();
		SCR_Init ();
		R_Init ();
#ifndef	_WIN32
	// on Win32, sound initialization has to come before video initialization, so we
	// can put up a popup if the sound hardware is in use
		S_Init ();
#else

#ifdef	GLQUAKE
	// FIXME: doesn't use the new one-window approach yet
	//	S_Init ();
#endif

#endif	// _WIN32
//TW		CDAudio_Init_Cvars();	// 2001-09-18 New cvar system by Maddes (Init)
#ifndef BENCH
		CDAudio_Init ();

		Sbar_Init ();
#endif
		CL_Init ();
// 2000-07-28 DOSQuake input init before video init fix by Norberto Alfredo Bensa  start
//#ifdef _WIN32 // on non win32, mouse comes before video for security reasons
#if defined(_WIN32) || defined(DOSQUAKE)	// on non dos/win32, mouse comes before video for security reasons
// 2000-07-28 DOSQuake input init before video init fix by Norberto Alfredo Bensa  end
		IN_Init ();
#endif
	}

	Cbuf_InsertText ("exec config.rc\n");	// 2001-09-18 New cvar system by Maddes
											// this creates all missing variables
											// some of them will be updated by config.cfg executed in quake.rc
											// this way you can use a non-set-compatible engine without loosing your new cvars
#ifdef BENCH
	Cbuf_InsertText ("exec bench.rc\n");
#else
	Cbuf_InsertText ("exec quake.rc\n");
#endif

	Hunk_AllocName (0, "-HOST_HUNKLEVEL-");
	host_hunklevel = Hunk_LowMark ();
	R_Presets();
	host_initialized = true;

	

	Sys_Printf ("========Quake Initialized=========\n");

}


/*
===============
Host_Shutdown

FIXME: this is a callback from Sys_Quit and Sys_Error.  It would be better
to run quit through here before the final handoff to the sys code.
===============
*/
void Host_Shutdown(void)
{
	static qboolean isdown = false;

	if (isdown)
	{
		printf ("recursive shutdown\n");
		return;
	}
	isdown = true;

// keep Con_Printf from trying to update the screen
	scr_disabled_for_loading = true;
#ifndef BENCH
	Host_WriteConfiguration ();
#endif
	CDAudio_Shutdown ();
	NET_Shutdown ();
	S_Shutdown();
	IN_Shutdown ();

	if (cls.state != ca_dedicated)
	{
		VID_Shutdown();
	}
}
