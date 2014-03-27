/* 
/ rasterlite.c
/
/ the RasterLite library core 
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

static void
reset_error (rasterlitePtr handle)
{
/* resetting the last error description */
    if (handle->last_error)
	free (handle->last_error);
    handle->last_error = NULL;
    handle->error = RASTERLITE_OK;
}

static void
set_error (rasterlitePtr handle, const char *error)
{
/* setting up the last error description */
    int len;
    handle->error = RASTERLITE_ERROR;
    len = strlen (error);
    handle->last_error = malloc (len + 1);
    strcpy (handle->last_error, error);
}

static int
get_extent (rasterlitePtr handle, double *min_x, double *min_y, double *max_x,
	    double *max_y)
{
/* trying to get the data source full extent */
    sqlite3_stmt *stmt;
    int ret;
    char sql[1024];
    char sql2[512];
    char error[1024];
    double mnx = DBL_MAX;
    double mny = DBL_MAX;
    double mxx = DBL_MAX;
    double mxy = DBL_MAX;
    strcpy (sql, "SELECT Min(MbrMinX(geometry)), Min(MbrMinY(geometry)), ");
    strcat (sql, "Max(MbrMaxX(geometry)), Max(MbrMaxY(geometry)) FROM ");
    sprintf (sql2, " \"%s_metadata\" ", handle->table_prefix);
    strcat (sql, sql2);
    ret = sqlite3_prepare_v2 (handle->handle, sql, strlen (sql), &stmt, NULL);
    if (ret != SQLITE_OK)
      {
	  sprintf (error, "SQL error: %s\n", sqlite3_errmsg (handle->handle));
	  set_error (handle, error);
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
		if (sqlite3_column_type (stmt, 0) == SQLITE_FLOAT)
		    mnx = sqlite3_column_double (stmt, 0);
		if (sqlite3_column_type (stmt, 1) == SQLITE_FLOAT)
		    mny = sqlite3_column_double (stmt, 1);
		if (sqlite3_column_type (stmt, 2) == SQLITE_FLOAT)
		    mxx = sqlite3_column_double (stmt, 2);
		if (sqlite3_column_type (stmt, 3) == SQLITE_FLOAT)
		    mxy = sqlite3_column_double (stmt, 3);
	    }
	  else
	    {
		sprintf (error, "SQL error: %s\n",
			 sqlite3_errmsg (handle->handle));
		set_error (handle, error);
		return 0;
	    }
      }
    sqlite3_finalize (stmt);
    if (mnx != DBL_MAX && mny != DBL_MAX && mxx != DBL_MAX && mxy != DBL_MAX)
      {
	  *min_x = mnx;
	  *min_y = mny;
	  *max_x = mxx;
	  *max_y = mxy;
	  return 1;
      }
    sprintf (error, "Unable to get the data source full extent\n");
    set_error (handle, error);
    return 0;
}

static void
fetch_resolutions (rasterlitePtr handle)
{
/* trying to retrieve the available raster resolutions */
    sqlite3_stmt *stmt;
    int ret;
    char sql[1024];
    char error[1024];
    int levels = 0;
    double pixel_x_size[1024];
    double pixel_y_size[1024];
    int tile_count[1024];
    int i;
/* counting the pyramid levels */
    sprintf (sql,
	     "SELECT pixel_x_size, pixel_y_size, tile_count FROM raster_pyramids");
    strcat (sql, " WHERE table_prefix LIKE ?");
    strcat (sql, " ORDER BY pixel_x_size DESC");
    ret = sqlite3_prepare_v2 (handle->handle, sql, strlen (sql), &stmt, NULL);
    if (ret != SQLITE_OK)
      {
	  sprintf (error, "SQL error: %s\n", sqlite3_errmsg (handle->handle));
	  set_error (handle, error);
	  return;
      }
/* binding query params */
    sqlite3_reset (stmt);
    sqlite3_clear_bindings (stmt);
    sqlite3_bind_text (stmt, 1, handle->table_prefix,
		       strlen (handle->table_prefix), SQLITE_STATIC);
    while (1)
      {
	  /* scrolling the result set */
	  ret = sqlite3_step (stmt);
	  if (ret == SQLITE_DONE)
	      break;		/* end of result set */
	  if (ret == SQLITE_ROW)
	    {
		/* retrieving query values */
		pixel_x_size[levels] = sqlite3_column_double (stmt, 0);
		pixel_y_size[levels] = sqlite3_column_double (stmt, 1);
		tile_count[levels] = sqlite3_column_int (stmt, 2);
		levels++;
	    }
	  else
	    {
		sprintf (error, "SQL error: %s\n",
			 sqlite3_errmsg (handle->handle));
		set_error (handle, error);
		return;
	    }
      }
    sqlite3_finalize (stmt);
    if (!levels)
	return;
/* copying the resolutions into the HANDLE struct */
    handle->levels = levels;
    handle->pixel_x_size = malloc (sizeof (double) * levels);
    handle->pixel_y_size = malloc (sizeof (double) * levels);
    handle->tile_count = malloc (sizeof (int) * levels);
    for (i = 0; i < levels; i++)
      {
	  handle->pixel_x_size[i] = pixel_x_size[i];
	  handle->pixel_y_size[i] = pixel_y_size[i];
	  handle->tile_count[i] = tile_count[i];
      }
}

static int
check_raster_pyramids (sqlite3 * handle)
{
/* checking if table 'raster_pyramids' exists */
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
    return 0;
}

