/* 
/ rasterlite_aux.c
/
/ RAW image helpers
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

#ifdef _WIN32
#include <io.h>
#else
#include <unistd.h>
#endif

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

RASTERLITE_DECLARE int
rasterliteJpegBlobToRawImage (const void *blob, int blob_size, int raw_format,
			      void **raw, int *width, int *height)
{
/* decompressing a Jpeg compressed image and returning a RAW image */
    void *raw_array;
    int size;
    rasterliteImagePtr img = NULL;
    char *errmsg;

    if (raw_format == GAIA_RGB_ARRAY || raw_format == GAIA_RGBA_ARRAY
	|| raw_format == GAIA_ARGB_ARRAY || raw_format == GAIA_BGR_ARRAY
	|| raw_format == GAIA_BGRA_ARRAY)
	;
    else
      {
	  errmsg = "invalid raster RAW format";
	  goto error;
      }

    img = image_from_jpeg (blob_size, blob);
    if (!img)
      {
	  errmsg = "Jpeg decoder error";
	  goto error;
      }

    if (raw_format == GAIA_RGB_ARRAY)
      {
	  raw_array = image_to_rgb_array (img, &size);
	  if (!raw_array)
	    {
		errmsg = "RGB ARRAY generation error";
		goto error;
	    }
      }
    if (raw_format == GAIA_RGBA_ARRAY)
      {
	  raw_array = image_to_rgba_array (-1, img, &size);
	  if (!raw_array)
	    {
		errmsg = "RGBA ARRAY generation error";
		goto error;
	    }
      }
    if (raw_format == GAIA_ARGB_ARRAY)
      {
	  raw_array = image_to_argb_array (-1, img, &size);
	  if (!raw_array)
	    {
		errmsg = "ARGB ARRAY generation error";
		goto error;
	    }
      }
    if (raw_format == GAIA_BGR_ARRAY)
      {
	  raw_array = image_to_bgr_array (img, &size);
	  if (!raw_array)
	    {
		errmsg = "BGR ARRAY generation error";
		goto error;
	    }
      }
    if (raw_format == GAIA_BGRA_ARRAY)
      {
	  raw_array = image_to_bgra_array (-1, img, &size);
	  if (!raw_array)
	    {
		errmsg = "BGRA ARRAY generation error";
		goto error;
	    }
      }

    image_destroy (img);
    *raw = raw_array;
    *width = img->sx;
    *height = img->sy;
    return RASTERLITE_OK;

  error:
    fprintf (stderr, "%s\n", errmsg);
    if (img)
	image_destroy (img);
    *raw = NULL;
    *width = 0;
    *height = 0;
    return RASTERLITE_ERROR;
}

RASTERLITE_DECLARE int
rasterlitePngBlobToRawImage (const void *blob, int blob_size, int raw_format,
			     void **raw, int *width, int *height)
{
/* decompressing a Png compressed image and returning a RAW image*/
    void *raw_array;
    int size;
    rasterliteImagePtr img = NULL;
    char *errmsg;

    if (raw_format == GAIA_RGB_ARRAY || raw_format == GAIA_RGBA_ARRAY
	|| raw_format == GAIA_ARGB_ARRAY || raw_format == GAIA_BGR_ARRAY
	|| raw_format == GAIA_BGRA_ARRAY)
	;
    else
      {
	  errmsg = "invalid raster RAW format";
	  goto error;
      }

    img = image_from_png (blob_size, blob);
    if (!img)
      {
	  errmsg = "Png decoder error";
	  goto error;
      }

    if (raw_format == GAIA_RGB_ARRAY)
      {
	  raw_array = image_to_rgb_array (img, &size);
	  if (!raw_array)
	    {
		errmsg = "RGB ARRAY generation error";
		goto error;
	    }
      }
    if (raw_format == GAIA_RGBA_ARRAY)
      {
	  raw_array = image_to_rgba_array (-1, img, &size);
	  if (!raw_array)
	    {
		errmsg = "RGBA ARRAY generation error";
		goto error;
	    }
      }
    if (raw_format == GAIA_ARGB_ARRAY)
      {
	  raw_array = image_to_argb_array (-1, img, &size);
	  if (!raw_array)
	    {
		errmsg = "ARGB ARRAY generation error";
		goto error;
	    }
      }
    if (raw_format == GAIA_BGR_ARRAY)
      {
	  raw_array = image_to_bgr_array (img, &size);
	  if (!raw_array)
	    {
		errmsg = "BGR ARRAY generation error";
		goto error;
	    }
      }
    if (raw_format == GAIA_BGRA_ARRAY)
      {
	  raw_array = image_to_bgra_array (-1, img, &size);
	  if (!raw_array)
	    {
		errmsg = "BGRA ARRAY generation error";
		goto error;
	    }
      }

    image_destroy (img);
    *raw = raw_array;
    *width = img->sx;
    *height = img->sy;
    return RASTERLITE_OK;

  error:
    fprintf (stderr, "%s\n", errmsg);
    if (img)
	image_destroy (img);
    *raw = NULL;
    *width = 0;
    *height = 0;
    return RASTERLITE_ERROR;
}

