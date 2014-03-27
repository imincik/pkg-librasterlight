/* 
/ rasterlite_tool.c
/
/ a tool generating rasters from a SpatiaLite DB 
/
/ version 1.1a, 2011 November 12
/
/ Author: Sandro Furieri a.furieri@lqt.it
/
/ ------------------------------------------------------------------------------
/ 
/ Version: MPL 1.1/GPL 2.0/LGPL 2.1
/ 
/ The contents of this file are subject to the Mozilla Public License Version
/ 1.1 (the "License"); you may not use this file except in compliance with
/ the License. You may obtain a copy of the License at
/ http://www.mozilla.org/MPL/
/ 
/ Software distributed under the License is distributed on an "AS IS" basis,
/ WITHOUT WARRANTY OF ANY KIND, either express or implied. See the License
/ for the specific language governing rights and limitations under the
/ License.
/
/ The Original Code is the RasterLite library
/
/ The Initial Developer of the Original Code is Alessandro Furieri
/ 
/ Portions created by the Initial Developer are Copyright (C) 2009
/ the Initial Developer. All Rights Reserved.
/
/ Alternatively, the contents of this file may be used under the terms of
/ either the GNU General Public License Version 2 or later (the "GPL"), or
/ the GNU Lesser General Public License Version 2.1 or later (the "LGPL"),
/ in which case the provisions of the GPL or the LGPL are applicable instead
/ of those above. If you wish to allow use of your version of this file only
/ under the terms of either the GPL or the LGPL, and not to allow others to
/ use your version of this file under the terms of the MPL, indicate your
/ decision by deleting the provisions above and replace them with the notice
/ and other provisions required by the GPL or the LGPL. If you do not delete
/ the provisions above, a recipient may use your version of this file under
/ the terms of any one of the MPL, the GPL or the LGPL.
/ 
*/

#if defined(_WIN32) && !defined(__MINGW32__)
/* MSVC strictly requires this include [off_t] */
#include <sys/types.h>
#endif

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <float.h>

#include <tiffio.h>

#ifdef SPATIALITE_AMALGAMATION
#include <spatialite/sqlite3.h>
#else
#include <sqlite3.h>
#endif

#include <spatialite/gaiaexif.h>
#include <spatialite/gaiageo.h>
#include <spatialite.h>

#include "rasterlite.h"
#include "rasterlite_internals.h"

#define ARG_NONE			0
#define ARG_IMAGE_PATH		1
#define ARG_DB_PATH			2
#define ARG_TABLE_NAME		3
#define ARG_CENTER_X		4
#define ARG_CENTER_Y		5
#define ARG_PIXEL_RATIO		6
#define ARG_RASTER_WIDTH	7
#define ARG_RASTER_HEIGHT	8
#define ARG_IMAGE_TYPE		9
#define ARG_QUALITY_FACTOR	10
#define ARG_TRANSPARENT		11
#define ARG_BACKGROUND		12

#define WRONG_COLOR			-100

#ifdef _WIN32
#define strcasecmp	_stricmp
#endif /* not WIN32 */

static int
parse_hex (const char hi, const char lo)
{
/* parsing an hexedecimal value XX */
    int hex;
    switch (hi)
      {
	  /* parsing the leftmost byte */
      case '0':
	  hex = 0;
	  break;
      case '1':
	  hex = 1 * 16;
	  break;
      case '2':
	  hex = 2 * 16;
	  break;
      case '3':
	  hex = 3 * 16;
	  break;
      case '4':
	  hex = 4 * 16;
	  break;
      case '5':
	  hex = 5 * 16;
	  break;
      case '6':
	  hex = 6 * 16;
	  break;
      case '7':
	  hex = 7 * 16;
	  break;
      case '8':
	  hex = 8 * 16;
	  break;
      case '9':
	  hex = 9 * 16;
	  break;
      case 'a':
      case 'A':
	  hex = 10 * 16;
	  break;
      case 'b':
      case 'B':
	  hex = 11 * 16;
	  break;
      case 'c':
      case 'C':
	  hex = 12 * 16;
	  break;
      case 'd':
      case 'D':
	  hex = 13 * 16;
	  break;
      case 'e':
      case 'E':
	  hex = 14 * 16;
	  break;
      case 'f':
      case 'F':
	  hex = 15 * 16;
	  break;
      default:
	  return -1;
      };
    switch (lo)
      {
	  /* parsing the leftmost byte */
      case '0':
	  break;
      case '1':
	  hex += 1;
	  break;
      case '2':
	  hex += 2;
	  break;
      case '3':
	  hex += 3;
	  break;
      case '4':
	  hex += 4;
	  break;
      case '5':
	  hex += 5;
	  break;
      case '6':
	  hex += 6;
	  break;
      case '7':
	  hex += 7;
	  break;
      case '8':
	  hex += 8;
	  break;
      case '9':
	  hex += 9;
	  break;
      case 'a':
      case 'A':
	  hex += 10;
	  break;
      case 'b':
      case 'B':
	  hex += 11;
	  break;
      case 'c':
      case 'C':
	  hex += 12;
	  break;
      case 'd':
      case 'D':
	  hex += 13;
	  break;
      case 'e':
      case 'E':
	  hex += 14;
	  break;
      case 'f':
      case 'F':
	  hex += 15;
	  break;
      default:
	  return -1;
      };
    return hex;
}

