#include "common.h"
#include "access.h"

#include "mms.h"

/*****************************************************************************
 * Local prototypes
 *****************************************************************************/
struct access_sys_t
{
    int i_proto;
};

/*****************************************************************************
 * Open:
 *****************************************************************************/
int MOpen( access_t *p_this )
{
    access_t *p_access = (access_t*)p_this;

    /* use specified method */
    if( *p_access->psz_access )
    {
        if( !strncmp( p_access->psz_access, "mmsu", 4 ) )
        {
            return  MMSTUOpen ( p_access );
        }
        else if( !strncmp( p_access->psz_access, "mmst", 4 ) )
        {
            return  MMSTUOpen ( p_access );
        }
    }

    if( MMSTUOpen ( p_access ) )
    {
        //if( p_access->b_die )
        return VLC_EGENERIC;

        /* try mmsh if mmstu failed */
	// return  MMSHOpen ( p_access );
    }
    return VLC_SUCCESS;
}

/*****************************************************************************
 * Close: free unused data structures
 *****************************************************************************/
void MClose( access_t *p_this )
{
    access_t     *p_access = (access_t*)p_this;
    access_sys_t *p_sys = p_access->p_sys;

    if( ( p_sys->i_proto == MMS_PROTO_TCP ) ||
        ( p_sys->i_proto == MMS_PROTO_UDP ) )
    {
         MMSTUClose ( p_access );
    }
}
