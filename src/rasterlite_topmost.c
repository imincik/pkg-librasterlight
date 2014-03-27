/* 
/ rasterlite_topmpst.c
/
/ a tool building raster topmost pyramid's levels into a SpatiaLite DB 
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
#include <math.h>

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

#ifdef _WIN32
#define strcasecmp	_stricmp
#endif /* not WIN32 */

#if defined(_WIN32) || defined (__MINGW32__)
#define FORMAT_64	"%I64d"
#else
#define FORMAT_64	"%lld"
#endif

#define ARG_NONE			0
#define ARG_DB_PATH			1
#define ARG_TABLE_NAME		2
#define ARG_IMAGE_TYPE		3
#define ARG_QUALITY_FACTOR	4
#define ARG_TILE_SIZE		5
#define ARG_TRANSPARENT		6
#define ARG_BACKGROUND		7

#define TILE_UPPER_LEFT		1
#define TILE_UPPER_RIGHT	2
#define TILE_LOWER_LEFT		3
#define TILE_LOWER_RIGHT	4

#define WRONG_COLOR			-100

struct top_info
{
    sqlite3 *handle;
    sqlite3_stmt *stmt_query;
    sqlite3_stmt *stmt_insert;
    double tile_min_x;
    double tile_min_y;
    double tile_max_x;
    double tile_max_y;
    double x_size;
    double y_size;
    int transparent_color;
    int background_color;
    int image_type;
    int quality_factor;
};

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

static void
free_source_item (struct source_item *item)
{
/* freeing a source item struct */
    if (item->name)
	free (item->name);
    free (item);
}

static void
init_sources (struct sources_list *list)
{
/* initializing the raster sources list */
    list->first = NULL;
    list->last = NULL;
}

static void
free_sources (struct sources_list *list)
{
/* freeing the raster sources list */
    struct source_item *p;
    struct source_item *pN;
    p = list->first;
    while (p)
      {
	  pN = p->next;
	  free_source_item (p);
	  p = pN;
      }
}

static void
add_source (struct sources_list *list, const char *name, int count)
{
/* adding a raster source to the list */
    struct source_item *p;
    int len = strlen (name);
    p = malloc (sizeof (struct source_item));
    p->name = malloc (len + 1);
    strcpy (p->name, name);
    p->count = count;
    p->next = NULL;
    if (list->first == NULL)
	list->first = p;
    if (list->last != NULL)
	list->last->next = p;
    list->last = p;
}

static void
copy_rectangle (rasterliteImagePtr output, rasterliteImagePtr input,
		int transparent_color, int base_x, int base_y)
{
/* copying a raster rectangle */
    int x;
    int y;
    int dst_x;
    int dst_y;
    int pixel;
    for (y = 0; y < input->sy; y++)
      {
	  dst_y = base_y + y;
	  if (dst_y < 0)
	      continue;
	  if (dst_y >= output->sy)
	      break;
	  for (x = 0; x < input->sx; x++)
	    {
		dst_x = base_x + x;
		if (dst_x < 0)
		    continue;
		if (dst_x >= output->sx)
		    break;
		pixel = input->pixels[y][x];
		if (pixel == transparent_color)
		    continue;
		image_set_pixel (output, dst_x, dst_y, pixel);
	    }
      }
}

static int
int_round (double value)
{
/* replacing the C99 round() function */
    double min = floor (value);
    if (fabs (value - min) < 0.5)
	return (int) min;
    return (int) (min + 1.0);
}

