/* 
/ rasterlite_tiff.c
/
/ TIFF auxiliary helpers
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
#include <math.h>
#include <string.h>
#include <stdlib.h>

#include "rasterlite_tiff_hdrs.h"

#ifdef SPATIALITE_AMALGAMATION
#include <spatialite/sqlite3.h>
#else
#include <sqlite3.h>
#endif

#include <spatialite/gaiageo.h>

#include "rasterlite_internals.h"

struct memfile
{
/* a struct emulating a file [memory mapped] */
    unsigned char *buffer;
    tsize_t size;
    tsize_t eof;
    toff_t current;
};

static tsize_t
readproc (thandle_t clientdata, tdata_t data, tsize_t size)
{
/* emulating the read()  function */
    struct memfile *mem = clientdata;
    tsize_t len;
    if (mem->current >= (toff_t) mem->eof)
	return 0;
    len = size;
    if ((mem->current + size) >= (toff_t) mem->eof)
	len = (tsize_t) (mem->eof - mem->current);
    memcpy (data, mem->buffer + mem->current, len);
    mem->current += len;
    return len;
}

static tsize_t
writeproc (thandle_t clientdata, tdata_t data, tsize_t size)
{
/* emulating the write()  function */
    struct memfile *mem = clientdata;
    if ((mem->current + size) >= (toff_t) mem->size)
	return -1;
    memcpy (mem->buffer + mem->current, (unsigned char *) data, size);
    mem->current += size;
    if (mem->current > (toff_t) mem->eof)
	mem->eof = (tsize_t) (mem->current);
    return size;
}

static toff_t
seekproc (thandle_t clientdata, toff_t offset, int whence)
{
/* emulating the lseek()  function */
    struct memfile *mem = clientdata;
    switch (whence)
      {
      case SEEK_CUR:
	  if ((int) (mem->current + offset) < 0)
	      return (toff_t) - 1;
	  mem->current += offset;
	  if ((toff_t) mem->eof < mem->current)
	      mem->eof = (tsize_t) (mem->current);
	  break;
      case SEEK_END:
	  if ((int) (mem->eof + offset) < 0)
	      return (toff_t) - 1;
	  mem->current = mem->eof + offset;
	  if ((toff_t) mem->eof < mem->current)
	      mem->eof = (tsize_t) (mem->current);
	  break;
      case SEEK_SET:
      default:
	  if ((int) offset < 0)
	      return (toff_t) - 1;
	  mem->current = offset;
	  if ((toff_t) mem->eof < mem->current)
	      mem->eof = (tsize_t) (mem->current);
	  break;
      };
    return mem->current;
}

static int
closeproc (thandle_t clientdata)
{
/* emulating the close()  function */
    if (clientdata)
	return 0;		/* does absolutely nothing - required in order to suppress warnings */
    return 0;
}

static toff_t
sizeproc (thandle_t clientdata)
{
/* returning the pseudo-file current size */
    struct memfile *mem = clientdata;
    return mem->eof;
}

static int
mapproc (thandle_t clientdata, tdata_t * data, toff_t * offset)
{
    if (clientdata || data || offset)
	return 0;		/* does absolutely nothing - required in order to suppress warnings */
    return 0;
}

static void
unmapproc (thandle_t clientdata, tdata_t data, toff_t offset)
{
    if (clientdata || data || offset)
	return;			/* does absolutely nothing - required in order to suppress warnings */
    return;
}

extern void *
image_to_tiff_fax4 (const rasterliteImagePtr img, int *size)
{
/* compressing a bi-level (monocrhome) image as TIFF FAX-4 */
    unsigned char *tiff_image = NULL;
    TIFF *out;
    int row;
    int col;
    int strip_rows;
    tsize_t scan_bytes;
    tsize_t strip_bytes;
    tsize_t strip_effective;
    tstrip_t nstrip = 0;
    unsigned char *strip = NULL;
    unsigned char *strip_ptr;
    struct memfile clientdata;
    int pixel;
    unsigned char byte;
    int pos;
    clientdata.buffer = malloc (1024 * 1024);
    memset (clientdata.buffer, '\0', 1024 * 1024);
    clientdata.size = 1024 * 1024;
    clientdata.eof = 0;
    clientdata.current = 0;
    *size = 0;
    out =
	TIFFClientOpen ("tiff", "w", &clientdata, readproc, writeproc, seekproc,
			closeproc, sizeproc, mapproc, unmapproc);
    if (out == NULL)
	return NULL;
/* setting up the TIFF headers */
    TIFFSetField (out, TIFFTAG_SUBFILETYPE, 0);
    TIFFSetField (out, TIFFTAG_IMAGEWIDTH, img->sx);
    TIFFSetField (out, TIFFTAG_IMAGELENGTH, img->sy);
    TIFFSetField (out, TIFFTAG_SAMPLESPERPIXEL, 1);
    TIFFSetField (out, TIFFTAG_BITSPERSAMPLE, 1);
    TIFFSetField (out, TIFFTAG_ORIENTATION, ORIENTATION_TOPLEFT);
    TIFFSetField (out, TIFFTAG_FILLORDER, FILLORDER_MSB2LSB);
    TIFFSetField (out, TIFFTAG_PLANARCONFIG, PLANARCONFIG_CONTIG);
    TIFFSetField (out, TIFFTAG_XRESOLUTION, 300.0);
    TIFFSetField (out, TIFFTAG_YRESOLUTION, 300.0);
    TIFFSetField (out, TIFFTAG_RESOLUTIONUNIT, RESUNIT_INCH);
    TIFFSetField (out, TIFFTAG_PHOTOMETRIC, PHOTOMETRIC_MINISWHITE);
    TIFFSetField (out, TIFFTAG_COMPRESSION, COMPRESSION_CCITTFAX4);
    TIFFSetField (out, TIFFTAG_SOFTWARE, "SpatiaLite-tools");
    if (img->sy < 64)
	strip_rows = img->sy;
    else
	strip_rows = 64;
    TIFFSetField (out, TIFFTAG_ROWSPERSTRIP, strip_rows);
/* allocating the scan-line buffer */
    scan_bytes = TIFFScanlineSize (out);
    strip_bytes = scan_bytes * strip_rows;
    strip = (unsigned char *) _TIFFmalloc (strip_bytes);
    strip_ptr = strip;
    for (row = 0; row < img->sy; row++)
      {
	  byte = 0x00;
	  pos = 0;
	  for (col = 0; col < img->sx; col++)
	    {
		pixel = img->pixels[row][col];
		if (pixel == 0)
		  {
		      /* handling a black pixel */
		      switch (pos)
			{
			case 0:
			    byte |= 0x80;
			    break;
			case 1:
			    byte |= 0x40;
			    break;
			case 2:
			    byte |= 0x20;
			    break;
			case 3:
			    byte |= 0x10;
			    break;
			case 4:
			    byte |= 0x08;
			    break;
			case 5:
			    byte |= 0x04;
			    break;
			case 6:
			    byte |= 0x02;
			    break;
			case 7:
			    byte |= 0x01;
			    break;
			};
		  }
		pos++;
		if (pos > 7)
		  {
		      /* exporting an octet */
		      *strip_ptr++ = byte;
		      byte = 0x00;
		      pos = 0;
		  }
	    }
	  if (pos > 0)		/* exporting the last octet */
	      *strip_ptr++ = byte;
	  strip_effective = strip_ptr - strip;
	  if (strip_effective >= strip_bytes)
	    {
		/* writing a strip */
		TIFFWriteEncodedStrip (out, nstrip++, strip, strip_bytes);
		strip_ptr = strip;
	    }
      }
    strip_effective = strip_ptr - strip;
    if (strip_effective)
      {
	  TIFFWriteEncodedStrip (out, nstrip++, strip, strip_effective);
      }
    _TIFFfree (strip);
    TIFFClose (out);
    if (clientdata.eof > 0)
      {
	  tiff_image = malloc (clientdata.eof);
	  memcpy (tiff_image, clientdata.buffer, clientdata.eof);
	  *size = clientdata.eof;
      }
    free (clientdata.buffer);
    return tiff_image;
}