static int
fetch_srid_infos (rasterlitePtr handle)
{
/* trying to connect SpatiaLite DB */
    int ret;
    char sql[1024];
    char sql2[512];
    int len;
    const char *value;
    int i;
    char **results;
    int rows;
    int columns;

/* retrieving the SRID infos */
    strcpy (sql,
	    "SELECT s.srid, s.auth_name, s.auth_srid, s.ref_sys_name, s.proj4text ");
    strcat (sql, "FROM spatial_ref_sys AS s, geometry_columns as g ");
    strcat (sql, "WHERE g.f_table_name LIKE");
    sprintf (sql2, " \"%s_metadata\" ", handle->table_prefix);
    strcat (sql, sql2);
    strcat (sql, "AND g.f_geometry_column LIKE 'geometry' AND s.srid = g.srid");
    ret =
	sqlite3_get_table (handle->handle, sql, &results, &rows, &columns,
			   NULL);
    if (ret != SQLITE_OK)
	goto error;
    if (rows < 1)
	goto error;
    else
      {
	  for (i = 1; i <= rows; i++)
	    {
		value = results[(i * columns) + 0];
		if (value)
		    handle->srid = atoi (value);
		value = results[(i * columns) + 1];
		if (value)
		  {
		      len = strlen (value);
		      handle->auth_name = malloc (len + 1);
		      strcpy (handle->auth_name, value);
		  }
		value = results[(i * columns) + 2];
		if (value)
		    handle->auth_srid = atoi (value);
		value = results[(i * columns) + 3];
		if (value)
		  {
		      len = strlen (value);
		      handle->ref_sys_name = malloc (len + 1);
		      strcpy (handle->ref_sys_name, value);
		  }
		value = results[(i * columns) + 4];
		if (value)
		  {
		      len = strlen (value);
		      handle->proj4text = malloc (len + 1);
		      strcpy (handle->proj4text, value);
		  }
	    }
      }
    sqlite3_free_table (results);
    return 1;
  error:
    sqlite3_free_table (results);
    return 0;
}

static sqlite3 *
db_connect (const char *path, const char *table, char *error)
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

    ret = sqlite3_open_v2 (path, &handle, SQLITE_OPEN_READWRITE, NULL);
    if (ret != SQLITE_OK)
      {
	  sprintf (error, "cannot open DB: %s", sqlite3_errmsg (handle));
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
    sprintf (error, "this DB doesn't seems to contain valid Spatial Metadata");
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
    sprintf (error, "this DB doesn't seems to contain the \"%s\" datasource",
	     table);
    return NULL;
}

RASTERLITE_DECLARE void *
rasterliteOpen (const char *path, const char *table_prefix)
{
/* trying to open the RasterLite data-source */
    rasterlitePtr handle;
    int len;
    int ret;
    const char *version;
    char error[1024];
    char sql[1024];
/* allocating and initializing the HANDLE struct */
    handle = malloc (sizeof (rasterlite));
    len = strlen (path);
    handle->path = malloc (len + 1);
    strcpy (handle->path, path);
    len = strlen (table_prefix);
    handle->table_prefix = malloc (len + 1);
    strcpy (handle->table_prefix, table_prefix);
    handle->handle = NULL;
    handle->sqlite_version = NULL;
    handle->spatialite_version = NULL;
    handle->srid = -1;
    handle->auth_name = NULL;
    handle->auth_srid = -1;
    handle->ref_sys_name = NULL;
    handle->proj4text = NULL;
    handle->stmt_rtree = NULL;
    handle->stmt_plain = NULL;
    handle->last_error = NULL;
    handle->error = RASTERLITE_OK;
    handle->pixel_x_size = NULL;
    handle->pixel_y_size = NULL;
    handle->tile_count = NULL;
    handle->levels = 0;
    handle->transparent_color = -1;
    handle->background_color = true_color (0, 0, 0);
/* initializing SpatiaLite */
    spatialite_init (0);
/* retrieving the Version Infos */
    version = sqlite3_libversion ();
    len = strlen (version);
    handle->sqlite_version = malloc (len + 1);
    strcpy (handle->sqlite_version, version);
    version = spatialite_version ();
    len = strlen (version);
    handle->spatialite_version = malloc (len + 1);
    strcpy (handle->spatialite_version, version);
/* trying to connect the DB */
    handle->handle = db_connect (path, table_prefix, error);
    if (!(handle->handle))
      {
	  set_error (handle, error);
	  return handle;
      }
/* checking if the 'raster pyramids' table exists */
    if (!check_raster_pyramids (handle->handle))
      {
	  strcpy (error,
		  "this DB doesn't seems to contain a valid \"raster_pyramids\" table");
	  set_error (handle, error);
	  return handle;
      }
/* retrieving the availble raster resolutions */
    fetch_resolutions (handle);
    if (!(handle->levels))
      {
	  sprintf (error,
		   "the datasource \"%s\" doesn't seems to contain any valid Pyramid Level",
		   table_prefix);
	  set_error (handle, error);
	  return handle;
      }
/* retrieving the SRID infos */
    if (!fetch_srid_infos (handle))
      {
	  sprintf (error,
		   "the datasource \"%s\" doesn't seems to correspond to any valid SRID",
		   table_prefix);
	  set_error (handle, error);
	  return handle;
      }
/* preparing the SQL statement [using the R*Tree Spatial Index] */
    strcpy (sql, "SELECT m.geometry, r.raster FROM \"");
    strcat (sql, handle->table_prefix);
    strcat (sql, "_metadata\" AS m, \"");
    strcat (sql, handle->table_prefix);
    strcat (sql, "_rasters\" AS r WHERE m.ROWID IN (SELECT pkid ");
    strcat (sql, "FROM \"idx_");
    strcat (sql, handle->table_prefix);
    strcat (sql, "_metadata_geometry\" ");
    strcat (sql, "WHERE xmin < ? AND xmax > ? AND ymin < ? AND ymax > ?) ");
    strcat (sql,
	    "AND m.pixel_x_size = ? AND m.pixel_y_size = ? AND r.id = m.id");
    ret =
	sqlite3_prepare_v2 (handle->handle, sql, strlen (sql),
			    &(handle->stmt_rtree), NULL);
    if (ret != SQLITE_OK)
      {
	  sprintf (error, "SQL error: %s\n", sqlite3_errmsg (handle->handle));
	  set_error (handle, error);
	  return handle;
      }
/* preparing the SQL statement [plain Table Scan] */
    strcpy (sql, "SELECT m.geometry, r.raster FROM \"");
    strcat (sql, handle->table_prefix);
    strcat (sql, "_metadata\" AS m, \"");
    strcat (sql, handle->table_prefix);
    strcat (sql, "_rasters\" AS r ");
    strcat (sql, "WHERE MbrIntersects(m.geometry, BuildMbr(?, ?, ?, ?)) ");
    strcat (sql,
	    "AND m.pixel_x_size = ? AND m.pixel_y_size = ? AND r.id = m.id");
    ret =
	sqlite3_prepare_v2 (handle->handle, sql, strlen (sql),
			    &(handle->stmt_plain), NULL);
    if (ret != SQLITE_OK)
      {
	  sprintf (error, "SQL error: %s\n", sqlite3_errmsg (handle->handle));
	  set_error (handle, error);
	  return handle;
      }
    return handle;
}

