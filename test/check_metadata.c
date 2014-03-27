/*

 check_metadata.c -- RasterLite Test Case

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

#include <spatialite.h>

#include "../headers/rasterlite.h"

int main (void)
{
    void *handle = NULL;
    int levels = 0;
    const char *prefixName = NULL;
    double min_x;
    double min_y;
    double max_x;
    double max_y;
    int srid;
    const char *auth_name;
    int auth_srid;
    const char *ref_sys_name;
    const char *proj4text;
	
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

    prefixName = rasterliteGetTablePrefix(handle);
    if (strcmp(prefixName, "globe") != 0)
    {
	printf("ERROR: unexpected prefix: %s\n", prefixName);
	rasterliteClose(handle);
	return -3;
    }
    
    if (rasterliteGetExtent(handle, &min_x, &min_y, &max_x, &max_y) != RASTERLITE_OK)
    {
	printf("ERROR: rasterliteGetExtent %s\n", rasterliteGetLastError(handle));
	rasterliteClose(handle);
	return -4;
    }
    if (min_x != -180.0)
    {
	printf("ERROR: bad minimum X extent: %f", min_x);
	rasterliteClose(handle);
	return -5;
    }
    if (max_x != 180.0)
    {
	printf("ERROR: bad maximum X extent: %f", max_x);
	rasterliteClose(handle);
	return -6;
    }
    if (min_y != -90.0)
    {
	printf("ERROR: bad minimum Y extent: %f", min_y);
	rasterliteClose(handle);
	return -7;
    }
    if (max_x != 180.0)
    {
	printf("ERROR: bad maximum Y extent: %f", max_y);
	rasterliteClose(handle);
	return -8;
    }
    
    if (rasterliteGetSrid(handle, &srid, &auth_name, &auth_srid, &ref_sys_name, &proj4text) != RASTERLITE_OK)
    {
	printf("ERROR: rasterliteGetSrid %s\n", rasterliteGetLastError(handle));
	rasterliteClose(handle);
	return -9;
    }
    if (srid != 4326)
    {
	printf("ERROR: unexpected SRID: %i\n", srid);
	rasterliteClose(handle);
	return -10;
    }
    if (strcmp(auth_name, "epsg") !=0)
    {
	printf("ERROR: unexpected SRID authority: %s\n", auth_name);
	rasterliteClose(handle);
	return -11;
    }
    if (auth_srid != 4326)
    {
	printf("ERROR: unexpected Authority SRID: %i\n", auth_srid);
	rasterliteClose(handle);
	return -12;
    }
    if (strcmp(ref_sys_name, "WGS 84") != 0)
    {
	printf("ERROR: unexpected Ref System name: %s\n", ref_sys_name);
	rasterliteClose(handle);
	return -13;
    }
    if (strcmp(proj4text, "+proj=longlat +datum=WGS84 +no_defs") != 0)
    {
	printf("ERROR: unexpected PROJ.4 text: %s\n", proj4text);
	rasterliteClose(handle);
	return -14;
    }
    
    if (strcmp(rasterliteGetSqliteVersion(handle), sqlite3_libversion()) != 0)
    {
	printf("ERROR: unexpected sqlite version: %s\n", rasterliteGetSqliteVersion(handle));
	rasterliteClose(handle);
	return -15;
    }
    
    if (strcmp(rasterliteGetSpatialiteVersion(handle), spatialite_version()) != 0)
    {
	printf("ERROR: unexpected spatialite version: %s\n", rasterliteGetSpatialiteVersion(handle));
	rasterliteClose(handle);
	return -16;
    }
    
    if (strcmp(rasterliteGetPath(handle), "globe.sqlite") != 0)
    {
	printf("ERROR: unexpected path: %s\n", rasterliteGetPath(handle));
	rasterliteClose(handle);
	return -17;
    }
    
    rasterliteClose(handle);
    return 0;
}
