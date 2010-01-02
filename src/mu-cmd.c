/*
** Copyright (C) 2010 Dirk-Jan C. Binnema <djcb@djcbsoftware.nl>
**
** This program is free software; you can redistribute it and/or modify it
** under the terms of the GNU General Public License as published by the
** Free Software Foundation; either version 3, or (at your option) any
** later version.
**
** This program is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
** GNU General Public License for more details.
**
** You should have received a copy of the GNU General Public License
** along with this program; if not, write to the Free Software Foundation,
** Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
**
*/

#include <config.h>

#include <unistd.h>
#include <stdio.h>
#include <string.h>

#include "mu-msg-gmime.h"
#include "mu-maildir.h"
#include "mu-index.h"
#include "mu-query-xapian.h"
#include "mu-msg-xapian.h"
#include "mu-msg-str.h"
#include "mu-cmd.h"

static MuCmd 
_cmd_from_string (const char* cmd)
{
	if (!cmd)
		return MU_CMD_UNKNOWN;

	if (strcmp (cmd, "index") == 0)
		return MU_CMD_INDEX;

	/* support some synonyms... */
	if ((strcmp (cmd, "query") == 0) ||
	    (strcmp (cmd, "find")  == 0) ||
	    (strcmp (cmd, "search") == 0))
		return MU_CMD_QUERY;

	if ((strcmp (cmd, "mkmdir") == 0) ||
	    (strcmp (cmd, "mkdir") == 0)) 
		return MU_CMD_MKDIR;

	if (strcmp (cmd, "link") == 0)
		return MU_CMD_LINK;
	
	if ((strcmp (cmd, "help") == 0) ||
	    (strcmp (cmd, "info") == 0))
		return MU_CMD_HELP;
	
	return MU_CMD_UNKNOWN;
}


static gboolean
_print_query (MuQueryXapian *xapian, const gchar *query)
{
	char *querystr;
	
	querystr = mu_query_xapian_as_string (xapian, query);
	g_print ("%s\n", querystr);
	g_free (querystr);

	return TRUE;
}


static const gchar*
_display_field (MuMsgXapian *row, const MuMsgField* field)
{
	gint64 val;

	switch (mu_msg_field_type(field)) {
	case MU_MSG_FIELD_TYPE_STRING:
		return mu_msg_xapian_get_field (row, field);

	case MU_MSG_FIELD_TYPE_INT:
	
		if (mu_msg_field_id(field) == MU_MSG_FIELD_ID_PRIORITY) {
			val = mu_msg_xapian_get_field_numeric (row, field);
			return mu_msg_str_prio ((MuMsgPriority)val);
		}
		
		if (mu_msg_field_id(field) == MU_MSG_FIELD_ID_FLAGS) {
			val = mu_msg_xapian_get_field_numeric (row, field);
			return mu_msg_str_flags_s ((MuMsgPriority)val);
		}

		return mu_msg_xapian_get_field (row, field); /* as string */
	case MU_MSG_FIELD_TYPE_TIME_T: 
		val = mu_msg_xapian_get_field_numeric (row, field);
		return mu_msg_str_date_s ((time_t)val);
	case MU_MSG_FIELD_TYPE_BYTESIZE: 
		val = mu_msg_xapian_get_field_numeric (row, field);
		return mu_msg_str_size_s ((time_t)val);
	default:
		g_return_val_if_reached (NULL);
	}
}


/* returns NULL if there is an error */
const MuMsgField*
_sort_field_from_string (const char* fieldstr)
{
	const MuMsgField *field;
		
	field = mu_msg_field_from_name (fieldstr);
	if (!field && strlen(fieldstr) == 1)
		field = mu_msg_field_from_shortcut(fieldstr[0]);
	if (!field)
		g_printerr ("not a valid sort field: '%s'\n",
			    fieldstr);
	return field;
}