static int
export_tile (struct top_info *info, int tile_width, int tile_height, int tileNo,
	     struct tile_info *tile)
{
/* exporting a topmost level tile */
    int ret;
    int hits = 0;
    void *blob;
    int blob_size;
    int new_tile_width;
    int new_tile_height;
    gaiaPolygonPtr polyg;
    rasterliteImagePtr full_size = NULL;
    rasterliteImagePtr thumbnail = NULL;
    int srid;
/* creating the full size image */
    full_size = image_create (tile_width, tile_height);
    image_fill (full_size, info->background_color);
/* binding query params */
    sqlite3_reset (info->stmt_query);
    sqlite3_clear_bindings (info->stmt_query);
    sqlite3_bind_double (info->stmt_query, 1, info->tile_max_x);
    sqlite3_bind_double (info->stmt_query, 2, info->tile_min_x);
    sqlite3_bind_double (info->stmt_query, 3, info->tile_max_y);
    sqlite3_bind_double (info->stmt_query, 4, info->tile_min_y);
    sqlite3_bind_double (info->stmt_query, 5, info->x_size);
    sqlite3_bind_double (info->stmt_query, 6, info->y_size);
    while (1)
      {
	  /* scrolling the result set */
	  ret = sqlite3_step (info->stmt_query);
	  if (ret == SQLITE_DONE)
	      break;		/* end of result set */
	  if (ret == SQLITE_ROW)
	    {
		/* retrieving query values */
		gaiaGeomCollPtr geom = NULL;
		rasterliteImagePtr img = NULL;
		if (sqlite3_column_type (info->stmt_query, 0) == SQLITE_BLOB)
		  {
		      /* fetching Geometry */
		      const void *blob =
			  sqlite3_column_blob (info->stmt_query, 0);
		      int blob_size =
			  sqlite3_column_bytes (info->stmt_query, 0);
		      geom =
			  gaiaFromSpatiaLiteBlobWkb ((const unsigned char *)
						     blob, blob_size);
		  }
		if (sqlite3_column_type (info->stmt_query, 1) == SQLITE_BLOB)
		  {
		      /* fetching Raster Image */
		      const void *blob =
			  sqlite3_column_blob (info->stmt_query, 1);
		      int blob_size =
			  sqlite3_column_bytes (info->stmt_query, 1);
		      int type = gaiaGuessBlobType (blob, blob_size);
		      if (type == GAIA_JPEG_BLOB || type == GAIA_EXIF_BLOB
			  || type == GAIA_EXIF_GPS_BLOB)
			  img = image_from_jpeg (blob_size, (void *) blob);
		      else if (type == GAIA_PNG_BLOB)
			  img = image_from_png (blob_size, (void *) blob);
		      else if (type == GAIA_GIF_BLOB)
			  img = image_from_gif (blob_size, (void *) blob);
		      else if (type == GAIA_TIFF_BLOB)
			  img = image_from_tiff (blob_size, (void *) blob);
		  }
		if (geom && img)
		  {
		      /* drawing the raster tile */
		      double x = (geom->MinX - info->tile_min_x) / info->x_size;
		      double y =
			  (double) tile_height -
			  ((geom->MaxY - info->tile_min_y) / info->y_size);
		      copy_rectangle (full_size, img, info->transparent_color,
				      int_round (x), int_round (y));
		      hits++;
		      srid = geom->Srid;
		  }
		if (geom)
		    gaiaFreeGeomColl (geom);
		if (img)
		    image_destroy (img);
	    }
	  else
	    {
		printf ("SQL error: %s\n", sqlite3_errmsg (info->handle));
		image_destroy (full_size);
		return 0;
	    }
      }
    if (!hits)
      {
	  image_destroy (full_size);	/* 2011-11-18 ASAHI Kosuke */
	  return -1;
      }
/* saving the tile params */
    new_tile_width = tile_width / 2;
    if ((new_tile_width * 2) < tile_width)
	new_tile_width++;
    new_tile_height = tile_height / 2;
    if ((new_tile_height * 2) < tile_height)
	new_tile_height++;
    tile->geometry = gaiaAllocGeomColl ();
    tile->geometry->Srid = srid;
    tile->tileNo = tileNo;
    tile->raster_horz = new_tile_width;
    tile->raster_vert = new_tile_height;
    polyg = gaiaAddPolygonToGeomColl (tile->geometry, 5, 0);
    gaiaSetPoint (polyg->Exterior->Coords, 0, info->tile_min_x,
		  info->tile_min_y);
    gaiaSetPoint (polyg->Exterior->Coords, 4, info->tile_min_x,
		  info->tile_min_y);
    gaiaSetPoint (polyg->Exterior->Coords, 1, info->tile_max_x,
		  info->tile_min_y);
    gaiaSetPoint (polyg->Exterior->Coords, 2, info->tile_max_x,
		  info->tile_max_y);
    gaiaSetPoint (polyg->Exterior->Coords, 3, info->tile_min_x,
		  info->tile_max_y);
/* creating the thumbnail image */
    thumbnail = image_create (new_tile_width, new_tile_height);
    make_thumbnail (thumbnail, full_size);
    if (info->image_type == GAIA_TIFF_BLOB)
      {
	  blob = image_to_tiff_rgb (thumbnail, &blob_size);
	  if (!blob)
	    {
		printf ("TIFF RGB compression error\n");
		return 0;
	    }
      }
    else if (info->image_type == GAIA_PNG_BLOB)
      {
	  blob = image_to_png_rgb (thumbnail, &blob_size);
	  if (!blob)
	    {
		printf ("PNG RGB compression error\n");
		return 0;
	    }
      }
    else
      {
	  blob = image_to_jpeg (thumbnail, &blob_size, info->quality_factor);
	  if (!blob)
	    {
		printf ("JPEG compression error\n");
		return 0;
	    }
      }
/* finally we are ready to INSERT this raster into the DB */
    sqlite3_reset (info->stmt_insert);
    sqlite3_clear_bindings (info->stmt_insert);
    sqlite3_bind_blob (info->stmt_insert, 1, blob, blob_size, free);
    ret = sqlite3_step (info->stmt_insert);
    if (ret == SQLITE_DONE || ret == SQLITE_ROW)
	;
    else
      {
	  printf ("sqlite3_step() error: %s\n", sqlite3_errmsg (info->handle));
	  return 0;
      }
    tile->id_raster = sqlite3_last_insert_rowid (info->handle);
    tile->valid = 1;
    image_destroy (full_size);	/* 2011-11-18 ASAHI Kosuke */
    image_destroy (thumbnail);	/* 2011-11-18 ASAHI Kosuke */
    return 1;
}

