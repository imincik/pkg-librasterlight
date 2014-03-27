/* 
/ rasterlite_image.c
/
/ image methods
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <float.h>
#include <limits.h>

#include <tiffio.h>

#ifdef SPATIALITE_AMALGAMATION
#include <spatialite/sqlite3.h>
#else
#include <sqlite3.h>
#endif

#include <spatialite/gaiageo.h>

#include "rasterlite_internals.h"

extern rasterliteImagePtr
image_create (int sx, int sy)
{
/*
/ creating a generic RGB image
/ vaguely inspired by GD lib
*/
    int i;
    int i2;
    rasterliteImagePtr img;
    img = malloc (sizeof (rasterliteImage));
    if (!img)
	return NULL;
    img->pixels = NULL;
    img->sx = sx;
    img->sy = sy;
    img->color_space = COLORSPACE_RGB;
    img->pixels = malloc (sizeof (int *) * sy);
    if (!img->pixels)
      {
	  free (img);
	  return NULL;
      }
    for (i = 0; i < sy; i++)
      {
	  img->pixels[i] = (int *) malloc (sizeof (int) * sx);
	  if (!img->pixels[i])
	    {
		for (i2 = 0; i2 < i; i2++)
		    free (img->pixels[i2]);
		free (img->pixels);
		free (img);
		return NULL;
	    }
      }
    return img;
}

extern void
image_destroy (rasterliteImagePtr img)
{
/*
/ destroying a generic RGB image
/ vaguely inspired by GD lib
*/
    int i;
    if (img->pixels)
      {
	  for (i = 0; (i < img->sy); i++)
	      free (img->pixels[i]);
	  free (img->pixels);
      }
    free (img);
}

extern void
image_fill (const rasterliteImagePtr img, int color)
{
/* filling the image with given color */
    int x;
    int y;
    if (img->pixels)
      {
	  for (y = 0; y < img->sy; y++)
	    {
		for (x = 0; x < img->sx; x++)
		    img->pixels[y][x] = color;
	    }
      }
}

static void
shrink_by (const rasterliteImagePtr dst, const rasterliteImagePtr src)
{
/*
/ this code is widely base upon the original wxWidgets gwxImage wxImage::ShrinkBy(() function
*/
    int xFactor = src->sx / dst->sx;
    int yFactor = src->sy / dst->sy;
    int x;
    int y;
    int x1;
    int y1;
    int y_offset;
    int x_offset;
    int pixel;
    for (y = 0; y < dst->sy; y++)
      {
	  for (x = 0; x < dst->sx; x++)
	    {
		/* determine average */
		unsigned int avgRed = 0;
		unsigned int avgGreen = 0;
		unsigned int avgBlue = 0;
		unsigned int counter = 0;
		for (y1 = 0; y1 < yFactor; ++y1)
		  {
		      y_offset = (y * yFactor + y1) * src->sx;
		      for (x1 = 0; x1 < xFactor; ++x1)
			{
			    x_offset = (x * xFactor) + x1;
			    pixel = src->pixels[y_offset][x_offset];
			    avgRed += true_color_get_red (pixel);
			    avgGreen += true_color_get_green (pixel);
			    avgBlue += true_color_get_blue (pixel);
			    counter++;
			}
		  }
		pixel =
		    true_color ((avgRed / counter), (avgGreen / counter),
				(avgBlue / counter));
		dst->pixels[y][x] = pixel;
	    }
      }
}

extern void
image_resize (const rasterliteImagePtr dst, const rasterliteImagePtr src)
{
/*
/ this function builds an ordinary quality resized image, applying pixel replication
/
/ this code is widely base upon the original wxWidgets gwxImage wxImage::Scale(() function
/ wxIMAGE_QUALITY_NORMAL
*/
    int x_delta;
    int y_delta;
    int y;
    int j;
    int x;
    int i;
    if ((src->sx % dst->sx) == 0 && src->sx >= dst->sx
	&& (src->sy % dst->sy) == 0 && src->sy >= dst->sy)
      {
	  shrink_by (src, dst);
	  return;
      }
    x = src->sx;
    y = src->sy;
    x_delta = (x << 16) / dst->sx;
    y_delta = (y << 16) / dst->sy;
    y = 0;
    for (j = 0; j < dst->sy; j++)
      {
	  x = 0;
	  for (i = 0; i < dst->sx; i++)
	    {
		dst->pixels[j][i] = src->pixels[y >> 16][x >> 16];
		x += x_delta;
	    }
	  y += y_delta;
      }
}