RASTERLITE_DECLARE int
rasterliteGifBlobToRawImage (const void *blob, int blob_size, int raw_format,
			     void **raw, int *width, int *height)
{
/* decompressing a GIF compressed image and returning a RAW image */
    void *raw_array;
    int size;
    rasterliteImagePtr img = NULL;
    char *errmsg;

    if (raw_format == GAIA_RGB_ARRAY || raw_format == GAIA_RGBA_ARRAY
	|| raw_format == GAIA_ARGB_ARRAY || raw_format == GAIA_BGR_ARRAY
	|| raw_format == GAIA_BGRA_ARRAY)
	;
    else
      {
	  errmsg = "invalid raster RAW format";
	  goto error;
      }

    img = image_from_gif (blob_size, blob);
    if (!img)
      {
	  errmsg = "Gif decoder error";
	  goto error;
      }

    if (raw_format == GAIA_RGB_ARRAY)
      {
	  raw_array = image_to_rgb_array (img, &size);
	  if (!raw_array)
	    {
		errmsg = "RGB ARRAY generation error";
		goto error;
	    }
      }
    if (raw_format == GAIA_RGBA_ARRAY)
      {
	  raw_array = image_to_rgba_array (-1, img, &size);
	  if (!raw_array)
	    {
		errmsg = "RGBA ARRAY generation error";
		goto error;
	    }
      }
    if (raw_format == GAIA_ARGB_ARRAY)
      {
	  raw_array = image_to_argb_array (-1, img, &size);
	  if (!raw_array)
	    {
		errmsg = "ARGB ARRAY generation error";
		goto error;
	    }
      }
    if (raw_format == GAIA_BGR_ARRAY)
      {
	  raw_array = image_to_bgr_array (img, &size);
	  if (!raw_array)
	    {
		errmsg = "BGR ARRAY generation error";
		goto error;
	    }
      }
    if (raw_format == GAIA_BGRA_ARRAY)
      {
	  raw_array = image_to_bgra_array (-1, img, &size);
	  if (!raw_array)
	    {
		errmsg = "BGRA ARRAY generation error";
		goto error;
	    }
      }

    image_destroy (img);
    *raw = raw_array;
    *width = img->sx;
    *height = img->sy;
    return RASTERLITE_OK;

  error:
    fprintf (stderr, "%s\n", errmsg);
    if (img)
	image_destroy (img);
    *raw = NULL;
    *width = 0;
    *height = 0;
    return RASTERLITE_ERROR;
}

RASTERLITE_DECLARE int
rasterliteTiffBlobToRawImage (const void *blob, int blob_size, int raw_format,
			      void **raw, int *width, int *height)
{
/* decoding a TIFF encoded image and returning a RAW image */
    void *raw_array;
    int size;
    rasterliteImagePtr img = NULL;
    char *errmsg;

    if (raw_format == GAIA_RGB_ARRAY || raw_format == GAIA_RGBA_ARRAY
	|| raw_format == GAIA_ARGB_ARRAY || raw_format == GAIA_BGR_ARRAY
	|| raw_format == GAIA_BGRA_ARRAY)
	;
    else
      {
	  errmsg = "invalid raster RAW format";
	  goto error;
      }

    img = image_from_tiff (blob_size, blob);
    if (!img)
      {
	  errmsg = "Tiff decoder error";
	  goto error;
      }

    if (raw_format == GAIA_RGB_ARRAY)
      {
	  raw_array = image_to_rgb_array (img, &size);
	  if (!raw_array)
	    {
		errmsg = "RGB ARRAY generation error";
		goto error;
	    }
      }
    if (raw_format == GAIA_RGBA_ARRAY)
      {
	  raw_array = image_to_rgba_array (-1, img, &size);
	  if (!raw_array)
	    {
		errmsg = "RGBA ARRAY generation error";
		goto error;
	    }
      }
    if (raw_format == GAIA_ARGB_ARRAY)
      {
	  raw_array = image_to_argb_array (-1, img, &size);
	  if (!raw_array)
	    {
		errmsg = "ARGB ARRAY generation error";
		goto error;
	    }
      }
    if (raw_format == GAIA_BGR_ARRAY)
      {
	  raw_array = image_to_bgr_array (img, &size);
	  if (!raw_array)
	    {
		errmsg = "BGR ARRAY generation error";
		goto error;
	    }
      }
    if (raw_format == GAIA_BGRA_ARRAY)
      {
	  raw_array = image_to_bgra_array (-1, img, &size);
	  if (!raw_array)
	    {
		errmsg = "BGRA ARRAY generation error";
		goto error;
	    }
      }

    image_destroy (img);
    *raw = raw_array;
    *width = img->sx;
    *height = img->sy;
    return RASTERLITE_OK;

  error:
    fprintf (stderr, "%s\n", errmsg);
    if (img)
	image_destroy (img);
    *raw = NULL;
    *width = 0;
    *height = 0;
    return RASTERLITE_ERROR;
}

RASTERLITE_DECLARE int
rasterliteRawImageToJpegFile (const void *raw, int raw_format, int width,
			      int height, const char *path, int quality)
{
/* exports a RAW image into a JPEG compressed file */
    rasterliteImagePtr img = NULL;
    void *blob = NULL;
    int blob_size;
    char *errmsg;
    int err = 0;
    FILE *out;

    if (raw == NULL)
      {
	  errmsg = "NULL RAW image";
	  goto error;
      }
    if (width <= 0 || height <= 0)
      {
	  errmsg = "invalid RAW image width/height";
	  goto error;
      }
    if (raw_format == GAIA_RGB_ARRAY || raw_format == GAIA_RGBA_ARRAY
	|| raw_format == GAIA_ARGB_ARRAY || raw_format == GAIA_BGR_ARRAY
	|| raw_format == GAIA_BGRA_ARRAY)
	;
    else
      {
	  errmsg = "invalid raster RAW format";
	  goto error;
      }
    if (raw_format == GAIA_RGB_ARRAY)
      {
	  img = image_from_rgb_array (raw, width, height);
	  if (!img)
	    {
		errmsg = "unable to get an image from RGB ARRAY";
		goto error;
	    }
      }
    if (raw_format == GAIA_RGBA_ARRAY)
      {
	  img = image_from_rgba_array (raw, width, height);
	  if (!img)
	    {
		errmsg = "unable to get an image from RGBA ARRAY";
		goto error;
	    }
      }
    if (raw_format == GAIA_ARGB_ARRAY)
      {
	  img = image_from_argb_array (raw, width, height);
	  if (!img)
	    {
		errmsg = "unable to get an image from ARGB ARRAY";
		goto error;
	    }
      }
    if (raw_format == GAIA_BGR_ARRAY)
      {
	  img = image_from_bgr_array (raw, width, height);
	  if (!img)
	    {
		errmsg = "unable to get an image from BGR ARRAY";
		goto error;
	    }
      }
    if (raw_format == GAIA_BGRA_ARRAY)
      {
	  img = image_from_bgra_array (raw, width, height);
	  if (!img)
	    {
		errmsg = "unable to get an image from BGRA ARRAY";
		goto error;
	    }
      }

/* compressing as JPEG */
    if (is_image_grayscale (img) == RASTERLITE_TRUE)
	blob = image_to_jpeg_grayscale (img, &blob_size, quality);
    else
	blob = image_to_jpeg (img, &blob_size, quality);
    if (!blob)
      {
	  errmsg = "Jpeg encoder error";
	  goto error;
      }

/* exporting to file */
    out = fopen (path, "wb");
    if (out == NULL)
      {
	  errmsg = "Unable to create output image";
	  goto error;
      }
    if (fwrite (blob, 1, blob_size, out) != (size_t) blob_size)
	err = 1;
    fclose (out);
    if (err)
      {
	  unlink (path);
	  goto error;
      }
    free (blob);
    image_destroy (img);
    return RASTERLITE_OK;

  error:
    fprintf (stderr, "%s\n", errmsg);
    if (img)
	image_destroy (img);
    if (blob)
	free (blob);
    return RASTERLITE_ERROR;
}

RASTERLITE_DECLARE unsigned char *
rasterliteRawImageToJpegMemBuf (const void *raw, int raw_format, int width,
				int height, int *size, int quality)
{
/* exports a RAW image into a JPEG compressed memory buffer */
    rasterliteImagePtr img = NULL;
    void *blob = NULL;
    int blob_size;
    char *errmsg;

    if (raw == NULL)
      {
	  errmsg = "NULL RAW image";
	  goto error;
      }
    if (width <= 0 || height <= 0)
      {
	  errmsg = "invalid RAW image width/height";
	  goto error;
      }
    if (raw_format == GAIA_RGB_ARRAY || raw_format == GAIA_RGBA_ARRAY
	|| raw_format == GAIA_ARGB_ARRAY || raw_format == GAIA_BGR_ARRAY
	|| raw_format == GAIA_BGRA_ARRAY)
	;
    else
      {
	  errmsg = "invalid raster RAW format";
	  goto error;
      }
    if (raw_format == GAIA_RGB_ARRAY)
      {
	  img = image_from_rgb_array (raw, width, height);
	  if (!img)
	    {
		errmsg = "unable to get an image from RGB ARRAY";
		goto error;
	    }
      }
    if (raw_format == GAIA_RGBA_ARRAY)
      {
	  img = image_from_rgba_array (raw, width, height);
	  if (!img)
	    {
		errmsg = "unable to get an image from RGBA ARRAY";
		goto error;
	    }
      }
    if (raw_format == GAIA_ARGB_ARRAY)
      {
	  img = image_from_argb_array (raw, width, height);
	  if (!img)
	    {
		errmsg = "unable to get an image from ARGB ARRAY";
		goto error;
	    }
      }
    if (raw_format == GAIA_BGR_ARRAY)
      {
	  img = image_from_bgr_array (raw, width, height);
	  if (!img)
	    {
		errmsg = "unable to get an image from BGR ARRAY";
		goto error;
	    }
      }
    if (raw_format == GAIA_BGRA_ARRAY)
      {
	  img = image_from_bgra_array (raw, width, height);
	  if (!img)
	    {
		errmsg = "unable to get an image from BGRA ARRAY";
		goto error;
	    }
      }

/* compressing as JPEG */
    if (is_image_grayscale (img) == RASTERLITE_TRUE)
	blob = image_to_jpeg_grayscale (img, &blob_size, quality);
    else
	blob = image_to_jpeg (img, &blob_size, quality);
    if (!blob)
      {
	  errmsg = "Jpeg encoder error";
	  goto error;
      }

/* exporting the memory buffer */
    image_destroy (img);
    *size = blob_size;
    return blob;

  error:
    fprintf (stderr, "%s\n", errmsg);
    if (img)
	image_destroy (img);
    if (blob)
	free (blob);
    *size = 0;
    return NULL;
}

