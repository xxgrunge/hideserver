/*
 * =================================================================
 * Filename:             hideserver.c
 * Description:          Hide certain or all servers from /map & /links.
 * Written by:           AngryWolf <angrywolf@flashmail.com>
 * Documentation:        hideserver.txt (comes with the package)
 * =================================================================
 */

#include "config.h"
#include "struct.h"
#include "common.h"
#include "sys.h"
#include "numeric.h"
#include "msg.h"
#include "channel.h"
#include <time.h>
#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <io.h>
#endif
#include <fcntl.h>
#include "h.h"
#ifdef STRIPBADWORDS
#include "badwords.h"
#endif
#ifdef _WIN32
#include "version.h"
#endif

extern void			sendto_one(aClient *to, char *pattern, ...);
extern char			*my_itoa(int i);

#define DelOverride(cmd, ovr)	if (ovr && CommandExists(cmd)) CmdoverrideDel(ovr); ovr = NULL
#define DelHook(x)      	if (x) HookDel(x); x = NULL
#define ircstrdup(x,y)		if (x) MyFree(x); if (!y) x = NULL; else x = strdup(y)
#define ircfree(x)		if (x) MyFree(x); x = NULL

Cmdoverride			*AddOverride(char *msg, iFP cb);
static int			override_map(Cmdoverride *, aClient *, aClient *, int, char *[]);
static int			override_links(Cmdoverride *, aClient *, aClient *, int, char *[]);
static int			cb_test(ConfigFile *, ConfigEntry *, int, int *);
static int			cb_conf(ConfigFile *, ConfigEntry *, int);
static int			cb_rehash();
/* static int			cb_stats(aClient *sptr, char *stats); */

Hook				*HookConfTest, *HookConfRun;
Hook				*HookConfRehash;
Cmdoverride			*OvrMap, *OvrLinks;
ConfigItem_ulines		*HiddenServers;

static struct
{
	unsigned	disable_map : 1;
	unsigned	disable_links : 1;
	char		*map_deny_message;
	char		*links_deny_message;
} Settings;

#ifndef STATIC_LINKING
static ModuleInfo	*MyModInfo;
 #define MyMod		MyModInfo->handle
 #define SAVE_MODINFO	MyModInfo = modinfo;
#else
 #define MyMod		NULL
 #define SAVE_MODINFO
#endif

ModuleHeader MOD_HEADER(hideserver)
  = {
	"hideserver",
	"$Id: hideserver.c,v 4.8 2004/04/05 07:25:04 angrywolf Exp $",
	"Hide servers from /map & /links",
	"3.2-b8-1",
	NULL 
    };

static void InitConf()
{
	memset(&Settings, 0, sizeof Settings);
}

static void FreeConf()
{
	ConfigItem_ulines	*h;
	ListStruct		*next;

	ircfree(Settings.map_deny_message);
	ircfree(Settings.links_deny_message);

	for (h = HiddenServers; h; h = (ConfigItem_ulines *) next)
	{
		next = (ListStruct *) h->next;
		DelListItem(h, HiddenServers);
		MyFree(h->servername);
		MyFree(h);
	}
}

DLLFUNC int MOD_TEST(hideserver)(ModuleInfo *modinfo)
{
	SAVE_MODINFO
	HookConfTest	= HookAddEx(modinfo->handle, HOOKTYPE_CONFIGTEST, cb_test);

	return MOD_SUCCESS;
}

DLLFUNC int MOD_INIT(hideserver)(ModuleInfo *modinfo)
{
	SAVE_MODINFO
	HiddenServers = NULL;
	InitConf();

	HookConfRun	= HookAddEx(modinfo->handle, HOOKTYPE_CONFIGRUN, cb_conf);
	HookConfRehash	= HookAddEx(modinfo->handle, HOOKTYPE_REHASH, cb_rehash);

        return MOD_SUCCESS;
}

DLLFUNC int MOD_LOAD(hideserver)(int module_load)
{
	int ret = MOD_SUCCESS;

	OvrMap		= AddOverride("map", override_map);
	OvrLinks	= AddOverride("links", override_links);

	if (!OvrMap || !OvrLinks)
		ret = MOD_FAILED;

	return ret;
}

DLLFUNC int MOD_UNLOAD(hideserver)(int module_unload)
{
	FreeConf();

	DelOverride("map", OvrMap);
	DelOverride("links", OvrLinks);

	DelHook(HookConfRehash);
	DelHook(HookConfRun);
	DelHook(HookConfTest);

	return MOD_SUCCESS;
}

static int cb_rehash()
{
	FreeConf();
	InitConf();

	return 1;
}

