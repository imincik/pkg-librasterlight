/*

 check_colours.c -- RasterLite Test Case

 Author: Brad Hards <bradh@frogmouth.net>

 ------------------------------------------------------------------------------
 
 Version: MPL 1.1/GPL 2.0/LGPL 2.1
 
 The contents of this file are subject to the Mozilla Public License Version
 1.1 (the "License"); you may not use this file except in compliance with
 the License. You may obtain a copy of the License at
 http://www.mozilla.org/MPL/
 
Software distributed under the License is distributed on an "AS IS" basis,
WITHOUT WARRANTY OF ANY KIND, either express or implied. See the License
for the specific language governing rights and limitations under the
License.

The Original Code is the SpatiaLite library

The Initial Developer of the Original Code is Alessandro Furieri
 
Portions created by the Initial Developer are Copyright (C) 2011
the Initial Developer. All Rights Reserved.

Contributor(s):
Brad Hards <bradh@frogmouth.net>

Alternatively, the contents of this file may be used under the terms of
either the GNU General Public License Version 2 or later (the "GPL"), or
the GNU Lesser General Public License Version 2.1 or later (the "LGPL"),
in which case the provisions of the GPL or the LGPL are applicable instead
of those above. If you wish to allow use of your version of this file only
under the terms of either the GPL or the LGPL, and not to allow others to
use your version of this file under the terms of the MPL, indicate your
decision by deleting the provisions above and replace them with the notice
and other provisions required by the GPL or the LGPL. If you do not delete
the provisions above, a recipient may use your version of this file under
the terms of any one of the MPL, the GPL or the LGPL.
 
*/
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "config.h"

#ifdef SPATIALITE_AMALGAMATION
#include <spatialite/sqlite3.h>
#else
#include <sqlite3.h>
#endif

#include "../headers/rasterlite.h"

int main (void)
{
    void *handle = NULL;
    int hasTransparentColour = RASTERLITE_OK;
    int hasBackgroundColour = RASTERLITE_OK;
    unsigned char red = 128;
    unsigned char green = 128;
    unsigned char blue = 128;
    
    handle = rasterliteOpen ("globe.sqlite", "globe");
    if (rasterliteIsError(handle))
    {
	/* some unexpected error occurred */
	printf("ERROR: rasterliteOpen %s\n", rasterliteGetLastError(handle));
	rasterliteClose(handle);
	return -1;
    }
    
    hasTransparentColour = rasterliteHasTransparentColor(handle);
    if (hasTransparentColour != RASTERLITE_ERROR)
    {
	printf("ERROR: unexpected transparent colour result: %i\n", hasTransparentColour);
	rasterliteClose(handle);
	return -2;
    }
    
    /* no colour yet, so we expect defaults */
    hasTransparentColour = rasterliteGetTransparentColor(handle, &red, &green, &blue);
    if (hasTransparentColour != RASTERLITE_ERROR)
    {
	printf("ERROR: unexpected default get transparent colour result: %i\n", hasTransparentColour);
	rasterliteClose(handle);
	return -3;
    }
    if ((red != 0) || (green != 0) || (blue != 0))
    {
	printf("ERROR: unexpected default values, red = %i, green = %i, blue = %i\n", red, green, blue);
	rasterliteClose(handle);
	return -4;
    }
    
    /* set the colour */
    rasterliteSetTransparentColor(handle, 64, 128, 172);
    
    /* check again */
    hasTransparentColour = rasterliteHasTransparentColor(handle);
    if (hasTransparentColour != RASTERLITE_OK)
    {
	printf("ERROR: unexpected transparent colour result2: %i\n", hasTransparentColour);
	rasterliteClose(handle);
	return -5;
    }
    hasTransparentColour = rasterliteGetTransparentColor(handle, &red, &green, &blue);
    if (hasTransparentColour != RASTERLITE_OK)
    {
	printf("ERROR: unexpected default get transparent colour result2: %i\n", hasTransparentColour);
	rasterliteClose(handle);
	return -6;
    }
    if ((red != 64) || (green != 128) || (blue != 172))
    {
	printf("ERROR: unexpected default values2, red = %i, green = %i, blue = %i\n", red, green, blue);
	rasterliteClose(handle);
	return -7;
    }
    
    /* check default background */
    hasBackgroundColour = rasterliteGetBackgroundColor(handle, &red, &green, &blue);
    if (hasBackgroundColour != RASTERLITE_OK)
    {
	printf("ERROR: unexpected default get background colour result: %i\n", hasBackgroundColour);
	rasterliteClose(handle);
	return -8;
    }
    if ((red != 0) || (green != 0) || (blue != 0))
    {
	printf("ERROR: unexpected background default values, red = %i, green = %i, blue = %i\n", red, green, blue);
	rasterliteClose(handle);
	return -9;
    }
    
    /* set background colour */
    rasterliteSetBackgroundColor(handle, 56, 103, 202);

    /* check again */
    hasBackgroundColour = rasterliteGetBackgroundColor(handle, &red, &green, &blue);
    if (hasBackgroundColour != RASTERLITE_OK)
    {
	printf("ERROR: unexpected default get background colour result2: %i\n", hasBackgroundColour);
	rasterliteClose(handle);
	return -10;
    }
    if ((red != 56) || (green != 103) || (blue != 202))
    {
	printf("ERROR: unexpected background values, red = %i, green = %i, blue = %i\n", red, green, blue);
	rasterliteClose(handle);
	return -11;
    }
    
    rasterliteClose(handle);
    return 0;
}