RASTERLITE_DECLARE int
rasterliteRawImageToPngFile (const void *raw, int raw_format, int width,
			     int height, const char *path)
{
/* exports a RAW image into a PNG compressed file */
    rasterliteImagePtr img = NULL;
    void *blob = NULL;
    int blob_size;
    char *errmsg;
    int err = 0;
    FILE *out;

    if (raw == NULL)
      {
	  errmsg = "NULL RAW image";
	  goto error;
      }
    if (width <= 0 || height <= 0)
      {
	  errmsg = "invalid RAW image width/height";
	  goto error;
      }
    if (raw_format == GAIA_RGB_ARRAY || raw_format == GAIA_RGBA_ARRAY
	|| raw_format == GAIA_ARGB_ARRAY || raw_format == GAIA_BGR_ARRAY
	|| raw_format == GAIA_BGRA_ARRAY)
	;
    else
      {
	  errmsg = "invalid raster RAW format";
	  goto error;
      }
    if (raw_format == GAIA_RGB_ARRAY)
      {
	  img = image_from_rgb_array (raw, width, height);
	  if (!img)
	    {
		errmsg = "unable to get an image from RGB ARRAY";
		goto error;
	    }
      }
    if (raw_format == GAIA_RGBA_ARRAY)
      {
	  img = image_from_rgba_array (raw, width, height);
	  if (!img)
	    {
		errmsg = "unable to get an image from RGBA ARRAY";
		goto error;
	    }
      }
    if (raw_format == GAIA_ARGB_ARRAY)
      {
	  img = image_from_argb_array (raw, width, height);
	  if (!img)
	    {
		errmsg = "unable to get an image from ARGB ARRAY";
		goto error;
	    }
      }
    if (raw_format == GAIA_BGR_ARRAY)
      {
	  img = image_from_bgr_array (raw, width, height);
	  if (!img)
	    {
		errmsg = "unable to get an image from BGR ARRAY";
		goto error;
	    }
      }
    if (raw_format == GAIA_BGRA_ARRAY)
      {
	  img = image_from_bgra_array (raw, width, height);
	  if (!img)
	    {
		errmsg = "unable to get an image from BGRA ARRAY";
		goto error;
	    }
      }

/* compressing as PNG */
    if (is_image_grayscale (img) == RASTERLITE_TRUE)
	blob = image_to_png_grayscale (img, &blob_size);
    else if (is_image_palette256 (img) == RASTERLITE_TRUE)
	blob = image_to_png_palette (img, &blob_size);
    else
	blob = image_to_png_rgb (img, &blob_size);
    if (!blob)
      {
	  errmsg = "Png encoder error";
	  goto error;
      }

/* exporting to file */
    out = fopen (path, "wb");
    if (out == NULL)
      {
	  errmsg = "Unable to create output image";
	  goto error;
      }
    if (fwrite (blob, 1, blob_size, out) != (size_t) blob_size)
	err = 1;
    fclose (out);
    if (err)
      {
	  unlink (path);
	  goto error;
      }
    free (blob);
    image_destroy (img);
    return RASTERLITE_OK;

  error:
    fprintf (stderr, "%s\n", errmsg);
    if (img)
	image_destroy (img);
    if (blob)
	free (blob);
    return RASTERLITE_ERROR;
}

RASTERLITE_DECLARE unsigned char *
rasterliteRawImageToPngMemBuf (const void *raw, int raw_format, int width,
			       int height, int *size)
{
/* exports a RAW image into a PNG compressed memory buffer */
    rasterliteImagePtr img = NULL;
    void *blob = NULL;
    int blob_size;
    char *errmsg;

    if (raw == NULL)
      {
	  errmsg = "NULL RAW image";
	  goto error;
      }
    if (width <= 0 || height <= 0)
      {
	  errmsg = "invalid RAW image width/height";
	  goto error;
      }
    if (raw_format == GAIA_RGB_ARRAY || raw_format == GAIA_RGBA_ARRAY
	|| raw_format == GAIA_ARGB_ARRAY || raw_format == GAIA_BGR_ARRAY
	|| raw_format == GAIA_BGRA_ARRAY)
	;
    else
      {
	  errmsg = "invalid raster RAW format";
	  goto error;
      }
    if (raw_format == GAIA_RGB_ARRAY)
      {
	  img = image_from_rgb_array (raw, width, height);
	  if (!img)
	    {
		errmsg = "unable to get an image from RGB ARRAY";
		goto error;
	    }
      }
    if (raw_format == GAIA_RGBA_ARRAY)
      {
	  img = image_from_rgba_array (raw, width, height);
	  if (!img)
	    {
		errmsg = "unable to get an image from RGBA ARRAY";
		goto error;
	    }
      }
    if (raw_format == GAIA_ARGB_ARRAY)
      {
	  img = image_from_argb_array (raw, width, height);
	  if (!img)
	    {
		errmsg = "unable to get an image from ARGB ARRAY";
		goto error;
	    }
      }
    if (raw_format == GAIA_BGR_ARRAY)
      {
	  img = image_from_bgr_array (raw, width, height);
	  if (!img)
	    {
		errmsg = "unable to get an image from BGR ARRAY";
		goto error;
	    }
      }
    if (raw_format == GAIA_BGRA_ARRAY)
      {
	  img = image_from_bgra_array (raw, width, height);
	  if (!img)
	    {
		errmsg = "unable to get an image from BGRA ARRAY";
		goto error;
	    }
      }

/* compressing as PNG */
    if (is_image_grayscale (img) == RASTERLITE_TRUE)
	blob = image_to_png_grayscale (img, &blob_size);
    else if (is_image_palette256 (img) == RASTERLITE_TRUE)
	blob = image_to_png_palette (img, &blob_size);
    else
	blob = image_to_png_rgb (img, &blob_size);
    if (!blob)
      {
	  errmsg = "Png encoder error";
	  goto error;
      }

/* exporting the memory buffer */
    image_destroy (img);
    *size = blob_size;
    return blob;

  error:
    fprintf (stderr, "%s\n", errmsg);
    if (img)
	image_destroy (img);
    if (blob)
	free (blob);
    *size = 0;
    return NULL;
}

