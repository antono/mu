/*
** Copyright (C) 2008-2011 Dirk-Jan C. Binnema <djcb@djcbsoftware.nl>
**
** This program is free software; you can redistribute it and/or modify
** it under the terms of the GNU General Public License as published by
** the Free Software Foundation; either version 3 of the License, or
** (at your option) any later version.
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

#include <stdlib.h>
#include <iostream>
#include <string.h>
#include <errno.h>
#include <xapian.h>

#include "mu-util.h"
#include "mu-msg-fields.h"
#include "mu-msg-db.h"

struct _MuMsgDb {
	_MuMsgDb (const Xapian::Document& doc) : _doc (doc) {}
	const Xapian::Document doc() const { return _doc; }
private:	
	const Xapian::Document& _doc; 
	
};


MuMsgDb*
mu_msg_db_new (const XapianDocument *doc, GError **err)
{
	g_return_val_if_fail (doc, NULL);
	
	try {
		MuMsgDb *db = new MuMsgDb ((const Xapian::Document&)*doc);
		return db;
			
	} MU_XAPIAN_CATCH_BLOCK_G_ERROR_RETURN(err, MU_ERROR_XAPIAN, NULL);

	return FALSE;
}

void
mu_msg_db_destroy (MuMsgDb *self)
{
	try {
		delete self;
		
	} MU_XAPIAN_CATCH_BLOCK;
}


gchar*
mu_msg_db_get_str_field (MuMsgDb *self, MuMsgFieldId mfid, gboolean *do_free)
{
	g_return_val_if_fail (self, NULL);
	g_return_val_if_fail (mu_msg_field_id_is_valid(mfid), NULL);
	g_return_val_if_fail (mu_msg_field_is_string(mfid), NULL);

	*do_free = TRUE;
	
	try {
		const std::string s (self->doc().get_value(mfid));
		return s.empty() ? NULL : g_strdup (s.c_str());

	} MU_XAPIAN_CATCH_BLOCK_RETURN(NULL);
}


gint64
mu_msg_db_get_num_field (MuMsgDb *self, MuMsgFieldId mfid)
{
	g_return_val_if_fail (self, -1);
	g_return_val_if_fail (mu_msg_field_id_is_valid(mfid), -1);
	g_return_val_if_fail (mu_msg_field_is_numeric(mfid), -1);
		
	try {
		const std::string s (self->doc().get_value(mfid));
		if (s.empty())
			return -1;
		else
			return static_cast<gint64>(Xapian::sortable_unserialise(s));

	} MU_XAPIAN_CATCH_BLOCK_RETURN(-1);
	
}


