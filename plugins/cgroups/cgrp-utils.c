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
