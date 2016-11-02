#include "snapconfig.h"
/* crdsysd0.c:  Routines to manage coordinate system definitions. */
/* The definitions are held in a data structure which includes a function
   pointers.  This is done to avoid linking in, say the coordinate system
	file handler, when we only want to use fixed definitions.  It also
	facilitates adding new sources of coordinate system definitions. */


/*
   $Log: crdsysd0.c,v $
   Revision 1.1  1995/12/22 16:28:07  CHRIS
   Initial revision

*/

#include <stdio.h>
#include <string.h>
#include <ctype.h>

#include "coordsys/coordsys.h"
#include "coordsys/crdsys_src.h"
#include "util/chkalloc.h"
#include "util/dateutil.h"
#include "util/errdef.h"


static crdsys_source_def *sources = NULL;
static int update_id = 0;
static char csfullcode[CRDCNV_CODE_LEN+1];

crdsys_source_def *crdsys_sources()
{
    return sources;
}

int crdsys_source_update()
{
    return update_id;
}


void register_crdsys_source( crdsys_source_def *csd )
{
    csd->next = sources;
    sources = (crdsys_source_def *) check_malloc( sizeof( crdsys_source_def ) );
    memcpy( sources, csd, sizeof( crdsys_source_def ) );
    update_id++;
}

void uninstall_crdsys_lists( void )
{
    crdsys_source_def *csd;
    update_id++;
    while( sources )
    {
        csd = sources;
        sources = sources->next;
        if( csd->delsource ) (*csd->delsource)( csd->data );
        check_free( csd );
    }
}


ref_frame * load_ref_frame( const char *code )
{
    crdsys_source_def *csd;
    int sts;
    ref_frame *rf = NULL;

    for( sts = MISSING_DATA, csd = sources;
            sts == MISSING_DATA && csd;
            csd = csd->next ) if( csd->getrf )
        {

            sts = (*csd->getrf)( csd->data, CS_ID_UNAVAILABLE, code, &rf );
        }

    if( sts == MISSING_DATA )
    {
        char errmsg[80];
        sprintf(errmsg,"Reference frame %.20s is not defined",code);
        handle_error(INVALID_DATA,errmsg,nullptr);
    }
    return rf;
}


ellipsoid * load_ellipsoid( const char *code )
{
    crdsys_source_def *csd;
    int sts;
    ellipsoid *el= NULL;

    for( sts = MISSING_DATA, csd = sources;
            sts == MISSING_DATA && csd;
            csd = csd->next ) if( csd->getel )
        {

            sts = (*csd->getel)( csd->data, CS_ID_UNAVAILABLE, code, &el );
        }
    if( sts == MISSING_DATA )
    {
        char errmsg[80];
        sprintf(errmsg,"Ellipsoid %.20s is not defined",code);
        handle_error(INVALID_DATA,errmsg,nullptr);
    }

    return el;
}

int parse_crdsys_epoch( const char *epochstr, double *epoch )
{
    *epoch=snap_datetime_parse(epochstr,0);
    if( ! *epoch ) return 0;
    *epoch = date_as_year(*epoch);
    return 1;
}

/* Coordinate systems defined by code, optionally followed by @epoch, where
   epoch is either a decimal year number (eg 2007.5) or "now" */

coordsys * load_coordsys( const char *code )
{
    crdsys_source_def *csd;
    height_ref *hrs=nullptr;
    char cscode[CRDSYS_CODE_LEN+1];
    char hrscode[CRDSYS_CODE_LEN+1];
    double epoch = 0;
    int sts;
    const char *epochptr;
    const char *hrsptr;
    int nch;

    coordsys *cs= NULL;

    /* Code format is  CSCODE/HRSCODE@epoch */
    epochptr=strchr(code,'@');
    hrsptr=strchr(code,'/');
    if( epochptr && hrsptr && hrsptr > epochptr ){ hrsptr=nullptr; }

    /* Check epoch */
    if( epochptr && ! parse_crdsys_epoch( epochptr+1, &epoch ) )
    {
        char errmsg[100];
        sprintf(errmsg,"Invalid coordinate system epoch in %.40s",code);
        handle_error(INVALID_DATA,errmsg,nullptr);
        return NULL;
    }

    /* Check length of coordinate system code */
    nch=hrsptr ? hrsptr-code : epochptr ? epochptr-code : strlen(code);
    if( nch < 1 || nch > CRDSYS_CODE_LEN ) 
    {
        char errmsg[100];
        sprintf(errmsg,"Invalid coordinate system code n %.40s",code);
        handle_error(INVALID_DATA,errmsg,nullptr);
        return NULL;
    }
    strncpy(cscode,code,nch);
    cscode[nch]=0;

    /* Check height reference surface */
    if( hrsptr )
    {
        nch=(epochptr ? epochptr-hrsptr : strlen(hrsptr))-1;
        if( nch > CRDSYS_CODE_LEN )
        {
            char errmsg[100];
            sprintf(errmsg,"Invalid height system code in %.40s",code);
            handle_error(INVALID_DATA,errmsg,nullptr);
            return NULL;
        }
        else if( nch > 0 )
        {
            strncpy(hrscode,hrsptr+1,nch);
            hrscode[nch]=0;
            hrs=load_height_ref( hrscode );
            if( ! hrs ) return NULL;
        }
    }

    for( sts = MISSING_DATA, csd = sources;
            sts == MISSING_DATA && csd;
            csd = csd->next ) if( csd->getcs )
        {

            sts = (*csd->getcs)( csd->data, CS_ID_UNAVAILABLE, cscode, &cs );
        }

    if( sts == MISSING_DATA )
    {
        char errmsg[80];
        sprintf(errmsg,"Coordinate system %.20s is not defined",cscode);
        handle_error(INVALID_DATA,errmsg,nullptr);
    }


    if( cs ) 
    {
        if( hrs )
        {
            if( ! coordsys_height_ref_compatible( cs, hrs ) )
            {
                char errmsg[80];
                sprintf(errmsg,"Height system %.20s not compatible with coordinate system %.20s",
                        hrs->code, cs->code );
                handle_error( INVALID_DATA, errmsg, nullptr );
                delete_coordsys( cs );
                delete_height_ref( hrs );
                return NULL;
            }
            set_coordsys_height_ref( cs, hrs );
        }
        define_deformation_model_epoch(cs,epoch);
    }
    else
    {
        if( hrs ) delete_height_ref( hrs );
    }
    return cs;
}