static int
parse_hex_color (const char *hex_color)
{
/* parsing and hexadecimal encoded color 0XRRGGBB */
    int red;
    int green;
    int blue;
    if (strlen (hex_color) != 8)
	return WRONG_COLOR;
    if (hex_color[0] && (hex_color[1] == 'x' || hex_color[1] == 'X'))
	;
    else
	return WRONG_COLOR;
    red = parse_hex (hex_color[2], hex_color[3]);
    green = parse_hex (hex_color[4], hex_color[5]);
    blue = parse_hex (hex_color[6], hex_color[7]);
    if (red < 0 || green < 0 || blue < 0)
	return WRONG_COLOR;
    return true_color (red, green, blue);
}

static int
build_raster (const char *img_path, const char *db_path, const char *table,
	      double cx, double cy, double pixel_ratio, int width, int height,
	      int image_type, int quality_factor, int transparent_color,
	      int background_color)
{
/* trying to build the requested raster */
    FILE *out;
    void *handle;
    void *raster;
    int size;
    int srid;
    const char *auth_name;
    int auth_srid;
    const char *ref_sys_name;
    const char *proj4text;
    int red;
    int green;
    int blue;
/* trying to open the RasterLite data-source */
    handle = rasterliteOpen (db_path, table);
    if (rasterliteIsError (handle))
      {
	  fprintf (stderr, "ERROR: %s\n", rasterliteGetLastError (handle));
	  rasterliteClose (handle);
	  return 1;
      }
    if (transparent_color >= 0)
      {
	  /* setting up the transparent color */
	  red = true_color_get_red (transparent_color);
	  green = true_color_get_green (transparent_color);
	  blue = true_color_get_blue (transparent_color);
	  rasterliteSetTransparentColor (handle, red, green, blue);
      }
/* setting up the background color */
    red = true_color_get_red (background_color);
    green = true_color_get_green (background_color);
    blue = true_color_get_blue (background_color);
    rasterliteSetBackgroundColor (handle, red, green, blue);
/* building the raster image */
    if (rasterliteGetRaster
	(handle, cx, cy, pixel_ratio, width, height, image_type, quality_factor,
	 &raster, &size) != RASTERLITE_OK)
      {
	  fprintf (stderr, "ERROR: %s\n", rasterliteGetLastError (handle));
	  rasterliteClose (handle);
	  return 1;
      }
    if (raster)
      {
	  /* saving the image to disk */
	  if (image_type == GAIA_TIFF_BLOB)
	    {
		/* writing as a GeoTIFF  */
		double xllcorner = cx - ((double) width * pixel_ratio / 2.0);
		double yllcorner = cy + ((double) height * pixel_ratio / 2.0);
		rasterliteGetSrid (handle, &srid, &auth_name, &auth_srid,
				   &ref_sys_name, &proj4text);
		if (!write_geotiff
		    (img_path, raster, size, pixel_ratio, pixel_ratio,
		     xllcorner, yllcorner, proj4text))
		    fprintf (stderr, "GeoTIFF write error on \"%s\"\n",
			     img_path);
	    }
	  else
	    {
		/* not a TIFF */
		out = fopen (img_path, "wb");
		if (!out)
		    fprintf (stderr, "cannot open \"%s\"\n", img_path);
		else
		  {
		      if ((int) fwrite (raster, 1, size, out) != size)
			  fprintf (stderr, "write error on \"%s\"\n", img_path);
		      fclose (out);
		  }
	    }
	  /* freeing the raster image */
	  free (raster);
      }
/* closing the RasterLite data-source */
    rasterliteClose (handle);
    return 0;
}

