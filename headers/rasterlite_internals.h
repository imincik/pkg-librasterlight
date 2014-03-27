/* 
/ rasterlite_internals.h
/
/ internal declarations
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

#define COLORSPACE_MONOCHROME	1
#define COLORSPACE_PALETTE	2
#define COLORSPACE_GRAYSCALE	3
#define COLORSPACE_RGB		4

#define IMAGE_JPEG_RGB		100
#define IMAGE_WAVELET_RGB	101
#define IMAGE_JPEG_BW		102
#define IMAGE_WAVELET_BW	103
#define IMAGE_TIFF_FAX4		104
#define IMAGE_TIFF_PALETTE	105
#define IMAGE_TIFF_GRAYSCALE	106
#define IMAGE_TIFF_RGB		107
#define IMAGE_PNG_PALETTE	108
#define IMAGE_PNG_GRAYSCALE	109
#define IMAGE_PNG_RGB		110
#define IMAGE_GIF_PALETTE	111

#define NTILES	8192

#define STRATEGY_RTREE	1
#define STRATEGY_PLAIN	2

#define RASTERLITE_TRUE	-1
#define RASTERLITE_FALSE	-2

#define true_color(r, g, b) (((r) << 16) + ((g) << 8) + (b))
#define image_set_pixel(img, x, y, color) 	img->pixels[y][x] = color
#define true_color_get_red(c) (((c) & 0xFF0000) >> 16)
#define true_color_get_green(c) (((c) & 0x00FF00) >> 8)
#define true_color_get_blue(c) ((c) & 0x0000FF)

struct tile_info
{
/* a struct to temporarily store tile infos */
    int valid;			/* validity marker: 0 ? 1 */
    int tileNo;			/* the tile ID */
    int raster_horz;		/* raster - horizontal pixels */
    int raster_vert;		/* raster - vertical pixels */
    sqlite3_int64 id_raster;	/* the raster ID */
    gaiaGeomCollPtr geometry;	/* geometry corresponding to this raster */
};

struct geo_info
{
/* a struct defining global GeoTiff attributes */
    uint32 height;		/* the GeoTiff height [in pixels] = TIFFTAG_IMAGELENGTH */
    uint32 width;		/* the GeoTiff height [in pixels] = TIFFTAG_IMAGEWIDTH */
    uint32 rows_strip;		/* the GeoTiff rows per strip      =  TIFFTAG_ROWSPERSTRIP */
    uint32 tif_tile_width;	/* the GeoTiff tile height = TIFFTAGE_TILEWIDTH */
    uint32 tif_tile_height;	/* the GeoTiff tile height = TIFFTAGE_TILELENGHT */
    int is_tiled;		/* the GeoTiff is tile-sturctured or not */
    int epsg;			/* the EPSG coordinate reference system code */
    int epsg_code;		/* the actual EPSG code [to be forced] */
    double upper_left_x;	/* geographic coords for each one raster corner */
    double upper_left_y;
    double lower_left_x;
    double lower_left_y;
    double upper_right_x;
    double upper_right_y;
    double lower_right_x;
    double lower_right_y;
    double pixel_x;		/* the X pixel size [in map units] */
    double pixel_y;		/* the Y pixel size [in map units] */
    int tile_height;		/* the TILE preferred height [in pixels]  */
    int tile_width;		/* the TILE preferred width [in pixels]  */
    sqlite3 *handle;		/* SQLite handle */
    sqlite3_stmt *stmt;		/* SQL preparared statement: INSERT INTO xx_rasters */
    const char *table;		/* the DB table name */
    int image_type;		/* the preferred image type [to be used for tiles] */
    int quality_factor;		/* the quality factor for JPEG compression */
    struct tile_info tiles[NTILES];
};

struct source_item
{
/* a raster source item */
    char *name;
    int count;
    struct source_item *next;
};

struct sources_list
{
/* the raster sources list */
    struct source_item *first;
    struct source_item *last;
};

struct tile_item
{
/* a raster tile item */
    sqlite3_int64 id;
    int srid;
    double min_x;
    double min_y;
    double max_x;
    double max_y;
    int width;
    int height;
    struct tile_item *next;
};

struct tiles_list
{
/* the raster tiles list */
    struct tile_item *first;
    struct tile_item *last;
};

struct thumbnail_tile
{
/* a struct to temporarily store thumbnail tile infos */
    struct tile_item *tile_1;	/* pointer to the uppermost-leftmost elementary tile */
    struct tile_item *tile_2;	/* pointer to the uppermost-rightmost elementary tile */
    struct tile_item *tile_3;	/* pointer to the lowermost-leftmost elementary tile */
    struct tile_item *tile_4;	/* pointer to the lowermost-rightmost elementary tile */
    int valid;			/* validity marker: 0 ? 1 */
    int tileNo;			/* the tile ID */
    int raster_horz;		/* raster - horizontal pixels */
    int raster_vert;		/* raster - vertical pixels */
    sqlite3_int64 id_raster;	/* the raster ID */
    gaiaGeomCollPtr geometry;	/* geometry corresponding to this raster */
};