extern void *
image_to_tiff_palette (const rasterliteImagePtr img, int *size)
{
/* compressing a palettte image as TIFF  PALETTE */
    unsigned char *tiff_image = NULL;
    TIFF *out;
    int row;
    int col;
    int extimated_size;
    tsize_t line_bytes;
    uint16 red[256];
    uint16 green[256];
    uint16 blue[256];
    int mapping[256];
    int index;
    unsigned char *scanline = NULL;
    unsigned char *line_ptr;
    struct memfile clientdata;
    int pixel;
    extimated_size = (256 * 1024) + (img->sx * img->sy);
    clientdata.buffer = malloc (extimated_size);
    memset (clientdata.buffer, '\0', extimated_size);
    clientdata.size = extimated_size;
    clientdata.eof = 0;
    clientdata.current = 0;
    *size = 0;
    out =
	TIFFClientOpen ("tiff", "w", &clientdata, readproc, writeproc, seekproc,
			closeproc, sizeproc, mapproc, unmapproc);
    if (out == NULL)
	return NULL;
/* bulding the palette */
    for (col = 0; col < 256; col++)
	mapping[col] = -1;
    for (row = 0; row < img->sy; row++)
      {
	  for (col = 0; col < img->sx; col++)
	    {
		pixel = img->pixels[row][col];
		index = palette_set (mapping, pixel);
		img->pixels[row][col] = index;
	    }
      }
    for (col = 0; col < 256; col++)
      {
	  if (mapping[col] == -1)
	    {
		red[col] = 0;
		green[col] = 0;
		blue[col] = 0;
	    }
	  else
	    {
		pixel = mapping[col];
		red[col] = (uint16) (true_color_get_red (pixel) * 256);
		green[col] = (uint16) (true_color_get_green (pixel) * 256);
		blue[col] = (uint16) (true_color_get_blue (pixel) * 256);
	    }
      }
/* setting up the TIFF headers */
    TIFFSetField (out, TIFFTAG_SUBFILETYPE, 0);
    TIFFSetField (out, TIFFTAG_IMAGEWIDTH, img->sx);
    TIFFSetField (out, TIFFTAG_IMAGELENGTH, img->sy);
    TIFFSetField (out, TIFFTAG_SAMPLESPERPIXEL, 1);
    TIFFSetField (out, TIFFTAG_BITSPERSAMPLE, 8);
    TIFFSetField (out, TIFFTAG_ORIENTATION, ORIENTATION_TOPLEFT);
    TIFFSetField (out, TIFFTAG_PLANARCONFIG, PLANARCONFIG_CONTIG);
    TIFFSetField (out, TIFFTAG_XRESOLUTION, 300.0);
    TIFFSetField (out, TIFFTAG_YRESOLUTION, 300.0);
    TIFFSetField (out, TIFFTAG_RESOLUTIONUNIT, RESUNIT_INCH);
    TIFFSetField (out, TIFFTAG_PHOTOMETRIC, PHOTOMETRIC_PALETTE);
    TIFFSetField (out, TIFFTAG_COLORMAP, red, green, blue);
    TIFFSetField (out, TIFFTAG_COMPRESSION, COMPRESSION_NONE);
    TIFFSetField (out, TIFFTAG_SOFTWARE, "SpatiaLite-tools");
    TIFFSetField (out, TIFFTAG_ROWSPERSTRIP, 1);
/* allocating the scan-line buffer */
    line_bytes = img->sx;
    scanline = (unsigned char *) _TIFFmalloc (line_bytes);
    for (row = 0; row < img->sy; row++)
      {
	  line_ptr = scanline;
	  for (col = 0; col < img->sx; col++)
	      *line_ptr++ = img->pixels[row][col];
	  TIFFWriteScanline (out, scanline, row, 0);
      }
    _TIFFfree (scanline);
    TIFFClose (out);
    if (clientdata.eof > 0)
      {
	  tiff_image = malloc (clientdata.eof);
	  memcpy (tiff_image, clientdata.buffer, clientdata.eof);
	  *size = clientdata.eof;
      }
    free (clientdata.buffer);
    return tiff_image;
}