static int
build_pyramid_topmost_level (sqlite3 * handle, const char *table,
			     int image_type, int quality_factor, int level,
			     int tile_size, double x_size, double y_size,
			     int transparent_color, int background_color,
			     int test_mode, int verbose)
{
/* building a pyramid topmost level */
    sqlite3_stmt *stmt_insert = NULL;
    sqlite3_stmt *stmt_query = NULL;
    sqlite3_stmt *stmt;
    int ret;
    char *sql_err = NULL;
    char sql[1024];
    char sql2[512];
    double extent_min_x = DBL_MAX;
    double extent_min_y = DBL_MAX;
    double extent_max_x = -DBL_MAX;
    double extent_max_y = -DBL_MAX;
    double extent_width;
    double extent_height;
    int pixel_width;
    int pixel_height;
    int tile_width;
    int tile_height;
    int sect;
    int extra_width;
    int extra_height;
    int baseHorz;
    int baseVert;
    int maxTile = 0;
    int tileNo;
    int raster_ok = 0;
    int eff_tile_width;
    int eff_tile_height;
    int tile_size2 = tile_size * 2;
    struct tile_info tiles[NTILES];
    struct tile_info *tile;
    struct top_info info;
    unsigned char *blob;
    int blob_size;
    int i;
    for (i = 0; i < NTILES; i++)
      {
	  tile = &(tiles[i]);
	  tile->valid = 0;
	  tile->geometry = NULL;
      }
    printf ("\nGenerating thumbnail tiles: Pyramid Topmost Level %d\n", level);
    printf ("------------------\n");
/* cheching the full extent */
    strcpy (sql, "SELECT Min(MbrMinX(geometry)), Min(MbrMinY(geometry)), ");
    strcat (sql, "Max(MbrMaxX(geometry)), Max(MbrMaxY(geometry)) ");
    sprintf (sql2, "FROM \"%s_metadata\"", table);
    strcat (sql, sql2);
    strcat (sql, " WHERE pixel_x_size = ? AND pixel_y_size = ?");
    ret = sqlite3_prepare_v2 (handle, sql, strlen (sql), &stmt, NULL);
    if (ret != SQLITE_OK)
      {
	  printf ("SQL error: %s\n", sqlite3_errmsg (handle));
	  printf ("Sorry, cowardly quitting ...\n");
	  return 0;
      }
/* binding query params */
    sqlite3_reset (stmt);
    sqlite3_clear_bindings (stmt);
    sqlite3_bind_double (stmt, 1, x_size);
    sqlite3_bind_double (stmt, 2, y_size);
    while (1)
      {
	  /* scrolling the result set */
	  ret = sqlite3_step (stmt);
	  if (ret == SQLITE_DONE)
	      break;		/* end of result set */
	  if (ret == SQLITE_ROW)
	    {
		/* retrieving query values */
		extent_min_x = sqlite3_column_double (stmt, 0);
		extent_min_y = sqlite3_column_double (stmt, 1);
		extent_max_x = sqlite3_column_double (stmt, 2);
		extent_max_y = sqlite3_column_double (stmt, 3);
	    }
	  else
	    {
		printf ("SQL error: %s\n", sqlite3_errmsg (handle));
		printf ("Sorry, cowardly quitting ...\n");
		sqlite3_finalize (stmt);
		return 0;
	    }
      }
    sqlite3_finalize (stmt);
/* computing the level total width and height in pixels */
    extent_width = extent_max_x - extent_min_x;
    extent_height = extent_max_y - extent_min_y;
    pixel_width = (int) (extent_width / x_size);
    if ((double) pixel_width * x_size < extent_width)
	pixel_width++;
    pixel_height = (int) (extent_height / y_size);
    if ((double) pixel_height * y_size < extent_height)
	pixel_height++;
    if (pixel_width < 0 || pixel_height < 0)
      {
	  printf ("Negative dimension [%d x %d]\n", pixel_width, pixel_height);
	  printf ("Sorry, cowardly quitting ...\n");
	  return 0;
      }
/* computing the tile dims */
    tile_width = pixel_width;
    tile_height = pixel_height;
    sect = 1;
    while (1)
      {
	  if (tile_width > tile_size2 || tile_height > tile_size2)
	    {
		sect++;
		tile_width = pixel_width / sect;
		tile_height = pixel_height / sect;
		continue;
	    }
	  if ((tile_width * sect) < pixel_width)
	      tile_width++;
	  if ((tile_height * sect) < pixel_height)
	      tile_height++;
	  break;
      }
    extra_width = tile_width * sect;
    extra_height = tile_height * sect;
    baseHorz = 0;
    baseVert = 0;
    while (1)
      {
	  /* computing the totale tiles # */
	  maxTile++;
	  baseHorz += tile_width;
	  if (baseHorz >= extra_width)
	    {
		baseHorz = 0;
		baseVert += tile_height;
		if (baseVert >= extra_height)
		    break;
	    }
      }
    printf ("RequiredTiles:   %d tiles [%dh x %dv]\n", maxTile, tile_width / 2,
	    tile_height / 2);
    printf ("----------------\n");
    if (test_mode)
      {
	  printf ("\n");
	  return 1;
      }

/* the complete operation is handled as an unique SQL Transaction */
    ret = sqlite3_exec (handle, "BEGIN", NULL, NULL, &sql_err);
    if (ret != SQLITE_OK)
      {
	  printf ("BEGIN TRANSACTION error: %s\n", sql_err);
	  sqlite3_free (sql_err);
	  goto stop;
      }

/* creating the INSERT INTO xx_rasters prepared statement */
    sprintf (sql, "INSERT INTO \"%s_rasters\" ", table);
    strcat (sql, "(id, raster) ");
    strcat (sql, " VALUES (NULL, ?)");
    ret = sqlite3_prepare_v2 (handle, sql, strlen (sql), &stmt_insert, NULL);
    if (ret != SQLITE_OK)
      {
	  printf ("SQL error: %s\n%s\n", sql, sqlite3_errmsg (handle));
	  goto stop;
      }
/* creating the SELECT SQL statement */
    strcpy (sql, "SELECT m.geometry, r.raster FROM \"");
    strcat (sql, table);
    strcat (sql, "_metadata\" AS m, \"");
    strcat (sql, table);
    strcat (sql, "_rasters\" AS r WHERE m.ROWID IN (SELECT pkid ");
    strcat (sql, "FROM \"idx_");
    strcat (sql, table);
    strcat (sql, "_metadata_geometry\" ");
    strcat (sql, "WHERE xmin < ? AND xmax > ? AND ymin < ? AND ymax > ?) ");
    strcat (sql,
	    "AND m.pixel_x_size = ? AND m.pixel_y_size = ? AND r.id = m.id");
    ret = sqlite3_prepare_v2 (handle, sql, strlen (sql), &stmt_query, NULL);
    if (ret != SQLITE_OK)
      {
	  printf ("SQL error: %s\n", sqlite3_errmsg (handle));
	  goto stop;
      }

    info.handle = handle;
    info.stmt_query = stmt_query;
    info.stmt_insert = stmt_insert;
    info.transparent_color = transparent_color;
    info.background_color = background_color;
    info.image_type = image_type;
    info.quality_factor = quality_factor;
    info.x_size = x_size;
    info.y_size = y_size;
    info.tile_min_x = extent_min_x;
    info.tile_max_y = extent_max_y;
    info.tile_min_y = extent_max_y - ((double) tile_height * y_size);
    if (info.tile_min_y < extent_min_y)
	info.tile_min_y = extent_min_y;
    baseHorz = 0;
    baseVert = 0;
    for (tileNo = 0; tileNo < maxTile; tileNo++)
      {
	  /* exporting sectioned images AKA tiles */
	  if (verbose)
	    {
		fprintf (stderr, "\tloading tile %d of %d\n", tileNo + 1,
			 maxTile);
		fflush (stderr);
	    }
	  if ((baseHorz + tile_width) <= pixel_width)
	      eff_tile_width = tile_width;
	  else
	      eff_tile_width = pixel_width - baseHorz;
	  if ((baseVert + tile_height) <= pixel_height)
	      eff_tile_height = tile_height;
	  else
	      eff_tile_height = pixel_height - baseVert;
	  info.tile_max_x =
	      extent_min_x + ((double) (baseHorz + eff_tile_width) * x_size);
	  if (info.tile_max_x > extent_max_x)
	      info.tile_max_x = extent_max_x;
	  if (!export_tile
	      (&info, eff_tile_width, eff_tile_height, tileNo,
	       &(tiles[tileNo])))
	      goto stop;
	  raster_ok++;
	  baseHorz += tile_width;
	  info.tile_min_x = info.tile_max_x;
	  if (baseHorz >= extra_width)
	    {
		baseHorz = 0;
		baseVert += tile_height;
		if (baseVert >= extra_height)
		    break;
		info.tile_min_x = extent_min_x;
		info.tile_max_y = info.tile_min_y;
		info.tile_min_y =
		    extent_max_y -
		    ((double) (baseVert + eff_tile_height) * y_size);
		if (info.tile_min_y < extent_min_y)
		    info.tile_min_y = extent_min_y;
	    }
      }
/* finalizing the INSERT INTO xx_rasters prepared statement */
    sqlite3_finalize (stmt_insert);
    stmt_insert = NULL;
/* finalizing the SELECT prepared statement */
    sqlite3_finalize (stmt_query);
    stmt_query = NULL;

/* creating the INSERT INTO xx_metadata prepared statements */
    sprintf (sql, "INSERT INTO \"%s_metadata\" ", table);
    strcat (sql, "(id, source_name, tile_id, width, height, ");
    strcat (sql, "pixel_x_size, pixel_y_size, geometry) ");
    strcat (sql, " VALUES (?, ?, ?, ?, ?, ?, ?, ?)");
    ret = sqlite3_prepare_v2 (handle, sql, strlen (sql), &stmt, NULL);
    if (ret != SQLITE_OK)
      {
	  printf ("SQL error: %s\n%s\n", sql, sqlite3_errmsg (handle));
	  goto stop;
      }

    for (i = 0; i < NTILES; i++)
      {
	  /* now we have to INSERT the raster's metadata into the DB */
	  tile = &(tiles[i]);
	  if (!(tile->valid))
	      continue;
	  sqlite3_reset (stmt);
	  sqlite3_clear_bindings (stmt);
	  sqlite3_bind_int64 (stmt, 1, tile->id_raster);
	  sqlite3_bind_text (stmt, 2, "TopMost", strlen ("TopMost"),
			     SQLITE_STATIC);
	  sqlite3_bind_int (stmt, 3, tile->tileNo);
	  sqlite3_bind_int (stmt, 4, tile->raster_horz);
	  sqlite3_bind_int (stmt, 5, tile->raster_vert);
	  sqlite3_bind_double (stmt, 6, x_size * 2.0);
	  sqlite3_bind_double (stmt, 7, y_size * 2.0);
	  gaiaToSpatiaLiteBlobWkb (tile->geometry, &blob, &blob_size);
	  sqlite3_bind_blob (stmt, 8, blob, blob_size, free);
	  ret = sqlite3_step (stmt);
	  if (ret == SQLITE_DONE || ret == SQLITE_ROW)
	      ;
	  else
	    {
		printf ("sqlite3_step() error: %s\n", sqlite3_errmsg (handle));
		sqlite3_finalize (stmt);
		goto stop;
	    }
      }
/* finalizing the INSERT INTO xx_metadata prepared statement */
    sqlite3_finalize (stmt);

/* committing the still pending SQL Transaction */
    ret = sqlite3_exec (handle, "COMMIT", NULL, NULL, &sql_err);
    if (ret != SQLITE_OK)
      {
	  printf ("COMMIT TRANSACTION error: %s\n", sql_err);
	  sqlite3_free (sql_err);
	  goto stop;
      }

    for (i = 0; i < NTILES; i++)
      {
	  tile = &(tiles[i]);
	  if (tile->geometry)
	      gaiaFreeGeomColl (tile->geometry);
      }

    return raster_ok;

  stop:
    if (stmt_insert)
	sqlite3_finalize (stmt_insert);
    if (stmt_query)
	sqlite3_finalize (stmt_query);
    for (i = 0; i < NTILES; i++)
      {
	  tile = &(tiles[i]);
	  if (tile->geometry)
	      gaiaFreeGeomColl (tile->geometry);
      }
    printf ("***\n***  Sorry, some unexpected error occurred ...\n***\n\n");
    printf ("----------------\n\n");
    return 0;
}