static void
do_help ()
{
/* printing the argument list */
    fprintf (stderr, "\n\nusage: rasterlite_tool ARGLIST\n");
    fprintf (stderr,
	     "==============================================================\n");
    fprintf (stderr,
	     "-? or --help                       print this help message\n");
    fprintf (stderr, "-o or --output       pathname      the image path\n");
    fprintf (stderr,
	     "-d or --db-path     pathname       the SpatiaLite db path\n");
    fprintf (stderr, "-T or --table-name  name           DB table name\n");
    fprintf (stderr,
	     "-x or --center-x    coordinate     center-point X coord\n");
    fprintf (stderr,
	     "-y or --center-y    coordinate     center-point Y coord\n");
    fprintf (stderr,
	     "-r or --pixel-ratio num            map-units per pixel\n");
    fprintf (stderr, "-w or --width       num            raster width\n");
    fprintf (stderr, "-h or --height      num            raster height\n");
    fprintf (stderr,
	     "-i or --image-type  type           [JPEG|GIF|PNG|TIFF]\n");
    fprintf (stderr,
	     "-q or --quality     num            [default = 75(JPEG)]\n");
    fprintf (stderr, "-c or --transparent-color 0xRRGGBB [default = NONE]\n");
    fprintf (stderr,
	     "-b or --background-color  0xRRGGBB [default = 0x000000]\n");
}

