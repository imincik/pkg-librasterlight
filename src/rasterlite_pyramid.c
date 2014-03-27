/* 
/ rasterlite_pyramid.c
/
/ a tool building raster pyramids into a SpatiaLite DB 
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

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <float.h>

#include "rasterlite_tiff_hdrs.h"
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

#define TILE_UPPER_LEFT		1
#define TILE_UPPER_RIGHT	2
#define TILE_LOWER_LEFT		3
#define TILE_LOWER_RIGHT	4

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
free_tile_item (struct tile_item *item)
{
/* freeing a tile item struct */
    free (item);
}

static void
init_tiles (struct tiles_list *list)
{
/* initializing the raster tiles list */
    list->first = NULL;
    list->last = NULL;
}

static void
free_tiles (struct tiles_list *list)
{
/* freeing the raster tiles list */
    struct tile_item *p;
    struct tile_item *pN;
    p = list->first;
    while (p)
      {
	  pN = p->next;
	  free_tile_item (p);
	  p = pN;
      }
}

static void
add_tile (struct tiles_list *list, sqlite3_int64 id, int srid, double min_x,
	  double min_y, double max_x, double max_y, int width, int height)
{
/* adding a raster tile to the list */
    struct tile_item *p;
    p = malloc (sizeof (struct tile_item));
    p->id = id;
    p->srid = srid;
    p->min_x = min_x;
    p->min_y = min_y;
    p->max_x = max_x;
    p->max_y = max_y;
    p->width = width;
    p->height = height;
    p->next = NULL;
    if (list->first == NULL)
	list->first = p;
    if (list->last != NULL)
	list->last->next = p;
    list->last = p;
}

static int
find_first_tile (struct tiles_list *list, double min_x, double max_y, double *x,
		 double *y)
{
/* searching the first tile [uppermost, leftmost] */
    struct tile_item *p = list->first;
    while (p)
      {
	  if (p->min_x == min_x && p->max_y == max_y)
	    {
		*x = p->max_x;
		*y = p->min_y;
		return 1;
	    }
	  p = p->next;
      }
    return 0;
}

static int
find_tile_right (struct tiles_list *list, double *x, double *y)
{
/* searching the next tile [rightmost] */
    struct tile_item *p;
    if (*x == DBL_MAX || *y == DBL_MAX)
	return 0;
    p = list->first;
    while (p)
      {
	  if (p->min_x == *x && p->min_y == *y)
	    {
		*x = p->max_x;
		*y = p->min_y;
		return 1;
	    }
	  p = p->next;
      }
    return 0;
}

static int
find_tile_down (struct tiles_list *list, double *x, double *y)
{
/* searching the next tile [lowermost] */
    struct tile_item *p;
    if (*x == DBL_MAX || *y == DBL_MAX)
	return 0;
    p = list->first;
    while (p)
      {
	  if (p->min_x == *x && p->max_y == *y)
	    {
		*x = p->max_x;
		*y = p->min_y;
		return 1;
	    }
	  p = p->next;
      }
    return 0;
}

static struct tile_item *
find_tile (struct tiles_list *list, double x, double y, int mode)
{
/* searching a tile */
    struct tile_item *p = list->first;
    while (p)
      {
	  switch (mode)
	    {
	    case TILE_UPPER_LEFT:
		if (p->max_x == x && p->min_y == y)
		    return p;
		break;
	    case TILE_UPPER_RIGHT:
		if (p->min_x == x && p->min_y == y)
		    return p;
		break;
	    case TILE_LOWER_LEFT:
		if (p->max_x == x && p->max_y == y)
		    return p;
		break;
	    case TILE_LOWER_RIGHT:
		if (p->min_x == x && p->max_y == y)
		    return p;
		break;
	    }
	  p = p->next;
      }
    return NULL;
}

static rasterliteImagePtr
raster_decode (const void *blob, int blob_size)
{
/* trying to decode a BLOB as an image */
    rasterliteImagePtr img = NULL;
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
    return img;
}

static int
get_tile (sqlite3 * handle, rasterliteImagePtr img, const char *table,
	  sqlite3_int64 tile_id, int declared_x, int declared_y, int where)
{
/* fetching a raster tile */
    sqlite3_stmt *stmt;
    int ret;
    char sql[1024];
    char dummy64[64];
    const void *blob;
    int blob_size;
    int row;
    int col;
    int x;
    int y;
    int pixel;
    rasterliteImagePtr tile_img = NULL;
    sprintf (sql, "SELECT raster FROM \"%s_rasters\" WHERE id = ?", table);
    ret = sqlite3_prepare_v2 (handle, sql, strlen (sql), &stmt, NULL);
    if (ret != SQLITE_OK)
      {
	  printf ("SQL error: %s\n", sqlite3_errmsg (handle));
	  return 0;
      }
/* binding query params */
    sqlite3_reset (stmt);
    sqlite3_clear_bindings (stmt);
    sqlite3_bind_int64 (stmt, 1, tile_id);
    while (1)
      {
	  /* scrolling the result set */
	  ret = sqlite3_step (stmt);
	  if (ret == SQLITE_DONE)
	      break;		/* end of result set */
	  if (ret == SQLITE_ROW)
	    {
		/* retrieving query values */
		if (sqlite3_column_type (stmt, 0) == SQLITE_BLOB)
		  {
		      blob = sqlite3_column_blob (stmt, 0);
		      blob_size = sqlite3_column_bytes (stmt, 0);
		      /* trying to decode the raster */
		      tile_img = raster_decode (blob, blob_size);
		      if (tile_img == NULL)
			{
			    printf (dummy64, FORMAT_64, tile_id);
			    printf ("Error: tile ID=%s [not a valid image]\n",
				    dummy64);
			    return 0;
			}
		      if (tile_img->sx == declared_x
			  && tile_img->sy == declared_y)
			  ;
		      else
			{
			    printf (dummy64, FORMAT_64, tile_id);
			    printf
				("Error: tile ID=%s [unexpected Width and Height]\n",
				 dummy64);
			    image_destroy (tile_img);
			    return 0;
			}
		  }
	    }
	  else
	    {
		printf ("SQL error: %s\n", sqlite3_errmsg (handle));
		sqlite3_finalize (stmt);
		return 0;
	    }
      }
    sqlite3_finalize (stmt);

    if (!tile_img)
      {
	  printf (dummy64, FORMAT_64, tile_id);
	  printf ("Error: tile ID=%s not found [or not a BLOB]\n", dummy64);
	  return 0;
      }
/* copying the tile image into the correct position */
    if (where == TILE_UPPER_LEFT)
      {
	  y = 0;
	  for (row = 0; row < tile_img->sy; row++)
	    {
		x = 0;
		for (col = 0; col < tile_img->sx; col++)
		  {
		      pixel = tile_img->pixels[row][col];
		      image_set_pixel (img, x++, y, pixel);
		  }
		y++;
	    }
      }
    if (where == TILE_UPPER_RIGHT)
      {
	  y = 0;
	  for (row = 0; row < tile_img->sy; row++)
	    {
		x = img->sx - 1;
		for (col = tile_img->sx - 1; col >= 0; col--)
		  {
		      pixel = tile_img->pixels[row][col];
		      image_set_pixel (img, x--, y, pixel);
		  }
		y++;
	    }
      }
    if (where == TILE_LOWER_LEFT)
      {
	  y = img->sy - 1;
	  for (row = tile_img->sy - 1; row >= 0; row--)
	    {
		x = 0;
		for (col = 0; col < tile_img->sx; col++)
		  {
		      pixel = tile_img->pixels[row][col];
		      image_set_pixel (img, x++, y, pixel);
		  }
		y--;
	    }
      }
    if (where == TILE_LOWER_RIGHT)
      {
	  y = img->sy - 1;
	  for (row = tile_img->sy - 1; row >= 0; row--)
	    {
		x = img->sx - 1;
		for (col = tile_img->sx - 1; col >= 0; col--)
		  {
		      pixel = tile_img->pixels[row][col];
		      image_set_pixel (img, x--, y, pixel);
		  }
		y--;
	    }
      }
    image_destroy (tile_img);

    return 1;
}

static void
thumbnail_export (sqlite3 * handle, sqlite3_stmt * stmt,
		  rasterliteImagePtr img, int image_type, int quality_factor,
		  struct thumbnail_tile *tile)
{
/* saving the thumbnail into the DB */
    int ret;
    void *blob;
    int blob_size;
    int width = img->sx / 2;
    int height = img->sy / 2;
    rasterliteImagePtr thumbnail = image_create (width, height);
    make_thumbnail (thumbnail, img);
    if (image_type == GAIA_TIFF_BLOB)
      {
	  blob = image_to_tiff_rgb (thumbnail, &blob_size);
	  if (!blob)
	    {
		printf ("TIFF RGB compression error\n");
		goto end;
	    }
      }
    else if (image_type == GAIA_PNG_BLOB)
      {
	  blob = image_to_png_rgb (thumbnail, &blob_size);
	  if (!blob)
	    {
		printf ("PNG RGB compression error\n");
		goto end;
	    }
      }
    else
      {
	  blob = image_to_jpeg (thumbnail, &blob_size, quality_factor);
	  if (!blob)
	    {
		printf ("JPEG compression error\n");
		goto end;
	    }
      }
    sqlite3_reset (stmt);
    sqlite3_clear_bindings (stmt);
    sqlite3_bind_blob (stmt, 1, blob, blob_size, free);
    ret = sqlite3_step (stmt);
    if (ret == SQLITE_DONE || ret == SQLITE_ROW)
	;
    else
      {
	  printf ("sqlite3_step() error: %s\n", sqlite3_errmsg (handle));
	  goto end;
      }
    tile->id_raster = sqlite3_last_insert_rowid (handle);
    tile->valid = 1;
    tile->raster_horz = width;
    tile->raster_vert = height;
  end:
    image_destroy (thumbnail);
}

static int
insert_metadata (sqlite3 * handle, const char *table, const char *source_name,
		 struct thumbnail_tile *thumb_tiles, int max_tile,
		 double x_size, double y_size)
{
/* inserting the thumbnail tiles metadata */
    sqlite3_stmt *stmt;
    int ret;
    char sql[1024];
    int metadata_ok = 0;
    unsigned char *blob;
    int blob_size;
    int i;
    struct thumbnail_tile *thumb_tile;
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
    for (i = 0; i < max_tile; i++)
      {
	  /* now we have to INSERT the raster's metadata into the DB */
	  thumb_tile = thumb_tiles + i;
	  if (thumb_tile->valid == 0)
	    {
		sqlite3_finalize (stmt);
		return 0;
	    }
	  sqlite3_reset (stmt);
	  sqlite3_clear_bindings (stmt);
	  sqlite3_bind_int64 (stmt, 1, thumb_tile->id_raster);
	  sqlite3_bind_text (stmt, 2, source_name, strlen (source_name),
			     SQLITE_STATIC);
	  sqlite3_bind_int (stmt, 3, thumb_tile->tileNo);
	  sqlite3_bind_int (stmt, 4, thumb_tile->raster_horz);
	  sqlite3_bind_int (stmt, 5, thumb_tile->raster_vert);
	  sqlite3_bind_double (stmt, 6, x_size);
	  sqlite3_bind_double (stmt, 7, y_size);
	  gaiaToSpatiaLiteBlobWkb (thumb_tile->geometry, &blob, &blob_size);
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
	  metadata_ok++;
      }
/* finalizing the INSERT INTO xx_metadata prepared statement */
    sqlite3_finalize (stmt);
    return metadata_ok;
  stop:
    return 0;
}

static int
build_pyramid_level (sqlite3 * handle, const char *table, int image_type,
		     int quality_factor, int level, double x_size,
		     double y_size, struct source_item *item, int verbose)
{
/* building a pyramid level */
    sqlite3_stmt *stmt;
    int ret;
    char *sql_err = NULL;
    char sql[1024];
    char sql2[512];
    char dummy64_1[64];
    char dummy64_2[64];
    char dummy64_3[64];
    char dummy64_4[64];
    sqlite3_int64 id;
    int srid;
    double tile_min_x;
    double tile_min_y;
    double tile_max_x;
    double tile_max_y;
    double source_min_x = DBL_MAX;
    double source_min_y = DBL_MAX;
    double source_max_x = -DBL_MAX;
    double source_max_y = -DBL_MAX;
    int tile_width;
    int tile_height;
    struct tiles_list tiles;
    struct tile_item *tile_1;
    struct tile_item *tile_2;
    struct tile_item *tile_3;
    struct tile_item *tile_4;
    double x;
    double y;
    rasterliteImagePtr img = NULL;
    struct thumbnail_tile thumb_tiles[NTILES];
    struct thumbnail_tile *thumb_tile;
    int max_tile;
    gaiaPolygonPtr polyg;
    int raster_ok = 0;
    int metadata_ok = 0;
    int i;
    for (i = 0; i < NTILES; i++)
      {
	  thumb_tile = &(thumb_tiles[i]);
	  thumb_tile->valid = 0;
	  thumb_tile->geometry = NULL;
      }
    printf ("\nGenerating thumbnail tiles: Pyramid Level %d\n", level);
    printf ("------------------\n");

    init_tiles (&tiles);
/* retrieving the tiles   */
    strcpy (sql,
	    "SELECT id, Srid(geometry), MbrMinX(geometry), MbrMinY(geometry), ");
    strcat (sql, "MbrMaxX(geometry),  MbrMaxY(geometry), width, height ");
    sprintf (sql2, "FROM \"%s_metadata\"", table);
    strcat (sql, sql2);
    strcat (sql,
	    " WHERE source_name = ? AND pixel_x_size = ? AND pixel_y_size = ?");
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
    sqlite3_bind_text (stmt, 1, item->name, strlen (item->name), SQLITE_STATIC);
    sqlite3_bind_double (stmt, 2, x_size);
    sqlite3_bind_double (stmt, 3, y_size);
    while (1)
      {
	  /* scrolling the result set */
	  ret = sqlite3_step (stmt);
	  if (ret == SQLITE_DONE)
	      break;		/* end of result set */
	  if (ret == SQLITE_ROW)
	    {
		/* retrieving query values */
		id = sqlite3_column_int64 (stmt, 0);
		srid = sqlite3_column_int (stmt, 1);
		tile_min_x = sqlite3_column_double (stmt, 2);
		if (tile_min_x < source_min_x)
		    source_min_x = tile_min_x;
		tile_min_y = sqlite3_column_double (stmt, 3);
		if (tile_min_y < source_min_y)
		    source_min_y = tile_min_y;
		tile_max_x = sqlite3_column_double (stmt, 4);
		if (tile_max_x > source_max_x)
		    source_max_x = tile_max_x;
		tile_max_y = sqlite3_column_double (stmt, 5);
		if (tile_max_y > source_max_y)
		    source_max_y = tile_max_y;
		tile_width = sqlite3_column_int (stmt, 6);
		tile_height = sqlite3_column_int (stmt, 7);
		add_tile (&tiles, id, srid, tile_min_x, tile_min_y, tile_max_x,
			  tile_max_y, tile_width, tile_height);
	    }
	  else
	    {
		printf ("SQL error: %s\n", sqlite3_errmsg (handle));
		printf ("Sorry, cowardly quitting ...\n");
		sqlite3_finalize (stmt);
		free_tiles (&tiles);
		return 0;
	    }
      }
    sqlite3_finalize (stmt);


/* the complete operation is handled as an unique SQL Transaction */
    ret = sqlite3_exec (handle, "BEGIN", NULL, NULL, &sql_err);
    if (ret != SQLITE_OK)
      {
	  printf ("BEGIN TRANSACTION error: %s\n", sql_err);
	  sqlite3_free (sql_err);
	  goto error;
      }

/* creating the INSERT INTO xx_rasters prepared statement */
    sprintf (sql, "INSERT INTO \"%s_rasters\" ", table);
    strcat (sql, "(id, raster) ");
    strcat (sql, " VALUES (NULL, ?)");
    ret = sqlite3_prepare_v2 (handle, sql, strlen (sql), &stmt, NULL);
    if (ret != SQLITE_OK)
      {
	  printf ("SQL error: %s\n%s\n", sql, sqlite3_errmsg (handle));
	  goto error;
      }
    if (!find_first_tile (&tiles, source_min_x, source_max_y, &x, &y))
      {
	  /* error: cannot find the first tile [uppermost, lefmost] */
	  printf
	      ("Error in raster source \"%s\"\nfirst tile [uppermost & leftmost] not found\n",
	       item->name);
	  goto error;
      }
    max_tile = 0;
    while (1)
      {
	  /* initializing the thumbnail tiles */
	  tile_1 = find_tile (&tiles, x, y, TILE_UPPER_LEFT);
	  tile_2 = find_tile (&tiles, x, y, TILE_UPPER_RIGHT);
	  tile_3 = find_tile (&tiles, x, y, TILE_LOWER_LEFT);
	  tile_4 = find_tile (&tiles, x, y, TILE_LOWER_RIGHT);
	  if (!tile_1 && !tile_2 && !tile_3 && !tile_4)
	      break;
	  /* initializing a thumbnail tile */
	  thumb_tile = &(thumb_tiles[max_tile++]);
	  thumb_tile->tile_1 = tile_1;
	  thumb_tile->tile_2 = tile_2;
	  thumb_tile->tile_3 = tile_3;
	  thumb_tile->tile_4 = tile_4;
	  /* trying to continue on the same row */
	  x = DBL_MAX;
	  y = DBL_MAX;
	  if (tile_2)
	    {
		x = tile_2->max_x;
		y = tile_2->min_y;
	    }
	  else if (tile_4)
	    {
		x = tile_4->max_x;
		y = tile_4->max_y;
	    }
	  if (find_tile_right (&tiles, &x, &y))
	      continue;
	  x = DBL_MAX;
	  y = DBL_MAX;
	  /* trying to continue on the next row */
	  x = source_min_x;
	  if (tile_3)
	      y = tile_3->min_y;
	  else if (tile_4)
	      y = tile_4->min_y;
	  if (find_tile_down (&tiles, &x, &y))
	      continue;
	  break;
      }
    printf ("RequiredThumbnails: %d tiles\n", max_tile);
    printf ("------------------\n");
    for (i = 0; i < max_tile; i++)
      {
	  /* generating the thumbail tiles */
	  thumb_tile = &(thumb_tiles[i]);
	  thumb_tile->tileNo = i;
	  thumb_tile->geometry = gaiaAllocGeomColl ();
	  tile_1 = thumb_tile->tile_1;
	  tile_2 = thumb_tile->tile_2;
	  tile_3 = thumb_tile->tile_3;
	  tile_4 = thumb_tile->tile_4;
	  if (verbose)
	    {
		fprintf (stderr, "\t\"%s\" PyramidLevel %d: tile %d of %d\n",
			 item->name, level, i + 1, max_tile);
		fflush (stderr);
	    }
	  if (tile_1 && tile_2 && tile_3 && tile_4)
	    {
		/* building a full thumbnail [4 tiles] */

		/* checking sizes */
		if (tile_1->width != tile_3->width)
		  {
		      printf (dummy64_1, FORMAT_64, tile_1->id);
		      printf (dummy64_2, FORMAT_64, tile_3->id);
		      printf
			  ("Mismatching tile sizes [Width] Tile ID=%s ID=%s\n",
			   dummy64_1, dummy64_2);
		      goto error;
		  }
		if (tile_2->width != tile_4->width)
		  {
		      printf (dummy64_1, FORMAT_64, tile_2->id);
		      printf (dummy64_2, FORMAT_64, tile_4->id);
		      printf
			  ("Mismatching tile sizes [Width] Tile ID=%s ID=%s\n",
			   dummy64_1, dummy64_2);
		      goto error;
		  }
		if (tile_1->height != tile_2->height)
		  {
		      printf (dummy64_1, FORMAT_64, tile_1->id);
		      printf (dummy64_2, FORMAT_64, tile_2->id);
		      printf
			  ("Mismatching tile sizes [Height] Tile ID=%s ID=%s\n",
			   dummy64_1, dummy64_2);
		      goto error;
		  }
		if (tile_3->height != tile_4->height)
		  {
		      printf (dummy64_1, FORMAT_64, tile_3->id);
		      printf (dummy64_2, FORMAT_64, tile_4->id);
		      printf
			  ("Mismatching tile sizes [Height] Tile ID=%s ID=%s\n",
			   dummy64_1, dummy64_2);
		      goto error;
		  }
		/* setting up the thumbnail tile MBR aka BBOX */
		if (tile_1->srid == tile_2->srid && tile_2->srid == tile_3->srid
		    && tile_3->srid == tile_4->srid)
		    thumb_tile->geometry->Srid = tile_1->srid;
		else
		  {
		      printf (dummy64_1, FORMAT_64, tile_1->id);
		      printf (dummy64_2, FORMAT_64, tile_2->id);
		      printf (dummy64_3, FORMAT_64, tile_3->id);
		      printf (dummy64_4, FORMAT_64, tile_4->id);
		      printf
			  ("Mismatching SRIDs: Tile ID=%s ID=%s ID=%s ID=%s\n",
			   dummy64_1, dummy64_2, dummy64_3, dummy64_4);
		      goto error;
		  }
		polyg = gaiaAddPolygonToGeomColl (thumb_tile->geometry, 5, 0);
		gaiaSetPoint (polyg->Exterior->Coords, 0, tile_3->min_x,
			      tile_3->min_y);
		gaiaSetPoint (polyg->Exterior->Coords, 4, tile_3->min_x,
			      tile_3->min_y);
		gaiaSetPoint (polyg->Exterior->Coords, 1, tile_4->max_x,
			      tile_4->min_y);
		gaiaSetPoint (polyg->Exterior->Coords, 2, tile_2->max_x,
			      tile_2->max_y);
		gaiaSetPoint (polyg->Exterior->Coords, 3, tile_1->min_x,
			      tile_1->max_y);
		/* preparing the full-size image */
		img =
		    image_create (tile_1->width + tile_2->width,
				  tile_1->height + tile_3->height);
		if (!get_tile
		    (handle, img, table, tile_1->id, tile_1->width,
		     tile_1->height, TILE_UPPER_LEFT))
		    goto error;
		if (!get_tile
		    (handle, img, table, tile_2->id, tile_2->width,
		     tile_2->height, TILE_UPPER_RIGHT))
		    goto error;
		if (!get_tile
		    (handle, img, table, tile_3->id, tile_3->width,
		     tile_3->height, TILE_LOWER_LEFT))
		    goto error;
		if (!get_tile
		    (handle, img, table, tile_4->id, tile_4->width,
		     tile_4->height, TILE_LOWER_RIGHT))
		    goto error;
		/* saving the thumbnail into the DB */
		thumbnail_export (handle, stmt, img, image_type, quality_factor,
				  thumb_tile);
		raster_ok++;

		if (img)
		    image_destroy (img);
		img = NULL;
	    }
	  else if (tile_1 && tile_3 && !tile_2 && !tile_4)
	    {
		/* building an half thumbnail [2 tiles - leftmost] */

		/* checking sizes */
		if (tile_1->width != tile_3->width)
		  {
		      printf (dummy64_1, FORMAT_64, tile_1->id);
		      printf (dummy64_2, FORMAT_64, tile_3->id);
		      printf
			  ("Mismatching tile sizes [Width] Tile ID=%s ID=%s\n",
			   dummy64_1, dummy64_2);
		      goto error;
		  }
		/* setting up the thumbnail tile MBR aka BBOX */
		if (tile_1->srid == tile_3->srid)
		    thumb_tile->geometry->Srid = tile_1->srid;
		else
		  {
		      printf (dummy64_1, FORMAT_64, tile_1->id);
		      printf (dummy64_2, FORMAT_64, tile_3->id);
		      printf ("Mismatching SRIDs: Tile IID=%s ID=%s\n",
			      dummy64_1, dummy64_2);
		      goto error;
		  }
		polyg = gaiaAddPolygonToGeomColl (thumb_tile->geometry, 5, 0);
		gaiaSetPoint (polyg->Exterior->Coords, 0, tile_3->min_x,
			      tile_3->min_y);
		gaiaSetPoint (polyg->Exterior->Coords, 4, tile_3->min_x,
			      tile_3->min_y);
		gaiaSetPoint (polyg->Exterior->Coords, 1, tile_3->max_x,
			      tile_3->min_y);
		gaiaSetPoint (polyg->Exterior->Coords, 2, tile_3->max_x,
			      tile_3->max_y);
		gaiaSetPoint (polyg->Exterior->Coords, 3, tile_1->min_x,
			      tile_1->max_y);
		/* preparing the full-size image */
		img =
		    image_create (tile_1->width,
				  tile_1->height + tile_3->height);
		if (!get_tile
		    (handle, img, table, tile_1->id, tile_1->width,
		     tile_1->height, TILE_UPPER_LEFT))
		    goto error;
		if (!get_tile
		    (handle, img, table, tile_3->id, tile_3->width,
		     tile_3->height, TILE_LOWER_LEFT))
		    goto error;
		/* saving the thumbnail into the DB */
		thumbnail_export (handle, stmt, img, image_type, quality_factor,
				  thumb_tile);
		raster_ok++;

		if (img)
		    image_destroy (img);
		img = NULL;
	    }
	  else if (tile_1 && tile_2 && !tile_3 && !tile_4)
	    {
		/* building an half thumbnail [2 tiles - uppermost] */

		/* checking sizes */
		if (tile_1->height != tile_2->height)
		  {
		      printf (dummy64_1, FORMAT_64, tile_1->id);
		      printf (dummy64_2, FORMAT_64, tile_2->id);
		      printf
			  ("Mismatching tile sizes [Height] Tile ID=%s ID=%s\n",
			   dummy64_1, dummy64_2);
		      goto error;
		  }
		/* setting up the thumbnail tile MBR aka BBOX */
		if (tile_1->srid == tile_2->srid)
		    thumb_tile->geometry->Srid = tile_1->srid;
		else
		  {
		      printf (dummy64_1, FORMAT_64, tile_1->id);
		      printf (dummy64_2, FORMAT_64, tile_2->id);
		      printf ("Mismatching SRIDs: Tile ID=%s ID=%s\n",
			      dummy64_1, dummy64_2);
		      goto error;
		  }
		polyg = gaiaAddPolygonToGeomColl (thumb_tile->geometry, 5, 0);
		gaiaSetPoint (polyg->Exterior->Coords, 0, tile_1->min_x,
			      tile_1->min_y);
		gaiaSetPoint (polyg->Exterior->Coords, 4, tile_1->min_x,
			      tile_1->min_y);
		gaiaSetPoint (polyg->Exterior->Coords, 1, tile_2->max_x,
			      tile_2->min_y);
		gaiaSetPoint (polyg->Exterior->Coords, 2, tile_2->max_x,
			      tile_2->max_y);
		gaiaSetPoint (polyg->Exterior->Coords, 3, tile_1->min_x,
			      tile_1->max_y);
		/* preparing the full-size image */
		img =
		    image_create (tile_1->width + tile_2->width,
				  tile_1->height);
		if (!get_tile
		    (handle, img, table, tile_1->id, tile_1->width,
		     tile_1->height, TILE_UPPER_LEFT))
		    goto error;
		if (!get_tile
		    (handle, img, table, tile_2->id, tile_2->width,
		     tile_2->height, TILE_UPPER_RIGHT))
		    goto error;
		/* saving the thumbnail into the DB */
		thumbnail_export (handle, stmt, img, image_type, quality_factor,
				  thumb_tile);
		raster_ok++;

		if (img)
		    image_destroy (img);
		img = NULL;
	    }
	  else if (tile_1 && !tile_2 && !tile_3 && !tile_4)
	    {
		/* building an quarter thumbnail [2 tiles - leftmost & uppermost] */

		/* preparing the full-size image */
		img = image_create (tile_1->width, tile_1->height);
		if (!get_tile
		    (handle, img, table, tile_1->id, tile_1->width,
		     tile_1->height, TILE_UPPER_LEFT))
		    goto error;
		/* setting up the thumbnail tile MBR aka BBOX */
		thumb_tile->geometry->Srid = tile_1->srid;
		polyg = gaiaAddPolygonToGeomColl (thumb_tile->geometry, 5, 0);
		gaiaSetPoint (polyg->Exterior->Coords, 0, tile_1->min_x,
			      tile_1->min_y);
		gaiaSetPoint (polyg->Exterior->Coords, 4, tile_1->min_x,
			      tile_1->min_y);
		gaiaSetPoint (polyg->Exterior->Coords, 1, tile_1->max_x,
			      tile_1->min_y);
		gaiaSetPoint (polyg->Exterior->Coords, 2, tile_1->max_x,
			      tile_1->max_y);
		gaiaSetPoint (polyg->Exterior->Coords, 3, tile_1->min_x,
			      tile_1->max_y);
		/* saving the thumbnail into the DB */
		thumbnail_export (handle, stmt, img, image_type, quality_factor,
				  thumb_tile);
		raster_ok++;

		if (img)
		    image_destroy (img);
		img = NULL;
	    }
	  else
	    {
		printf ("Error in raster source \"%s\"\ninvalid tile pattern\n",
			item->name);
		goto error;
	    }
      }
    sqlite3_finalize (stmt);
    stmt = NULL;

/* inserting thumbnail tiles metadata into the DB */
    metadata_ok =
	insert_metadata (handle, table, item->name, thumb_tiles, max_tile,
			 x_size * 2.0, y_size * 2.0);
/* committing the still pending SQL Transaction */
    ret = sqlite3_exec (handle, "COMMIT", NULL, NULL, &sql_err);
    if (ret != SQLITE_OK)
      {
	  printf ("COMMIT TRANSACTION error: %s\n", sql_err);
	  sqlite3_free (sql_err);
	  goto error2;
      }

    printf ("Pyramid Level %d succesfully created\n", level);
    printf ("ThumbnailTiles:     %d rows in table \"%s_rasters\"\n", raster_ok,
	    table);
    printf ("                    %d rows in table \"%s_metadata\"\n",
	    metadata_ok, table);
    printf ("------------------\n\n");

/* memory cleanup */
    free_tiles (&tiles);
    for (i = 0; i < NTILES; i++)
      {
	  thumb_tile = &(thumb_tiles[i]);
	  if (thumb_tile->geometry)
	      gaiaFreeGeomColl (thumb_tile->geometry);
      }
/* recursively building the superior level */
    return raster_ok;
  error:
    sqlite3_finalize (stmt);
  error2:
    free_tiles (&tiles);
    for (i = 0; i < NTILES; i++)
      {
	  thumb_tile = &(thumb_tiles[i]);
	  if (thumb_tile->geometry)
	      gaiaFreeGeomColl (thumb_tile->geometry);
      }
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
create_raster_pyramids (sqlite3 * handle)
{
/* checking if table 'raster_pyramids' exists - if not, we'll create */
    int ret;
    char sql[1024];
    const char *name;
    int i;
    char **results;
    int rows;
    int columns;
    char *errMsg = NULL;
    int table_prefix = 0;
    int pixel_x_size = 0;
    int pixel_y_size = 0;
    int tile_count = 0;
/* checking if already exists */
    strcpy (sql, "PRAGMA table_info(raster_pyramids)");
    ret = sqlite3_get_table (handle, sql, &results, &rows, &columns, &errMsg);
    if (ret != SQLITE_OK)
      {
	  printf ("SQL error: %s\n", errMsg);
	  sqlite3_free (errMsg);
	  return 0;
      }
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
	return 1;
    else if (!table_prefix && !pixel_x_size && !pixel_y_size && !tile_count)
	;
    else
      {
	  printf
	      ("table \"raster_pyramids\" already exists, but has an invalid column layout\n");
	  return 0;
      }
/* creating the table */
    strcpy (sql, "CREATE TABLE raster_pyramids (\n");
    strcat (sql, "table_prefix TEXT NOT NULL,\n");
    strcat (sql, "pixel_x_size DOUBLE NOT NULL,\n");
    strcat (sql, "pixel_y_size DOUBLE NOT NULL,\n");
    strcat (sql, "tile_count INTEGER NOT NULL)");
    ret = sqlite3_exec (handle, sql, NULL, 0, &errMsg);
    if (ret != SQLITE_OK)
      {
	  printf ("SQL error: %s\n", errMsg);
	  sqlite3_free (errMsg);
	  return 0;
      }
    return 1;
}

static int
build_pyramids (sqlite3 * handle, const char *table, int test_mode, int verbose,
		int image_type, int quality_factor)
{
/* trying to  build raster pyramids */
    sqlite3_stmt *stmt;
    int ret;
    char sql[1024];
    char sql2[512];
    double x_size = 0.0;
    double y_size = 0.0;
    int to_be_deleted = 0;
    struct sources_list sources;
    struct source_item *item;
    int count;
    const char *raster_source;
    int nitems;
    int pr;
    char *sql_err = NULL;
/* retrieving the tiled raster scale */
    sprintf (sql,
	     "SELECT Min(pixel_x_size), Min(pixel_y_size) FROM \"%s_metadata\"",
	     table);
    strcat (sql, " WHERE pixel_x_size > 0 AND pixel_y_size > 0");
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
		x_size = sqlite3_column_double (stmt, 0);
		y_size = sqlite3_column_double (stmt, 1);
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

/* counting the already existing pyramid tiles */
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
    printf ("Raster sources to be pyramidized:\n");
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

    if (create_raster_pyramids (handle) == 0)
	return 0;
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
	  sqlite3_bind_double (stmt, 1, x_size);
	  sqlite3_bind_double (stmt, 2, y_size);
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
	  sqlite3_bind_double (stmt, 1, x_size);
	  sqlite3_bind_double (stmt, 2, y_size);
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
/* proceding to actually building pyramids */
    printf ("\nPyramidizing raster sources:\n");
    printf ("=======================================================\n");
    pr = 0;
    item = sources.first;
    while (item)
      {
	  int level = 1;
	  double new_x_size = x_size;
	  double new_y_size = y_size;
	  printf ("%d/%d] Raster source: \"%s\"\tTiles=%d\n", pr + 1, nitems,
		  item->name, item->count);
	  while (1)
	    {
		if (build_pyramid_level
		    (handle, table, image_type, quality_factor, level,
		     new_x_size, new_y_size, item, verbose) <= 1)
		    break;
		/* looping on the next level */
		level++;
		new_x_size *= 2.0;
		new_y_size *= 2.0;
	    }
	  pr++;
	  item = item->next;
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
	return handle;

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
    fprintf (stderr, "\n\nusage: rasterlite_pyramid ARGLIST\n");
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
    int error = 0;
    int cnt = 0;
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
    switch (image_type)
      {
      case GAIA_JPEG_BLOB:
	  printf ("Pyramid Tile image type: JPEG quality=%d\n", quality_factor);
	  break;
      case GAIA_PNG_BLOB:
	  printf ("Pyramid Tile image type: PNG [RGB]\n");
	  break;
      case GAIA_TIFF_BLOB:
	  printf ("Pyramid Tile image type: TIFF [RGB]\n");
	  break;
      default:
	  printf ("Pyramid Tile image type: UNKNOWN\n");
	  break;
      };
    printf ("=====================================================\n\n");
/* trying to connect DB */
    handle = db_connect (path, table);
    if (!handle)
	return 1;
    cnt =
	build_pyramids (handle, table, test_mode, verbose, image_type,
			quality_factor);
/* disconnecting DB */
    sqlite3_close (handle);
    return 0;
}