static gboolean
_print_rows (MuQueryXapian *xapian, const gchar *query, MuConfigOptions *opts)
{
	MuMsgXapian *row;
	const MuMsgField *sortfield;

	sortfield = NULL;
	if (opts->sortfield) {
		sortfield = _sort_field_from_string (opts->sortfield);
		if (!sortfield) /* error occured? */
			return FALSE;
	}
	
	row = mu_query_xapian_run (xapian, query, sortfield,
				   !opts->descending);
	if (!row) {
		g_printerr ("error: running query failed\n");
		return FALSE;
	}

	/* iterate over the found rows */
	while (!mu_msg_xapian_is_done (row)) {
	 	const char*	fields		= opts->fields;
		int		printlen	= 0;
		while (*fields) {
			const MuMsgField* field = 
				mu_msg_field_from_shortcut (*fields);
			if (!field || 
			    !mu_msg_field_is_xapian_enabled (field)) 
				printlen += printf ("%c", *fields);
			else
				printlen += printf ("%s",
						    _display_field(row, field));
			++fields;
		}	
		if (printlen >0)
			printf ("\n");
		
		mu_msg_xapian_next (row);
	}
	
	mu_msg_xapian_destroy (row);
	return TRUE;
}


static gboolean
_do_output_text (MuQueryXapian *xapian, MuConfigOptions* opts,
		 const gchar **params)
{
	gchar *query;
	gboolean retval = TRUE;
	
	query = mu_query_xapian_combine (params, FALSE);
	
	/* if xquery is set, we print the xapian query instead of the
	 * output; this is for debugging purposes */
	if (opts->xquery) 
		retval = _print_query (xapian, query);
	else
		retval = _print_rows (xapian, query, opts);
	
	g_free (query);

	return retval;
}

static gboolean
_create_linkdir_if_nonexistant (const gchar* linkdir)
{
	if (access (linkdir, F_OK) != 0)
		if (!mu_maildir_mkmdir (linkdir, 0700, TRUE)) 
			return FALSE;
	
	return TRUE;
}

static gboolean
_do_output_links (MuQueryXapian *xapian, MuConfigOptions* opts,
		  const gchar **params)
{
	gchar *query;
	gboolean retval = TRUE;
	MuMsgXapian *row;
	const MuMsgField *pathfield;

	if (!_create_linkdir_if_nonexistant (opts->linksdir))
		return FALSE;
	
	query = mu_query_xapian_combine (params, FALSE);
	row = mu_query_xapian_run (xapian, query, NULL, FALSE);
	if (!row) {
		g_printerr ("error: running query failed\n");
		return FALSE;
	}
	
	pathfield = mu_msg_field_from_id (MU_MSG_FIELD_ID_PATH);

	/* iterate over the found rows */
	while (!mu_msg_xapian_is_done (row)) {
		const char *path;
		path = mu_msg_xapian_get_field (row, pathfield);
		if (path) {
			retval = mu_maildir_link (path, opts->linksdir);
			if (!retval)
				break;
		}
		mu_msg_xapian_next (row);
	}
	
	mu_msg_xapian_destroy (row);
	g_free (query);

	return retval;
}


static gboolean
_check_query_params (MuConfigOptions *opts)
{
	if (opts->linksdir) 
		if (opts->xquery) {
			g_warning ("Invalid option for '--linksdir'");
			return FALSE;
		}
	
	if (opts->xquery) 
		if (opts->fields || opts->sortfield) {
			g_warning ("Invalid option for '--xquery'");
			return FALSE;
		}
		
	if (!opts->params[0] || !opts->params[1]) {
		g_warning ("Missing search expression");
		return FALSE;
	}
	
	return TRUE;
}



gboolean
_cmd_query (MuConfigOptions *opts)
{
	MuQueryXapian *xapian;
	gboolean rv;
	const gchar **params;
		
	if (!_check_query_params (opts))
		return FALSE;

	/* first param is 'query', search params are after that */
	params = (const gchar**)&opts->params[1];

	mu_msg_gmime_init();
	
	xapian = mu_query_xapian_new (opts->muhome);
	if (!xapian) {
		mu_msg_gmime_uninit ();
		return FALSE;
	}

	if (opts->linksdir)
		rv = _do_output_links (xapian, opts, params);
	else
		rv = _do_output_text (xapian, opts, params);
	
	mu_query_xapian_destroy (xapian);
	mu_msg_gmime_uninit();
	
	return rv;
}