typedef struct raster_lite
{
/* the RasterLite HANDLE struct */
    char *path;
    char *table_prefix;
    sqlite3 *handle;
    char *sqlite_version;
    char *spatialite_version;
    int srid;
    char *auth_name;
    int auth_srid;
    char *ref_sys_name;
    char *proj4text;
    sqlite3_stmt *stmt_rtree;
    sqlite3_stmt *stmt_plain;
    char *last_error;
    int error;
    double *pixel_x_size;
    double *pixel_y_size;
    int *tile_count;
    int levels;
    int transparent_color;
    int background_color;
} rasterlite;

typedef rasterlite *rasterlitePtr;

typedef struct raster_lite_image
{
/* a generic RGB image  */
    int **pixels;
    int sx;
    int sy;
    int color_space;
} rasterliteImage;

typedef rasterliteImage *rasterliteImagePtr;

extern rasterliteImagePtr image_create (int sx, int sy);
extern void image_destroy (rasterliteImagePtr img);
extern void image_fill (const rasterliteImagePtr img, int color);
extern void make_thumbnail (const rasterliteImagePtr thumbnail,
			    const rasterliteImagePtr image);
extern void image_resize (const rasterliteImagePtr dst,
			  const rasterliteImagePtr src);

extern void *image_to_jpeg (const rasterliteImagePtr img, int *size,
			    int quality);
extern void *image_to_jpeg_grayscale (const rasterliteImagePtr img, int *size,
				      int quality);
extern void *image_to_png_palette (const rasterliteImagePtr img, int *size);
extern void *image_to_png_grayscale (const rasterliteImagePtr img, int *size);
extern void *image_to_png_rgb (const rasterliteImagePtr img, int *size);
extern void *image_to_gif (const rasterliteImagePtr img, int *size);
extern void *image_to_tiff_fax4 (const rasterliteImagePtr img, int *size);
extern void *image_to_tiff_palette (const rasterliteImagePtr img, int *size);
extern void *image_to_tiff_grayscale (const rasterliteImagePtr img, int *size);
extern void *image_to_tiff_rgb (const rasterliteImagePtr img, int *size);

extern void *image_to_rgb_array (const rasterliteImagePtr img, int *size);
extern void *image_to_rgba_array (int transparent_color,
				  const rasterliteImagePtr img, int *size);
extern void *image_to_argb_array (int transparent_color,
				  const rasterliteImagePtr img, int *size);
extern void *image_to_bgr_array (const rasterliteImagePtr img, int *size);
extern void *image_to_bgra_array (int transparent_color,
				  const rasterliteImagePtr img, int *size);

extern rasterliteImagePtr image_from_rgb_array (const void *raw, int width,
						int height);
extern rasterliteImagePtr image_from_rgba_array (const void *raw, int width,
						 int height);
extern rasterliteImagePtr image_from_argb_array (const void *raw, int width,
						 int height);
extern rasterliteImagePtr image_from_bgr_array (const void *raw, int width,
						int height);
extern rasterliteImagePtr image_from_bgra_array (const void *raw, int width,
						 int height);

extern rasterliteImagePtr image_from_jpeg (int size, const void *data);
extern rasterliteImagePtr image_from_png (int size, const void *data);
extern rasterliteImagePtr image_from_gif (int size, const void *data);
extern rasterliteImagePtr image_from_tiff (int size, const void *data);

extern int is_image_monochrome (const rasterliteImagePtr img);
extern int is_image_grayscale (const rasterliteImagePtr img);
extern int is_image_palette256 (const rasterliteImagePtr img);
extern void image_resample_as_palette256 (const rasterliteImagePtr img);

extern int write_geotiff (const char *path, const void *raster, int size,
			  double xsize, double ysize, double xllcorner,
			  double yllcorner, const char *proj4text);

/* 
/
/ DISCLAIMER:
/ all the following code merely represents an 'ad hoc' adaption
/ deriving from the original GD lib code
/
*/

typedef struct xgdIOCtx
{
    int (*getC) (struct xgdIOCtx *);
    int (*getBuf) (struct xgdIOCtx *, void *, int);
    void (*putC) (struct xgdIOCtx *, int);
    int (*putBuf) (struct xgdIOCtx *, const void *, int);
    int (*seek) (struct xgdIOCtx *, const int);
    long (*tell) (struct xgdIOCtx *);
    void (*xgd_free) (struct xgdIOCtx *);
}
xgdIOCtx;

typedef struct xgdIOCtx *xgdIOCtxPtr;

typedef struct dpStruct
{
    void *data;
    int logicalSize;
    int realSize;
    int dataGood;
    int pos;
    int freeOK;
}
dynamicPtr;

typedef struct dpIOCtx
{
    xgdIOCtx ctx;
    dynamicPtr *dp;
}
dpIOCtx;

typedef struct dpIOCtx *dpIOCtxPtr;

extern int overflow2 (int a, int b);
extern void *xgdDPExtractData (struct xgdIOCtx *ctx, int *size);
extern xgdIOCtx *xgdNewDynamicCtx (int initialSize, const void *data);
extern xgdIOCtx *xgdNewDynamicCtxEx (int initialSize, const void *data,
				     int freeOKFlag);
extern int palette_set (int *mapping, int color);
extern int xgdPutBuf (const void *buf, int size, xgdIOCtx * ctx);
extern int xgdGetBuf (void *, int, xgdIOCtx *);