static int
refresh_resolution_index (sqlite3 * handle, const char *table)
{
/* refreshing the 'idx_resolution' index */
    int ret;
    char *errMsg = NULL;
    char sql[1024];
    strcpy (sql, "DROP INDEX IF EXISTS idx_resolution");
    ret = sqlite3_exec (handle, sql, NULL, 0, &errMsg);
    if (ret != SQLITE_OK)
      {
	  printf ("SQL error: %s\n", errMsg);
	  sqlite3_free (errMsg);
	  return 0;
      }
    sprintf (sql, "CREATE INDEX idx_resolution ON \"%s_metadata\" ", table);
    strcat (sql, " (pixel_x_size, pixel_y_size)");
    ret = sqlite3_exec (handle, sql, NULL, 0, &errMsg);
    if (ret != SQLITE_OK)
      {
	  printf ("SQL error: %s\n", errMsg);
	  sqlite3_free (errMsg);
	  return 0;
      }
    printf ("\nindex \"idx_resolution\" has been successfully refreshed\n");
    return 1;
}

static int
update_raster_pyramids (sqlite3 * handle, const char *table)
{
/* updating the 'raster_pyramids' table */
    int ret;
    char *errMsg = NULL;
    char sql[1024];
    char sql2[512];
    sprintf (sql, "DELETE FROM raster_pyramids WHERE table_prefix LIKE '%s'",
	     table);
    ret = sqlite3_exec (handle, sql, NULL, 0, &errMsg);
    if (ret != SQLITE_OK)
      {
	  printf ("SQL error: %s\n", errMsg);
	  sqlite3_free (errMsg);
	  return 0;
      }
    strcpy (sql, "INSERT INTO raster_pyramids ");
    strcat (sql, "(table_prefix, pixel_x_size, pixel_y_size, tile_count) ");
    sprintf (sql2, "SELECT '%s', pixel_x_size, pixel_y_size, Count(*) ", table);
    strcat (sql, sql2);
    sprintf (sql2, "FROM \"%s_metadata\" ", table);
    strcat (sql, sql2);
    strcat (sql, "WHERE pixel_x_size > 0 AND pixel_y_size > 0 ");
    strcat (sql, "GROUP BY pixel_x_size, pixel_y_size");
    ret = sqlite3_exec (handle, sql, NULL, 0, &errMsg);
    if (ret != SQLITE_OK)
      {
	  printf ("SQL error: %s\n", errMsg);
	  sqlite3_free (errMsg);
	  return 0;
      }
    printf ("\ntable \"raster_pyramids\" has been successfully updated\n");
    return 1;
}