const char *coordsys_load_code( coordsys *cs )
{
    strncpy( csfullcode, cs->code, CRDCNV_CODE_LEN );
    if( cs->hrs 
            && cs->hrs->code 
            && strlen(cs->hrs->code)+strlen(csfullcode)+1 < CRDCNV_CODE_LEN
      )
    {
        strcat(csfullcode,"/");
        strcat(csfullcode,cs->hrs->code);
    }
    return csfullcode;
}

height_ref * load_height_ref( const char *code )
{
    crdsys_source_def *csd;
    int sts;
    height_ref *hrs= NULL;

    for( sts = MISSING_DATA, csd = sources;
            sts == MISSING_DATA && csd;
            csd = csd->next ) if( csd->getel )
        {

            sts = (*csd->gethrs)( csd->data, CS_ID_UNAVAILABLE, code, &hrs );
        }

    if( sts == MISSING_DATA )
    {
        char errmsg[80];
        sprintf(errmsg,"Height reference %.20s is not defined",code);
        handle_error(INVALID_DATA,errmsg,nullptr);
    }
    return hrs;
}


int get_notes( int type, const char *code, output_string_def *os )
{
    crdsys_source_def *csd;
    char cscode[CRDCNV_CODE_LEN+1];
    int nch;
    double epoch = 0;
    int sts = MISSING_DATA;

    /* Look for an @ character, defining an deformation model reference epoch */
    for( nch = 0; code[nch] != 0 && code[nch] != '@'; nch++ ) {}
    if( code[nch] && ! parse_crdsys_epoch( code+nch+1, &epoch ) )
    {
        return INVALID_DATA;
    }
    if( nch > CRDCNV_CODE_LEN ) return INVALID_DATA;
    strncpy(cscode,code,nch);
    cscode[nch] = 0;

    for( csd = sources; csd; csd = csd->next )
        if( csd->getnotes )
        {

            sts = (*csd->getnotes)( csd->data, type, cscode, os->sink, os->write );
            if( sts == OK ) break;
        }
    return sts;
}


int get_crdsys_notes( coordsys *cs, output_string_def *os  )
{
    int sts;

    sts = get_notes( CS_COORDSYS_NOTE, cs->code, os );
    if( cs->rf )
    {
        if( get_notes( CS_REF_FRAME_NOTE, cs->rf->code, os  ) == OK ) sts=OK;
    }
    return sts;
}

static int gcc_notes( int type, const char *code1, const char *code2, output_string_def *os )
{
    char convcode[CRDCNV_CODE_LEN+1];
    if( ! code1 || ! code2 ) return INVALID_DATA;
    if( strlen(code1) > CRDSYS_CODE_LEN || strlen(code2) > CRDSYS_CODE_LEN ) return INVALID_DATA;
    strcpy(convcode,code1);
    strcat(convcode,":");
    strcat(convcode,code2);
    return get_notes(type,convcode,os);
}

int get_conv_code_notes( int type, const char *code1, const char *code2, output_string_def *os )
{
    if( gcc_notes(type,code1,code2,os)==OK || gcc_notes(type,code2,code1,os)==OK ) return OK;
    return MISSING_DATA;
}

int get_conv_notes( coord_conversion *conv, output_string_def *os )
{
    int sts = MISSING_DATA;
    int icrf;
    const char *code1, *code2;
    if( get_conv_code_notes( CS_COORDSYS_NOTE,conv->from->code, conv->to->code, os ) == OK )
    {
        sts = OK;
    }
    code2=conv->from->rf->code;
    for( icrf=0; icrf < conv->ncrf; icrf++ )
    {
        coord_conversion_rf *crf = &(conv->crf[icrf]);
        if( crf->def_only ) continue;
        if( ! crf->rf ) continue;
        code1=code2;
        code2=crf->rf->code;
        if( get_conv_code_notes( CS_REF_FRAME_NOTE,code1,code2, os ) == OK )
        {
            sts = OK;
        }
    }
    return sts;
}