RASTERLITE_DECLARE void
rasterliteClose (void *ext_handle)
{
/* closing the RasterLite data-source - memory cleanup */
    rasterlitePtr handle = (rasterlitePtr) ext_handle;
    if (handle->path)
	free (handle->path);
    if (handle->table_prefix)
	free (handle->table_prefix);
    if (handle->last_error)
	free (handle->last_error);
    if (handle->auth_name)
	free (handle->auth_name);
    if (handle->ref_sys_name)
	free (handle->ref_sys_name);
    if (handle->proj4text)
	free (handle->proj4text);
    if (handle->pixel_x_size)
	free (handle->pixel_x_size);
    if (handle->pixel_y_size)
	free (handle->pixel_y_size);
    if (handle->tile_count)
	free (handle->tile_count);
    if (handle->stmt_rtree)
	sqlite3_finalize (handle->stmt_rtree);
    if (handle->stmt_plain)
	sqlite3_finalize (handle->stmt_plain);
    if (handle->sqlite_version)
	free (handle->sqlite_version);
    if (handle->spatialite_version)
	free (handle->spatialite_version);
    if (handle->handle)
	sqlite3_close (handle->handle);
    free (handle);
    spatialite_cleanup ();
}

RASTERLITE_DECLARE int
rasterliteHasTransparentColor (void *ext_handle)
{
/* return 1 if the TransparentColor is set; otherwise 0 */
    rasterlitePtr handle = (rasterlitePtr) ext_handle;
    if (handle->transparent_color < 0)
	return RASTERLITE_ERROR;
    return RASTERLITE_OK;
}

RASTERLITE_DECLARE void
rasterliteSetTransparentColor (void *ext_handle, unsigned char red,
			       unsigned char green, unsigned char blue)
{
/* setting the TransparentColor */
    rasterlitePtr handle = (rasterlitePtr) ext_handle;
    handle->transparent_color = true_color (red, green, blue);
}

RASTERLITE_DECLARE int
rasterliteGetTransparentColor (void *ext_handle, unsigned char *red,
			       unsigned char *green, unsigned char *blue)
{
/* gets the TransparentColor currently set; return 1 if the TransparentColor is set; otherwise 0 */
    rasterlitePtr handle = (rasterlitePtr) ext_handle;
    if (handle->transparent_color < 0)
      {
	  *red = 0;
	  *green = 0;
	  *blue = 0;
	  return RASTERLITE_ERROR;
      }
    *red = true_color_get_red (handle->transparent_color);
    *green = true_color_get_green (handle->transparent_color);
    *blue = true_color_get_blue (handle->transparent_color);
    return RASTERLITE_OK;
}

RASTERLITE_DECLARE void
rasterliteSetBackgroundColor (void *ext_handle, unsigned char red,
			      unsigned char green, unsigned char blue)
{
/* setting the BackgroundColor */
    rasterlitePtr handle = (rasterlitePtr) ext_handle;
    handle->background_color = true_color (red, green, blue);
}

RASTERLITE_DECLARE int
rasterliteGetBackgroundColor (void *ext_handle, unsigned char *red,
			      unsigned char *green, unsigned char *blue)
{
/* gets the BackgroundColor currently set; return 1 if the BackgroundColor is set; otherwise 0 */
    rasterlitePtr handle = (rasterlitePtr) ext_handle;
    if (handle->background_color < 0)
      {
	  *red = 0;
	  *green = 0;
	  *blue = 0;
	  return RASTERLITE_ERROR;
      }
    *red = true_color_get_red (handle->background_color);
    *green = true_color_get_green (handle->background_color);
    *blue = true_color_get_blue (handle->background_color);
    return RASTERLITE_OK;
}

static int
best_raster_resolution (rasterlitePtr handle, double requested_ratio_x,
			double *pixel_x_size, double *pixel_y_size,
			int *strategy)
{
/* selects the best available resolution */
    int i;
    double min_dist = DBL_MAX;
    double dist;
    double best_x = DBL_MAX;
    double best_y = DBL_MAX;
    int tile_count = -1;
    for (i = 0; i < handle->levels; i++)
      {
	  dist = fabs (requested_ratio_x - handle->pixel_x_size[i]);
	  if (dist < min_dist)
	    {
		min_dist = dist;
		best_x = handle->pixel_x_size[i];
		best_y = handle->pixel_y_size[i];
		tile_count = handle->tile_count[i];
	    }
      }
    if (best_x == DBL_MAX || best_y == DBL_MAX)
	return RASTERLITE_ERROR;
    *pixel_x_size = best_x;
    *pixel_y_size = best_y;
    if (tile_count > 500)
      {
	  /* best access strategy: USING R*TRee */
	  *strategy = STRATEGY_RTREE;
      }
    else
      {
	  /* best access strategy: TABLE SCAN */
	  *strategy = STRATEGY_PLAIN;
      }
    return RASTERLITE_OK;
}

