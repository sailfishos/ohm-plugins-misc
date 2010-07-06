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


#include <pwd.h>
#include <grp.h>
#include <sys/types.h>

#include "cgrp-plugin.h"


/********************
 * cgrp_getuid
 ********************/
uid_t
cgrp_getuid(const char *user)
{
    struct passwd *pwent;

    if ((pwent = getpwnam(user)) == NULL)
        return (uid_t)-1;
    else
        return pwent->pw_uid;
}


/********************
 * cgrp_getgid
 ********************/
gid_t
cgrp_getgid(const char *group)
{
    struct group *grent;

    if ((grent = getgrnam(group)) == NULL)
        return (gid_t)-1;
    else
        return grent->gr_gid;
}


/* 
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */
