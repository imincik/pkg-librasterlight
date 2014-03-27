/*

 check_resolution.c -- RasterLite Test Case

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
    int levels = 0;
    double x_size = 0.0;
    double y_size = 0.0;
    int tile_count = 0;

    handle = rasterliteOpen ("globe.sqlite", "globe");
    if (rasterliteIsError(handle))
    {
	/* some unexpected error occurred */
	printf("ERROR: rasterliteOpen %s\n", rasterliteGetLastError(handle));
	rasterliteClose(handle);
	return -1;
    }
    
    levels = rasterliteGetLevels(handle);
    if (levels != 4)
    {
	printf("ERROR: unexpected levels count: %i\n", levels);
	rasterliteClose(handle);
	return -2;
    }

    rasterliteGetResolution(handle, 0, &x_size, &y_size, &tile_count);
    if ((x_size != 1.44) || (y_size != 1.44) || (tile_count != 1))
    {
	printf("ERROR: unexpected resolution at level 0: x_size = %f, y_size = %f, tiles = %i\n", x_size, y_size, tile_count);
	rasterliteClose(handle);
	return -3;
    }
    
    rasterliteGetResolution(handle, 1, &x_size, &y_size, &tile_count);
    if ((x_size != 0.72) || (y_size != 0.72) || (tile_count != 1))
    {
	printf("ERROR: unexpected resolution at level 1: x_size = %f, y_size = %f, tiles = %i\n", x_size, y_size, tile_count);
	rasterliteClose(handle);
	return -4;
    }
    
    rasterliteGetResolution(handle, 2, &x_size, &y_size, &tile_count);
    if ((x_size != 0.36) || (y_size != 0.36) || (tile_count != 4))
    {
	printf("ERROR: unexpected resolution at level 2: x_size = %f, y_size = %f, tiles = %i\n", x_size, y_size, tile_count);
	rasterliteClose(handle);
	return -5;
    }

    rasterliteGetResolution(handle, 3, &x_size, &y_size, &tile_count);
    if ((x_size != 0.18) || (y_size != 0.18) || (tile_count != 16))
    {
	printf("ERROR: unexpected resolution at level 3: x_size = %f, y_size = %f, tiles = %i\n", x_size, y_size, tile_count);
	rasterliteClose(handle);
	return -6;
    }
    
    rasterliteClose(handle);
    return 0;
}