RASTERLITE_DECLARE int
rasterliteRawImageToGifFile (const void *raw, int raw_format, int width,
			     int height, const char *path)
{
/* exports a RAW image into a GIF compressed file */
    rasterliteImagePtr img = NULL;
    void *blob = NULL;
    int blob_size;
    char *errmsg;
    int err = 0;
    FILE *out;

    if (raw == NULL)
      {
	  errmsg = "NULL RAW image";
	  goto error;
      }
    if (width <= 0 || height <= 0)
      {
	  errmsg = "invalid RAW image width/height";
	  goto error;
      }
    if (raw_format == GAIA_RGB_ARRAY || raw_format == GAIA_RGBA_ARRAY
	|| raw_format == GAIA_ARGB_ARRAY || raw_format == GAIA_BGR_ARRAY
	|| raw_format == GAIA_BGRA_ARRAY)
	;
    else
      {
	  errmsg = "invalid raster RAW format";
	  goto error;
      }
    if (raw_format == GAIA_RGB_ARRAY)
      {
	  img = image_from_rgb_array (raw, width, height);
	  if (!img)
	    {
		errmsg = "unable to get an image from RGB ARRAY";
		goto error;
	    }
      }
    if (raw_format == GAIA_RGBA_ARRAY)
      {
	  img = image_from_rgba_array (raw, width, height);
	  if (!img)
	    {
		errmsg = "unable to get an image from RGBA ARRAY";
		goto error;
	    }
      }
    if (raw_format == GAIA_ARGB_ARRAY)
      {
	  img = image_from_argb_array (raw, width, height);
	  if (!img)
	    {
		errmsg = "unable to get an image from ARGB ARRAY";
		goto error;
	    }
      }
    if (raw_format == GAIA_BGR_ARRAY)
      {
	  img = image_from_bgr_array (raw, width, height);
	  if (!img)
	    {
		errmsg = "unable to get an image from BGR ARRAY";
		goto error;
	    }
      }
    if (raw_format == GAIA_BGRA_ARRAY)
      {
	  img = image_from_bgra_array (raw, width, height);
	  if (!img)
	    {
		errmsg = "unable to get an image from BGRA ARRAY";
		goto error;
	    }
      }

/* compressing as GIF */
    if (is_image_palette256 (img) == RASTERLITE_FALSE)
	image_resample_as_palette256 (img);

    blob = image_to_gif (img, &blob_size);
    if (!blob)
      {
	  errmsg = "Gif encoder error";
	  goto error;
      }

/* exporting to file */
    out = fopen (path, "wb");
    if (out == NULL)
      {
	  errmsg = "Unable to create output image";
	  goto error;
      }
    if (fwrite (blob, 1, blob_size, out) != (size_t) blob_size)
	err = 1;
    fclose (out);
    if (err)
      {
	  unlink (path);
	  goto error;
      }
    free (blob);
    image_destroy (img);
    return RASTERLITE_OK;

  error:
    fprintf (stderr, "%s\n", errmsg);
    if (img)
	image_destroy (img);
    if (blob)
	free (blob);
    return RASTERLITE_ERROR;
}