static int
build_top_pyramids (sqlite3 * handle, const char *table, int test_mode,
		    int verbose, int image_type, int quality_factor,
		    int tile_size, int transparent_color, int background_color)
{
/* trying to build the topmost pyramid's levels for this raster data source */
    sqlite3_stmt *stmt;
    int ret;
    char sql[1024];
    char sql2[512];
    double max_x_size = 0.0;
    double max_y_size = 0.0;
    double min_x_size = 0.0;
    double min_y_size = 0.0;
    int to_be_deleted = 0;
    struct sources_list sources;
    struct source_item *item;
    int count;
    const char *raster_source;
    int nitems;
    char *sql_err = NULL;
    int level = 1;
    double new_x_size;
    double new_y_size;
/* retrieving the pyramid max scale */
    sprintf (sql,
	     "SELECT Max(pixel_x_size), Max(pixel_y_size) FROM \"%s_metadata\"",
	     table);
    strcat (sql, " WHERE source_name <> 'TopMost'");
    strcat (sql, " AND pixel_x_size > 0 AND pixel_y_size > 0");
    ret = sqlite3_prepare_v2 (handle, sql, strlen (sql), &stmt, NULL);
    if (ret != SQLITE_OK)
      {
	  printf ("SQL error: %s\n", sqlite3_errmsg (handle));
	  printf ("Sorry, cowardly quitting ...\n");
	  return 0;
      }
    while (1)
      {
	  /* scrolling the result set */
	  ret = sqlite3_step (stmt);
	  if (ret == SQLITE_DONE)
	      break;		/* end of result set */
	  if (ret == SQLITE_ROW)
	    {
		/* retrieving query values */
		max_x_size = sqlite3_column_double (stmt, 0);
		max_y_size = sqlite3_column_double (stmt, 1);
		min_x_size = sqlite3_column_double (stmt, 2);
		min_y_size = sqlite3_column_double (stmt, 3);
	    }
	  else
	    {
		printf ("SQL error: %s\n", sqlite3_errmsg (handle));
		printf ("Sorry, cowardly quitting ...\n");
		sqlite3_finalize (stmt);
		return 0;
	    }
      }
    sqlite3_finalize (stmt);
    if (max_x_size == min_x_size && max_y_size == min_y_size)
      {
	  printf ("The Raster Data Source '%s' ", table);
	  printf ("doesn't seems to contain any Pyramid Level\n\n");
	  printf
	      ("Are you really sure 'rasterlite_pyramid' was already invoked ?\n\n");
	  printf ("Sorry, cowardly quitting ...\n");
      }

/* counting the already existing topmost pyramid tiles */
    sprintf (sql, "SELECT Count(*) FROM \"%s_metadata\"", table);
    strcat (sql, " WHERE pixel_x_size > ? AND pixel_y_size > ?");
    ret = sqlite3_prepare_v2 (handle, sql, strlen (sql), &stmt, NULL);
    if (ret != SQLITE_OK)
      {
	  printf ("SQL error: %s\n", sqlite3_errmsg (handle));
	  printf ("Sorry, cowardly quitting ...\n");
	  return 0;
      }
/* binding query params */
    sqlite3_reset (stmt);
    sqlite3_clear_bindings (stmt);
    sqlite3_bind_double (stmt, 1, max_x_size);
    sqlite3_bind_double (stmt, 2, max_y_size);
    while (1)
      {
	  /* scrolling the result set */
	  ret = sqlite3_step (stmt);
	  if (ret == SQLITE_DONE)
	      break;		/* end of result set */
	  if (ret == SQLITE_ROW)
	    {
		/* retrieving query values */
		to_be_deleted = sqlite3_column_int (stmt, 0);
	    }
	  else
	    {
		printf ("SQL error: %s\n", sqlite3_errmsg (handle));
		printf ("Sorry, cowardly quitting ...\n");
		sqlite3_finalize (stmt);
		return 0;
	    }
      }
    sqlite3_finalize (stmt);

    init_sources (&sources);
/* identifying the raster sources to be pyramidized */
    sprintf (sql, "SELECT source_name, Count(*) FROM \"%s_metadata\"", table);
    strcat (sql, " WHERE pixel_x_size = ? AND pixel_y_size = ?");
    strcat (sql, " GROUP BY source_name ORDER BY source_name");
    ret = sqlite3_prepare_v2 (handle, sql, strlen (sql), &stmt, NULL);
    if (ret != SQLITE_OK)
      {
	  printf ("SQL error: %s\n", sqlite3_errmsg (handle));
	  printf ("Sorry, cowardly quitting ...\n");
	  return 0;
      }
/* binding query params */
    sqlite3_reset (stmt);
    sqlite3_clear_bindings (stmt);
    sqlite3_bind_double (stmt, 1, max_x_size);
    sqlite3_bind_double (stmt, 2, max_y_size);
    while (1)
      {
	  /* scrolling the result set */
	  ret = sqlite3_step (stmt);
	  if (ret == SQLITE_DONE)
	      break;		/* end of result set */
	  if (ret == SQLITE_ROW)
	    {
		/* retrieving query values */
		raster_source = (const char *) sqlite3_column_text (stmt, 0);
		count = sqlite3_column_int (stmt, 1);
		add_source (&sources, raster_source, count);
	    }
	  else
	    {
		printf ("SQL error: %s\n", sqlite3_errmsg (handle));
		printf ("Sorry, cowardly quitting ...\n");
		sqlite3_finalize (stmt);
		free_sources (&sources);
		return 0;
	    }
      }
    sqlite3_finalize (stmt);

    nitems = 0;
/* pyramidize preview */
    printf ("Raster sources to be used for Topmost Pyramid's Levels:\n");
    printf ("=======================================================\n");
    item = sources.first;
    while (item)
      {
	  printf ("Raster source: \"%s\"\tTiles=%d\n", item->name, item->count);
	  nitems++;
	  item = item->next;
      }
    if (!nitems)
      {
	  printf ("There is no raster source to be pyramidized\n");
	  printf ("Sorry, cowardly quitting ...\n");
	  free_sources (&sources);
	  return 0;
      }
    if (test_mode)
      {
	  free_sources (&sources);
	  return 0;
      }
    if (to_be_deleted)
      {
	  /*
	     / deleting any already existing thumbnail tile 
	     / the complete operation is handled as an unique SQL Transaction 
	   */
	  ret = sqlite3_exec (handle, "BEGIN", NULL, NULL, &sql_err);
	  if (ret != SQLITE_OK)
	    {
		printf ("BEGIN TRANSACTION error: %s\n", sql_err);
		sqlite3_free (sql_err);
		return 0;
	    }

	  sprintf (sql, "DELETE FROM \"%s_rasters\"", table);
	  sprintf (sql2, " WHERE id IN (SELECT id FROM \"%s_metadata\" ",
		   table);
	  strcat (sql, sql2);
	  strcat (sql, " WHERE pixel_x_size > ? AND pixel_y_size > ?)");
	  ret = sqlite3_prepare_v2 (handle, sql, strlen (sql), &stmt, NULL);
	  if (ret != SQLITE_OK)
	    {
		printf ("SQL error: %s\n", sqlite3_errmsg (handle));
		printf ("Sorry, cowardly quitting ...\n");
		return 0;
	    }
	  /* binding query params */
	  sqlite3_reset (stmt);
	  sqlite3_clear_bindings (stmt);
	  sqlite3_bind_double (stmt, 1, max_x_size);
	  sqlite3_bind_double (stmt, 2, max_y_size);
	  ret = sqlite3_step (stmt);
	  if (ret == SQLITE_DONE || ret == SQLITE_ROW)
	      ;
	  else
	    {
		printf ("SQL error: %s\n", sqlite3_errmsg (handle));
		printf ("Sorry, cowardly quitting ...\n");
		sqlite3_finalize (stmt);
		free_sources (&sources);
		return 0;
	    }
	  sqlite3_finalize (stmt);

	  sprintf (sql, "DELETE FROM \"%s_metadata\"", table);
	  strcat (sql, " WHERE pixel_x_size > ? AND pixel_y_size > ?");
	  ret = sqlite3_prepare_v2 (handle, sql, strlen (sql), &stmt, NULL);
	  if (ret != SQLITE_OK)
	    {
		printf ("SQL error: %s\n", sqlite3_errmsg (handle));
		printf ("Sorry, cowardly quitting ...\n");
		return 0;
	    }
	  /* binding query params */
	  sqlite3_reset (stmt);
	  sqlite3_clear_bindings (stmt);
	  sqlite3_bind_double (stmt, 1, max_x_size);
	  sqlite3_bind_double (stmt, 2, max_y_size);
	  ret = sqlite3_step (stmt);
	  if (ret == SQLITE_DONE || ret == SQLITE_ROW)
	      ;
	  else
	    {
		printf ("SQL error: %s\n", sqlite3_errmsg (handle));
		printf ("Sorry, cowardly quitting ...\n");
		sqlite3_finalize (stmt);
		free_sources (&sources);
		return 0;
	    }
	  sqlite3_finalize (stmt);

	  ret = sqlite3_exec (handle, "COMMIT", NULL, NULL, &sql_err);
	  if (ret != SQLITE_OK)
	    {
		printf ("COMMIT TRANSACTION error: %s\n", sql_err);
		sqlite3_free (sql_err);
		return 0;
	    }

	  printf ("\nAlready existing thumbnail tiles where found:\n");
	  printf ("=======================================================\n");
	  printf ("table \"%s_raster\":   %d rows deleted\n", table,
		  to_be_deleted);
	  printf ("table \"%s_metadata\": %d rows deleted\n", table,
		  to_be_deleted);
	  printf ("=======================================================\n");
      }
/* proceding to actually building topmost pyramid's levels */
    printf ("\nPyramidizing Topmost Levels:\n");
    printf ("=======================================================\n");
    new_x_size = max_x_size;
    new_y_size = max_y_size;
    while (1)
      {
	  if (build_pyramid_topmost_level
	      (handle, table, image_type, quality_factor, level, tile_size,
	       new_x_size, new_y_size, transparent_color, background_color,
	       test_mode, verbose) <= 1)
	      break;
	  /* looping on the next level */
	  level++;
	  new_x_size *= 2.0;
	  new_y_size *= 2.0;
      }

    free_sources (&sources);
    update_raster_pyramids (handle, table);
    refresh_resolution_index (handle, table);
    return 1;
}

