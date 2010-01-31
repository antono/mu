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

#include "config.h"

#include <errno.h>
#include <string.h>
#include <stdio.h>

#include "mu-util.h"
#include "mu-util-xapian.h"

#include "mu-msg-gmime.h"

#include "mu-index.h"
#include "mu-cmd-index.h"

static gboolean MU_CAUGHT_SIGNAL;

static void
update_warning (void)
{
	g_warning ("the database needs to be updated to version %s",
		   MU_XAPIAN_DB_VERSION);
	g_message ("please run 'mu index --empty' (see the manpage)");
}

static void
sig_handler (int sig)
{
	if (!MU_CAUGHT_SIGNAL && sig == SIGINT) /* Ctrl-C */
		g_message ("Shutting down gracefully, "
			   "press again to kill immediately");
	
        MU_CAUGHT_SIGNAL = TRUE;
}

static void
install_sig_handler (void)
{
        struct sigaction action;
        int i, sigs[] = { SIGINT, SIGHUP, SIGTERM };
	
        MU_CAUGHT_SIGNAL = FALSE;

        action.sa_handler = sig_handler;
        sigemptyset(&action.sa_mask);
        action.sa_flags = SA_RESETHAND;

        for (i = 0; i != G_N_ELEMENTS(sigs); ++i)
                if (sigaction (sigs[i], &action, NULL) != 0)
                        g_warning ("error: set sigaction for %d failed: %s",
				   sigs[i], strerror (errno));;
}


static gboolean
check_index_params (MuConfigOptions *opts)
{
	if (opts->linksdir || opts->xquery) {
		g_warning ("Invalid option(s) for command");
		return FALSE;
	}
	
	if (!mu_util_check_dir (opts->maildir, TRUE, TRUE)) {
		g_message ("Please provide a valid Maildir");
		return FALSE;
	}
	
	return TRUE;
}


static MuResult
index_msg_silent_cb  (MuIndexStats* stats, void *user_data)
{
	return MU_CAUGHT_SIGNAL ? MU_STOP: MU_OK;
}


static MuResult
index_msg_cb  (MuIndexStats* stats, void *user_data)
{
	char *kars="-\\|/";
	char output[314];
	
	static int i = 0;
	static int len = 0;

	while (len --> 0) /* note the --> operator :-) */
		printf ("\b");
	
	len = snprintf (output, sizeof(output),
			"%c processing mail; processed: %d; "
			"updated/new: %d, cleaned-up: %d",
			kars[i % 4], stats->_processed,
			stats->_updated, stats->_cleaned_up);
	g_print ("%s", output);
	++i;
	
	return MU_CAUGHT_SIGNAL ? MU_STOP: MU_OK;
}



static gboolean
database_version_check_and_update (MuConfigOptions *opts)
{
	if (mu_util_xapian_db_is_empty (opts->xpath))
		return TRUE;
	
	/* we empty the database before doing anything */
	if (opts->empty) {
		opts->reindex = TRUE;
		g_message ("Emptying database %s", opts->xpath);
		return mu_util_xapian_clear_database (opts->xpath);
	}

	if (mu_util_xapian_db_version_up_to_date (opts->xpath))
		return TRUE; /* ok, nothing to do */
	
	/* ok, database is not up to date */
	if (opts->autoupgrade) {
		opts->reindex = TRUE;
		g_message ("Auto-upgrade: clearing old database first");
		return mu_util_xapian_clear_database (opts->xpath);
	}

	update_warning ();
	return FALSE;
}

gboolean
mu_cmd_cleanup (MuConfigOptions *opts)
{
	int rv;	
	MuIndex *midx;
	MuIndexStats stats;

	g_return_val_if_fail (opts, FALSE);
	
	if (!check_index_params (opts))
		return FALSE;

	install_sig_handler ();
	
	midx = mu_index_new (opts->xpath);
	if (!midx) {
		g_warning ("Cleanup failed");
		return FALSE;
	}
	
	g_message ("Cleaning up removed messages from %s",
		   opts->xpath);
	
	mu_index_stats_clear (&stats);
	rv = mu_index_cleanup (midx, &stats,
			       opts->quiet ? index_msg_silent_cb : index_msg_cb,
			       NULL);
	mu_index_destroy (midx);

	if (!opts->quiet)
		g_print ("\n");

	if (rv == MU_OK || rv == MU_STOP)
		return TRUE;
	else
		return FALSE;
}


gboolean
mu_cmd_index (MuConfigOptions *opts)
{
	int rv;

	g_return_val_if_fail (opts, FALSE);
	
	if (!check_index_params (opts))
		return FALSE;
	
	if (!database_version_check_and_update(opts))
		return FALSE;

	install_sig_handler ();
	
	mu_msg_gmime_init ();
	{
		MuIndex *midx;
		MuIndexStats stats;
		
		mu_index_stats_clear (&stats);
		midx = mu_index_new (opts->xpath);
		
		if (!midx) {
			g_warning ("Indexing failed");
			return FALSE;
		} 

		g_message ("Indexing messages from %s", opts->maildir);
		g_message ("Database: %s", opts->xpath);
		
		rv = mu_index_run (midx, opts->maildir,
				   opts->reindex, &stats,
				   opts->quiet ?
				        index_msg_silent_cb :index_msg_cb,
				   NULL, NULL);
		if (!opts->nocleanup && !MU_CAUGHT_SIGNAL) {
			stats._processed = 0; /* start over */
			if (!opts->quiet)
				g_print ("\n");
			g_message ("Cleaning up missing messages");
			mu_index_cleanup (midx, &stats,
					  opts->quiet ?
					       index_msg_silent_cb : index_msg_cb,
					  NULL);
		}

		if (!opts->quiet) {
			index_msg_cb (&stats, NULL);
			g_print ("\n");
		}
		
		MU_WRITE_LOG ("processed: %d; updated/new: %d, "
			      "cleaned-up: %d",
			      stats._processed, stats._updated,
			      stats._cleaned_up);
		
		mu_index_destroy (midx);
	}
	mu_msg_gmime_uninit ();
	
	return rv == MU_OK ? TRUE : FALSE;
}