RASTERLITE_DECLARE unsigned char *
rasterliteRawImageToGifMemBuf (const void *raw, int raw_format, int width,
			       int height, int *size)
{
/* exports a RAW image into a GIF compressed memory buffer */
    rasterliteImagePtr img = NULL;
    void *blob = NULL;
    int blob_size;
    char *errmsg;

    if (raw == NULL)
      {
	  errmsg = "NULL RAW image";
	  goto error;
      }
    if (width <= 0 || height <= 0)
      {
	  errmsg = "invalid RAW image width/height";
	  goto error;
      }
    if (raw_format == GAIA_RGB_ARRAY || raw_format == GAIA_RGBA_ARRAY
	|| raw_format == GAIA_ARGB_ARRAY || raw_format == GAIA_BGR_ARRAY
	|| raw_format == GAIA_BGRA_ARRAY)
	;
    else
      {
	  errmsg = "invalid raster RAW format";
	  goto error;
      }
    if (raw_format == GAIA_RGB_ARRAY)
      {
	  img = image_from_rgb_array (raw, width, height);
	  if (!img)
	    {
		errmsg = "unable to get an image from RGB ARRAY";
		goto error;
	    }
      }
    if (raw_format == GAIA_RGBA_ARRAY)
      {
	  img = image_from_rgba_array (raw, width, height);
	  if (!img)
	    {
		errmsg = "unable to get an image from RGBA ARRAY";
		goto error;
	    }
      }
    if (raw_format == GAIA_ARGB_ARRAY)
      {
	  img = image_from_argb_array (raw, width, height);
	  if (!img)
	    {
		errmsg = "unable to get an image from ARGB ARRAY";
		goto error;
	    }
      }
    if (raw_format == GAIA_BGR_ARRAY)
      {
	  img = image_from_bgr_array (raw, width, height);
	  if (!img)
	    {
		errmsg = "unable to get an image from BGR ARRAY";
		goto error;
	    }
      }
    if (raw_format == GAIA_BGRA_ARRAY)
      {
	  img = image_from_bgra_array (raw, width, height);
	  if (!img)
	    {
		errmsg = "unable to get an image from BGRA ARRAY";
		goto error;
	    }
      }

/* compressing as GIF */
    if (is_image_palette256 (img) == RASTERLITE_FALSE)
	image_resample_as_palette256 (img);

    blob = image_to_gif (img, &blob_size);
    if (!blob)
      {
	  errmsg = "Gif encoder error";
	  goto error;
      }

/* exporting the memory buffer */
    image_destroy (img);
    *size = blob_size;
    return blob;

  error:
    fprintf (stderr, "%s\n", errmsg);
    if (img)
	image_destroy (img);
    if (blob)
	free (blob);
    *size = 0;
    return NULL;
}

RASTERLITE_DECLARE int
rasterliteRawImageToGeoTiffFile (const void *raw, int raw_format, int width,
				 int height, const char *path, double x_size,
				 double y_size, double xllcorner,
				 double yllcorner, const char *proj4text)
{
/* exports a RAW image into a TIFF encoded file */
    rasterliteImagePtr img = NULL;
    void *blob = NULL;
    int blob_size;
    char *errmsg;

    if (raw == NULL)
      {
	  errmsg = "NULL RAW image";
	  goto error;
      }
    if (width <= 0 || height <= 0)
      {
	  errmsg = "invalid RAW image width/height";
	  goto error;
      }
    if (raw_format == GAIA_RGB_ARRAY || raw_format == GAIA_RGBA_ARRAY
	|| raw_format == GAIA_ARGB_ARRAY || raw_format == GAIA_BGR_ARRAY
	|| raw_format == GAIA_BGRA_ARRAY)
	;
    else
      {
	  errmsg = "invalid raster RAW format";
	  goto error;
      }
    if (raw_format == GAIA_RGB_ARRAY)
      {
	  img = image_from_rgb_array (raw, width, height);
	  if (!img)
	    {
		errmsg = "unable to get an image from RGB ARRAY";
		goto error;
	    }
      }
    if (raw_format == GAIA_RGBA_ARRAY)
      {
	  img = image_from_rgba_array (raw, width, height);
	  if (!img)
	    {
		errmsg = "unable to get an image from RGBA ARRAY";
		goto error;
	    }
      }
    if (raw_format == GAIA_ARGB_ARRAY)
      {
	  img = image_from_argb_array (raw, width, height);
	  if (!img)
	    {
		errmsg = "unable to get an image from ARGB ARRAY";
		goto error;
	    }
      }
    if (raw_format == GAIA_BGR_ARRAY)
      {
	  img = image_from_bgr_array (raw, width, height);
	  if (!img)
	    {
		errmsg = "unable to get an image from BGR ARRAY";
		goto error;
	    }
      }
    if (raw_format == GAIA_BGRA_ARRAY)
      {
	  img = image_from_bgra_array (raw, width, height);
	  if (!img)
	    {
		errmsg = "unable to get an image from BGRA ARRAY";
		goto error;
	    }
      }

/* encoding as TIFF */
    if (is_image_monochrome (img) == RASTERLITE_TRUE)
	blob = image_to_tiff_fax4 (img, &blob_size);
    else if (is_image_grayscale (img) == RASTERLITE_TRUE)
	blob = image_to_tiff_grayscale (img, &blob_size);
    else if (is_image_palette256 (img) == RASTERLITE_TRUE)
	blob = image_to_tiff_palette (img, &blob_size);
    else
	blob = image_to_tiff_rgb (img, &blob_size);
    if (!blob)
      {
	  errmsg = "Tiff encoder error";
	  goto error;
      }

/* exporting to file as GeoTiff */
    if (!write_geotiff
	(path, blob, blob_size, x_size, y_size, xllcorner, yllcorner,
	 proj4text))
      {
	  errmsg = "Unable to create output image";
	  goto error;
      }
    free (blob);
    image_destroy (img);
    return RASTERLITE_OK;

  error:
    fprintf (stderr, "%s\n", errmsg);
    if (img)
	image_destroy (img);
    if (blob)
	free (blob);
    return RASTERLITE_ERROR;
}

