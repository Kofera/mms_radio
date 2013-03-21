#include "common.h"
#include "url.h"

char* ct_DecodeUri( char *psz )
{
	unsigned char *in = (unsigned char *)psz, *out = in, c;

	if( psz == 0 )
		return 0;

	while( ( c = *in++ ) != '\0' )
	{
		switch( c )
		{
		case '%':
			{
				char hex[3];

				if( ( ( hex[0] = *in++ ) == 0 )
					|| ( ( hex[1] = *in++ ) == 0 ) )
					return 0;

				hex[2] = '\0';
				*out++ = (unsigned char)strtoul( hex, 0, 0x10 );
				break;
			}

		default:
			/* Inserting non-ASCII or non-printable characters is unsafe,
			* and no sane browser will send these unencoded */
			if( ( c < 32 ) || ( c > 127 ) )
				*out++ = '?';
			else
				*out++ = c;
		}
	}
	*out = '\0';
	return psz;
}


/*****************************************************************************
* ct_UrlParse:
*****************************************************************************
* option : if != 0 then path is split at this char
*
* format [protocol://[login[:password]@]][host[:port]]/path[OPTIONoption]
*****************************************************************************/
void ct_UrlParse(url_t *url, const char *psz_url,
								char option )
{
	char *psz_dup;
	char *psz_parse;
	char *p;
	char *p2;

	url->psz_protocol = 0;
	url->psz_username = 0;
	url->psz_password = 0;
	url->psz_host     = 0;
	url->i_port       = 0;
	url->psz_path     = 0;
	url->psz_option   = 0;

	if( psz_url == 0 )
	{
		url->psz_buffer = 0;
		return;
	}
	url->psz_buffer = psz_parse = psz_dup = strdup( psz_url );

	/* Search a valid protocol */
	p  = strstr( psz_parse, ":/" );
	if( p != 0 )
	{
		for( p2 = psz_parse; p2 < p; p2++ )
		{
#define I(i,a,b) ( (a) <= (i) && (i) <= (b) )
			if( !I(*p2, 'a', 'z' ) && !I(*p2, 'A', 'Z') && !I(*p2, '0', '9') && *p2 != '+' && *p2 != '-' && *p2 != '.' )
			{
				p = 0;
				break;
			}
#undef I
		}
	}

	if( p != 0 )
	{
		/* we have a protocol */

		/* skip :// */
		*p++ = '\0';
		if( p[1] == '/' )
			p += 2;
		url->psz_protocol = psz_parse;
		psz_parse = p;
	}
	p = strchr( psz_parse, '@' );
	p2 = strchr( psz_parse, '/' );
	if( p != 0 && ( p2 != 0 ? p < p2 : 1 ) )
	{
		/* We have a login */
		url->psz_username = psz_parse;
		*p++ = '\0';

		psz_parse = strchr( psz_parse, ':' );
		if( psz_parse != 0 )
		{
			/* We have a password */
			*psz_parse++ = '\0';
			url->psz_password = psz_parse;
			ct_DecodeUri( url->psz_password );
		}
		ct_DecodeUri( url->psz_username );
		psz_parse = p;
	}

	p = strchr( psz_parse, '/' );
	if( !p || psz_parse < p )
	{
		/* We have a host[:port] */
		url->psz_host = strdup( psz_parse );
		if( p )
		{
			url->psz_host[p - psz_parse] = '\0';
		}

		if( *url->psz_host == '[' )
		{
			/* Ipv6 address */
			p2 = strchr( url->psz_host, ']' );
			if( p2 )
			{
				p2 = strchr( p2, ':' );
			}
		}
		else
		{
			p2 = strchr( url->psz_host, ':' );
		}
		if( p2 )
		{
			*p2++ = '\0';
			url->i_port = atoi( p2 );
		}
	}
	psz_parse = p;

	/* Now parse psz_path and psz_option */
	if( psz_parse )
	{
		url->psz_path = psz_parse;
		if( option != '\0' )
		{
			p = strchr( url->psz_path, option );
			if( p )
			{
				*p++ = '\0';
				url->psz_option = p;
			}
		}
	}
	
}

/*****************************************************************************
* ct_UrlClean:
*****************************************************************************/
void ct_UrlClean( url_t *url )
{
	free( url->psz_buffer );
	free( url->psz_host );

	url->psz_protocol = 0;
	url->psz_username = 0;
	url->psz_password = 0;
	url->psz_host     = 0;
	url->i_port       = 0;
	url->psz_path     = 0;
	url->psz_option   = 0;

	url->psz_buffer   = 0;
}

#include <ctype.h>

/** Check whether a given string is not a valid URL and must hence be
*  encoded */
int ct_UrlIsNotEncoded( const char *psz_url )
{
	const char *ptr;

	for( ptr = psz_url; *ptr; ptr++ )
	{
		char c = *ptr;

		if( c == '%' )
		{
			if( !isxdigit( ptr[1] ) || !isxdigit( ptr[2] ) )
				return 1; /* not encoded */
			ptr += 2;
		}
		else
			if(  ( (unsigned char)( c - 'a' ) < 26 )
				|| ( (unsigned char)( c - 'A' ) < 26 )
				|| ( (unsigned char)( c - '0' ) < 10 )
				|| ( strchr( "-_.", c ) != 0 ) )
				return 1;
	}
	return 0; /* looks fine - but maybe it is not encoded */
}