static sqlite3 *
db_connect (const char *path, const char *table)
{
/* trying to connect SpatiaLite DB */
    sqlite3 *handle = NULL;
    int ret;
    char rasters[512];
    char meta[512];
    char sql[1024];
    int spatialite_rs = 0;
    int spatialite_legacy_gc = 0;
    int spatialite_gc = 0;
    int rs_srid = 0;
    int auth_name = 0;
    int auth_srid = 0;
    int ref_sys_name = 0;
    int proj4text = 0;
    int f_table_name = 0;
    int f_geometry_column = 0;
    int coord_dimension = 0;
    int gc_srid = 0;
    int type = 0;
    int geometry_type = 0;
    int spatial_index_enabled = 0;
    int tbl_rasters = 0;
    int tbl_meta = 0;
    const char *name;
    int i;
    char **results;
    int rows;
    int columns;

    spatialite_init (0);
    printf ("SQLite version: %s\n", sqlite3_libversion ());
    printf ("SpatiaLite version: %s\n\n", spatialite_version ());

    ret = sqlite3_open_v2 (path, &handle, SQLITE_OPEN_READWRITE, NULL);
    if (ret != SQLITE_OK)
      {
	  printf ("cannot open DB '%s': %s\n", path, sqlite3_errmsg (handle));
	  sqlite3_close (handle);
	  return NULL;
      }

/* checking the GEOMETRY_COLUMNS table */
    strcpy (sql, "PRAGMA table_info(geometry_columns)");
    ret = sqlite3_get_table (handle, sql, &results, &rows, &columns, NULL);
    if (ret != SQLITE_OK)
	goto unknown;
    if (rows < 1)
	;
    else
      {
	  for (i = 1; i <= rows; i++)
	    {
		name = results[(i * columns) + 1];
		if (strcasecmp (name, "f_table_name") == 0)
		    f_table_name = 1;
		if (strcasecmp (name, "f_geometry_column") == 0)
		    f_geometry_column = 1;
		if (strcasecmp (name, "coord_dimension") == 0)
		    coord_dimension = 1;
		if (strcasecmp (name, "srid") == 0)
		    gc_srid = 1;
		if (strcasecmp (name, "type") == 0)
		    type = 1;
		if (strcasecmp (name, "geometry_type") == 0)
		    geometry_type = 1;
		if (strcasecmp (name, "spatial_index_enabled") == 0)
		    spatial_index_enabled = 1;
	    }
      }
    sqlite3_free_table (results);
    if (f_table_name && f_geometry_column && type && coord_dimension && gc_srid
	&& spatial_index_enabled)
	spatialite_legacy_gc = 1;
    if (f_table_name && f_geometry_column && geometry_type && coord_dimension
	&& gc_srid && spatial_index_enabled)
	spatialite_gc = 1;

/* checking the SPATIAL_REF_SYS table */
    strcpy (sql, "PRAGMA table_info(spatial_ref_sys)");
    ret = sqlite3_get_table (handle, sql, &results, &rows, &columns, NULL);
    if (ret != SQLITE_OK)
	goto unknown;
    if (rows < 1)
	;
    else
      {
	  for (i = 1; i <= rows; i++)
	    {
		name = results[(i * columns) + 1];
		if (strcasecmp (name, "srid") == 0)
		    rs_srid = 1;
		if (strcasecmp (name, "auth_name") == 0)
		    auth_name = 1;
		if (strcasecmp (name, "auth_srid") == 0)
		    auth_srid = 1;
		if (strcasecmp (name, "ref_sys_name") == 0)
		    ref_sys_name = 1;
		if (strcasecmp (name, "proj4text") == 0)
		    proj4text = 1;
	    }
      }
    sqlite3_free_table (results);
    if (rs_srid && auth_name && auth_srid && ref_sys_name && proj4text)
	spatialite_rs = 1;

/* verifying the MetaData format */
    if ((spatialite_legacy_gc || spatialite_gc) && spatialite_rs)
	goto check_tables;

  unknown:
    if (handle)
	sqlite3_close (handle);
    printf ("DB '%s'\n", path);
    printf ("doesn't seems to contain valid Spatial Metadata ...\n\n");
    printf ("Please, run the 'spatialite-init' SQL script \n");
    printf ("in order to initialize Spatial Metadata\n\n");
    return NULL;

  check_tables:
/* checking the PREFIX_rasters and PREFIX_metadata tables */
    sprintf (rasters, "%s_rasters", table);
    sprintf (meta, "%s_metadata", table);
    strcpy (sql, "SELECT name FROM sqlite_master WHERE type = 'table'");
    ret = sqlite3_get_table (handle, sql, &results, &rows, &columns, NULL);
    if (ret != SQLITE_OK)
	goto no_table;
    if (rows < 1)
	;
    else
      {
	  for (i = 1; i <= rows; i++)
	    {
		name = results[(i * columns) + 0];
		if (strcasecmp (name, rasters) == 0)
		    tbl_rasters = 1;
		if (strcasecmp (name, meta) == 0)
		    tbl_meta = 1;
	    }
      }
    sqlite3_free_table (results);
    if (tbl_rasters && tbl_meta)
      {
	  /* checking the raster_pyramids table */
	  int table_prefix = 0;
	  int pixel_x_size = 0;
	  int pixel_y_size = 0;
	  int tile_count = 0;
	  strcpy (sql, "PRAGMA table_info(raster_pyramids)");
	  ret =
	      sqlite3_get_table (handle, sql, &results, &rows, &columns, NULL);
	  if (ret != SQLITE_OK)
	      goto no_pyramids;
	  if (rows < 1)
	      ;
	  else
	    {
		for (i = 1; i <= rows; i++)
		  {
		      name = results[(i * columns) + 1];
		      if (name != NULL)
			{
			    if (strcasecmp (name, "table_prefix") == 0)
				table_prefix = 1;
			    if (strcasecmp (name, "pixel_x_size") == 0)
				pixel_x_size = 1;
			    if (strcasecmp (name, "pixel_y_size") == 0)
				pixel_y_size = 1;
			    if (strcasecmp (name, "tile_count") == 0)
				tile_count = 1;
			}
		  }
	    }
	  sqlite3_free_table (results);
	  if (table_prefix && pixel_x_size && pixel_y_size && tile_count)
	      return handle;

	no_pyramids:
	  if (handle)
	      sqlite3_close (handle);
	  printf ("DB '%s'\n", path);
	  printf
	      ("doesn't seems to contain the \"raster_pyramids\" table [or invalid layout found] !!!\n\n");
	  return NULL;
      }

  no_table:
    if (handle)
	sqlite3_close (handle);
    printf ("DB '%s'\n", path);
    printf ("doesn't seems to contain a \"%s\"\n", rasters);
    printf ("or \"%s\" table !!!\n\n", meta);
    return NULL;
}

