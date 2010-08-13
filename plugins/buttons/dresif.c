/*************************************************************************
Copyright (C) 2010 Nokia Corporation.

These OHM Modules are free software; you can redistribute
it and/or modify it under the terms of the GNU Lesser General Public
License as published by the Free Software Foundation
version 2.1 of the License.

This library is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
Lesser General Public License for more details.

You should have received a copy of the GNU Lesser General Public
License along with this library; if not, write to the Free Software
Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301
USA.
*************************************************************************/


static int dresif_power_key_pressed(void)
{
#define DRESIF_VARTYPE(t)  (char *)(t)
#define DRESIF_VARVALUE(v) (char *)(v)

    static char *target = "power_key_pressed";

    char *vars[10];
    int   i;
    int   status;


    OHM_DEBUG(DBG_DRES, "Resolving target '%s'", target);

    vars[i=0] = NULL;

    status = resolve(target, vars);

    return status;

#undef DRESIF_VARVALUE
#undef DRESIF_VARTYPE
}

/* 
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */
