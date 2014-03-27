/* 
/ rasterlite_load.c
/
/ a tool for uploading GeoTIFF rasters into a SpatiaLite DB 
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

#include "rasterlite_tiff_hdrs.h"
#include <tiffio.h>

#if defined(_WIN32) && !defined(__MINGW32__)
#include <io.h>
#include <direct.h>
#else
#include <dirent.h>
#endif

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

#define ARG_NONE			0
#define ARG_DB_PATH			1
#define ARG_TABLE_NAME		2
#define ARG_DIR				3
#define ARG_FILE			4
#define ARG_TILE_SIZE		5
#define ARG_IMAGE_TYPE		6
#define ARG_QUALITY_FACTOR	7
#define ARG_EPSG_CODE		8

static int
read_by_tile (TIFF * tif, rasterliteImagePtr img, struct geo_info *infos,
	      int baseVert, int baseHorz)
{
/* reading from a TIFF using TILES */
    uint32 *raster = NULL;
    int x;
    int y;
    int img_x;
    int img_y;
    int dst_x;
    int dst_y;
    int width;
    int height;
    uint32 pixel;
    int color;
    uint32 *scanline;
    int current_row;
    int current_col;
    int current_col_save;
/* allocating the tile raster */
    height = infos->tif_tile_height;
    width = infos->tif_tile_width;
    raster = malloc (sizeof (uint32) * (width * height));

/* determining the first tile to be read */
    for (y = 0; y < (int) infos->height; y += height)
      {
	  if (y <= baseVert && baseVert < (int) (y + height))
	    {
		current_row = y;
		for (x = 0; x < (int) infos->width; x += width)
		  {
		      if (x <= baseHorz && baseHorz < (x + (int) width))
			{
			    current_col = x;
			    goto done;
			}
		  }
	    }
      }
  done:
    current_col_save = current_col;
/* feeding tile pixels */
    while (current_row < infos->height)
      {
	  if (current_row > (baseVert + img->sy))
	      break;
	  while (current_col < infos->width)
	    {
		/* reading a TIFF tile */
		if (current_col > (baseHorz + img->sx))
		    break;
		if (!TIFFReadRGBATile (tif, current_col, current_row, raster))
		    goto error;
		for (y = 0; y < height; y++)
		  {
		      img_y = current_row + (height - y) - 1;
		      dst_y = img_y - baseVert;
		      if (dst_y < 0)
			  continue;
		      if (dst_y >= (int) img->sy)
			  continue;
		      scanline = raster + (width * y);
		      for (x = 0; x < width; x++)
			{
			    img_x = current_col + x;
			    dst_x = img_x - baseHorz;
			    if (dst_x < 0)
				continue;
			    if (dst_x >= (int) img->sx)
				break;
			    pixel = scanline[x];
			    color =
				true_color (TIFFGetR (pixel), TIFFGetG (pixel),
					    TIFFGetB (pixel));
			    image_set_pixel (img, dst_x, dst_y, color);
			}
		  }
		current_col += width;
	    }
	  current_row += height;
	  current_col = current_col_save;
      }

    if (raster)
	free (raster);
    return 1;
  error:
    if (raster)
	free (raster);
    return 0;
}

static int
read_by_strip (TIFF * tif, rasterliteImagePtr img, struct geo_info *infos,
	       int baseVert, int baseHorz)
{
/* reading from a TIFF using STRIPS */
    uint32 *raster = NULL;
    int x;
    int y;
    int img_y;
    int to_read;
    int effective_strip;
    uint32 pixel;
    int color;
    uint32 *scanline;
    int current_row;
    int current_y;
/* allocating the strip raster */
    raster = malloc (sizeof (uint32) * (infos->width * infos->rows_strip));

    to_read = 1;
    current_row = 0;
    for (y = 0; y < (int) infos->height; y += infos->rows_strip)
      {
	  for (x = 0; x < (int) infos->rows_strip; x++)
	    {
		if (baseVert == (y + x))
		  {
		      /* reading the first TIFF strip */
		      current_row = y;
		      if (!TIFFReadRGBAStrip (tif, current_row, raster))
			  goto error;
		      effective_strip = infos->rows_strip;
		      if ((current_row + infos->rows_strip) > infos->height)
			  effective_strip = infos->height - current_row;
		      to_read = 0;
		      goto done;
		  }
	    }
      }
  done:
/* feeding tile pixels */
    y = 0;
    while (y < img->sy)
      {
	  if (to_read)
	    {
		/* reading a further TIFF strip */
		current_row += infos->rows_strip;
		if (!TIFFReadRGBAStrip (tif, current_row, raster))
		    goto error;
		effective_strip = infos->rows_strip;
		if ((current_row + infos->rows_strip) > infos->height)
		    effective_strip = infos->height - current_row;
	    }
	  for (current_y = 0; current_y < effective_strip; current_y++)
	    {
		img_y =
		    (current_row - baseVert) + ((effective_strip - current_y) -
						1);
		if (img_y >= img->sy || img_y < 0)
		    continue;
		scanline = raster + (infos->width * current_y);
		for (x = 0; x < img->sx; x++)
		  {
		      pixel = scanline[baseHorz + x];
		      color =
			  true_color (TIFFGetR (pixel), TIFFGetG (pixel),
				      TIFFGetB (pixel));
		      image_set_pixel (img, x, img_y, color);
		  }
		y++;
	    }
	  to_read = 1;
      }
    if (raster)
	free (raster);
    return 1;
  error:
    if (raster)
	free (raster);
    return 0;
}

static int
export_tile (TIFF * tif, struct geo_info *infos, int baseVert, int baseHorz,
	     int tileNo, struct tile_info *tile)
{
/* accessing the TIFF by strips or tiles */
    int retval = 0;
    void *image;
    int image_size;
    int ret;
    double xx;
    double yy;
    rasterliteImagePtr img = NULL;
    gaiaPolygonPtr polyg;
    tile->geometry = gaiaAllocGeomColl ();
    tile->tileNo = tileNo;

/* computing actual tile dims */
    tile->raster_horz = infos->tile_width;
    tile->raster_vert = infos->tile_height;
    if ((baseHorz + tile->raster_horz) > (int) infos->width)
	tile->raster_horz = infos->width - baseHorz;
    if ((baseVert + tile->raster_vert) > (int) infos->height)
	tile->raster_vert = infos->height - baseVert;

/* creating a Geometry corresponding to this raster */
    if (infos->epsg_code >= 0)
	tile->geometry->Srid = infos->epsg_code;
    else
	tile->geometry->Srid = infos->epsg;
    polyg = gaiaAddPolygonToGeomColl (tile->geometry, 5, 0);

    xx = infos->upper_left_x + ((double) baseHorz * infos->pixel_x);
    yy = infos->upper_left_y - ((double) baseVert * infos->pixel_y);
    gaiaSetPoint (polyg->Exterior->Coords, 0, xx, yy);
    gaiaSetPoint (polyg->Exterior->Coords, 4, xx, yy);

    xx = infos->upper_left_x +
	((double) (baseHorz + tile->raster_horz) * infos->pixel_x);
    yy = infos->upper_left_y - ((double) baseVert * infos->pixel_y);
    gaiaSetPoint (polyg->Exterior->Coords, 1, xx, yy);

    xx = infos->upper_left_x +
	((double) (baseHorz + tile->raster_horz) * infos->pixel_x);
    yy = infos->upper_left_y -
	((double) (baseVert + tile->raster_vert) * infos->pixel_y);
    gaiaSetPoint (polyg->Exterior->Coords, 2, xx, yy);

    xx = infos->upper_left_x + ((double) baseHorz * infos->pixel_x);
    yy = infos->upper_left_y -
	((double) (baseVert + tile->raster_vert) * infos->pixel_y);
    gaiaSetPoint (polyg->Exterior->Coords, 3, xx, yy);

/* allocating a true color image */
    img = image_create (tile->raster_horz, tile->raster_vert);
    if (infos->is_tiled)
      {
	  /* reading from a TIFF containing TILES */
	  if (!read_by_tile (tif, img, infos, baseVert, baseHorz))
	      goto error;
      }
    else
      {
	  /* reading from a TIFF containing STRIPS */
	  if (!read_by_strip (tif, img, infos, baseVert, baseHorz))
	      goto error;
      }

    if (infos->image_type == IMAGE_PNG_PALETTE)
      {
	  /* compressing the section image as PNG PALETTE */
	  image = image_to_png_palette (img, &image_size);
	  if (!image)
	    {
		printf ("PNG PALETTE compression error\n");
		goto stop;
	    }
      }
    else if (infos->image_type == IMAGE_PNG_GRAYSCALE)
      {
	  /* compressing the section image as PNG GRAYSCALE */
	  image = image_to_png_grayscale (img, &image_size);
	  if (!image)
	    {
		printf ("PNG GRAYSCALE compression error\n");
		goto stop;
	    }
      }
    else if (infos->image_type == IMAGE_PNG_RGB)
      {
	  /* compressing the section image as PNG RGB */
	  image = image_to_png_rgb (img, &image_size);
	  if (!image)
	    {
		printf ("PNG RGB compression error\n");
		goto stop;
	    }
      }
    else if (infos->image_type == IMAGE_GIF_PALETTE)
      {
	  /* compressing the section image as GIF */
	  image = image_to_gif (img, &image_size);
	  if (!image)
	    {
		printf ("GIF compression error\n");
		goto stop;
	    }
      }
    else if (infos->image_type == IMAGE_TIFF_FAX4)
      {
	  /* compressing the section image as TIFF CCITT4 FAX-4 */
	  image = image_to_tiff_fax4 (img, &image_size);
	  if (!image)
	    {
		printf ("TIFF CCITT FAX-4 compression error\n");
		goto stop;
	    }
      }
    else if (infos->image_type == IMAGE_TIFF_PALETTE)
      {
	  /* compressing the section image as TIFF PALETTE */
	  image = image_to_tiff_palette (img, &image_size);
	  if (!image)
	    {
		printf ("TIFF PALETTE compression error\n");
		goto stop;
	    }
      }
    else if (infos->image_type == IMAGE_TIFF_GRAYSCALE)
      {
	  /* compressing the section image as TIFF GRAYSCALE */
	  image = image_to_tiff_grayscale (img, &image_size);
	  if (!image)
	    {
		printf ("TIFF GRAYSCALE compression error\n");
		goto stop;
	    }
      }
    else if (infos->image_type == IMAGE_TIFF_RGB)
      {
	  /* compressing the section image as TIFF RGB */
	  image = image_to_tiff_rgb (img, &image_size);
	  if (!image)
	    {
		printf ("TIFF RGB compression error\n");
		goto stop;
	    }
      }
    else if (infos->image_type == IMAGE_JPEG_BW)
      {
	  /* compressing the section image as JPEG GRAYSCALE */
	  image =
	      image_to_jpeg_grayscale (img, &image_size, infos->quality_factor);
	  if (!image)
	    {
		printf ("JPEG compression error\n");
		goto stop;
	    }
      }
    else
      {
	  /* default: compressing the section image as JPEG RGB */
	  image = image_to_jpeg (img, &image_size, infos->quality_factor);
	  if (!image)
	    {
		printf ("JPEG compression error\n");
		goto stop;
	    }
      }

/* finally we are ready to INSERT this raster into the DB */
    sqlite3_reset (infos->stmt);
    sqlite3_clear_bindings (infos->stmt);
    sqlite3_bind_blob (infos->stmt, 1, image, image_size, free);
    ret = sqlite3_step (infos->stmt);
    if (ret == SQLITE_DONE || ret == SQLITE_ROW)
	;
    else
      {
	  printf ("sqlite3_step() error: %s\n", sqlite3_errmsg (infos->handle));
	  sqlite3_finalize (infos->stmt);
	  goto stop;
      }
    tile->id_raster = sqlite3_last_insert_rowid (infos->handle);
    tile->valid = 1;
    retval = 1;
    goto stop;

  error:
    printf ("TIFF Raster read error\n");

  stop:
    if (!retval)
      {
	  /* some error occurred; performing a ROLLBACK */
	  printf ("\nSome unexpected error occurred: performing a ROLLBACK\n");
	  sqlite3_exec (infos->handle, "ROLLBACK", NULL, NULL, NULL);
      }
    if (img)
	image_destroy (img);
    return retval;
}

static int
create_raster_table (struct geo_info *infos)
{
    int ret;
    char *sql_err = NULL;
    char sql[1024];
    const char *name;
    int i;
    char **results;
    int rows;
    int columns;
    char *errMsg = NULL;
    int id_metadata = 0;
    int source_name = 0;
    int tile_id = 0;
    int width = 0;
    int height = 0;
    int pixel_x_size = 0;
    int pixel_y_size = 0;
    int id_raster = 0;
    int raster = 0;
    int prefix_rasters;
    int prefix_metadata;
    int already_defined = 0;

/* checking if already exists */
    sprintf (sql, "PRAGMA table_info(\"%s_rasters\")", infos->table);
    ret =
	sqlite3_get_table (infos->handle, sql, &results, &rows, &columns,
			   &errMsg);
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
		      if (strcasecmp (name, "id") == 0)
			  id_raster = 1;
		      if (strcasecmp (name, "raster") == 0)
			  raster = 1;
		  }
	    }
      }
    sqlite3_free_table (results);
    if (id_raster && raster)
	prefix_rasters = 1;
    else if (!id_raster && !raster)
	prefix_rasters = 0;
    else
      {
	  printf
	      ("table \"%s_rasters\" already exists, but has an invalid column layout\n",
	       infos->table);
	  return 0;
      }

    sprintf (sql, "PRAGMA table_info(\"%s_metadata\")", infos->table);
    ret =
	sqlite3_get_table (infos->handle, sql, &results, &rows, &columns,
			   &errMsg);
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
		      if (strcasecmp (name, "id") == 0)
			  id_metadata = 1;
		      if (strcasecmp (name, "source_name") == 0)
			  source_name = 1;
		      if (strcasecmp (name, "tile_id") == 0)
			  tile_id = 1;
		      if (strcasecmp (name, "width") == 0)
			  width = 1;
		      if (strcasecmp (name, "height") == 0)
			  height = 1;
		      if (strcasecmp (name, "pixel_x_size") == 0)
			  pixel_x_size = 1;
		      if (strcasecmp (name, "pixel_y_size") == 0)
			  pixel_y_size = 1;
		  }
	    }
      }
    sqlite3_free_table (results);
    if (id_metadata && source_name && tile_id && width && height && pixel_x_size
	&& pixel_y_size)
	prefix_metadata = 1;
    else if (!id_metadata && !source_name && !tile_id && !width && !height
	     && !pixel_x_size && !pixel_y_size)
	prefix_metadata = 0;
    else
      {
	  printf
	      ("table \"%s_metadata\" already exists, but has an invalid column layout\n",
	       infos->table);
	  return 0;
      }

    sprintf (sql,
	     "SELECT f_geometry_column FROM geometry_columns WHERE "
	     "Lower(f_table_name) = Lower('%s_metadata')", infos->table);
    ret =
	sqlite3_get_table (infos->handle, sql, &results, &rows, &columns,
			   &errMsg);
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
	      already_defined = 1;
      }
    sqlite3_free_table (results);
    if (already_defined && prefix_rasters && prefix_metadata)
	return 1;
    if (prefix_rasters && prefix_metadata)
      {
	  printf
	      ("both tables \"%s_rasters\" and \"%s_metadata\" already exist\n",
	       infos->table, infos->table);
	  printf
	      ("but no corresponding definition was found in table \"geometry_columns\"\n");
	  return 0;
      }
    if (prefix_rasters)
      {
	  printf ("table \"%s_rasters\" already exists\n", infos->table);
	  printf
	      ("but no corresponding definition was found in table \"geometry_columns\"\n");
	  return 0;
      }
    if (prefix_rasters && prefix_metadata)
      {
	  printf ("table \"%s_metadata\" already exist\n", infos->table);
	  printf
	      ("but no corresponding definition was found in table \"geometry_columns\"\n");
	  return 0;
      }

    printf ("Creating DB table '%s_metadata'\n", infos->table);
/* creating the xx_metadata DB table */
    sprintf (sql, "CREATE TABLE %s_metadata (\n", infos->table);
    strcat (sql, "id INTEGER NOT NULL PRIMARY KEY,\n");
    strcat (sql, "source_name TEXT NOT NULL,\n");
    strcat (sql, "tile_id INTEGER NOT NULL,\n");
    strcat (sql, "width INTEGER NOT NULL,\n");
    strcat (sql, "height INTEGER NOT NULL,\n");
    strcat (sql, "pixel_x_size DOUBLE NOT NULL,\n");
    strcat (sql, "pixel_y_size DOUBLE NOT NULL)\n");
    ret = sqlite3_exec (infos->handle, sql, NULL, NULL, &sql_err);
    if (ret != SQLITE_OK)
      {
	  printf ("CREATE TABLE '%s_metadata' error: %s\n", infos->table,
		  sql_err);
	  sqlite3_free (sql_err);
	  return 0;
      }

/* adding Geometry */
    if (infos->epsg_code >= 0)
	sprintf (sql,
		 "SELECT AddGeometryColumn('%s_metadata', 'geometry', %d, 'POLYGON', 2)",
		 infos->table, infos->epsg_code);
    else
	sprintf (sql,
		 "SELECT AddGeometryColumn('%s_metadata', 'geometry', %d, 'POLYGON', 2)",
		 infos->table, infos->epsg);
    ret = sqlite3_exec (infos->handle, sql, NULL, NULL, &sql_err);
    if (ret != SQLITE_OK)
      {
	  printf ("AddGeometryColumn '%s_metadata.geometry' error: %s\n",
		  infos->table, sql_err);
	  sqlite3_free (sql_err);
	  return 0;
      }

/* creating Spatial Index for Geometry */
    sprintf (sql, "SELECT CreateSpatialIndex('%s_metadata', 'geometry')",
	     infos->table);
    ret = sqlite3_exec (infos->handle, sql, NULL, NULL, &sql_err);
    if (ret != SQLITE_OK)
      {
	  printf ("CreateSpatialIndex '%s_metadata.geometry' error: %s\n",
		  infos->table, sql_err);
	  sqlite3_free (sql_err);
	  return 0;
      }

    printf ("Creating DB table '%s_rasters'\n\n", infos->table);
/* creating the xx_rasters DB table */
    sprintf (sql, "CREATE TABLE %s_rasters (\n", infos->table);
    strcat (sql, "id INTEGER NOT NULL PRIMARY KEY AUTOINCREMENT,\n");
    strcat (sql, "raster BLOB NOT NULL)\n");
    ret = sqlite3_exec (infos->handle, sql, NULL, NULL, &sql_err);
    if (ret != SQLITE_OK)
      {
	  printf ("CREATE TABLE '%s_rasters' error: %s\n", infos->table,
		  sql_err);
	  sqlite3_free (sql_err);
	  return 0;
      }

/* 
/ inserting a fake row to prime the DB table
/
/ for some unclear reason the first INSERT seems to fail anyway,
/ so we'll perform a fake insert as a tricky solution
/ ...
/ it seems to work - hope well !!! 
*/
    sprintf (sql, "INSERT INTO \"%s_metadata\" ", infos->table);
    strcat (sql,
	    "(id, source_name, tile_id, width, height, pixel_x_size, pixel_y_size, geometry) VALUES (");
    strcat (sql, "0, 'raster metadata', 0, 0, 0, 0, 0, NULL)");
    ret = sqlite3_exec (infos->handle, sql, NULL, NULL, &sql_err);
    if (ret != SQLITE_OK)
      {
	  printf ("INSERT INTO '%s_metadata.geometry' error: %s\n",
		  infos->table, sql_err);
	  sqlite3_free (sql_err);
	  return 0;
      }
    return 1;
}

static int
load_file (sqlite3 * handle, const char *file_path, const char *table,
	   int tile_size, int test_mode, int verbose, int image_type,
	   int quality_factor, int epsg_code)
{
/* importing a single GeoTIFF file */
    int ret;
    char *sql_err = NULL;
    sqlite3_stmt *stmt;
    char sql[1024];
    char dummy[128];
    unsigned char *blob;
    int blob_size;
    uint32 width = 0;
    uint32 height = 0;
    int extra_width;
    int extra_height;
    double cx;
    double cy;
    const char *compression_name = (const char *) "UNKNOWN";
    const char *colorspace_name = (const char *) "UNKNOWN";
    const char *format_hint = (const char *) "UNKNOWN";
    const char *actual_format = (const char *) "UNKNOWN";
    uint16 compression;
    uint16 bits_per_sample;
    uint16 samples_per_pixel;
    uint16 photometric;
    uint32 rows_strip = 0;
    uint32 tif_tile_width = 0;
    uint32 tif_tile_height = 0;
    struct geo_info infos;
    struct tile_info *tile;
    int i;
    int tile_width;
    int tile_height;
    int sect;
    int tileNo = 0;
    int maxTile = 0;
    int baseHorz;
    int baseVert;
    int raster_ok = 0;
    int metadata_ok = 0;
    TIFF *tif = (TIFF *) 0;
    GTIF *gtif = (GTIF *) 0;
    GTIFDefn definition;
/* initializing the geo_info struct */
    infos.handle = handle;
    infos.table = table;
    infos.image_type = image_type;
    infos.quality_factor = quality_factor;
    infos.epsg_code = epsg_code;
    for (i = 0; i < NTILES; i++)
      {
	  tile = &(infos.tiles[i]);
	  tile->valid = 0;
	  tile->geometry = NULL;
      }
/* opening the GeoTIFF image */
    tif = XTIFFOpen (file_path, "r");
    if (!tif)
      {
	  printf ("discarding '%s': not a GeoTIFF (or read error)\n",
		  file_path);
	  return 0;
      }
    gtif = GTIFNew (tif);
    if (!gtif)
      {
	  printf ("discarding '%s': not a GeoTIFF (or read error)\n",
		  file_path);
	  goto stop;
      }
    if (!GTIFGetDefn (gtif, &definition))
      {
	  printf ("discarding '%s': not a GeoTIFF (or read error)\n",
		  file_path);
	  goto stop;
      }

    printf ("\nProcessing GeoTIFF: '%s'\n", file_path);
    printf ("=====================================================\n");
/* retrieving the TIFF dimensions */
    TIFFGetField (tif, TIFFTAG_IMAGELENGTH, &height);
    TIFFGetField (tif, TIFFTAG_IMAGEWIDTH, &width);
    infos.height = height;
    infos.width = width;
/* retrieving other TIFF settings */
    TIFFGetField (tif, TIFFTAG_BITSPERSAMPLE, &bits_per_sample);
    TIFFGetField (tif, TIFFTAG_SAMPLESPERPIXEL, &samples_per_pixel);
    TIFFGetField (tif, TIFFTAG_PHOTOMETRIC, &photometric);
    colorspace_name = (const char *) "UNKNOWN";
    if (bits_per_sample == 1 && samples_per_pixel == 1)
      {
	  colorspace_name = (const char *) "BiLevel - monochrome";
	  format_hint = (const char *) "TIFF [CCITT FAX-4]";
	  actual_format = (const char *) "TIFF [CCITT FAX-4]";
	  infos.image_type = IMAGE_TIFF_FAX4;
      }
    if (bits_per_sample == 8 && samples_per_pixel == 1 && photometric == 3)
      {
	  colorspace_name = (const char *) "Palette - 256 colors";
	  format_hint = (const char *) "GIF / PNG / TIFF";
	  if (image_type == GAIA_TIFF_BLOB)
	    {
		actual_format = (const char *) "TIFF [PALETTE]";
		infos.image_type = IMAGE_TIFF_PALETTE;
	    }
	  else if (image_type == GAIA_PNG_BLOB)
	    {
		actual_format = (const char *) "PNG [PALETTE]";
		infos.image_type = IMAGE_PNG_PALETTE;
	    }
	  else
	    {
		actual_format = (const char *) "GIF";
		infos.image_type = IMAGE_GIF_PALETTE;
	    }
      }
    if (bits_per_sample == 8 && samples_per_pixel == 1 && photometric < 2)
      {
	  colorspace_name = (const char *) "GrayScale - 256 levels";
	  format_hint = (const char *) "JPEG / PNG / TIFF";
	  if (image_type == GAIA_TIFF_BLOB)
	    {
		strcpy (dummy, "TIFF [GRAYSCALE]");
		infos.image_type = IMAGE_TIFF_GRAYSCALE;
	    }
	  else if (image_type == GAIA_PNG_BLOB)
	    {
		strcpy (dummy, "PNG [GRAYSCALE]");
		infos.image_type = IMAGE_PNG_GRAYSCALE;
	    }
	  else
	    {
		sprintf (dummy, "JPEG [GRAYSCALE] quality=%d", quality_factor);
		infos.image_type = IMAGE_JPEG_BW;
	    }
	  actual_format = (const char *) dummy;
      }
    if (samples_per_pixel >= 3)
      {
	  colorspace_name = (const char *) "RGB - TrueColor";
	  format_hint = (const char *) "JPEG / TIFF";
	  if (image_type == GAIA_TIFF_BLOB)
	    {
		sprintf (dummy, "TIFF [RGB]");
		infos.image_type = IMAGE_TIFF_RGB;
	    }
	  else if (image_type == GAIA_PNG_BLOB)
	    {
		sprintf (dummy, "PNG [RGB]");
		infos.image_type = IMAGE_PNG_RGB;
	    }
	  else
	    {
		sprintf (dummy, "JPEG [RGB] quality=%d", quality_factor);
		infos.image_type = IMAGE_JPEG_RGB;
	    }
	  actual_format = (const char *) dummy;
      }
    TIFFGetField (tif, TIFFTAG_COMPRESSION, &compression);
    switch (compression)
      {
      case COMPRESSION_NONE:
	  compression_name = (const char *) "Uncompressed";
	  break;
      case COMPRESSION_CCITTRLE:
	  compression_name = (const char *) "CCITT RLE";
	  break;
      case COMPRESSION_CCITTFAX3:
	  compression_name = (const char *) "CCITT FAX-3";
	  break;
      case COMPRESSION_CCITTFAX4:
	  compression_name = (const char *) "CCITT FAX-4";
	  break;
      case COMPRESSION_LZW:
	  compression_name = (const char *) "LZW";
	  break;
      case COMPRESSION_OJPEG:
	  compression_name = (const char *) "OLD-STYLED JPEG";
	  break;
      case COMPRESSION_JPEG:
	  compression_name = (const char *) "JPEG";
	  break;
      case COMPRESSION_NEXT:
	  compression_name = (const char *) "NEXT";
	  break;
      case COMPRESSION_CCITTRLEW:
	  compression_name = (const char *) "CCITT RLEW";
	  break;
      case COMPRESSION_PACKBITS:
	  compression_name = (const char *) "PACKBITS";
	  break;
      case COMPRESSION_THUNDERSCAN:
	  compression_name = (const char *) "THUNDERSCAN";
	  break;
      case COMPRESSION_IT8CTPAD:
	  compression_name = (const char *) "IT8CTPAD";
	  break;
      case COMPRESSION_IT8LW:
	  compression_name = (const char *) "IT8LW";
	  break;
      case COMPRESSION_IT8MP:
	  compression_name = (const char *) "IT8MP";
	  break;
      case COMPRESSION_IT8BL:
	  compression_name = (const char *) "IT8BL";
	  break;
      case COMPRESSION_PIXARFILM:
	  compression_name = (const char *) "PIXARFILM";
	  break;
      case COMPRESSION_PIXARLOG:
	  compression_name = (const char *) "PIXARLOG";
	  break;
      case COMPRESSION_DEFLATE:
	  compression_name = (const char *) "DEFLATE";
	  break;
      case COMPRESSION_ADOBE_DEFLATE:
	  compression_name = (const char *) "ADOBE-DEFLATE";
	  break;
      case COMPRESSION_DCS:
	  compression_name = (const char *) "DCS";
	  break;
      case COMPRESSION_JBIG:
	  compression_name = (const char *) "JBIG";
	  break;
      case COMPRESSION_SGILOG:
	  compression_name = (const char *) "SGILOG";
	  break;
      case COMPRESSION_SGILOG24:
	  compression_name = (const char *) "SGILOG24";
	  break;
      case COMPRESSION_JP2000:
	  compression_name = (const char *) "JPEG-2000";
	  break;
      default:
	  compression_name = (const char *) "UNKNOWN";
	  break;
      };

/* retrieving the EPSG code: Sandro 2012-12-15 */
    if (definition.PCS == 32767)
      {
	  if (definition.GCS != 32767)
	      infos.epsg = definition.GCS;
      }
    else
	infos.epsg = definition.PCS;

/* computing the corners coords */
    cx = 0.0;
    cy = 0.0;
    GTIFImageToPCS (gtif, &cx, &cy);
    infos.upper_left_x = cx;
    infos.upper_left_y = cy;
    cx = 0.0;
    cy = height;
    GTIFImageToPCS (gtif, &cx, &cy);
    infos.lower_left_x = cx;
    infos.lower_left_y = cy;
    cx = width;
    cy = 0.0;
    GTIFImageToPCS (gtif, &cx, &cy);
    infos.upper_right_x = cx;
    infos.upper_right_y = cy;
    cx = width;
    cy = height;
    GTIFImageToPCS (gtif, &cx, &cy);
    infos.lower_right_x = cx;
    infos.lower_right_y = cy;
/* computing the pixel size */
    infos.pixel_x = (infos.upper_right_x - infos.upper_left_x) / (double) width;
    infos.pixel_y = (infos.upper_left_y - infos.lower_left_y) / (double) height;

/* printing out some general info */
    printf ("Pixels:     %dh x %dv\n", width, height);
    printf ("Pixel size: %1.10fh %1.10fv\n", infos.pixel_x, infos.pixel_y);
    if (infos.epsg_code >= 0)
	printf ("EPSG code:  %d [forcibly remapped, was %d]\n", infos.epsg_code,
		infos.epsg);
    else
	printf ("EPSG code:  %d\n", infos.epsg);
    printf ("\tUpper Left  corner: %1.10f %1.10f\n", infos.upper_left_x,
	    infos.upper_left_y);
    printf ("\tUpper Right corner: %1.10f %1.10f\n", infos.upper_right_x,
	    infos.upper_right_y);
    printf ("\tLower Left  corner: %1.10f %1.10f\n", infos.lower_left_x,
	    infos.lower_left_y);
    printf ("\tLower Right corner: %1.10f %1.10f\n", infos.lower_right_x,
	    infos.lower_right_y);
    if (TIFFIsTiled (tif))
      {
	  /* processing a tiled Tiff IS NOT SUPPORTED */
	  TIFFGetField (tif, TIFFTAG_TILELENGTH, &tif_tile_height);
	  TIFFGetField (tif, TIFFTAG_TILEWIDTH, &tif_tile_width);
	  infos.tif_tile_width = tif_tile_width;
	  infos.tif_tile_height = tif_tile_height;
	  infos.is_tiled = 1;
      }
    else
      {
	  /* OK, processing a stripped TIFF */
	  TIFFGetField (tif, TIFFTAG_ROWSPERSTRIP, &rows_strip);
	  infos.rows_strip = rows_strip;
	  infos.is_tiled = 0;
      }
    printf ("----------------\n");
    printf ("Compression:     %s\n", compression_name);
    printf ("BitsPerSample:   %d\n", bits_per_sample);
    printf ("SamplesPerPixel: %d\n", samples_per_pixel);
    if (infos.is_tiled)
      {
	  printf ("TileWidth:       %d\n", tif_tile_width);
	  printf ("TileLength:      %d\n", tif_tile_height);
      }
    else
	printf ("RowsPerStrip:    %d\n", rows_strip);
    printf ("----------------\n");
    printf ("Colorspace:      %s\n", colorspace_name);
    printf ("FormatHint:      %s\n", format_hint);
    printf ("ActualFormat:    %s\n", actual_format);
    printf ("----------------\n");
/* computing the tile dims */
    tile_width = width;
    tile_height = height;
    sect = 1;
    while (1)
      {
	  if (tile_width > tile_size || tile_height > tile_size)
	    {
		sect++;
		tile_width = width / sect;
		tile_height = height / sect;
		continue;
	    }
	  if ((tile_width * sect) < (int) width)
	      tile_width++;
	  if ((tile_height * sect) < (int) height)
	      tile_height++;
	  infos.tile_width = tile_width;
	  infos.tile_height = tile_height;
	  break;
      }
    extra_width = tile_width * sect;
    extra_height = tile_height * sect;
    baseHorz = 0;
    baseVert = 0;
    while (1)
      {
	  /* computing the total tiles # */
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
    printf ("RequiredTiles:   %d tiles [%dh x %dv]\n", maxTile, tile_width,
	    tile_height);
    printf ("----------------\n");
    if (test_mode)
      {
	  printf ("\n");
	  return 1;
      }

/* the complete operation is handled as an unique SQL Transaction */
    ret = sqlite3_exec (infos.handle, "BEGIN", NULL, NULL, &sql_err);
    if (ret != SQLITE_OK)
      {
	  printf ("BEGIN TRANSACTION error: %s\n", sql_err);
	  sqlite3_free (sql_err);
	  goto stop;
      }

/* just in case it doesn't exist, we'll try anyway to create the table */
    if (!create_raster_table (&infos))
	goto stop;

/* creating the INSERT INTO xx_rasters prepared statement */
    sprintf (sql, "INSERT INTO \"%s_rasters\" ", infos.table);
    strcat (sql, "(id, raster) ");
    strcat (sql, " VALUES (NULL, ?)");
    ret =
	sqlite3_prepare_v2 (infos.handle, sql, strlen (sql), &(infos.stmt),
			    NULL);
    if (ret != SQLITE_OK)
      {
	  printf ("SQL error: %s\n%s\n", sql, sqlite3_errmsg (infos.handle));
	  goto stop;
      }

    baseHorz = 0;
    baseVert = 0;
    for (tileNo = 0; tileNo < maxTile; tileNo++)
      {
	  /* exporting sectioned images AKA tiles */
	  if (verbose)
	    {
		fprintf (stderr, "\tloading %s [tile %d of %d]\n", file_path,
			 tileNo + 1, maxTile);
		fflush (stderr);
	    }
	  if (!export_tile
	      (tif, &infos, baseVert, baseHorz, tileNo, &(infos.tiles[tileNo])))
	      goto stop;
	  raster_ok++;
	  baseHorz += tile_width;
	  if (baseHorz >= extra_width)
	    {
		baseHorz = 0;
		baseVert += tile_height;
		if (baseVert >= extra_height)
		    break;
	    }
      }
/* finalizing the INSERT INTO xx_rasters prepared statement */
    sqlite3_finalize (infos.stmt);

/* creating the INSERT INTO xx_metadata prepared statements */
    sprintf (sql, "INSERT INTO \"%s_metadata\" ", infos.table);
    strcat (sql, "(id, source_name, tile_id, width, height, ");
    strcat (sql, "pixel_x_size, pixel_y_size, geometry) ");
    strcat (sql, " VALUES (?, ?, ?, ?, ?, ?, ?, ?)");
    ret = sqlite3_prepare_v2 (infos.handle, sql, strlen (sql), &stmt, NULL);
    if (ret != SQLITE_OK)
      {
	  printf ("SQL error: %s\n%s\n", sql, sqlite3_errmsg (infos.handle));
	  goto stop;
      }

    for (i = 0; i < NTILES; i++)
      {
	  /* now we have to INSERT the raster's metadata into the DB */
	  tile = &(infos.tiles[i]);
	  if (!(tile->valid))
	      continue;
	  sqlite3_reset (stmt);
	  sqlite3_clear_bindings (stmt);
	  sqlite3_bind_int64 (stmt, 1, tile->id_raster);
	  sqlite3_bind_text (stmt, 2, file_path, strlen (file_path),
			     SQLITE_STATIC);
	  sqlite3_bind_int (stmt, 3, tile->tileNo);
	  sqlite3_bind_int (stmt, 4, tile->raster_horz);
	  sqlite3_bind_int (stmt, 5, tile->raster_vert);
	  sqlite3_bind_double (stmt, 6, infos.pixel_x);
	  sqlite3_bind_double (stmt, 7, infos.pixel_y);
	  gaiaToSpatiaLiteBlobWkb (tile->geometry, &blob, &blob_size);
	  sqlite3_bind_blob (stmt, 8, blob, blob_size, free);
	  ret = sqlite3_step (stmt);
	  if (ret == SQLITE_DONE || ret == SQLITE_ROW)
	      ;
	  else
	    {
		printf ("sqlite3_step() error: %s\n",
			sqlite3_errmsg (infos.handle));
		sqlite3_finalize (stmt);
		goto stop;
	    }
	  metadata_ok++;
      }
/* finalizing the INSERT INTO xx_metadata prepared statement */
    sqlite3_finalize (stmt);

/* committing the still pending SQL Transaction */
    ret = sqlite3_exec (infos.handle, "COMMIT", NULL, NULL, &sql_err);
    if (ret != SQLITE_OK)
      {
	  printf ("COMMIT TRANSACTION error: %s\n", sql_err);
	  sqlite3_free (sql_err);
	  goto stop;
      }

    if (tif)
	XTIFFClose (tif);
    if (gtif)
	GTIFFree (gtif);
    for (i = 0; i < NTILES; i++)
      {
	  tile = &(infos.tiles[i]);
	  if (tile->geometry)
	      gaiaFreeGeomColl (tile->geometry);
      }

    printf ("InsertedTiles:   %d rows in table \"%s_rasters\"\n", raster_ok,
	    table);
    printf ("                 %d rows in table \"%s_metadata\"\n", metadata_ok,
	    table);
    printf ("----------------\n\n");
    return 1;
  stop:
    if (tif)
	XTIFFClose (tif);
    if (gtif)
	GTIFFree (gtif);
    for (i = 0; i < NTILES; i++)
      {
	  tile = &(infos.tiles[i]);
	  if (tile->geometry)
	      gaiaFreeGeomColl (tile->geometry);
      }
    printf ("***\n***  Sorry, some unexpected error occurred ...\n***\n\n");
    printf ("----------------\n\n");
    return 0;
}

static int
load_dir (sqlite3 * handle, const char *dir_path, const char *table,
	  int tile_size, int test_mode, int verbose, int image_type,
	  int quality_factor, int epsg_code)
{
/* importing GeoTIFF files from a whole DIRECTORY */
#if defined(_WIN32) && !defined(__MINGW32__)
/* Visual Studio .NET */
    struct _finddata_t c_file;
    intptr_t hFile;
    int cnt = 0;
    char file_path[1024];
    if (_chdir (dir_path) < 0)
      {
	  fprintf (stderr, "rasterlite_load: cannot access dir '%s'", dir_path);
	  return 0;
      }
    if ((hFile = _findfirst ("*.*", &c_file)) == -1L)
	fprintf (stderr,
		 "rasterlite_load: cannot access dir '%s' [or empty dir]\n",
		 dir_path);
    else
      {
	  while (1)
	    {
		if ((c_file.attrib & _A_RDONLY) == _A_RDONLY
		    || (c_file.attrib & _A_NORMAL) == _A_NORMAL)
		  {
		      sprintf (file_path, "%s\\%s", dir_path, c_file.name);
		      cnt +=
			  load_file (handle, file_path, table, tile_size,
				     test_mode, verbose, image_type,
				     quality_factor, epsg_code);
		  }
		if (_findnext (hFile, &c_file) != 0)
		    break;
	    };
	  _findclose (hFile);
      }
    return cnt;
#else
/* not Visual Studio .NET */
    int cnt = 0;
    char file_path[4096];
    char msg[256];
    struct dirent *entry;
    DIR *dir = opendir (dir_path);
    if (!dir)
      {
	  sprintf (msg, "rasterlite_load: cannot access dir '%s'", dir_path);
	  perror (msg);
	  return 0;
      }
    while (1)
      {
	  /* scanning dir-entries */
	  entry = readdir (dir);
	  if (!entry)
	      break;
	  sprintf (file_path, "%s/%s", dir_path, entry->d_name);
	  cnt +=
	      load_file (handle, file_path, table, tile_size, test_mode,
			 verbose, image_type, quality_factor, epsg_code);
      }
    closedir (dir);
    return cnt;
#endif
}

static sqlite3 *
db_connect (const char *path)
{
/* trying to connect SpatiaLite DB */
    sqlite3 *handle = NULL;
    int ret;
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
	return handle;

  unknown:
    if (handle)
	sqlite3_close (handle);
    printf ("DB '%s'\n", path);
    printf ("doesn't seems to contain valid Spatial Metadata ...\n\n");
    printf ("Please, run the 'spatialite-init' SQL script \n");
    printf ("in order to initialize Spatial Metadata\n\n");
    return NULL;
}

static void
do_help ()
{
/* printing the argument list */
    fprintf (stderr, "\n\nusage: rasterlite_load ARGLIST\n");
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
    fprintf (stderr,
	     "-D or --dir         dir_path      the DIR path containing GeoTIFF files\n");
    fprintf (stderr,
	     "-f or --file        file_name     a single GeoTIFF file\n");
    fprintf (stderr, "-s or --tile-size   num           [default = 512]\n");
    fprintf (stderr,
	     "-e or --epsg-code   num           [optional: EPSG code]\n");
    fprintf (stderr, "-i or --image-type  type          [JPEG|PNG|GIF|TIFF]\n");
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
    char *dir_path = NULL;
    char *file_path = NULL;
    int tile_size = 512;
    int test_mode = 0;
    int quality_factor = -999999;
    int epsg_code = -1;
    int image_type = GAIA_JPEG_BLOB;
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
		  case ARG_DIR:
		      dir_path = argv[i];
		      break;
		  case ARG_FILE:
		      file_path = argv[i];
		      break;
		  case ARG_TILE_SIZE:
		      tile_size = atoi (argv[i]);
		      if (tile_size < 128)
			  tile_size = 128;
		      if (tile_size > 8192)
			  tile_size = 8192;
		      break;
		  case ARG_IMAGE_TYPE:
		      if (strcasecmp (argv[i], "JPEG") == 0)
			  image_type = GAIA_JPEG_BLOB;
		      if (strcasecmp (argv[i], "PNG") == 0)
			  image_type = GAIA_PNG_BLOB;
		      if (strcasecmp (argv[i], "GIF") == 0)
			  image_type = GAIA_GIF_BLOB;
		      if (strcasecmp (argv[i], "TIFF") == 0)
			  image_type = GAIA_TIFF_BLOB;
		      break;
		  case ARG_QUALITY_FACTOR:
		      quality_factor = atoi (argv[i]);
		      break;
		  case ARG_EPSG_CODE:
		      epsg_code = atoi (argv[i]);
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
	  if (strcasecmp (argv[i], "--dir-path") == 0)
	    {
		next_arg = ARG_DIR;
		continue;
	    }
	  if (strcmp (argv[i], "-D") == 0)
	    {
		next_arg = ARG_DIR;
		continue;
	    }
	  if (strcasecmp (argv[i], "--file-path") == 0)
	    {
		next_arg = ARG_FILE;
		continue;
	    }
	  if (strcmp (argv[i], "-f") == 0)
	    {
		next_arg = ARG_FILE;
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
	  if (strcmp (argv[i], "-e") == 0)
	    {
		next_arg = ARG_EPSG_CODE;
		continue;
	    }
	  if (strcasecmp (argv[i], "--epsg_code") == 0)
	    {
		next_arg = ARG_EPSG_CODE;
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
    if (!test_mode)
      {
	  if (!path)
	    {
		fprintf (stderr,
			 "did you forget to set the --db-path argument ?\n");
		error = 1;
	    }
	  if (!table)
	    {
		printf ("did you forget to set the --table-name argument ?\n");
		error = 1;
	    }
      }
    if (!dir_path && !file_path)
      {
	  fprintf (stderr,
		   "did you forget to set the --dir-path OR --file-path argument ?\n");
	  error = 1;
      }
    if (dir_path && file_path)
      {
	  fprintf (stderr,
		   "--dir_path AND --file_path arguments are mutually exclusive\n");
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
      {
	  printf ("TEST mode: no actual action will be performed\n");
	  printf ("*** ignoring the SpatiaLite DB ***\n");
      }
    else
      {
	  printf ("SpatiaLite DB path: '%s'\n", path);
	  printf ("Table prefix: '%s'\n", table);
	  printf ("\ttable '%s_rasters' will store raster tiles\n", table);
	  printf ("\ttable '%s_metadata' will store tile metadata\n", table);
	  if (epsg_code > 0)
	      printf ("*** forcing SRID as EPSG code %d ***\n", epsg_code);
      }
    if (dir_path)
	printf ("Exploring directory: '%s'\n", dir_path);
    else
	printf ("GeoTIFF pathname: '%s'\n", file_path);
    printf ("Tile preferred max size: %d pixels\n", tile_size);
    switch (image_type)
      {
      case GAIA_JPEG_BLOB:
	  printf ("Tile image type: JPEG quality=%d\n", quality_factor);
	  break;
      case GAIA_PNG_BLOB:
	  printf ("Tile image type: PNG\n");
	  break;
      case GAIA_GIF_BLOB:
	  printf ("Tile image type: GIF\n");
	  break;
      case GAIA_TIFF_BLOB:
	  printf ("Tile image type: TIFF\n");
	  break;
      default:
	  printf ("Tile image type: UNKNOWN\n");
	  break;
      };
    printf ("=====================================================\n\n");
    if (!test_mode)
      {
	  /* trying to connect DB */
	  handle = db_connect (path);
	  if (!handle)
	      return 1;
      }
    if (dir_path)
	cnt =
	    load_dir (handle, dir_path, table, tile_size, test_mode, verbose,
		      image_type, quality_factor, epsg_code);
    else
	cnt =
	    load_file (handle, file_path, table, tile_size, test_mode, verbose,
		       image_type, quality_factor, epsg_code);
    if (!test_mode)
      {
	  /* disconnecting DB */
	  sqlite3_close (handle);
      }
    return 0;
}
