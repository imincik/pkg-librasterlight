/* 
/ rasterlite_png.c
/
/ PNG auxiliary helpers
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

#include <stdio.h>
#include <math.h>
#include <string.h>
#include <stdlib.h>

#include <tiffio.h>
#include <png.h>

#ifdef SPATIALITE_AMALGAMATION
#include <spatialite/sqlite3.h>
#else
#include <sqlite3.h>
#endif

#include <spatialite/gaiageo.h>

#define TRUE 1
#define FALSE 0

#include "rasterlite_internals.h"

/* 
/
/ DISCLAIMER:
/ all the following code merely is an 'ad hoc' adaption
/ deriving from the original GD lib code
/
*/

#ifndef PNG_SETJMP_NOT_SUPPORTED
typedef struct _jmpbuf_wrapper
{
    jmp_buf jmpbuf;
}
jmpbuf_wrapper;

static jmpbuf_wrapper xgdPngJmpbufStruct;

static void
xgdPngErrorHandler (png_structp png_ptr, png_const_charp msg)
{
    jmpbuf_wrapper *jmpbuf_ptr;
    fprintf (stderr, "png-wrapper:  fatal libpng error: %s\n", msg);
    fflush (stderr);
    jmpbuf_ptr = png_get_error_ptr (png_ptr);
    if (jmpbuf_ptr == NULL)
      {
	  fprintf (stderr,
		   "png-wrapper:  EXTREMELY fatal error: jmpbuf unrecoverable; terminating.\n");
	  fflush (stderr);
	  exit (99);
      }
    longjmp (jmpbuf_ptr->jmpbuf, 1);
}
#endif

static void
xgdPngReadData (png_structp png_ptr, png_bytep data, png_size_t length)
{
    int check;
    check = xgdGetBuf (data, length, (xgdIOCtx *) png_get_io_ptr (png_ptr));
    if (check != (int) length)
      {
	  png_error (png_ptr, "Read Error: truncated data");
      }
}

static void
xgdPngWriteData (png_structp png_ptr, png_bytep data, png_size_t length)
{
    xgdPutBuf (data, length, (xgdIOCtx *) png_get_io_ptr (png_ptr));
}

static void
xgdPngFlushData (png_structp png_ptr)
{
    if (png_ptr)
	return;			/* does absolutely nothing - required in order to suppress warnings */
}

