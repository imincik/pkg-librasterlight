/*

 check_badopen.c -- RasterLite Test Case

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

#include <spatialite/gaiaexif.h>

#include "../headers/rasterlite.h"

int main (void)
{
    void *handle = NULL;
    int result;
    unsigned char *raster;
    int size;
    
    handle = rasterliteOpen ("no such file.sqlite", "globe");
    if ( ! rasterliteIsError(handle))
    {
	/* some unexpected error occurred */
	printf("ERROR: expected failure for bad filename\n");
	return -1;
    }
    rasterliteClose(handle);

    handle = rasterliteOpen ("globe.sqlite", "no such entries");
    if ( ! rasterliteIsError(handle))
    {
	/* some unexpected error occurred */
	printf("ERROR: expected failure for bad table\n");
	return -2;
    }
    
    result = rasterliteGetRaster(handle, 133.0, -40.0, 0.36, 256, 256, GAIA_JPEG_BLOB, 50, (void**)&raster, &size);
    if (result != RASTERLITE_ERROR)
    {
	printf("ERROR: GetRaster bad handle unexpected result: %i\n", result);
	rasterliteClose(handle);
	return -3;
    }
    if (strcmp(rasterliteGetLastError(handle), "invalid datasource") != 0)
    {
	printf("ERROR: last error mismatch: %s\n", rasterliteGetLastError(handle));
	rasterliteClose(handle);
	return -4;
    }
    
    rasterliteClose(handle);
    
    return 0;
}