extern void *
image_to_tiff_grayscale (const rasterliteImagePtr img, int *size)
{
/* compressing a grayscale image as TIFF  GRAYSCALE */
    unsigned char *tiff_image = NULL;
    TIFF *out;
    int row;
    int col;
    int extimated_size;
    tsize_t line_bytes;
    unsigned char *scanline = NULL;
    unsigned char *line_ptr;
    struct memfile clientdata;
    int pixel;
    extimated_size = (256 * 1024) + (img->sx * img->sy);
    clientdata.buffer = malloc (extimated_size);
    memset (clientdata.buffer, '\0', extimated_size);
    clientdata.size = extimated_size;
    clientdata.eof = 0;
    clientdata.current = 0;
    *size = 0;
    out =
	TIFFClientOpen ("tiff", "w", &clientdata, readproc, writeproc, seekproc,
			closeproc, sizeproc, mapproc, unmapproc);
    if (out == NULL)
	return NULL;
/* setting up the TIFF headers */
    TIFFSetField (out, TIFFTAG_SUBFILETYPE, 0);
    TIFFSetField (out, TIFFTAG_IMAGEWIDTH, img->sx);
    TIFFSetField (out, TIFFTAG_IMAGELENGTH, img->sy);
    TIFFSetField (out, TIFFTAG_SAMPLESPERPIXEL, 1);
    TIFFSetField (out, TIFFTAG_BITSPERSAMPLE, 8);
    TIFFSetField (out, TIFFTAG_ORIENTATION, ORIENTATION_TOPLEFT);
    TIFFSetField (out, TIFFTAG_PLANARCONFIG, PLANARCONFIG_CONTIG);
    TIFFSetField (out, TIFFTAG_XRESOLUTION, 300.0);
    TIFFSetField (out, TIFFTAG_YRESOLUTION, 300.0);
    TIFFSetField (out, TIFFTAG_RESOLUTIONUNIT, RESUNIT_INCH);
    TIFFSetField (out, TIFFTAG_PHOTOMETRIC, PHOTOMETRIC_MINISBLACK);
    TIFFSetField (out, TIFFTAG_COMPRESSION, COMPRESSION_NONE);
    TIFFSetField (out, TIFFTAG_SOFTWARE, "SpatiaLite-tools");
    TIFFSetField (out, TIFFTAG_ROWSPERSTRIP, 1);
/* allocating the scan-line buffer */
    line_bytes = img->sx;
    scanline = (unsigned char *) _TIFFmalloc (line_bytes);
    for (row = 0; row < img->sy; row++)
      {
	  line_ptr = scanline;
	  for (col = 0; col < img->sx; col++)
	    {
		pixel = img->pixels[row][col];
		*line_ptr++ = true_color_get_red (pixel);
	    }
	  TIFFWriteScanline (out, scanline, row, 0);
      }
    _TIFFfree (scanline);
    TIFFClose (out);
    if (clientdata.eof > 0)
      {
	  tiff_image = malloc (clientdata.eof);
	  memcpy (tiff_image, clientdata.buffer, clientdata.eof);
	  *size = clientdata.eof;
      }
    free (clientdata.buffer);
    return tiff_image;
}