static gboolean
_check_index_params (MuConfigOptions *opts)
{
	if (opts->linksdir) 
		if (opts->linksdir  || opts->fields ||
		    opts->sortfield || opts->xquery ||
		    opts->descending|| opts->xquery) {
			g_warning ("Invalid option(s) for command");
			return FALSE;
		}
	
	return TRUE;
}
	

static MuResult
_msg_cb  (MuIndexStats* stats, void *user_data)
{
	char *kars="-\\|/";
	char output[100];
	
	static int i = 0;
	static int len = 0;

	while (len --> 0) 
		printf ("\b");
	
	len = snprintf (output, sizeof(output),
			"%c mu is indexing your mails; processed: %d; "
			"updated/new: %d",
			kars[i % 4], stats->_processed, stats->_updated);
	g_print ("%s", output);
	
	++i;
	
	return MU_OK;
}


static gboolean
_cmd_index (MuConfigOptions *opts)
{
	MuIndex *midx;
	MuIndexStats stats;
	int rv;

	if (!_check_index_params (opts))
		return FALSE;

	mu_msg_gmime_init ();
	{
		midx = mu_index_new (opts->muhome);
		rv = mu_index_run (midx,
				   opts->maildir,
				   opts->reindex,
				   &stats,
				   opts->quiet ? NULL : _msg_cb,
				   NULL,
				   NULL);
		g_print ("\n");
		mu_index_destroy (midx);
	}
	mu_msg_gmime_uninit ();
	
	return rv == MU_OK ? TRUE : FALSE;
}


static int
_cmd_mkdir (MuConfigOptions *opts)
{
	int i;
	
	if (!opts->params[0])
		return FALSE;  /* shouldn't happen */
 	
	if (!opts->params[1]) {
		g_printerr ("usage: mu mkdir <dir> [more dirs]\n");
		return FALSE;
	}
	
	i = 1;
	while (opts->params[i]) {
		if (!mu_maildir_mkmdir (opts->params[i], 0755, FALSE))
			return FALSE;
		++i;
	}

	return TRUE;
}



static gboolean
_cmd_link (MuConfigOptions *opts)
{
	if (!opts->params[0])
		return FALSE;  /* shouldn't happen */
 	
	if (!opts->params[1] || !opts->params[2]) {
		g_printerr ("usage: mu link <src> <targetdir>\n");
		return FALSE;
	}

	return mu_maildir_link (opts->params[1], opts->params[2]);
}



static gboolean
_show_usage (gboolean noerror)
{
	const char* usage=
		"usage: mu [options] command [parameters]\n"
		"\twhere command is one of index, query, help\n"
		"see mu(1) for for information\n";

	if (noerror)
		g_print ("%s", usage);
	else
		g_printerr ("%s", usage);

	return noerror;
}

static gboolean
_show_version (void)
{
	const char* msg =
		"mu (mail indexer / searcher version) " VERSION "\n\n"
		"Copyright (C) 2010 Dirk-Jan C. Binnema\n"
		"License GPLv3+: GNU GPL version 3 or later "
		"<http://gnu.org/licenses/gpl.html>.\n\n"
		"This is free software: you are free to change "
		"and redistribute it.\n"
		"There is NO WARRANTY, to the extent permitted by law.";

	g_print ("%s\n", msg);

	return TRUE;
}


static gboolean
_cmd_help (MuConfigOptions *opts)
{
	/* FIXME: get context-sensitive help */
	_show_version ();
	return _show_usage (FALSE);
}



gboolean
mu_cmd_execute (MuConfigOptions *opts)
{
	MuCmd cmd;
	
	if (opts->version)
		return _show_version ();
	
	if (!opts->params||!opts->params[0]) /* no command? */
		return _show_usage (FALSE);
	
	cmd = _cmd_from_string (opts->params[0]);

	switch (cmd) {
	case MU_CMD_UNKNOWN: return _show_usage (FALSE);
	case MU_CMD_HELP:    return _cmd_help  (opts);
	case MU_CMD_MKDIR:   return _cmd_mkdir (opts);
	case MU_CMD_LINK:    return _cmd_link  (opts);
	case MU_CMD_INDEX:   return _cmd_index (opts);
	case MU_CMD_QUERY:   return _cmd_query (opts);
	default:
		g_return_val_if_reached (FALSE);
	}	
}