static int cb_test(ConfigFile *cf, ConfigEntry *ce, int type, int *errs)
{
	ConfigEntry	*cep, *cepp;
	int		errors = 0;

	if (type == CONFIG_MAIN)
	{
		if (!strcmp(ce->ce_varname, "hideserver"))
		{
			for (cep = ce->ce_entries; cep; cep = cep->ce_next)
			{
				if (!strcmp(cep->ce_varname, "hide"))
				{
					for (cepp = cep->ce_entries; cepp; cepp = cepp->ce_next)
					{
						if (!cepp->ce_varname)
						{
							config_error("%s:%i: blank hideserver::hide item",
								cepp->ce_fileptr->cf_filename,
								cepp->ce_varlinenum);
							errors++;
						}
					}
				}
				else if (!cep->ce_varname)
				{
					config_error("%s:%i: blank %s item",
						cep->ce_fileptr->cf_filename,
						cep->ce_varlinenum, ce->ce_varname);
					errors++;
					continue;
				}
				else if (!cep->ce_vardata)
				{
					config_error("%s:%i: %s::%s without value",
						cep->ce_fileptr->cf_filename,
						cep->ce_varlinenum,
						ce->ce_varname, cep->ce_varname);
					errors++;
					continue;
				}
				else if (!strcmp(cep->ce_varname, "disable-map"))
					;
				else if (!strcmp(cep->ce_varname, "disable-links"))
					;
				else if (!strcmp(cep->ce_varname, "map-deny-message"))
					;
				else if (!strcmp(cep->ce_varname, "links-deny-message"))
					;
				else
				{
					config_error("%s:%i: unknown directive hideserver::%s",
						cep->ce_fileptr->cf_filename, cep->ce_varlinenum, cep->ce_varname);
					errors++;
				}
			}
			*errs = errors;
			return errors ? -1 : 1;
		}
	}

	return 0;
}

static int cb_conf(ConfigFile *cf, ConfigEntry *ce, int type)
{
	ConfigEntry		*cep, *cepp;
	ConfigItem_ulines	*ca;

	if (type == CONFIG_MAIN)
	{
		if (!strcmp(ce->ce_varname, "hideserver"))
		{
			for (cep = ce->ce_entries; cep; cep = cep->ce_next)
			{
				if (!strcmp(cep->ce_varname, "disable-map"))
					Settings.disable_map = config_checkval(cep->ce_vardata, CFG_YESNO);
				else if (!strcmp(cep->ce_varname, "disable-links"))
					Settings.disable_links = config_checkval(cep->ce_vardata, CFG_YESNO);
				else if (!strcmp(cep->ce_varname, "map-deny-message"))
				{
					ircstrdup(Settings.map_deny_message, cep->ce_vardata);
				}
				else if (!strcmp(cep->ce_varname, "links-deny-message"))
				{
					ircstrdup(Settings.links_deny_message, cep->ce_vardata);
				}
				else if (!strcmp(cep->ce_varname, "hide"))
				{
					for (cepp = cep->ce_entries; cepp; cepp = cepp->ce_next)
					{
						if (!strcasecmp(cepp->ce_varname, me.name))
							continue;

						ca = MyMallocEx(sizeof(ConfigItem_ulines));
						ircstrdup(ca->servername, cepp->ce_varname);
						AddListItem(ca, HiddenServers);
					}
				}
			}

			return 1;
		}
	}

	return 0;
}

/*
 * static int cb_stats(aClient *sptr, char *stats)
 * {
 * 	if (*stats == 'S')
 * 	{
 * 		sendto_one(sptr, ":%s %i %s :disable-map: %d",
 * 			me.name, RPL_TEXT, sptr->name, Settings.disable_map);
 * 		sendto_one(sptr, ":%s %i %s :disable-links: %d",
 * 			me.name, RPL_TEXT, sptr->name, Settings.disable_links);
 * 		sendto_one(sptr, ":%s %i %s :map-deny-message: %s",
 * 			me.name, RPL_TEXT, sptr->name, Settings.map_deny_message ? Settings.map_deny_message : "<none>");
 * 		sendto_one(sptr, ":%s %i %s :links-deny-message: %s",
 * 			me.name, RPL_TEXT, sptr->name, Settings.links_deny_message ? Settings.links_deny_message : "<none>");
 * 	}
 *         return 0;
 * }
 */

Cmdoverride *AddOverride(char *msg, iFP cb)
{
	Cmdoverride *ovr = CmdoverrideAdd(MyMod, msg, cb);

#ifndef STATIC_LINKING
        if (ModuleGetError(MyMod) != MODERR_NOERROR || !ovr)
#else
        if (!ovr)
#endif
	{
#ifndef STATIC_LINKING
		config_error("Error replacing command %s when loading module %s: %s",
			msg, MOD_HEADER(hideserver).name, ModuleGetErrorStr(MyMod));
#else
		config_error("Error replacing command %s when loading module %s",
			msg, MOD_HEADER(hideserver).name);
#endif
		return NULL;
	}

	return ovr;
}

ConfigItem_ulines *FindHiddenServer(char *servername)
{
	ConfigItem_ulines *h;

	for (h = HiddenServers; h; h = (ConfigItem_ulines *) h->next)
		if (!strcasecmp(servername, h->servername))
			break;

	return h;
}

/*
 * New /MAP format -Potvin
 * dump_map function.
 * Rewritten by AngryWolf
 */

