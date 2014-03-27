/*

 check_rastergen.c -- RasterLite Test Case

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
    unsigned char *refraster;
    int size;
    int sizeref = 4540;
    int sizemin = sizeref;
    FILE *reffilestream;
    int i;
    
    handle = rasterliteOpen ("globe.sqlite", "globe");
    if (rasterliteIsError(handle))
    {
	/* some unexpected error occurred */
	printf("ERROR: rasterliteOpen %s\n", rasterliteGetLastError(handle));
	rasterliteClose(handle);
	return -1;
    }
    
    
    result = rasterliteGetRaster(handle, 133.0, -40.0, 0.36, 256, 256, GAIA_JPEG_BLOB, 50, (void**)&raster, &size);
    if (result != RASTERLITE_OK)
    {
	printf("ERROR: GetRaster JPEG 50 %s\n", rasterliteGetLastError(handle));
	rasterliteClose(handle);
	return -2;
    }
    if (sizemin > size)
        sizemin = size;
 
    reffilestream = fopen("jpeg50ref.jpg", "rb");
    refraster = malloc(sizeref);
    if ((int) fread (refraster, 1, sizeref, reffilestream) != sizeref)
    {
	fprintf (stderr, "read error on jpeg50ref.jpg reference file\n");
	rasterliteClose(handle);
	return -3;
    }
    fclose (reffilestream);
    
    for (i = 0; i < sizemin; i++)
    {
        if (i == 63 || i == 64)
        {
        /* skipping libjpeg version number */
            continue;
        }
	if (refraster[i] != raster[i])
	{
	    printf("ERROR: jpeg50 reference image mismatch at offset %i\n", i);
	    rasterliteClose(handle);
	    free(raster);
	    free(refraster);
	    return -4;
	}
    }
    free(raster);
    free(refraster);
  
    /* TODO: we could generate reference images and do the comparison for each case */
    result = rasterliteGetRaster(handle, 133.0, -40.0, 0.36, 256, 256, GAIA_EXIF_BLOB, 50, (void**)&raster, &size);
    if ((result != RASTERLITE_OK) || (size != 4540))
    {
	printf("ERROR: GetRaster EXIF 50 %s, %i bytes\n", rasterliteGetLastError(handle), size);
	rasterliteClose(handle);
	return -5;
    }
    free(raster);
	
    result = rasterliteGetRaster(handle, 133.0, -40.0, 0.36, 256, 256, GAIA_EXIF_GPS_BLOB, 50, (void**)&raster, &size);
    if ((result != RASTERLITE_OK) || (size != 4540))
    {
	printf("ERROR: GetRaster EXIF GPS 50 %s, %i bytes\n", rasterliteGetLastError(handle), size);
	rasterliteClose(handle);
	return -6;
    }
    free(raster);
    
    result = rasterliteGetRaster(handle, 133.0, -40.0, 0.36, 256, 256, GAIA_PNG_BLOB, 0, (void**)&raster, &size);
    if ((result != RASTERLITE_OK) || (size != 43280))
    {
	printf("ERROR: GetRaster PNG %s, %i bytes\n", rasterliteGetLastError(handle), size);
	rasterliteClose(handle);
	return -7;
    }
    free(raster);
    
    /* very small blob */
    result = rasterliteGetRaster(handle, 133.0, -40.0, 0.36, 64, 64, GAIA_PNG_BLOB, 0, (void**)&raster, &size);
    if ((result != RASTERLITE_OK) || (size != 3955))
    {
	printf("ERROR: GetRaster PNG 64 x 64 %s, %i bytes\n", rasterliteGetLastError(handle), size);
	rasterliteClose(handle);
	return -8;
    }
    free(raster);
    
    /* too small */
    result = rasterliteGetRaster(handle, 133.0, -40.0, 0.36, 63, 63, GAIA_PNG_BLOB, 0, (void**)&raster, &size);
    if (result != RASTERLITE_ERROR)
    {
	printf("ERROR: unexpected result for GetRaster PNG 63 x 63\n");
	rasterliteClose(handle);
	return -9;
    }
    free(raster);
    
    result = rasterliteGetRaster(handle, 133.0, -40.0, 0.36, 64, 63, GAIA_PNG_BLOB, 0, (void**)&raster, &size);
    if (result != RASTERLITE_ERROR)
    {
	printf("ERROR: unexpected result for GetRaster PNG 63 x 63\n");
	rasterliteClose(handle);
	return -10;
    }
    free(raster);
    
    /* too big */
    result = rasterliteGetRaster(handle, 133.0, -40.0, 0.36, 32769, 64, GAIA_PNG_BLOB, 0, (void**)&raster, &size);
    if (result != RASTERLITE_ERROR)
    {
	printf("ERROR: unexpected result for GetRaster PNG 32769 x 64\n");
	rasterliteClose(handle);
	return -11;
    }
    free(raster);
    
    result = rasterliteGetRaster(handle, 133.0, -40.0, 0.36, 1024, 32769, GAIA_PNG_BLOB, 0, (void**)&raster, &size);
    if (result != RASTERLITE_ERROR)
    {
	printf("ERROR: unexpected result for GetRaster PNG 64 x 32769\n");
	rasterliteClose(handle);
	return -12;
    }
    free(raster);

    /* pretty big - you may wish to disable this test if you don't have much RAM */
    result = rasterliteGetRaster(handle, 133.0, -40.0, 0.36, 8192, 8192, GAIA_PNG_BLOB, 0, (void**)&raster, &size);
    if (result != RASTERLITE_OK)
    {
	printf("ERROR: unexpected result for GetRaster PNG 8192 x 8192\n");
	rasterliteClose(handle);
	return -13;
    }
    free(raster);
    
    result = rasterliteGetRaster(handle, 133.0, -40.0, 0.36, 256, 256, GAIA_RGB_ARRAY, 50, (void**)&raster, &size);
    if ((result != RASTERLITE_OK) || (size != (256*256*3)))
    {
	printf("ERROR: GetRaster RGB ARRAY 256 x 256 %s, %i bytes\n", rasterliteGetLastError(handle), size);
	rasterliteClose(handle);
	return -14;
    }
    free(raster);
    
#if 0
    /* this returns a "GIF compression error" */
    result = rasterliteGetRaster(handle, 133.0, -40.0, 0.36, 256, 256, GAIA_GIF_BLOB, 50, (void**)&raster, &size);
    if ((result != RASTERLITE_OK) || (size != 5000))
    {
	printf("ERROR: GetRaster GIF BLOB 256 x 256 %s, %i bytes\n", rasterliteGetLastError(handle), size);
	rasterliteClose(handle);
	return -15;
    }
    free(raster);
#endif

    result = rasterliteGetRaster(handle, 133.0, -40.0, 0.36, 256, 256, GAIA_TIFF_BLOB, 50, (void**)&raster, &size);
    if ((result != RASTERLITE_OK) || (size != 198901))
    {
	printf("ERROR: GetRaster TIFF BLOB 256 x 256 %s, %i bytes\n", rasterliteGetLastError(handle), size);
	rasterliteClose(handle);
	return -15;
    }
    free(raster);
    rasterliteClose(handle);
    
    return 0;
}