RASTERLITE_DECLARE unsigned char *
rasterliteRawImageToTiffMemBuf (const void *raw, int raw_format, int width,
				int height, int *size)
{
/* exports a RAW image into a TIFF encoded memory buffer */
    rasterliteImagePtr img = NULL;
    void *blob = NULL;
    int blob_size;
    char *errmsg;

    if (raw == NULL)
      {
	  errmsg = "NULL RAW image";
	  goto error;
      }
    if (width <= 0 || height <= 0)
      {
	  errmsg = "invalid RAW image width/height";
	  goto error;
      }
    if (raw_format == GAIA_RGB_ARRAY || raw_format == GAIA_RGBA_ARRAY
	|| raw_format == GAIA_ARGB_ARRAY || raw_format == GAIA_BGR_ARRAY
	|| raw_format == GAIA_BGRA_ARRAY)
	;
    else
      {
	  errmsg = "invalid raster RAW format";
	  goto error;
      }
    if (raw_format == GAIA_RGB_ARRAY)
      {
	  img = image_from_rgb_array (raw, width, height);
	  if (!img)
	    {
		errmsg = "unable to get an image from RGB ARRAY";
		goto error;
	    }
      }
    if (raw_format == GAIA_RGBA_ARRAY)
      {
	  img = image_from_rgba_array (raw, width, height);
	  if (!img)
	    {
		errmsg = "unable to get an image from RGBA ARRAY";
		goto error;
	    }
      }
    if (raw_format == GAIA_ARGB_ARRAY)
      {
	  img = image_from_argb_array (raw, width, height);
	  if (!img)
	    {
		errmsg = "unable to get an image from ARGB ARRAY";
		goto error;
	    }
      }
    if (raw_format == GAIA_BGR_ARRAY)
      {
	  img = image_from_bgr_array (raw, width, height);
	  if (!img)
	    {
		errmsg = "unable to get an image from BGR ARRAY";
		goto error;
	    }
      }
    if (raw_format == GAIA_BGRA_ARRAY)
      {
	  img = image_from_bgra_array (raw, width, height);
	  if (!img)
	    {
		errmsg = "unable to get an image from BGRA ARRAY";
		goto error;
	    }
      }

/* encoding as TIFF */
    if (is_image_monochrome (img) == RASTERLITE_TRUE)
	blob = image_to_tiff_fax4 (img, &blob_size);
    else if (is_image_grayscale (img) == RASTERLITE_TRUE)
	blob = image_to_tiff_grayscale (img, &blob_size);
    else if (is_image_palette256 (img) == RASTERLITE_TRUE)
	blob = image_to_tiff_palette (img, &blob_size);
    else
	blob = image_to_tiff_rgb (img, &blob_size);
    if (!blob)
      {
	  errmsg = "Tiff encoder error";
	  goto error;
      }

/* exporting the memory buffer */
    image_destroy (img);
    *size = blob_size;
    return blob;

  error:
    fprintf (stderr, "%s\n", errmsg);
    if (img)
	image_destroy (img);
    if (blob)
	free (blob);
    *size = 0;
    return NULL;
}