#define floor2(exp) ((long) exp)

extern void
make_thumbnail (const rasterliteImagePtr thumbnail,
		const rasterliteImagePtr image)
{
/*
/ this function builds an high quality thumbnail image, applying pixel interpolation
/
/ this code is widely base upon the original GD gdImageCopyResampled() function
*/
    int x, y;
    double sy1, sy2, sx1, sx2;
    for (y = 0; y < thumbnail->sy; y++)
      {
	  sy1 = ((double) y) * (double) image->sy / (double) thumbnail->sy;
	  sy2 =
	      ((double) (y + 1)) * (double) image->sy / (double) thumbnail->sy;
	  for (x = 0; x < thumbnail->sx; x++)
	    {
		double sx, sy;
		double spixels = 0;
		double red = 0.0, green = 0.0, blue = 0.0;
		sx1 =
		    ((double) x) * (double) image->sx / (double) thumbnail->sx;
		sx2 =
		    ((double) (x + 1)) * (double) image->sx /
		    (double) thumbnail->sx;
		sy = sy1;
		do
		  {
		      double yportion;
		      if (floor2 (sy) == floor2 (sy1))
			{
			    yportion = 1.0 - (sy - floor2 (sy));
			    if (yportion > sy2 - sy1)
			      {
				  yportion = sy2 - sy1;
			      }
			    sy = floor2 (sy);
			}
		      else if (sy == floor2 (sy2))
			{
			    yportion = sy2 - floor2 (sy2);
			}
		      else
			{
			    yportion = 1.0;
			}
		      sx = sx1;
		      do
			{
			    double xportion;
			    double pcontribution;
			    int p;
			    if (floor2 (sx) == floor2 (sx1))
			      {
				  xportion = 1.0 - (sx - floor2 (sx));
				  if (xportion > sx2 - sx1)
				    {
					xportion = sx2 - sx1;
				    }
				  sx = floor2 (sx);
			      }
			    else if (sx == floor2 (sx2))
			      {
				  xportion = sx2 - floor2 (sx2);
			      }
			    else
			      {
				  xportion = 1.0;
			      }
			    pcontribution = xportion * yportion;
			    p = image->pixels[(int) sy][(int) sx];
			    red += true_color_get_red (p) * pcontribution;
			    green += true_color_get_green (p) * pcontribution;
			    blue += true_color_get_blue (p) * pcontribution;
			    spixels += xportion * yportion;
			    sx += 1.0;
			}
		      while (sx < sx2);
		      sy += 1.0;
		  }
		while (sy < sy2);
		if (spixels != 0.0)
		  {
		      red /= spixels;
		      green /= spixels;
		      blue /= spixels;
		  }
		if (red > 255.0)
		    red = 255.0;
		if (green > 255.0)
		    green = 255.0;
		if (blue > 255.0)
		    blue = 255.0;
		image_set_pixel (thumbnail, x, y,
				 true_color ((int) red, (int) green,
					     (int) blue));
	    }
      }
}

extern void *
image_to_rgb_array (const rasterliteImagePtr img, int *size)
{
/* building a flat RGB array from this image */
    int x;
    int y;
    int pixel;
    unsigned char *data = NULL;
    unsigned char *p;
    int sz = img->sx * img->sy * 3;
    *size = 0;
/* allocating the RGB array */
    data = malloc (sz);
    p = data;
    for (y = 0; y < img->sy; y++)
      {
	  for (x = 0; x < img->sx; x++)
	    {
		pixel = img->pixels[y][x];
		*p++ = true_color_get_red (pixel);
		*p++ = true_color_get_green (pixel);
		*p++ = true_color_get_blue (pixel);
	    }
      }
    *size = sz;
    return data;
}

extern void *
image_to_rgba_array (int transparent_color, const rasterliteImagePtr img,
		     int *size)
{
/* building a flat RGBA array from this image */
    int x;
    int y;
    int pixel;
    int r;
    int g;
    int b;
    int a;
    unsigned char *data = NULL;
    unsigned char *p;
    int sz = img->sx * img->sy * 4;
    *size = 0;
/* allocating the RGB array */
    data = malloc (sz);
    p = data;
    for (y = 0; y < img->sy; y++)
      {
	  for (x = 0; x < img->sx; x++)
	    {
		pixel = img->pixels[y][x];
		r = true_color_get_red (pixel);
		g = true_color_get_green (pixel);
		b = true_color_get_blue (pixel);
		if (transparent_color == true_color (r, g, b))
		    a = 0;
		else
		    a = 255;
		*p++ = r;
		*p++ = g;
		*p++ = b;
		*p++ = a;
	    }
      }
    *size = sz;
    return data;
}