static void
do_help ()
{
/* printing the argument list */
    fprintf (stderr, "\n\nusage: rasterlite_topmost ARGLIST\n");
    fprintf (stderr,
	     "==============================================================\n");
    fprintf (stderr,
	     "-? or --help                      print this help message\n");
    fprintf (stderr,
	     "-t or --test                      test only - no actual action\n");
    fprintf (stderr, "-v or --verbose                   verbose output\n");
    fprintf (stderr,
	     "-d or --db-path     pathname      the SpatiaLite db path\n");
    fprintf (stderr, "-T or --table-name  name          DB table name\n");
    fprintf (stderr, "-i or --image-type  type          [JPEG|TIFF]\n");
    fprintf (stderr,
	     "-q or --quality     num           [default = 75(JPEG)]\n");
    fprintf (stderr, "-c or --transparent-color 0xRRGGBB [default = NONE]\n");
    fprintf (stderr,
	     "-b or --background-color  0xRRGGBB [default = 0x000000]\n");
}

int
main (int argc, char *argv[])
{
/* the MAIN function simply perform arguments checking */
    sqlite3 *handle;
    int i;
    int next_arg = ARG_NONE;
    const char *path = NULL;
    const char *table = NULL;
    int test_mode = 0;
    int quality_factor = -999999;
    int image_type = GAIA_PNG_BLOB;
    int verbose = 0;
    int transparent_color = -1;
    int background_color = true_color (0, 0, 0);
    int error = 0;
    int cnt = 0;
    char error_color[1024];
    char error_back_color[1024];
    int tile_size = 512;
    for (i = 1; i < argc; i++)
      {
	  /* parsing the invocation arguments */
	  if (next_arg != ARG_NONE)
	    {
		switch (next_arg)
		  {
		  case ARG_DB_PATH:
		      path = argv[i];
		      break;
		  case ARG_TABLE_NAME:
		      table = argv[i];
		      break;
		  case ARG_IMAGE_TYPE:
		      if (strcasecmp (argv[i], "JPEG") == 0)
			  image_type = GAIA_JPEG_BLOB;
		      if (strcasecmp (argv[i], "PNG") == 0)
			  image_type = GAIA_PNG_BLOB;
		      if (strcasecmp (argv[i], "TIFF") == 0)
			  image_type = GAIA_TIFF_BLOB;
		      break;
		  case ARG_QUALITY_FACTOR:
		      quality_factor = atoi (argv[i]);
		      break;
		  case ARG_TILE_SIZE:
		      tile_size = atoi (argv[i]);
		      if (tile_size < 128)
			  tile_size = 128;
		      if (tile_size > 8192)
			  tile_size = 8192;
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
	  if (strcmp (argv[i], "-i") == 0)
	    {
		next_arg = ARG_IMAGE_TYPE;
		continue;
	    }
	  if (strcmp (argv[i], "-s") == 0)
	    {
		next_arg = ARG_TILE_SIZE;
		continue;
	    }
	  if (strcasecmp (argv[i], "--tile-size") == 0)
	    {
		next_arg = ARG_TILE_SIZE;
		continue;
	    }
	  if (strcasecmp (argv[i], "--image-type") == 0)
	    {
		next_arg = ARG_IMAGE_TYPE;
		continue;
	    }
	  if (strcasecmp (argv[i], "--test") == 0)
	    {
		test_mode = 1;
		continue;
	    }
	  if (strcmp (argv[i], "-t") == 0)
	    {
		test_mode = 1;
		continue;
	    }
	  if (strcasecmp (argv[i], "--verbose") == 0)
	    {
		verbose = 1;
		continue;
	    }
	  if (strcmp (argv[i], "-v") == 0)
	    {
		verbose = 1;
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
    if (!path)
      {
	  fprintf (stderr, "did you forget setting the --db-path argument ?\n");
	  error = 1;
      }
    if (!table)
      {
	  printf ("did you forget setting the --table-name argument ?\n");
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
    printf ("=====================================================\n");
    printf ("             Arguments Summary\n");
    printf ("=====================================================\n");
    if (test_mode)
	printf ("TEST mode: no actual action will be performed\n");
    printf ("SpatiaLite DB path: '%s'\n", path);
    printf ("Table prefix: '%s'\n", table);
    printf ("\ttable '%s_rasters' is assumed to store raster tiles\n", table);
    printf ("\ttable '%s_metadata' is assumed to store tile metadata\n", table);
    printf ("Tile preferred max size: %d pixels\n", tile_size);
    switch (image_type)
      {
      case GAIA_JPEG_BLOB:
	  printf ("Topmost Pyramid Tile image type: JPEG quality=%d\n",
		  quality_factor);
	  break;
      case GAIA_PNG_BLOB:
	  printf ("Topmost Pyramid Tile image type: PNG [RGB]\n");
	  break;
      case GAIA_TIFF_BLOB:
	  printf ("Topmost Pyramid Tile image type: TIFF [RGB]\n");
	  break;
      default:
	  printf ("Topmost Tile image type: UNKNOWN\n");
	  break;
      };
    printf ("Background Color: 0x%02x%02x%02x\n",
	    true_color_get_red (background_color),
	    true_color_get_green (background_color),
	    true_color_get_blue (background_color));
    if (transparent_color < 0)
	printf ("Transparent Color: NONE\n");
    else
	printf ("Transparent Color: 0x%02x%02x%02x\n",
		true_color_get_red (transparent_color),
		true_color_get_green (transparent_color),
		true_color_get_blue (transparent_color));
    printf ("=====================================================\n\n");
/* trying to connect DB */
    handle = db_connect (path, table);
    if (!handle)
	return 1;
    cnt =
	build_top_pyramids (handle, table, test_mode, verbose, image_type,
			    quality_factor, tile_size, transparent_color,
			    background_color);
/* disconnecting DB */
    sqlite3_close (handle);
    return 0;
}