int
main (int argc, char *argv[])
{
/* the MAIN function simply perform arguments checking */
    int i;
    int next_arg = ARG_NONE;
    const char *img_path = NULL;
    const char *db_path = NULL;
    const char *table = NULL;
    double cx = DBL_MAX;
    double cy = DBL_MAX;
    double pixel_ratio = DBL_MAX;
    int width = -1;
    int height = -1;
    int quality_factor = -999999;
    int image_type = GAIA_JPEG_BLOB;
    int transparent_color = -1;
    int background_color = true_color (0, 0, 0);
    int error = 0;
    char error_color[1024];
    char error_back_color[1024];
    for (i = 1; i < argc; i++)
      {
	  /* parsing the invocation arguments */
	  if (next_arg != ARG_NONE)
	    {
		switch (next_arg)
		  {
		  case ARG_IMAGE_PATH:
		      img_path = argv[i];
		      break;
		  case ARG_DB_PATH:
		      db_path = argv[i];
		      break;
		  case ARG_TABLE_NAME:
		      table = argv[i];
		      break;
		  case ARG_CENTER_X:
		      cx = atof (argv[i]);
		      break;
		  case ARG_CENTER_Y:
		      cy = atof (argv[i]);
		      break;
		  case ARG_PIXEL_RATIO:
		      pixel_ratio = atof (argv[i]);
		      break;
		  case ARG_RASTER_WIDTH:
		      width = atoi (argv[i]);
		      break;
		  case ARG_RASTER_HEIGHT:
		      height = atoi (argv[i]);
		      break;
		  case ARG_IMAGE_TYPE:
		      if (strcasecmp (argv[i], "JPEG") == 0)
			  image_type = GAIA_JPEG_BLOB;
		      if (strcasecmp (argv[i], "GIF") == 0)
			  image_type = GAIA_GIF_BLOB;
		      if (strcasecmp (argv[i], "PNG") == 0)
			  image_type = GAIA_PNG_BLOB;
		      if (strcasecmp (argv[i], "TIFF") == 0)
			  image_type = GAIA_TIFF_BLOB;
		      break;
		  case ARG_QUALITY_FACTOR:
		      quality_factor = atoi (argv[i]);
		      break;
		  case ARG_TRANSPARENT:
		      transparent_color = parse_hex_color (argv[i]);
		      if (transparent_color == WRONG_COLOR)
			  strcpy (error_color, argv[i]);
		      break;
		  case ARG_BACKGROUND:
		      background_color = parse_hex_color (argv[i]);
		      if (background_color == WRONG_COLOR)
			  strcpy (error_back_color, argv[i]);
		      break;
		  };
		next_arg = ARG_NONE;
		continue;
	    }
	  if (strcasecmp (argv[i], "--help") == 0
	      || strcmp (argv[i], "-?") == 0)
	    {
		do_help ();
		return -1;
	    }
	  if (strcasecmp (argv[i], "--output") == 0)
	    {
		next_arg = ARG_IMAGE_PATH;
		continue;
	    }
	  if (strcmp (argv[i], "-o") == 0)
	    {
		next_arg = ARG_IMAGE_PATH;
		continue;
	    }
	  if (strcasecmp (argv[i], "--db-path") == 0)
	    {
		next_arg = ARG_DB_PATH;
		continue;
	    }
	  if (strcmp (argv[i], "-d") == 0)
	    {
		next_arg = ARG_DB_PATH;
		continue;
	    }
	  if (strcmp (argv[i], "-T") == 0)
	    {
		next_arg = ARG_TABLE_NAME;
		continue;
	    }
	  if (strcasecmp (argv[i], "--table") == 0)
	    {
		next_arg = ARG_TABLE_NAME;
		continue;
	    }
	  if (strcmp (argv[i], "-x") == 0)
	    {
		next_arg = ARG_CENTER_X;
		continue;
	    }
	  if (strcasecmp (argv[i], "--center-x") == 0)
	    {
		next_arg = ARG_CENTER_X;
		continue;
	    }
	  if (strcmp (argv[i], "-y") == 0)
	    {
		next_arg = ARG_CENTER_Y;
		continue;
	    }
	  if (strcasecmp (argv[i], "--center-y") == 0)
	    {
		next_arg = ARG_CENTER_Y;
		continue;
	    }
	  if (strcmp (argv[i], "-r") == 0)
	    {
		next_arg = ARG_PIXEL_RATIO;
		continue;
	    }
	  if (strcasecmp (argv[i], "--pixel_ratio") == 0)
	    {
		next_arg = ARG_PIXEL_RATIO;
		continue;
	    }
	  if (strcmp (argv[i], "-w") == 0)
	    {
		next_arg = ARG_RASTER_WIDTH;
		continue;
	    }
	  if (strcasecmp (argv[i], "--width") == 0)
	    {
		next_arg = ARG_RASTER_WIDTH;
		continue;
	    }
	  if (strcmp (argv[i], "-h") == 0)
	    {
		next_arg = ARG_RASTER_HEIGHT;
		continue;
	    }
	  if (strcasecmp (argv[i], "--height") == 0)
	    {
		next_arg = ARG_RASTER_HEIGHT;
		continue;
	    }
	  if (strcmp (argv[i], "-i") == 0)
	    {
		next_arg = ARG_IMAGE_TYPE;
		continue;
	    }
	  if (strcasecmp (argv[i], "--image-type") == 0)
	    {
		next_arg = ARG_IMAGE_TYPE;
		continue;
	    }
	  if (strcmp (argv[i], "-q") == 0)
	    {
		next_arg = ARG_QUALITY_FACTOR;
		continue;
	    }
	  if (strcasecmp (argv[i], "--quality") == 0)
	    {
		next_arg = ARG_QUALITY_FACTOR;
		continue;
	    }
	  if (strcmp (argv[i], "-c") == 0)
	    {
		next_arg = ARG_TRANSPARENT;
		continue;
	    }
	  if (strcasecmp (argv[i], "--transparent-color") == 0)
	    {
		next_arg = ARG_TRANSPARENT;
		continue;
	    }
	  if (strcmp (argv[i], "-b") == 0)
	    {
		next_arg = ARG_BACKGROUND;
		continue;
	    }
	  if (strcasecmp (argv[i], "--background-color") == 0)
	    {
		next_arg = ARG_BACKGROUND;
		continue;
	    }
	  fprintf (stderr, "unknown argument: %s\n", argv[i]);
	  error = 1;
      }
    if (error)
      {
	  do_help ();
	  return -1;
      }
/* checking the arguments */
    if (!img_path)
      {
	  fprintf (stderr, "did you forget setting the --output argument ?\n");
	  error = 1;
      }
    if (!db_path)
      {
	  fprintf (stderr, "did you forget setting the --db-path argument ?\n");
	  error = 1;
      }
    if (!table)
      {
	  printf ("did you forget setting the --table-name argument ?\n");
	  error = 1;
      }
    if (cx == DBL_MAX)
      {
	  printf ("did you forget setting the --center-x argument ?\n");
	  error = 1;
      }
    if (cy == DBL_MAX)
      {
	  printf ("did you forget setting the --center-y argument ?\n");
	  error = 1;
      }
    if (pixel_ratio == DBL_MAX)
      {
	  printf ("did you forget setting the --pixel-ratio argument ?\n");
	  error = 1;
      }
    if (width <= 0)
      {
	  printf ("did you forget setting the --width argument ?\n");
	  error = 1;
      }
    if (height <= 0)
      {
	  printf ("did you forget setting the --height argument ?\n");
	  error = 1;
      }
    if (width < 64 || width > 32768 || height < 64 || height > 32768)
      {
	  printf ("invalid raster dims [%dh X %dv]\n", width, height);
	  error = 1;
      }
    if (transparent_color == WRONG_COLOR)
      {
	  printf ("invalid transparent color '%s'\n", error_color);
	  error = 1;
      }
    if (background_color == WRONG_COLOR)
      {
	  printf ("invalid background color '%s'\n", error_back_color);
	  error = 1;
      }
    if (error)
      {
	  do_help ();
	  return -1;
      }
    if (image_type == GAIA_JPEG_BLOB)
      {
	  /* normalizing the quality factor */
	  if (quality_factor == -999999)
	      quality_factor = 75;
	  if (quality_factor < 10)
	      quality_factor = 10;
	  if (quality_factor > 90)
	      quality_factor = 90;
      }
    return build_raster (img_path, db_path, table, cx, cy, pixel_ratio, width,
			 height, image_type, quality_factor, transparent_color,
			 background_color);
}