extern void *
image_to_tiff_rgb (const rasterliteImagePtr img, int *size)
{
/* compressing an RGBimage as TIFF  RGB */
    unsigned char *tiff_image = NULL;
    TIFF *out;
    int row;
    int col;
    int extimated_size;
    tsize_t line_bytes;
    unsigned char *scanline = NULL;
    unsigned char *line_ptr;
    struct memfile clientdata;
    int pixel;
    extimated_size = (256 * 1024) + (img->sx * img->sy * 3);
    clientdata.buffer = malloc (extimated_size);
    memset (clientdata.buffer, '\0', extimated_size);
    clientdata.size = extimated_size;
    clientdata.eof = 0;
    clientdata.current = 0;
    *size = 0;
    out =
	TIFFClientOpen ("tiff", "w", &clientdata, readproc, writeproc, seekproc,
			closeproc, sizeproc, mapproc, unmapproc);
    if (out == NULL)
	return NULL;
/* setting up the TIFF headers */
    TIFFSetField (out, TIFFTAG_SUBFILETYPE, 0);
    TIFFSetField (out, TIFFTAG_IMAGEWIDTH, img->sx);
    TIFFSetField (out, TIFFTAG_IMAGELENGTH, img->sy);
    TIFFSetField (out, TIFFTAG_SAMPLESPERPIXEL, 3);
    TIFFSetField (out, TIFFTAG_BITSPERSAMPLE, 8);
    TIFFSetField (out, TIFFTAG_ORIENTATION, ORIENTATION_TOPLEFT);
    TIFFSetField (out, TIFFTAG_PLANARCONFIG, PLANARCONFIG_CONTIG);
    TIFFSetField (out, TIFFTAG_XRESOLUTION, 300.0);
    TIFFSetField (out, TIFFTAG_YRESOLUTION, 300.0);
    TIFFSetField (out, TIFFTAG_RESOLUTIONUNIT, RESUNIT_INCH);
    TIFFSetField (out, TIFFTAG_PHOTOMETRIC, PHOTOMETRIC_RGB);
    TIFFSetField (out, TIFFTAG_COMPRESSION, COMPRESSION_NONE);
    TIFFSetField (out, TIFFTAG_SOFTWARE, "SpatiaLite-tools");
    TIFFSetField (out, TIFFTAG_ROWSPERSTRIP, 1);
/* allocating the scan-line buffer */
    line_bytes = img->sx * 3;
    scanline = (unsigned char *) _TIFFmalloc (line_bytes);
    for (row = 0; row < img->sy; row++)
      {
	  line_ptr = scanline;
	  for (col = 0; col < img->sx; col++)
	    {
		pixel = img->pixels[row][col];
		*line_ptr++ = true_color_get_red (pixel);
		*line_ptr++ = true_color_get_green (pixel);
		*line_ptr++ = true_color_get_blue (pixel);
	    }
	  TIFFWriteScanline (out, scanline, row, 0);
      }
    _TIFFfree (scanline);
    TIFFClose (out);
    if (clientdata.eof > 0)
      {
	  tiff_image = malloc (clientdata.eof);
	  memcpy (tiff_image, clientdata.buffer, clientdata.eof);
	  *size = clientdata.eof;
      }
    free (clientdata.buffer);
    return tiff_image;
}

extern rasterliteImagePtr
image_from_tiff (int size, const void *data)
{
/* uncompressing a TIFF */
    rasterliteImagePtr img;
    uint16 bits_per_sample;
    uint16 samples_per_pixel;
    uint16 photometric;
    uint32 width = 0;
    uint32 height = 0;
    uint32 rows_strip = 0;
    uint32 *raster = NULL;
    uint32 *scanline;
    struct memfile clientdata;
    int x;
    int y;
    int img_y;
    int strip_no;
    int effective_strip;
    uint32 pixel;
    int color;
    TIFF *in = (TIFF *) 0;
    clientdata.buffer = (unsigned char *) data;
    clientdata.size = size;
    clientdata.eof = size;
    clientdata.current = 0;
    in = TIFFClientOpen ("tiff", "r", &clientdata, readproc, writeproc,
			 seekproc, closeproc, sizeproc, mapproc, unmapproc);
    if (in == NULL)
	return NULL;
    if (TIFFIsTiled (in))
	return NULL;
/* retrieving the TIFF dimensions */
    TIFFGetField (in, TIFFTAG_IMAGELENGTH, &height);
    TIFFGetField (in, TIFFTAG_IMAGEWIDTH, &width);
    TIFFGetField (in, TIFFTAG_ROWSPERSTRIP, &rows_strip);
    TIFFGetField (in, TIFFTAG_BITSPERSAMPLE, &bits_per_sample);
    TIFFGetField (in, TIFFTAG_SAMPLESPERPIXEL, &samples_per_pixel);
    TIFFGetField (in, TIFFTAG_PHOTOMETRIC, &photometric);
    img = image_create (width, height);
    raster = malloc (sizeof (uint32) * (width * rows_strip));
    if (bits_per_sample == 1 && samples_per_pixel == 1)
	img->color_space = COLORSPACE_MONOCHROME;
    if (bits_per_sample == 8 && samples_per_pixel == 1 && photometric == 3)
	img->color_space = COLORSPACE_PALETTE;
    if (bits_per_sample == 8 && samples_per_pixel == 1 && photometric < 2)
	img->color_space = COLORSPACE_GRAYSCALE;
    if (samples_per_pixel >= 3)
	img->color_space = COLORSPACE_RGB;
    for (strip_no = 0; strip_no < (int) height; strip_no += rows_strip)
      {
	  if (!TIFFReadRGBAStrip (in, strip_no, raster))
	      goto error;
	  effective_strip = rows_strip;
	  if ((strip_no + rows_strip) > height)
	      effective_strip = height - strip_no;
	  for (y = 0; y < effective_strip; y++)
	    {
		img_y = strip_no + ((effective_strip - y) - 1);
		scanline = raster + (width * y);
		for (x = 0; x < (int) width; x++)
		  {
		      pixel = scanline[x];
		      color =
			  true_color (TIFFGetR (pixel), TIFFGetG (pixel),
				      TIFFGetB (pixel));
		      image_set_pixel (img, x, img_y, color);
		  }
	    }
      }
    TIFFClose (in);
    free (raster);
    return img;

  error:
    TIFFClose (in);
    image_destroy (img);
    free (raster);
    return NULL;
}

