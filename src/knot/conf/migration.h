/*  Copyright (C) 2017 CZ.NIC, z.s.p.o. <knot-dns@labs.nic.cz>

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#pragma once

#include "knot/conf/base.h"

/*!
 * Migrates from an old configuration schema.
 *
 * \param[in] conf  Configuration.
 *
 * \return Error code, KNOT_EOK if success.
 */
int conf_migrate(
	conf_t *conf
);

#define C_MOD_ONLINE_SIGN "\x0f""mod-online-sign"
extern const yp_item_t schema_mod_online_sign[];
int check_mod_online_sign(knotd_conf_check_args_t *args);

#define C_MOD_SYNTH_RECORD "\x10""mod-synth-record"
extern const yp_item_t schema_mod_synth_record[];
int check_mod_synth_record(knotd_conf_check_args_t *args);
