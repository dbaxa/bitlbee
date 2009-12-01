/***************************************************************************\
*                                                                           *
*  BitlBee - An IRC to IM gateway                                           *
*  Simple module to facilitate twitter functionality.                       *
*                                                                           *
*  Copyright 2009 Geert Mulders <g.c.w.m.mulders@gmail.com>                 *
*                                                                           *
*  This library is free software; you can redistribute it and/or            *
*  modify it under the terms of the GNU Lesser General Public               *
*  License as published by the Free Software Foundation, version            *
*  2.1.                                                                     *
*                                                                           *
*  This library is distributed in the hope that it will be useful,          *
*  but WITHOUT ANY WARRANTY; without even the implied warranty of           *
*  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU        *
*  Lesser General Public License for more details.                          *
*                                                                           *
*  You should have received a copy of the GNU Lesser General Public License *
*  along with this library; if not, write to the Free Software Foundation,  *
*  Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA           *
*                                                                           *
****************************************************************************/

/***************************************************************************\
*                                                                           *
*  Some funtions within this file have been copied from other files within  *
*  BitlBee.                                                                 *
*                                                                           *
****************************************************************************/ 

#include "twitter_http.h"
#include "twitter.h"
#include "bitlbee.h"
#include "url.h"
#include "misc.h"
#include "base64.h"
#include <ctype.h>
#include <errno.h>


char *twitter_urlencode(const char *instr);
char *twitter_url_append(char *url, char *key, char* value);
static int isurlchar(unsigned char c);

/**
 * Do a request.
 * This is actually pretty generic function... Perhaps it should move to the lib/http_client.c
 */
void *twitter_http(char *url_string, http_input_function func, gpointer data, int is_post, char* user, char* pass, char** arguments, int arguments_len)
{
	url_t *url = g_new0( url_t, 1 );
	char *tmp;
	char *request;
	void *ret;
	char *userpass = NULL;
	char *userpass_base64;
	char *url_arguments;

	// Fill the url structure.
	if( !url_set( url, url_string ) )
	{
		g_free( url );
		return NULL;
	}

	if( url->proto != PROTO_HTTP && url->proto != PROTO_HTTPS )
	{
		g_free( url );
		return NULL;
	}

	// Concatenate user and pass
	if (user && pass) {
		userpass = g_strdup_printf("%s:%s", user, pass);
		userpass_base64 = base64_encode((unsigned char*)userpass, strlen(userpass));
	} else {
		userpass_base64 = NULL;
	}

	url_arguments = g_malloc(1);
	url_arguments[0] = '\0';

	// Construct the url arguments.
	if (arguments_len != 0)
	{
		int i;
		for (i=0; i<arguments_len; i+=2) 
		{
			tmp = twitter_url_append(url_arguments, arguments[i], arguments[i+1]);
			g_free(url_arguments);
			url_arguments = tmp;
		}
	}

	// Do GET stuff...
	if (!is_post)
	{
		// Find the char-pointer of the end of the string.
		tmp = url->file + strlen(url->file);
		tmp[0] = '?';
		// append the url_arguments to the end of the url->file.
		// TODO GM: Check the length?
		g_stpcpy (tmp+1, url_arguments);
	}


	// Make the request.
	request = g_strdup_printf(  "%s %s HTTP/1.0\r\n"
								"Host: %s\r\n"
								"User-Agent: BitlBee " BITLBEE_VERSION " " ARCH "/" CPU "\r\n",
								is_post ? "POST" : "GET", url->file, url->host );

	// If a pass and user are given we append them to the request.
	if (userpass_base64)
	{
		tmp = g_strdup_printf("%sAuthorization: Basic %s\r\n", request, userpass_base64);
		g_free(request);
		request = tmp;
	}

	// Do POST stuff..
	if (is_post)
	{
		// Append the Content-Type and url-encoded arguments.
		tmp = g_strdup_printf("%sContent-Type: application/x-www-form-urlencoded\r\nContent-Length: %i\r\n\r\n%s", 
								request, strlen(url_arguments), url_arguments);
		g_free(request);
		request = tmp;
	} else {
		// Append an extra \r\n to end the request...
		tmp = g_strdup_printf("%s\r\n", request);
		g_free(request);
		request = tmp;
	}

	ret = http_dorequest( url->host, url->port,	url->proto == PROTO_HTTPS, request, func, data );

	g_free( url );
	g_free( userpass );
	g_free( userpass_base64 );
	g_free( url_arguments );
	g_free( request );
	return ret;
}

char *twitter_url_append(char *url, char *key, char* value)
{
	char *key_encoded = twitter_urlencode(key);
	char *value_encoded = twitter_urlencode(value);
	char *retval;
	if (strlen(url) != 0)
		retval = g_strdup_printf("%s&%s=%s", url, key_encoded, value_encoded);
	else
		retval = g_strdup_printf("%s=%s", key_encoded, value_encoded);

	g_free(key_encoded);
	g_free(value_encoded);

	return retval;
}

char *twitter_urlencode(const char *instr)
{
	int ipos=0, bpos=0;
	char *str = NULL;
	int len = strlen(instr);

	if(!(str = g_new(char, 3*len + 1) ))
		return "";

	while(instr[ipos]) {
		while(isurlchar(instr[ipos]))
			str[bpos++] = instr[ipos++];
		if(!instr[ipos])
			break;

		g_snprintf(&str[bpos], 4, "%%%.2x", instr[ipos]);
		bpos+=3;
		ipos++;
	}
	str[bpos]='\0';

	/* free extra alloc'ed mem. */
	len = strlen(str);
	str = g_renew(char, str, len+1);

	return (str);
}


char *twitter_urldecode(const char *instr)
{
	int ipos=0, bpos=0;
	char *str = NULL;
	char entity[3]={0,0,0};
	unsigned dec;
	int len = strlen(instr);

	if(!(str = g_new(char, len+1) ))
		return "";

	while(instr[ipos]) {
		while(instr[ipos] && instr[ipos]!='%')
			if(instr[ipos]=='+') {
				str[bpos++]=' ';
				ipos++;
			} else
				str[bpos++] = instr[ipos++];
			if(!instr[ipos])
				break;

			if(instr[ipos+1] && instr[ipos+2]) {
				ipos++;
				entity[0]=instr[ipos++];
				entity[1]=instr[ipos++];
				sscanf(entity, "%2x", &dec);
				str[bpos++] = (char)dec;
			} else {
				str[bpos++] = instr[ipos++];
			}
		}
	str[bpos]='\0';

	/* free extra alloc'ed mem. */
	len = strlen(str);
	str = g_renew(char, str, len+1);

	return (str);
}

static int isurlchar(unsigned char c)
{
	return (isalnum(c) || '-' == c || '_' == c);
}