static rasterliteImagePtr
xgdImageCreateFromPngCtx (xgdIOCtx * infile)
{
    png_byte sig[8];
    png_structp png_ptr;
    png_infop info_ptr;
    png_uint_32 width, height, rowbytes, w, h;
    int bit_depth, color_type, interlace_type;
    int num_palette;
    png_colorp palette;
    int red[256];
    int green[256];
    int blue[256];
    png_bytep image_data = NULL;
    png_bytepp row_pointers = NULL;
    rasterliteImagePtr im = NULL;
    int i, j;
    volatile int palette_allocated = FALSE;
    memset (sig, 0, sizeof (sig));
    if (xgdGetBuf (sig, 8, infile) < 8)
      {
	  return NULL;
      }
    if (png_sig_cmp (sig, 0, 8))
      {
	  return NULL;
      }
#ifndef PNG_SETJMP_NOT_SUPPORTED
    png_ptr =
	png_create_read_struct (PNG_LIBPNG_VER_STRING, &xgdPngJmpbufStruct,
				xgdPngErrorHandler, NULL);
#else
    png_ptr = png_create_read_struct (PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
#endif
    if (png_ptr == NULL)
      {
	  fprintf (stderr,
		   "png-wrapper error: cannot allocate libpng main struct\n");
	  return NULL;
      }
    info_ptr = png_create_info_struct (png_ptr);
    if (info_ptr == NULL)
      {
	  fprintf (stderr,
		   "png-wrapper error: cannot allocate libpng info struct\n");
	  png_destroy_read_struct (&png_ptr, NULL, NULL);
	  return NULL;
      }
#ifndef PNG_SETJMP_NOT_SUPPORTED
    if (setjmp (xgdPngJmpbufStruct.jmpbuf))
      {
	  fprintf (stderr,
		   "png-wrapper error: setjmp returns error condition 1\n");
	  png_destroy_read_struct (&png_ptr, &info_ptr, NULL);
	  return NULL;
      }
#endif
    png_set_sig_bytes (png_ptr, 8);
    png_set_read_fn (png_ptr, (void *) infile, xgdPngReadData);
    png_read_info (png_ptr, info_ptr);
    png_get_IHDR (png_ptr, info_ptr, &width, &height, &bit_depth, &color_type,
		  &interlace_type, NULL, NULL);
    im = image_create ((int) width, (int) height);
    if (im == NULL)
      {
	  fprintf (stderr,
		   "png-wrapper error: cannot allocate gdImage struct\n");
	  png_destroy_read_struct (&png_ptr, &info_ptr, NULL);
	  free (image_data);
	  free (row_pointers);
	  return NULL;
      }
    if (bit_depth == 16)
      {
	  png_set_strip_16 (png_ptr);
      }
    else if (bit_depth < 8)
      {
	  png_set_packing (png_ptr);
      }
#ifndef PNG_SETJMP_NOT_SUPPORTED
    if (setjmp (xgdPngJmpbufStruct.jmpbuf))
      {
	  fprintf (stderr,
		   "png-wrapper error: setjmp returns error condition 2");
	  png_destroy_read_struct (&png_ptr, &info_ptr, NULL);
	  free (image_data);
	  free (row_pointers);
	  if (im)
	    {
		image_destroy (im);
	    }
	  return NULL;
      }
#endif
    switch (color_type)
      {
      case PNG_COLOR_TYPE_PALETTE:
	  im->color_space = COLORSPACE_PALETTE;
	  png_get_PLTE (png_ptr, info_ptr, &palette, &num_palette);
	  for (i = 0; i < num_palette; i++)
	    {
		red[i] = palette[i].red;
		green[i] = palette[i].green;
		blue[i] = palette[i].blue;
	    }
	  break;
      case PNG_COLOR_TYPE_GRAY:
      case PNG_COLOR_TYPE_GRAY_ALPHA:
	  im->color_space = COLORSPACE_GRAYSCALE;
	  if ((palette = malloc (256 * sizeof (png_color))) == NULL)
	    {
		fprintf (stderr,
			 "png-wrapper error: cannot allocate gray palette\n");
		png_destroy_read_struct (&png_ptr, &info_ptr, NULL);
		return NULL;
	    }
	  palette_allocated = TRUE;
	  if (bit_depth < 8)
	    {
		num_palette = 1 << bit_depth;
		for (i = 0; i < 256; ++i)
		  {
		      j = (255 * i) / (num_palette - 1);
		      palette[i].red = palette[i].green = palette[i].blue = j;
		  }
	    }
	  else
	    {
		num_palette = 256;
		for (i = 0; i < 256; ++i)
		  {
		      palette[i].red = palette[i].green = palette[i].blue = i;
		  }
	    }
	  break;

      case PNG_COLOR_TYPE_RGB:
      case PNG_COLOR_TYPE_RGB_ALPHA:
	  im->color_space = COLORSPACE_RGB;
	  break;
      }
    png_read_update_info (png_ptr, info_ptr);
    rowbytes = png_get_rowbytes (png_ptr, info_ptr);
    if (overflow2 (rowbytes, height))
      {
	  png_destroy_read_struct (&png_ptr, &info_ptr, NULL);
	  return NULL;
      }
    image_data = malloc (rowbytes * height);
    if (!image_data)
      {
	  fprintf (stderr, "png-wrapper error: cannot allocate image data\n");
	  png_destroy_read_struct (&png_ptr, &info_ptr, NULL);
	  if (im)
	    {
		image_destroy (im);
	    }
	  return NULL;
      }
    if (overflow2 (height, sizeof (png_bytep)))
      {
	  png_destroy_read_struct (&png_ptr, &info_ptr, NULL);
	  free (image_data);
	  if (im)
	    {
		image_destroy (im);
	    }
	  return NULL;
      }
    row_pointers = malloc (height * sizeof (png_bytep));
    if (!row_pointers)
      {
	  fprintf (stderr, "png-wrapper error: cannot allocate row pointers\n");
	  png_destroy_read_struct (&png_ptr, &info_ptr, NULL);
	  if (im)
	    {
		image_destroy (im);
	    }
	  free (image_data);
	  return NULL;
      }
    for (h = 0; h < height; ++h)
      {
	  row_pointers[h] = image_data + h * rowbytes;
      }
    png_read_image (png_ptr, row_pointers);
    png_read_end (png_ptr, NULL);
    png_destroy_read_struct (&png_ptr, &info_ptr, NULL);
    switch (color_type)
      {
      case PNG_COLOR_TYPE_RGB:
	  for (h = 0; h < height; h++)
	    {
		int boffset = 0;
		for (w = 0; w < width; w++)
		  {
		      register png_byte r = row_pointers[h][boffset++];
		      register png_byte g = row_pointers[h][boffset++];
		      register png_byte b = row_pointers[h][boffset++];
		      im->pixels[h][w] = true_color (r, g, b);
		  }
	    }
	  break;
      case PNG_COLOR_TYPE_RGB_ALPHA:
	  for (h = 0; h < height; h++)
	    {
		int boffset = 0;
		for (w = 0; w < width; w++)
		  {
		      register png_byte r = row_pointers[h][boffset++];
		      register png_byte g = row_pointers[h][boffset++];
		      register png_byte b = row_pointers[h][boffset++];
		      boffset++;
		      im->pixels[h][w] = true_color (r, g, b);
		  }
	    }
	  break;
      case PNG_COLOR_TYPE_GRAY:
      case PNG_COLOR_TYPE_GRAY_ALPHA:
	  for (h = 0; h < height; ++h)
	    {
		for (w = 0; w < width; ++w)
		  {
		      register png_byte idx = row_pointers[h][w];
		      im->pixels[h][w] = true_color (idx, idx, idx);
		  }
	    }
	  break;
      default:
	  for (h = 0; h < height; ++h)
	    {
		for (w = 0; w < width; ++w)
		  {
		      register png_byte idx = row_pointers[h][w];
		      im->pixels[h][w] =
			  true_color (red[idx], green[idx], blue[idx]);
		  }
	    }
      }
    if (palette_allocated)
      {
	  free (palette);
      }
    free (image_data);
    free (row_pointers);
    return im;
}

static void
xgdImagePngCtxPalette (rasterliteImagePtr img, xgdIOCtx * outfile, int level)
{
    int i, j, bit_depth = 0, interlace_type;
    int width = img->sx;
    int height = img->sy;
    int mapping[256];
    int colors;
    png_color palette[256];
    png_structp png_ptr;
    png_infop info_ptr;
    png_bytep *row_pointers;
    int **ptpixels = img->pixels;
    int *pThisRow;
    int thisPixel;
#ifndef PNG_SETJMP_NOT_SUPPORTED
    png_ptr = png_create_write_struct (PNG_LIBPNG_VER_STRING,
				       &xgdPngJmpbufStruct, xgdPngErrorHandler,
				       NULL);
#else
    png_ptr = png_create_write_struct (PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
#endif
    if (png_ptr == NULL)
      {
	  fprintf (stderr,
		   "png-wrapper error: cannot allocate libpng main struct\n");
	  return;
      }
    info_ptr = png_create_info_struct (png_ptr);
    if (info_ptr == NULL)
      {
	  fprintf (stderr,
		   "png-wrapper error: cannot allocate libpng info struct\n");
	  png_destroy_write_struct (&png_ptr, (png_infopp) NULL);
	  return;
      }
#ifndef PNG_SETJMP_NOT_SUPPORTED
    if (setjmp (xgdPngJmpbufStruct.jmpbuf))
      {
	  fprintf (stderr,
		   "png-wrapper error: setjmp returns error condition\n");
	  png_destroy_write_struct (&png_ptr, &info_ptr);
	  return;
      }
#endif
    png_set_write_fn (png_ptr, (void *) outfile, xgdPngWriteData,
		      xgdPngFlushData);
    png_set_compression_level (png_ptr, level);
    for (i = 0; i < 256; ++i)
	mapping[i] = -1;
    for (j = 0; j < height; ++j)
      {
	  pThisRow = *ptpixels++;
	  for (i = 0; i < width; ++i)
	    {
		int index;
		thisPixel = *pThisRow;
		index = palette_set (mapping, thisPixel);
		*pThisRow++ = index;
	    }
      }
    colors = 0;
    for (i = 0; i < 256; ++i)
      {
	  if (mapping[i] == -1)
	      break;
	  colors++;
      }
    if (colors <= 2)
	bit_depth = 1;
    else if (colors <= 4)
	bit_depth = 2;
    else if (colors <= 16)
	bit_depth = 4;
    else
	bit_depth = 8;
    interlace_type = PNG_INTERLACE_NONE;
    png_set_IHDR (png_ptr, info_ptr, width, height, bit_depth,
		  PNG_COLOR_TYPE_PALETTE, interlace_type,
		  PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);
    for (i = 0; i < colors; i++)
      {
	  palette[i].red = true_color_get_red (mapping[i]);
	  palette[i].green = true_color_get_green (mapping[i]);
	  palette[i].blue = true_color_get_blue (mapping[i]);
      }
    png_set_PLTE (png_ptr, info_ptr, palette, colors);
    png_write_info (png_ptr, info_ptr);
    png_set_packing (png_ptr);
    if (overflow2 (sizeof (png_bytep), height))
	return;
    row_pointers = malloc (sizeof (png_bytep) * height);
    if (row_pointers == NULL)
      {
	  fprintf (stderr,
		   "png-wrapper error: unable to allocate row_pointers\n");
	  return;
      }
    ptpixels = img->pixels;
    for (j = 0; j < height; ++j)
      {
	  if ((row_pointers[j] = malloc (width)) == NULL)
	    {
		fprintf (stderr,
			 "png-wrapper error: unable to allocate rows\n");
		for (i = 0; i < j; ++i)
		    free (row_pointers[i]);
		free (row_pointers);
		return;
	    }
	  pThisRow = *ptpixels++;
	  for (i = 0; i < width; ++i)
	      row_pointers[j][i] = *pThisRow++;
      }
    png_write_image (png_ptr, row_pointers);
    png_write_end (png_ptr, info_ptr);
    for (j = 0; j < height; ++j)
	free (row_pointers[j]);
    free (row_pointers);
    png_destroy_write_struct (&png_ptr, &info_ptr);
}

static void
xgdImagePngCtxGrayscale (rasterliteImagePtr img, xgdIOCtx * outfile, int level)
{
    int i, j, bit_depth = 0, interlace_type;
    int width = img->sx;
    int height = img->sy;
    png_structp png_ptr;
    png_infop info_ptr;
    png_bytep *row_pointers;
    int **ptpixels = img->pixels;
    int *pThisRow;
#ifndef PNG_SETJMP_NOT_SUPPORTED
    png_ptr = png_create_write_struct (PNG_LIBPNG_VER_STRING,
				       &xgdPngJmpbufStruct, xgdPngErrorHandler,
				       NULL);
#else
    png_ptr = png_create_write_struct (PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
#endif
    if (png_ptr == NULL)
      {
	  fprintf (stderr,
		   "png-wrapper error: cannot allocate libpng main struct\n");
	  return;
      }
    info_ptr = png_create_info_struct (png_ptr);
    if (info_ptr == NULL)
      {
	  fprintf (stderr,
		   "png-wrapper error: cannot allocate libpng info struct\n");
	  png_destroy_write_struct (&png_ptr, (png_infopp) NULL);
	  return;
      }
#ifndef PNG_SETJMP_NOT_SUPPORTED
    if (setjmp (xgdPngJmpbufStruct.jmpbuf))
      {
	  fprintf (stderr,
		   "png-wrapper error: setjmp returns error condition\n");
	  png_destroy_write_struct (&png_ptr, &info_ptr);
	  return;
      }
#endif
    png_set_write_fn (png_ptr, (void *) outfile, xgdPngWriteData,
		      xgdPngFlushData);
    png_set_compression_level (png_ptr, level);
    bit_depth = 8;
    interlace_type = PNG_INTERLACE_NONE;
    png_set_IHDR (png_ptr, info_ptr, width, height, bit_depth,
		  PNG_COLOR_TYPE_GRAY, interlace_type,
		  PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);
    png_write_info (png_ptr, info_ptr);
    png_set_packing (png_ptr);
    if (overflow2 (sizeof (png_bytep), height))
	return;
    row_pointers = malloc (sizeof (png_bytep) * height);
    if (row_pointers == NULL)
      {
	  fprintf (stderr,
		   "png-wrapper error: unable to allocate row_pointers\n");
	  return;
      }
    ptpixels = img->pixels;
    for (j = 0; j < height; ++j)
      {
	  if ((row_pointers[j] = malloc (width)) == NULL)
	    {
		fprintf (stderr,
			 "png-wrapper error: unable to allocate rows\n");
		for (i = 0; i < j; ++i)
		    free (row_pointers[i]);
		free (row_pointers);
		return;
	    }
	  pThisRow = *ptpixels++;
	  for (i = 0; i < width; ++i)
	      row_pointers[j][i] = *pThisRow++;
      }
    png_write_image (png_ptr, row_pointers);
    png_write_end (png_ptr, info_ptr);
    for (j = 0; j < height; ++j)
	free (row_pointers[j]);
    free (row_pointers);
    png_destroy_write_struct (&png_ptr, &info_ptr);
}

static void
xgdImagePngCtxRgb (rasterliteImagePtr img, xgdIOCtx * outfile, int level)
{
    int i, j, bit_depth = 0, interlace_type;
    int width = img->sx;
    int height = img->sy;
    png_structp png_ptr;
    png_infop info_ptr;
    png_bytep *row_pointers;
    png_bytep p_scanline;
    int **ptpixels = img->pixels;
    int *pThisRow;
    int color;
#ifndef PNG_SETJMP_NOT_SUPPORTED
    png_ptr = png_create_write_struct (PNG_LIBPNG_VER_STRING,
				       &xgdPngJmpbufStruct, xgdPngErrorHandler,
				       NULL);
#else
    png_ptr = png_create_write_struct (PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
#endif
    if (png_ptr == NULL)
      {
	  fprintf (stderr,
		   "png-wrapper error: cannot allocate libpng main struct\n");
	  return;
      }
    info_ptr = png_create_info_struct (png_ptr);
    if (info_ptr == NULL)
      {
	  fprintf (stderr,
		   "png-wrapper error: cannot allocate libpng info struct\n");
	  png_destroy_write_struct (&png_ptr, (png_infopp) NULL);
	  return;
      }
#ifndef PNG_SETJMP_NOT_SUPPORTED
    if (setjmp (xgdPngJmpbufStruct.jmpbuf))
      {
	  fprintf (stderr,
		   "png-wrapper error: setjmp returns error condition\n");
	  png_destroy_write_struct (&png_ptr, &info_ptr);
	  return;
      }
#endif
    png_set_write_fn (png_ptr, (void *) outfile, xgdPngWriteData,
		      xgdPngFlushData);
    png_set_compression_level (png_ptr, level);
    bit_depth = 8;
    interlace_type = PNG_INTERLACE_NONE;
    png_set_IHDR (png_ptr, info_ptr, width, height, bit_depth,
		  PNG_COLOR_TYPE_RGB, interlace_type,
		  PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);
    png_write_info (png_ptr, info_ptr);
    png_set_packing (png_ptr);
    if (overflow2 (sizeof (png_bytep), height))
	return;
    row_pointers = malloc (sizeof (png_bytep) * height);
    if (row_pointers == NULL)
      {
	  fprintf (stderr,
		   "png-wrapper error: unable to allocate row_pointers\n");
	  return;
      }
    ptpixels = img->pixels;
    for (j = 0; j < height; ++j)
      {
	  if ((row_pointers[j] = malloc (width * 3)) == NULL)
	    {
		fprintf (stderr,
			 "png-wrapper error: unable to allocate rows\n");
		for (i = 0; i < j; ++i)
		    free (row_pointers[i]);
		free (row_pointers);
		return;
	    }
	  pThisRow = *ptpixels++;
	  p_scanline = row_pointers[j];
	  for (i = 0; i < width; ++i)
	    {
		color = *pThisRow++;
		*p_scanline++ = true_color_get_red (color);
		*p_scanline++ = true_color_get_green (color);
		*p_scanline++ = true_color_get_blue (color);
	    }
      }
    png_write_image (png_ptr, row_pointers);
    png_write_end (png_ptr, info_ptr);
    for (j = 0; j < height; ++j)
	free (row_pointers[j]);
    free (row_pointers);
    png_destroy_write_struct (&png_ptr, &info_ptr);
}

extern void *
image_to_png_palette (const rasterliteImagePtr img, int *size)
{
/* compressing an image as PNG PALETTE */
    void *rv;
    xgdIOCtx *out = xgdNewDynamicCtx (2048, NULL);
    xgdImagePngCtxPalette (img, out, -1);
    rv = xgdDPExtractData (out, size);
    out->xgd_free (out);
    return rv;
}

extern void *
image_to_png_grayscale (const rasterliteImagePtr img, int *size)
{
/* compressing an image as PNG GRAYSCALE */
    void *rv;
    xgdIOCtx *out = xgdNewDynamicCtx (2048, NULL);
    xgdImagePngCtxGrayscale (img, out, -1);
    rv = xgdDPExtractData (out, size);
    out->xgd_free (out);
    return rv;
}

extern void *
image_to_png_rgb (const rasterliteImagePtr img, int *size)
{
/* compressing an image as PNG RGB */
    void *rv;
    xgdIOCtx *out = xgdNewDynamicCtx (2048, NULL);
    xgdImagePngCtxRgb (img, out, -1);
    rv = xgdDPExtractData (out, size);
    out->xgd_free (out);
    return rv;
}

extern rasterliteImagePtr
image_from_png (int size, const void *data)
{
/* uncompressing a PNG */
    rasterliteImagePtr img;
    xgdIOCtx *in = xgdNewDynamicCtxEx (size, data, 0);
    img = xgdImageCreateFromPngCtx (in);
    in->xgd_free (in);
    return img;
}