extern void *
image_to_argb_array (int transparent_color, const rasterliteImagePtr img,
		     int *size)
{
/* building a flat ARGB array from this image */
    int x;
    int y;
    int pixel;
    int r;
    int g;
    int b;
    int a;
    unsigned char *data = NULL;
    unsigned char *p;
    int sz = img->sx * img->sy * 4;
    *size = 0;
/* allocating the RGB array */
    data = malloc (sz);
    p = data;
    for (y = 0; y < img->sy; y++)
      {
	  for (x = 0; x < img->sx; x++)
	    {
		pixel = img->pixels[y][x];
		r = true_color_get_red (pixel);
		g = true_color_get_green (pixel);
		b = true_color_get_blue (pixel);
		if (transparent_color == true_color (r, g, b))
		    a = 0;
		else
		    a = 255;
		*p++ = a;
		*p++ = r;
		*p++ = g;
		*p++ = b;
	    }
      }
    *size = sz;
    return data;
}

extern void *
image_to_bgr_array (const rasterliteImagePtr img, int *size)
{
/* building a flat BGR array from this image */
    int x;
    int y;
    int pixel;
    unsigned char *data = NULL;
    unsigned char *p;
    int sz = img->sx * img->sy * 3;
    *size = 0;
/* allocating the RGB array */
    data = malloc (sz);
    p = data;
    for (y = 0; y < img->sy; y++)
      {
	  for (x = 0; x < img->sx; x++)
	    {
		pixel = img->pixels[y][x];
		*p++ = true_color_get_blue (pixel);
		*p++ = true_color_get_green (pixel);
		*p++ = true_color_get_red (pixel);
	    }
      }
    *size = sz;
    return data;
}

extern void *
image_to_bgra_array (int transparent_color, const rasterliteImagePtr img,
		     int *size)
{
/* building a flat BGRA array from this image */
    int x;
    int y;
    int pixel;
    int r;
    int g;
    int b;
    int a;
    unsigned char *data = NULL;
    unsigned char *p;
    int sz = img->sx * img->sy * 4;
    *size = 0;
/* allocating the RGB array */
    data = malloc (sz);
    p = data;
    for (y = 0; y < img->sy; y++)
      {
	  for (x = 0; x < img->sx; x++)
	    {
		pixel = img->pixels[y][x];
		r = true_color_get_red (pixel);
		g = true_color_get_green (pixel);
		b = true_color_get_blue (pixel);
		if (transparent_color == true_color (r, g, b))
		    a = 0;
		else
		    a = 255;
		*p++ = b;
		*p++ = g;
		*p++ = r;
		*p++ = a;
	    }
      }
    *size = sz;
    return data;
}

extern rasterliteImagePtr
image_from_rgb_array (const void *raw, int width, int height)
{
/* building an image form this flat RGB array */
    int x;
    int y;
    int r;
    int g;
    int b;
    int pixel;
    const unsigned char *data = raw;
    const unsigned char *p;
    rasterliteImagePtr img = image_create (width, height);
    if (!img)
	return NULL;
    for (y = 0; y < img->sy; y++)
      {
	  p = data + (y * (width * 3));
	  for (x = 0; x < img->sx; x++)
	    {
		r = *p++;
		g = *p++;
		b = *p++;
		pixel = true_color (r, g, b);
		img->pixels[y][x] = pixel;
	    }
      }
    return img;
}

extern rasterliteImagePtr
image_from_rgba_array (const void *raw, int width, int height)
{
/* building an image form this flat RGBA array */
    int x;
    int y;
    int r;
    int g;
    int b;
    int alpha;
    int pixel;
    const unsigned char *data = raw;
    const unsigned char *p;
    rasterliteImagePtr img = image_create (width, height);
    if (!img)
	return NULL;
    for (y = 0; y < img->sy; y++)
      {
	  p = data + (y * (width * 4));
	  for (x = 0; x < img->sx; x++)
	    {
		r = *p++;
		g = *p++;
		b = *p++;
		alpha = *p++;
		pixel = true_color (r, g, b);
		img->pixels[y][x] = pixel;
	    }
      }
    return img;
}