void my_dump_map(aClient *cptr, aClient *server, int prompt_length, int length)
{
	static char	prompt[64];
	char		*p = &prompt[prompt_length];
	int		cnt = 0;
	aClient		*acptr;
	Link		*lp;

	*p = '\0';

	if (prompt_length > 60)
		sendto_one(cptr, rpl_str(RPL_MAPMORE), me.name, cptr->name,
		    prompt, server->name);
	else
	{
		sendto_one(cptr, rpl_str(RPL_MAP), me.name, cptr->name, prompt,
		    length, server->name, server->serv->users,
		    (server->serv->numeric ? (char *)my_itoa(server->serv->
		    numeric) : ""));
		cnt = 0;
	}

	if (prompt_length > 0)
	{
		p[-1] = ' ';
		if (p[-2] == '`')
			p[-2] = ' ';
	}
	if (prompt_length > 60)
		return;

	strcpy(p, "|-");

	for (lp = Servers; lp; lp = lp->next)
	{
		acptr = lp->value.cptr;
		if (acptr->srvptr != server || !(acptr->flags & FLAGS_MAP))
			continue;
		cnt++;
	}

	for (lp = Servers; lp; lp = lp->next)
	{
		acptr = lp->value.cptr;
		if (acptr->srvptr != server || !(acptr->flags & FLAGS_MAP))
			continue;
		if (--cnt == 0)
			*p = '`';
		my_dump_map(cptr, acptr, prompt_length + 2, length - 2);

	}

	if (prompt_length > 0)
		p[-1] = '-';
}

/*
** New /MAP format. -Potvin
** m_map (NEW)
** Rewritten by AngryWolf
**
**      parv[0] = sender prefix
**      parv[1] = server mask
**/

static int override_map(Cmdoverride *ovr, aClient *cptr, aClient *sptr, int parc, char *parv[])
{
	Link		*lp;
	aClient		*acptr, *uplink;
	u_int		longest = strlen(me.name);
	int		hide_ulines = (HIDE_ULINES && !IsAnOper(sptr));

	if (!IsClient(sptr))
		return CallCmdoverride(ovr, cptr, sptr, parc, parv);

	if (!IsAnOper(sptr) && Settings.disable_map)
	{
		if (Settings.map_deny_message)
			sendto_one(sptr, ":%s %s %s :%s",
				me.name, IsWebTV(sptr) ? "PRIVMSG" : "NOTICE",
				sptr->name, Settings.map_deny_message);
		else
			sendto_one(sptr, rpl_str(RPL_MAPEND), me.name, parv[0]);

		return 0;
	}

	if (parc < 2 || BadPtr(parv[1]))
		parv[1] = "*";
	for (lp = Servers; lp; lp = lp->next)
		lp->value.cptr->flags &= ~FLAGS_MAP;
	me.flags |= FLAGS_MAP;
	for (lp = Servers; lp; lp = lp->next)
	{
		acptr = lp->value.cptr;
		if (IsMe(acptr) || _match(parv[1], acptr->name))
			continue;
		for (uplink = acptr; uplink && uplink != &me; uplink = uplink->srvptr)
		{
			if (hide_ulines && IsULine(uplink))
				break;
			if (!IsAnOper(sptr) && FindHiddenServer(uplink->name))
				break;
			if (uplink->flags & FLAGS_MAP)
				break;
			uplink->flags |= FLAGS_MAP;
			if ((strlen(uplink->name) + uplink->hopcount * 2) > longest)
				longest = strlen(uplink->name) + uplink->hopcount * 2;
		}
	}
	if (longest > 60)
		longest = 60;
	longest += 2;
	my_dump_map(sptr, &me, 0, longest);
	sendto_one(sptr, rpl_str(RPL_MAPEND), me.name, parv[0]);

	return 0;
}

/*
** m_links
**	parv[0] = sender prefix
** or
**	parv[0] = sender prefix
**
** Recoded by Stskeeps
** Modified by AngryWolf
*/

static int override_links(Cmdoverride *ovr, aClient *cptr, aClient *sptr, int parc, char *parv[])
{
	Link		*lp;
	aClient		*acptr;

	if (!IsClient(sptr))
		return CallCmdoverride(ovr, cptr, sptr, parc, parv);

	if (!IsAnOper(sptr) && Settings.disable_links)
	{
		if (Settings.links_deny_message)
			sendto_one(sptr, ":%s %s %s :%s",
				me.name, IsWebTV(sptr) ? "PRIVMSG" : "NOTICE",
				sptr->name, Settings.links_deny_message);
		else
			sendto_one(sptr, rpl_str(RPL_ENDOFLINKS), me.name, parv[0], "*");

		return 0;
	}

	for (lp = Servers; lp; lp = lp->next)
	{
		acptr = lp->value.cptr;

		/* Some checks */
		if (!IsAnOper(sptr))
		{
			if (HIDE_ULINES && IsULine(acptr))
				continue;
			if (FindHiddenServer(acptr->name))
				continue;
		}
		sendto_one(sptr, rpl_str(RPL_LINKS),
		    me.name, parv[0], acptr->name, acptr->serv->up,
		    acptr->hopcount, (acptr->info[0] ? acptr->info :
		    "(Unknown Location)"));
	}

	sendto_one(sptr, rpl_str(RPL_ENDOFLINKS), me.name, parv[0], "*");
	return 0;
}