static void
mark_gray_rectangle (rasterliteImagePtr output, int base_x, int base_y,
		     int width, int height)
{
/* marking a gray rectangle */
    int x;
    int y;
    int dst_x;
    int dst_y;
    int border_color = true_color (192, 192, 192);
    int fill_color = true_color (240, 240, 240);
    for (y = 0; y < height; y++)
      {
	  dst_y = base_y + y;
	  if (dst_y < 0)
	      continue;
	  if (dst_y >= output->sy)
	      break;
	  for (x = 0; x < width; x++)
	    {
		dst_x = base_x + x;
		if (dst_x < 0)
		    continue;
		if (dst_x >= output->sx)
		    break;
		if (x == 0 || x == (width - 1) || y == 0 || y == (height - 1))
		    image_set_pixel (output, dst_x, dst_y, border_color);
		else
		    image_set_pixel (output, dst_x, dst_y, fill_color);
	    }
      }
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

static double
internal_round (double value)
{
/* replacing the C99 round() function */
    double min = floor (value);
    if (fabs (value - min) < 0.5)
	return min;
    return min + 1.0;
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

RASTERLITE_DECLARE int
rasterliteGetRaster2 (void *ext_handle, double cx, double cy,
		      double ext_pixel_x_size, double ext_pixel_y_size,
		      int width, int height, int image_type, int quality_factor,
		      void **raster, int *size)
{
/* trying to build the required raster image */
    rasterlitePtr handle = (rasterlitePtr) ext_handle;
    int raster_size = 0;
    void *tmp_raster = NULL;
    char error[1024];
    int ret;
    double pixel_x_size;
    double pixel_y_size;
    int strategy;
    sqlite3_stmt *stmt;
    double map_width = (double) width * ext_pixel_x_size;
    double map_height = (double) height * ext_pixel_y_size;
    double min_x = cx - (map_width / 2.0);
    double max_x = cx + (map_width / 2.0);
    double min_y = cy - (map_height / 2.0);
    double max_y = cy + (map_height / 2.0);
    rasterliteImagePtr output = NULL;
    reset_error (handle);
    if (handle->handle == NULL || handle->stmt_rtree == NULL
	|| handle->stmt_plain == NULL)
      {
	  sprintf (error, "invalid datasource");
	  set_error (handle, error);
	  *raster = NULL;
	  *size = 0;
	  return RASTERLITE_ERROR;
      }
    if (width < 64 || width > 32768 || height < 64 || height > 32768)
      {
	  sprintf (error, "invalid raster dims [%dh X %dv]", width, height);
	  set_error (handle, error);
	  *raster = NULL;
	  *size = 0;
	  return RASTERLITE_ERROR;
      }
    if (best_raster_resolution
	(ext_handle, ext_pixel_x_size, &pixel_x_size, &pixel_y_size,
	 &strategy) != RASTERLITE_OK)
      {
	  *raster = NULL;
	  *size = 0;
	  return RASTERLITE_ERROR;
      }
    if (strategy == STRATEGY_RTREE)
	stmt = handle->stmt_rtree;
    else
	stmt = handle->stmt_plain;
/* creating the output image */
    output = image_create (width, height);
    output->color_space = COLORSPACE_MONOCHROME;
    image_fill (output, handle->background_color);
/* binding query params */
    sqlite3_reset (stmt);
    sqlite3_clear_bindings (stmt);
    if (strategy == STRATEGY_RTREE)
      {
	  /* using the Spatial Index R*Tree */
	  sqlite3_bind_double (stmt, 1, max_x);
	  sqlite3_bind_double (stmt, 2, min_x);
	  sqlite3_bind_double (stmt, 3, max_y);
	  sqlite3_bind_double (stmt, 4, min_y);
      }
    else
      {
	  /* plain Table Scan */
	  sqlite3_bind_double (stmt, 1, min_x);
	  sqlite3_bind_double (stmt, 2, min_y);
	  sqlite3_bind_double (stmt, 3, max_x);
	  sqlite3_bind_double (stmt, 4, max_y);
      }
    sqlite3_bind_double (stmt, 5, pixel_x_size);
    sqlite3_bind_double (stmt, 6, pixel_y_size);
    while (1)
      {
	  /* scrolling the result set */
	  ret = sqlite3_step (stmt);
	  if (ret == SQLITE_DONE)
	      break;		/* end of result set */
	  if (ret == SQLITE_ROW)
	    {
		/* retrieving query values */
		gaiaGeomCollPtr geom = NULL;
		rasterliteImagePtr img = NULL;
		if (sqlite3_column_type (stmt, 0) == SQLITE_BLOB)
		  {
		      /* fetching Geometry */
		      const void *blob = sqlite3_column_blob (stmt, 0);
		      int blob_size = sqlite3_column_bytes (stmt, 0);
		      geom =
			  gaiaFromSpatiaLiteBlobWkb ((const unsigned char *)
						     blob, blob_size);
		  }
		if (sqlite3_column_type (stmt, 1) == SQLITE_BLOB)
		  {
		      /* fetching Raster Image */
		      const void *blob = sqlite3_column_blob (stmt, 1);
		      int blob_size = sqlite3_column_bytes (stmt, 1);
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
		      /* resizing the image [tile] */
		      double pre_width =
			  internal_round (((double) (img->sx) * pixel_x_size) /
					  ext_pixel_x_size);
		      double pre_height =
			  internal_round (((double) (img->sy) * pixel_y_size) /
					  ext_pixel_y_size);
		      int new_width = (int) pre_width + 1;
		      int new_height = (int) pre_height + 1;
		      double x = (geom->MinX - min_x) / ext_pixel_x_size;
		      double y =
			  (double) height -
			  ((geom->MaxY - min_y) / ext_pixel_y_size);
		      if (new_width > (img->sx * 16)
			  || new_height > (img->sy * 16))
			{
			    /* TOO BIG: drawing a gray rectangle */
			    mark_gray_rectangle (output, int_round (x),
						 int_round (y), new_width,
						 new_height);
			}
		      else
			{
			    /* resizing the raster tile */
			    if (new_width == img->sx && new_height == img->sy)
				;
			    else
			      {
				  rasterliteImagePtr img2 = img;
				  img = image_create (new_width, new_height);
				  image_resize (img, img2);
				  image_destroy (img2);
			      }
			    /* drawing the raster tile */
			    copy_rectangle (output, img,
					    handle->transparent_color,
					    int_round (x), int_round (y));
			    /* adjunsting the required colorspace */
			    if (output->color_space == COLORSPACE_MONOCHROME)
			      {
				  if (img->color_space != COLORSPACE_MONOCHROME)
				      output->color_space = img->color_space;
			      }
			    if (output->color_space == COLORSPACE_PALETTE)
			      {
				  if (img->color_space != COLORSPACE_PALETTE)
				      output->color_space = COLORSPACE_RGB;
			      }
			    if (output->color_space == COLORSPACE_GRAYSCALE)
			      {
				  if (img->color_space != COLORSPACE_GRAYSCALE)
				      output->color_space = COLORSPACE_RGB;
			      }
			}
		  }
		if (geom)
		    gaiaFreeGeomColl (geom);
		if (img)
		    image_destroy (img);
	    }
	  else
	    {
		sprintf (error, "SQL error: %s\n",
			 sqlite3_errmsg (handle->handle));
		set_error (handle, error);
		image_destroy (output);
		*raster = NULL;
		*size = 0;
		return RASTERLITE_ERROR;
	    }
      }
    if (image_type == GAIA_RGB_ARRAY)
      {
	  tmp_raster = image_to_rgb_array (output, &raster_size);
	  if (!tmp_raster)
	    {
		sprintf (error, "RGB ARRAY generation error\n");
		set_error (handle, error);
		image_destroy (output);
		*raster = NULL;
		*size = 0;
		return RASTERLITE_ERROR;
	    }
      }
    else if (image_type == GAIA_TIFF_BLOB)
      {
	  if (output->color_space == COLORSPACE_MONOCHROME)
	      tmp_raster = image_to_tiff_fax4 (output, &raster_size);
	  else if (output->color_space == COLORSPACE_GRAYSCALE)
	      tmp_raster = image_to_tiff_grayscale (output, &raster_size);
	  else if (output->color_space == COLORSPACE_PALETTE)
	      tmp_raster = image_to_tiff_palette (output, &raster_size);
	  else
	      tmp_raster = image_to_tiff_rgb (output, &raster_size);
	  if (!tmp_raster)
	    {
		sprintf (error, "TIFF compression error\n");
		set_error (handle, error);
		image_destroy (output);
		*raster = NULL;
		*size = 0;
		return RASTERLITE_ERROR;
	    }
      }
    else if (image_type == GAIA_PNG_BLOB)
      {
	  if (output->color_space == COLORSPACE_GRAYSCALE
	      || output->color_space == COLORSPACE_MONOCHROME)
	      tmp_raster = image_to_png_grayscale (output, &raster_size);
	  else if (output->color_space == COLORSPACE_PALETTE)
	      tmp_raster = image_to_png_palette (output, &raster_size);
	  else
	      tmp_raster = image_to_png_rgb (output, &raster_size);
	  if (!tmp_raster)
	    {
		sprintf (error, "PNG compression error\n");
		set_error (handle, error);
		image_destroy (output);
		*raster = NULL;
		*size = 0;
		return RASTERLITE_ERROR;
	    }
      }
    else if (image_type == GAIA_GIF_BLOB)
      {
	  if (output->color_space == COLORSPACE_GRAYSCALE
	      || output->color_space == COLORSPACE_MONOCHROME
	      || output->color_space == COLORSPACE_PALETTE)
	      ;
	  else
	    {
		sprintf (error, "GIF compression error\n");
		set_error (handle, error);
		image_destroy (output);
		*raster = NULL;
		*size = 0;
		return 0;
	    }
	  tmp_raster = image_to_gif (output, &raster_size);
	  if (!tmp_raster)
	    {
		sprintf (error, "GIF compression error\n");
		set_error (handle, error);
		image_destroy (output);
		*raster = NULL;
		*size = 0;
		return RASTERLITE_ERROR;
	    }
      }
    else
      {
	  if (output->color_space == COLORSPACE_GRAYSCALE
	      || output->color_space == COLORSPACE_MONOCHROME)
	      tmp_raster =
		  image_to_jpeg_grayscale (output, &raster_size,
					   quality_factor);
	  else
	      tmp_raster = image_to_jpeg (output, &raster_size, quality_factor);
	  if (!tmp_raster)
	    {
		sprintf (error, "JPEG compression error\n");
		set_error (handle, error);
		image_destroy (output);
		*raster = NULL;
		*size = 0;
		return RASTERLITE_ERROR;
	    }
      }
    *raster = tmp_raster;
    *size = raster_size;
    image_destroy (output);
    return RASTERLITE_OK;
}

RASTERLITE_DECLARE int
rasterliteGetRaster (void *handle, double cx, double cy, double pixel_size,
		     int width, int height, int image_type, int quality_factor,
		     void **raster, int *size)
{
/* trying to build the required raster image */
    return rasterliteGetRaster2 (handle, cx, cy, pixel_size, pixel_size, width,
				 height, image_type, quality_factor, raster,
				 size);
}

RASTERLITE_DECLARE int
rasterliteGetRasterByRect2 (void *handle, double x1, double y1, double x2,
			    double y2, double pixel_x_size, double pixel_y_size,
			    int width, int height, int image_type,
			    int quality_factor, void **raster, int *size)
{
/* trying to build the required raster image */
    double cx;
    double cy;
    double min_x = x1;
    double min_y = y1;
    double max_x = x2;
    double max_y = y2;
    if (x2 < min_x)
	min_x = x2;
    if (x1 > max_x)
	max_x = x1;
    if (y2 < min_y)
	min_y = y2;
    if (y1 > max_y)
	max_y = y1;
    cx = min_x + ((max_x - min_x) / 2.0);
    cy = min_y + ((max_y - min_y) / 2.0);
    return rasterliteGetRaster2 (handle, cx, cy, pixel_x_size, pixel_y_size,
				 width, height, image_type, quality_factor,
				 raster, size);
}

RASTERLITE_DECLARE int
rasterliteGetRasterByRect (void *handle, double x1, double y1, double x2,
			   double y2, double pixel_size, int width, int height,
			   int image_type, int quality_factor, void **raster,
			   int *size)
{
/* trying to build the required raster image */
    return rasterliteGetRasterByRect2 (handle, x1, y1, x2, y2, pixel_size,
				       pixel_size, width, height, image_type,
				       quality_factor, raster, size);
}

RASTERLITE_DECLARE int
rasterliteGetRawImage2 (void *ext_handle, double cx, double cy,
			double ext_pixel_x_size, double ext_pixel_y_size,
			int width, int height, int raw_format, void **raster,
			int *size)
{
/* trying to build the required RAW raster image */
    rasterlitePtr handle = (rasterlitePtr) ext_handle;
    int raster_size = 0;
    void *tmp_raster = NULL;
    char error[1024];
    int ret;
    double pixel_x_size;
    double pixel_y_size;
    int strategy;
    sqlite3_stmt *stmt;
    double map_width = (double) width * ext_pixel_x_size;
    double map_height = (double) height * ext_pixel_y_size;
    double min_x = cx - (map_width / 2.0);
    double max_x = cx + (map_width / 2.0);
    double min_y = cy - (map_height / 2.0);
    double max_y = cy + (map_height / 2.0);
    rasterliteImagePtr output = NULL;
    reset_error (handle);
    if (handle->handle == NULL || handle->stmt_rtree == NULL
	|| handle->stmt_plain == NULL)
      {
	  sprintf (error, "invalid datasource");
	  set_error (handle, error);
	  *raster = NULL;
	  *size = 0;
	  return RASTERLITE_ERROR;
      }
    if (width < 64 || width > 32768 || height < 64 || height > 32768)
      {
	  sprintf (error, "invalid raster dims [%dh X %dv]", width, height);
	  set_error (handle, error);
	  *raster = NULL;
	  *size = 0;
	  return RASTERLITE_ERROR;
      }
    if (raw_format == GAIA_RGB_ARRAY || raw_format == GAIA_RGBA_ARRAY
	|| raw_format == GAIA_ARGB_ARRAY || raw_format == GAIA_BGR_ARRAY
	|| raw_format == GAIA_BGRA_ARRAY)
	;
    else
      {
	  sprintf (error, "invalid raster RAW format");
	  set_error (handle, error);
	  *raster = NULL;
	  *size = 0;
	  return RASTERLITE_ERROR;
      }
    if (best_raster_resolution
	(ext_handle, ext_pixel_x_size, &pixel_x_size, &pixel_y_size,
	 &strategy) != RASTERLITE_OK)
      {
	  *raster = NULL;
	  *size = 0;
	  return RASTERLITE_ERROR;
      }
    if (strategy == STRATEGY_RTREE)
	stmt = handle->stmt_rtree;
    else
	stmt = handle->stmt_plain;
/* creating the output image */
    output = image_create (width, height);
    output->color_space = COLORSPACE_MONOCHROME;
    image_fill (output, handle->background_color);
/* binding query params */
    sqlite3_reset (stmt);
    sqlite3_clear_bindings (stmt);
    if (strategy == STRATEGY_RTREE)
      {
	  /* using the Spatial Index R*Tree */
	  sqlite3_bind_double (stmt, 1, max_x);
	  sqlite3_bind_double (stmt, 2, min_x);
	  sqlite3_bind_double (stmt, 3, max_y);
	  sqlite3_bind_double (stmt, 4, min_y);
      }
    else
      {
	  /* plain Table Scan */
	  sqlite3_bind_double (stmt, 1, min_x);
	  sqlite3_bind_double (stmt, 2, min_y);
	  sqlite3_bind_double (stmt, 3, max_x);
	  sqlite3_bind_double (stmt, 4, max_y);
      }
    sqlite3_bind_double (stmt, 5, pixel_x_size);
    sqlite3_bind_double (stmt, 6, pixel_y_size);
    while (1)
      {
	  /* scrolling the result set */
	  ret = sqlite3_step (stmt);
	  if (ret == SQLITE_DONE)
	      break;		/* end of result set */
	  if (ret == SQLITE_ROW)
	    {
		/* retrieving query values */
		gaiaGeomCollPtr geom = NULL;
		rasterliteImagePtr img = NULL;
		if (sqlite3_column_type (stmt, 0) == SQLITE_BLOB)
		  {
		      /* fetching Geometry */
		      const void *blob = sqlite3_column_blob (stmt, 0);
		      int blob_size = sqlite3_column_bytes (stmt, 0);
		      geom =
			  gaiaFromSpatiaLiteBlobWkb ((const unsigned char *)
						     blob, blob_size);
		  }
		if (sqlite3_column_type (stmt, 1) == SQLITE_BLOB)
		  {
		      /* fetching Raster Image */
		      const void *blob = sqlite3_column_blob (stmt, 1);
		      int blob_size = sqlite3_column_bytes (stmt, 1);
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
		      /* resizing the image [tile] */
		      double pre_width =
			  internal_round (((double) (img->sx) * pixel_x_size) /
					  ext_pixel_x_size);
		      double pre_height =
			  internal_round (((double) (img->sy) * pixel_y_size) /
					  ext_pixel_y_size);
		      int new_width = (int) pre_width + 1;
		      int new_height = (int) pre_height + 1;
		      double x = (geom->MinX - min_x) / ext_pixel_x_size;
		      double y =
			  (double) height -
			  ((geom->MaxY - min_y) / ext_pixel_y_size);
		      if (new_width > (img->sx * 16)
			  || new_height > (img->sy * 16))
			{
			    /* TOO BIG: drawing a gray rectangle */
			    mark_gray_rectangle (output, int_round (x),
						 int_round (y), new_width,
						 new_height);
			}
		      else
			{
			    /* resizing the raster tile */
			    if (new_width == img->sx && new_height == img->sy)
				;
			    else
			      {
				  rasterliteImagePtr img2 = img;
				  img = image_create (new_width, new_height);
				  image_resize (img, img2);
				  image_destroy (img2);
			      }
			    /* drawing the raster tile */
			    copy_rectangle (output, img,
					    handle->transparent_color,
					    int_round (x), int_round (y));
			    /* adjunsting the required colorspace */
			    if (output->color_space == COLORSPACE_MONOCHROME)
			      {
				  if (img->color_space != COLORSPACE_MONOCHROME)
				      output->color_space = img->color_space;
			      }
			    if (output->color_space == COLORSPACE_PALETTE)
			      {
				  if (img->color_space != COLORSPACE_PALETTE)
				      output->color_space = COLORSPACE_RGB;
			      }
			    if (output->color_space == COLORSPACE_GRAYSCALE)
			      {
				  if (img->color_space != COLORSPACE_GRAYSCALE)
				      output->color_space = COLORSPACE_RGB;
			      }
			}
		  }
		if (geom)
		    gaiaFreeGeomColl (geom);
		if (img)
		    image_destroy (img);
	    }
	  else
	    {
		sprintf (error, "SQL error: %s\n",
			 sqlite3_errmsg (handle->handle));
		set_error (handle, error);
		image_destroy (output);
		*raster = NULL;
		*size = 0;
		return RASTERLITE_ERROR;
	    }
      }
    if (raw_format == GAIA_RGB_ARRAY)
      {
	  tmp_raster = image_to_rgb_array (output, &raster_size);
	  if (!tmp_raster)
	    {
		sprintf (error, "RGB ARRAY generation error\n");
		set_error (handle, error);
		image_destroy (output);
		*raster = NULL;
		*size = 0;
		return RASTERLITE_ERROR;
	    }
      }
    if (raw_format == GAIA_RGBA_ARRAY)
      {
	  tmp_raster =
	      image_to_rgba_array (handle->transparent_color, output,
				   &raster_size);
	  if (!tmp_raster)
	    {
		sprintf (error, "RGBA ARRAY generation error\n");
		set_error (handle, error);
		image_destroy (output);
		*raster = NULL;
		*size = 0;
		return RASTERLITE_ERROR;
	    }
      }
    if (raw_format == GAIA_ARGB_ARRAY)
      {
	  tmp_raster =
	      image_to_argb_array (handle->transparent_color, output,
				   &raster_size);
	  if (!tmp_raster)
	    {
		sprintf (error, "ARGB ARRAY generation error\n");
		set_error (handle, error);
		image_destroy (output);
		*raster = NULL;
		*size = 0;
		return RASTERLITE_ERROR;
	    }
      }
    if (raw_format == GAIA_BGR_ARRAY)
      {
	  tmp_raster = image_to_bgr_array (output, &raster_size);
	  if (!tmp_raster)
	    {
		sprintf (error, "BGR ARRAY generation error\n");
		set_error (handle, error);
		image_destroy (output);
		*raster = NULL;
		*size = 0;
		return RASTERLITE_ERROR;
	    }
      }
    if (raw_format == GAIA_BGRA_ARRAY)
      {
	  tmp_raster =
	      image_to_bgra_array (handle->transparent_color, output,
				   &raster_size);
	  if (!tmp_raster)
	    {
		sprintf (error, "BGRA ARRAY generation error\n");
		set_error (handle, error);
		image_destroy (output);
		*raster = NULL;
		*size = 0;
		return RASTERLITE_ERROR;
	    }
      }

    *raster = tmp_raster;
    *size = raster_size;
    image_destroy (output);
    return RASTERLITE_OK;
}

RASTERLITE_DECLARE int
rasterliteGetRawImage (void *handle, double cx, double cy, double pixel_size,
		       int width, int height, int raw_format, void **raster,
		       int *size)
{
/* trying to build the required raster image */
    return rasterliteGetRawImage2 (handle, cx, cy, pixel_size, pixel_size,
				   width, height, raw_format, raster, size);
}

RASTERLITE_DECLARE int
rasterliteGetRawImageByRect2 (void *handle, double x1, double y1, double x2,
			      double y2, double pixel_x_size,
			      double pixel_y_size, int width, int height,
			      int raw_format, void **raster, int *size)
{
/* trying to build the required raster image */
    double cx;
    double cy;
    double min_x = x1;
    double min_y = y1;
    double max_x = x2;
    double max_y = y2;
    if (x2 < min_x)
	min_x = x2;
    if (x1 > max_x)
	max_x = x1;
    if (y2 < min_y)
	min_y = y2;
    if (y1 > max_y)
	max_y = y1;
    cx = min_x + ((max_x - min_x) / 2.0);
    cy = min_y + ((max_y - min_y) / 2.0);
    return rasterliteGetRawImage2 (handle, cx, cy, pixel_x_size, pixel_y_size,
				   width, height, raw_format, raster, size);
}

RASTERLITE_DECLARE int
rasterliteGetRawImageByRect (void *handle, double x1, double y1, double x2,
			     double y2, double pixel_size, int width,
			     int height, int raw_format, void **raster,
			     int *size)
{
/* trying to build the required raster image */
    return rasterliteGetRawImageByRect2 (handle, x1, y1, x2, y2, pixel_size,
					 pixel_size, width, height, raw_format,
					 raster, size);
}

RASTERLITE_DECLARE int
rasterliteGetLevels (void *ext_handle)
{
/* returns the Pyramid Levels */
    rasterlitePtr handle = (rasterlitePtr) ext_handle;
    return handle->levels;
}

RASTERLITE_DECLARE int
rasterliteGetResolution (void *ext_handle, int level, double *pixel_x_size,
			 double *pixel_y_size, int *tile_count)
{
/* returns the Resolution for the requested Pyramid Level */
    rasterlitePtr handle = (rasterlitePtr) ext_handle;
    if (level >= 0 && level < handle->levels)
      {
	  *pixel_x_size = handle->pixel_x_size[level];
	  *pixel_y_size = handle->pixel_y_size[level];
	  *tile_count = handle->tile_count[level];
	  return RASTERLITE_OK;
      }
    *pixel_x_size = 0.0;
    *pixel_y_size = 0.0;
    *tile_count = 0;
    return RASTERLITE_ERROR;
}

RASTERLITE_DECLARE int
rasterliteGetSrid (void *ext_handle, int *srid, const char **auth_name,
		   int *auth_srid, const char **ref_sys_name,
		   const char **proj4text)
{
    rasterlitePtr handle = (rasterlitePtr) ext_handle;
    *srid = handle->srid;
    *auth_name = handle->auth_name;
    *auth_srid = handle->auth_srid;
    *ref_sys_name = handle->ref_sys_name;
    *proj4text = handle->proj4text;
    return handle->error;
}

RASTERLITE_DECLARE int
rasterliteGetExtent (void *ext_handle, double *min_x, double *min_y,
		     double *max_x, double *max_y)
{
    rasterlitePtr handle = (rasterlitePtr) ext_handle;
    if (get_extent (handle, min_x, min_y, max_x, max_y))
	return RASTERLITE_OK;
    return handle->error;
}

RASTERLITE_DECLARE int
rasterliteIsError (void *ext_handle)
{
    rasterlitePtr handle = (rasterlitePtr) ext_handle;
    return handle->error;
}

RASTERLITE_DECLARE const char *
rasterliteGetPath (void *ext_handle)
{
    rasterlitePtr handle = (rasterlitePtr) ext_handle;
    return handle->path;
}

RASTERLITE_DECLARE const char *
rasterliteGetTablePrefix (void *ext_handle)
{
    rasterlitePtr handle = (rasterlitePtr) ext_handle;
    return handle->table_prefix;
}

RASTERLITE_DECLARE const char *
rasterliteGetLastError (void *ext_handle)
{
    rasterlitePtr handle = (rasterlitePtr) ext_handle;
    return handle->last_error;
}

RASTERLITE_DECLARE const char *
rasterliteGetSqliteVersion (void *ext_handle)
{
    rasterlitePtr handle = (rasterlitePtr) ext_handle;
    return handle->sqlite_version;
}

RASTERLITE_DECLARE const char *
rasterliteGetSpatialiteVersion (void *ext_handle)
{
    rasterlitePtr handle = (rasterlitePtr) ext_handle;
    return handle->spatialite_version;
}

RASTERLITE_DECLARE int
rasterliteExportGeoTiff (void *handle, const char *img_path, void *raster,
			 int size, double cx, double cy, double pixel_x_size,
			 double pixel_y_size, int width, int height)
{
/* exporting a TIFF as a GeoTIFF */
    double xllcorner;
    double yllcorner;
    int srid;
    const char *auth_name;
    int auth_srid;
    const char *ref_sys_name;
    const char *proj4text;
    int type = gaiaGuessBlobType (raster, size);
    if (type != GAIA_TIFF_BLOB)
	return RASTERLITE_ERROR;
    xllcorner = cx - ((double) width * pixel_x_size / 2.0);
    yllcorner = cy + ((double) height * pixel_y_size / 2.0);
    rasterliteGetSrid (handle, &srid, &auth_name, &auth_srid, &ref_sys_name,
		       &proj4text);
    if (!write_geotiff
	(img_path, raster, size, pixel_x_size, pixel_y_size, xllcorner,
	 yllcorner, proj4text))
	return RASTERLITE_ERROR;
    return RASTERLITE_OK;
}

RASTERLITE_DECLARE int
rasterliteGetBestAccess (void *ext_handle, double pixel_size,
			 double *pixel_x_size, double *pixel_y_size,
			 sqlite3_stmt ** stmt, int *use_rtree)
{
/* return the Best Access Method */
    int strategy;
    rasterlitePtr handle = (rasterlitePtr) ext_handle;
    if (best_raster_resolution
	(ext_handle, pixel_size, pixel_x_size, pixel_y_size,
	 &strategy) != RASTERLITE_OK)
      {
	  *stmt = NULL;
	  return RASTERLITE_ERROR;
      }
    if (strategy == STRATEGY_RTREE)
      {
	  *use_rtree = 1;
	  *stmt = handle->stmt_rtree;
      }
    else
      {
	  *use_rtree = 0;
	  *stmt = handle->stmt_plain;
      }
    return RASTERLITE_OK;
}