extern rasterliteImagePtr
image_from_argb_array (const void *raw, int width, int height)
{
/* building an image form this flat ARGB array */
    int x;
    int y;
    int r;
    int g;
    int b;
    int alpha;
    int pixel;
    const unsigned char *data = raw;
    const unsigned char *p;
    rasterliteImagePtr img = image_create (width, height);
    if (!img)
	return NULL;
    for (y = 0; y < img->sy; y++)
      {
	  p = data + (y * (width * 4));
	  for (x = 0; x < img->sx; x++)
	    {

		alpha = *p++;
		r = *p++;
		g = *p++;
		b = *p++;
		pixel = true_color (r, g, b);
		img->pixels[y][x] = pixel;
	    }
      }
    return img;
}

extern rasterliteImagePtr
image_from_bgr_array (const void *raw, int width, int height)
{
/* building an image form this flat BGR array */
    int x;
    int y;
    int r;
    int g;
    int b;
    int pixel;
    const unsigned char *data = raw;
    const unsigned char *p;
    rasterliteImagePtr img = image_create (width, height);
    if (!img)
	return NULL;
    for (y = 0; y < img->sy; y++)
      {
	  p = data + (y * (width * 3));
	  for (x = 0; x < img->sx; x++)
	    {
		b = *p++;
		g = *p++;
		r = *p++;
		pixel = true_color (r, g, b);
		img->pixels[y][x] = pixel;
	    }
      }
    return img;
}

extern rasterliteImagePtr
image_from_bgra_array (const void *raw, int width, int height)
{
/* building an image form this flat BGRA array */
    int x;
    int y;
    int r;
    int g;
    int b;
    int alpha;
    int pixel;
    const unsigned char *data = raw;
    const unsigned char *p;
    rasterliteImagePtr img = image_create (width, height);
    if (!img)
	return NULL;
    for (y = 0; y < img->sy; y++)
      {
	  p = data + (y * (width * 4));
	  for (x = 0; x < img->sx; x++)
	    {
		b = *p++;
		g = *p++;
		r = *p++;
		alpha = *p++;
		pixel = true_color (r, g, b);
		img->pixels[y][x] = pixel;
	    }
      }
    return img;
}

extern int
is_image_monochrome (const rasterliteImagePtr img)
{
/* checking if this Image is into the Monochrome colorspace */
    int x;
    int y;
    int pixel;
    int r;
    int g;
    int b;
    for (y = 0; y < img->sy; y++)
      {
	  for (x = 0; x < img->sx; x++)
	    {
		pixel = img->pixels[y][x];
		r = true_color_get_red (pixel);
		g = true_color_get_green (pixel);
		b = true_color_get_blue (pixel);
		if (r == 0 && g == 0 && b == 0)
		    continue;
		if (r == 255 && g == 255 && b == 255)
		    continue;
		return RASTERLITE_FALSE;
	    }
      }
    return RASTERLITE_TRUE;
}

extern int
is_image_grayscale (const rasterliteImagePtr img)
{
/* checking if this Image is into the GrayScale colorspace */
    int x;
    int y;
    int pixel;
    int r;
    int g;
    int b;
    for (y = 0; y < img->sy; y++)
      {
	  for (x = 0; x < img->sx; x++)
	    {
		pixel = img->pixels[y][x];
		r = true_color_get_red (pixel);
		g = true_color_get_green (pixel);
		b = true_color_get_blue (pixel);
		if (r == g && r == b)
		    continue;
		return RASTERLITE_FALSE;
	    }
      }
    return RASTERLITE_TRUE;
}

static void
palette_init (int *palette)
{
/* initializing an empty palette */
    int x;
    for (x = 0; x < 256; x++)
	palette[x] = INT_MAX;
}

static int
palette_check (int *palette, int pixel)
{
    int x;
    for (x = 0; x < 256; x++)
      {
	  if (palette[x] == pixel)
	      return RASTERLITE_TRUE;
	  if (palette[x] == INT_MAX)
	    {
		palette[x] = pixel;
		return RASTERLITE_TRUE;
	    }
      }
    return RASTERLITE_FALSE;
}

extern int
is_image_palette256 (const rasterliteImagePtr img)
{
/* checking if this Image may be represented using a 256 colors palette */
    int x;
    int y;
    int pixel;
    int palette[256];
    palette_init (palette);
    for (y = 0; y < img->sy; y++)
      {
	  for (x = 0; x < img->sx; x++)
	    {
		pixel = img->pixels[y][x];
		if (palette_check (palette, pixel) == RASTERLITE_TRUE)
		    continue;
		return RASTERLITE_FALSE;
	    }
      }
    return RASTERLITE_TRUE;
}