extern int
write_geotiff (const char *path, const void *raster,
	       int size, double xsize, double ysize, double xllcorner,
	       double yllcorner, const char *proj4text)
{
/* writing to disk a plain TIFF as a GeoTIFF */
    TIFF *tiff = NULL;
    GTIF *gtif = NULL;
    TIFF *in = NULL;
    tdata_t strip = NULL;
    int item;
    int maxItem;
    uint32 width;
    uint32 height;
    uint16 compression;
    uint16 bits_per_sample;
    uint16 samples_per_pixel;
    uint16 photometric;
    uint32 rows_strip;
    uint16 orientation_topleft;
    uint16 fill_order;
    uint16 planar_config;
    uint16 resolution_unit;
    uint16 *red;
    uint16 *green;
    uint16 *blue;
    char *software;
    float x_resolution;
    float y_resolution;
    int ok_width = 0;
    int ok_height = 0;
    int ok_compression = 0;
    int ok_bits_per_sample = 0;
    int ok_samples_per_pixel = 0;
    int ok_photometric = 0;
    int ok_rows_strip = 0;
    int ok_orientation_topleft = 0;
    int ok_fill_order = 0;
    int ok_planar_config = 0;
    int ok_resolution_unit = 0;
    int ok_x_resolution = 0;
    int ok_y_resolution = 0;
    int ok_software = 0;
    int ok_colormap = 0;
    double tiepoint[6];
    double pixsize[3];
    struct memfile clientdata;
/* opening the in-memory input TIFF image */
    clientdata.buffer = (unsigned char *) raster;
    clientdata.size = size;
    clientdata.eof = size;
    clientdata.current = 0;
    in = TIFFClientOpen ("tiff", "r", &clientdata, readproc, writeproc,
			 seekproc, closeproc, sizeproc, mapproc, unmapproc);
    if (!in)
	return 0;
/* retrieving the TIFF tags */
    if (TIFFGetField (in, TIFFTAG_IMAGEWIDTH, &width))
	ok_width = 1;
    if (TIFFGetField (in, TIFFTAG_IMAGELENGTH, &height))
	ok_height = 1;
    if (TIFFGetField (in, TIFFTAG_SAMPLESPERPIXEL, &samples_per_pixel))
	ok_samples_per_pixel = 1;
    if (TIFFGetField (in, TIFFTAG_BITSPERSAMPLE, &bits_per_sample))
	ok_bits_per_sample = 1;
    if (TIFFGetField (in, TIFFTAG_ORIENTATION, &orientation_topleft))
	ok_orientation_topleft = 1;
    if (TIFFGetField (in, TIFFTAG_FILLORDER, &fill_order))
	ok_fill_order = 1;
    if (TIFFGetField (in, TIFFTAG_PLANARCONFIG, &planar_config))
	ok_planar_config = 1;
    if (TIFFGetField (in, TIFFTAG_XRESOLUTION, &x_resolution))
	ok_x_resolution = 1;
    if (TIFFGetField (in, TIFFTAG_YRESOLUTION, &y_resolution))
	ok_y_resolution = 1;
    if (TIFFGetField (in, TIFFTAG_RESOLUTIONUNIT, &resolution_unit))
	ok_resolution_unit = 1;
    if (TIFFGetField (in, TIFFTAG_PHOTOMETRIC, &photometric))
	ok_photometric = 1;
    if (TIFFGetField (in, TIFFTAG_COMPRESSION, &compression))
	ok_compression = 1;
    if (TIFFGetField (in, TIFFTAG_SOFTWARE, &software))
	ok_software = 1;
    if (TIFFGetField (in, TIFFTAG_ROWSPERSTRIP, &rows_strip))
	ok_rows_strip = 1;
    if (TIFFGetField (in, TIFFTAG_COLORMAP, &red, &green, &blue))
	ok_colormap = 1;
/* opening the file-system based output GeoTIFF image */
    tiff = XTIFFOpen (path, "w");
    if (!tiff)
      {
	  printf ("\tCould not open TIFF image '%s'\n", path);
	  goto stop;
      }
    gtif = GTIFNew (tiff);
    if (!gtif)
      {
	  printf ("\tCould not open GeoTIFF image '%s'\n", path);
	  goto stop;
      }
/* setting up the GeiTIFF headers */
    TIFFSetField (tiff, TIFFTAG_SUBFILETYPE, 0);
    if (ok_width)
	TIFFSetField (tiff, TIFFTAG_IMAGEWIDTH, width);
    if (ok_height)
	TIFFSetField (tiff, TIFFTAG_IMAGELENGTH, height);
    if (ok_samples_per_pixel)
	TIFFSetField (tiff, TIFFTAG_SAMPLESPERPIXEL, samples_per_pixel);
    if (ok_bits_per_sample)
	TIFFSetField (tiff, TIFFTAG_BITSPERSAMPLE, bits_per_sample);
    if (ok_orientation_topleft)
	TIFFSetField (tiff, TIFFTAG_ORIENTATION, orientation_topleft);
    if (ok_fill_order)
	TIFFSetField (tiff, TIFFTAG_FILLORDER, fill_order);
    if (ok_planar_config)
	TIFFSetField (tiff, TIFFTAG_PLANARCONFIG, planar_config);
    if (ok_x_resolution)
	TIFFSetField (tiff, TIFFTAG_XRESOLUTION, x_resolution);
    if (ok_y_resolution)
	TIFFSetField (tiff, TIFFTAG_YRESOLUTION, y_resolution);
    if (ok_resolution_unit)
	TIFFSetField (tiff, TIFFTAG_RESOLUTIONUNIT, resolution_unit);
    if (ok_photometric)
	TIFFSetField (tiff, TIFFTAG_PHOTOMETRIC, photometric);
    if (ok_compression)
	TIFFSetField (tiff, TIFFTAG_COMPRESSION, compression);
    if (ok_software)
	TIFFSetField (tiff, TIFFTAG_SOFTWARE, software);
    if (ok_rows_strip)
	TIFFSetField (tiff, TIFFTAG_ROWSPERSTRIP, rows_strip);
    if (ok_colormap)
	TIFFSetField (tiff, TIFFTAG_COLORMAP, red, green, blue);
/* copying by strips */
    strip = _TIFFmalloc (TIFFStripSize (in));
    maxItem = TIFFNumberOfStrips (in);
    for (item = 0; item < maxItem; item++)
      {
	  tsize_t strip_size = TIFFRawStripSize (in, item);
	  TIFFReadRawStrip (in, item, strip, strip_size);
	  TIFFWriteRawStrip (tiff, item, strip, strip_size);
      }
    _TIFFfree (strip);
/* writing the GeoTIFF Tags */
    pixsize[0] = xsize;
    pixsize[1] = ysize;
    pixsize[2] = 0.0;
    TIFFSetField (tiff, GTIFF_PIXELSCALE, 3, pixsize);
    tiepoint[0] = 0.0;
    tiepoint[1] = 0.0;
    tiepoint[2] = 0.0;
    tiepoint[3] = xllcorner;
    tiepoint[4] = yllcorner;
    tiepoint[5] = 0.0;
    TIFFSetField (tiff, GTIFF_TIEPOINTS, 6, tiepoint);
/* writing the GeoTIFF  Keys */
    GTIFSetFromProj4 (gtif, proj4text);
    GTIFWriteKeys (gtif);

    if (gtif)
	GTIFFree (gtif);
    if (tiff)
	XTIFFClose (tiff);
    return 1;
  stop:
    if (gtif)
	GTIFFree (gtif);
    if (tiff)
	XTIFFClose (tiff);
    return 0;
}
