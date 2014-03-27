/* 
/ rasterlite_jpeg.c
/
/ JPEG auxiliary helpers
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
#include <setjmp.h>
#include <limits.h>
#include <string.h>

#include <tiffio.h>
#include <jpeglib.h>
#include <jerror.h>

#ifdef SPATIALITE_AMALGAMATION
#include <spatialite/sqlite3.h>
#else
#include <sqlite3.h>
#endif

#include <spatialite/gaiageo.h>

#include "rasterlite_internals.h"

/* 
/
/ DISCLAIMER:
/ all the following code merely is an 'ad hoc' adaption
/ deriving from the original GD lib code
/
*/

#define OUTPUT_BUF_SIZE  4096

#ifdef HAVE_BOOLEAN
typedef boolean safeboolean;
#else
typedef int safeboolean;
#endif /* HAVE_BOOLEAN */

typedef struct _jmpbuf_wrapper
{
    jmp_buf jmpbuf;
}
jmpbuf_wrapper;

typedef struct
{
    struct jpeg_destination_mgr pub;
    xgdIOCtx *outfile;
    unsigned char *buffer;
}
my_destination_mgr;

typedef struct
{
    struct jpeg_source_mgr pub;
    xgdIOCtx *infile;
    unsigned char *buffer;
    safeboolean start_of_file;
}
my_source_mgr;

typedef my_source_mgr *my_src_ptr;

#define INPUT_BUF_SIZE  4096

typedef my_destination_mgr *my_dest_ptr;

static void
fatal_jpeg_error (j_common_ptr cinfo)
{
    jmpbuf_wrapper *jmpbufw;

    fprintf (stderr,
	     "jpeg-wrapper: JPEG library reports unrecoverable error: ");
    (*cinfo->err->output_message) (cinfo);
    fflush (stderr);
    jmpbufw = (jmpbuf_wrapper *) cinfo->client_data;
    jpeg_destroy (cinfo);
    if (jmpbufw != 0)
      {
	  longjmp (jmpbufw->jmpbuf, 1);
	  fprintf (stderr, "jpeg-wrappeg: EXTREMELY fatal error: longjmp"
		   " returned control; terminating\n");
      }
    else
      {
	  fprintf (stderr, "jpeg-wrappeg: EXTREMELY fatal error: jmpbuf"
		   " unrecoverable; terminating\n");
      }
    fflush (stderr);
    exit (99);
}

static void
init_source (j_decompress_ptr cinfo)
{
    my_src_ptr src = (my_src_ptr) cinfo->src;
    src->start_of_file = TRUE;
}

#define END_JPEG_SEQUENCE "\r\n[*]--:END JPEG:--[*]\r\n"
static safeboolean
fill_input_buffer (j_decompress_ptr cinfo)
{
    my_src_ptr src = (my_src_ptr) cinfo->src;
    int nbytes = 0;
    memset (src->buffer, 0, INPUT_BUF_SIZE);
    while (nbytes < INPUT_BUF_SIZE)
      {
	  int got = xgdGetBuf (src->buffer + nbytes,
			       INPUT_BUF_SIZE - nbytes,
			       src->infile);
	  if ((got == EOF) || (got == 0))
	    {
		if (!nbytes)
		  {
		      nbytes = -1;
		  }
		break;
	    }
	  nbytes += got;
      }
    if (nbytes <= 0)
      {
	  if (src->start_of_file)
	      ERREXIT (cinfo, JERR_INPUT_EMPTY);
	  WARNMS (cinfo, JWRN_JPEG_EOF);
	  src->buffer[0] = (unsigned char) 0xFF;
	  src->buffer[1] = (unsigned char) JPEG_EOI;
	  nbytes = 2;
      }
    src->pub.next_input_byte = src->buffer;
    src->pub.bytes_in_buffer = nbytes;
    src->start_of_file = FALSE;
    return TRUE;
}

static void
skip_input_data (j_decompress_ptr cinfo, long num_bytes)
{
    my_src_ptr src = (my_src_ptr) cinfo->src;
    if (num_bytes > 0)
      {
	  while (num_bytes > (long) src->pub.bytes_in_buffer)
	    {
		num_bytes -= (long) src->pub.bytes_in_buffer;
		(void) fill_input_buffer (cinfo);
	    }
	  src->pub.next_input_byte += (size_t) num_bytes;
	  src->pub.bytes_in_buffer -= (size_t) num_bytes;
      }
}

static void
term_source (j_decompress_ptr cinfo)
{
    if (cinfo)
	return;			/* does absolutely nothing - required in order to suppress warnings */
}

static void
init_destination (j_compress_ptr cinfo)
{
    my_dest_ptr dest = (my_dest_ptr) cinfo->dest;
    dest->buffer = (unsigned char *)
	(*cinfo->mem->alloc_small) ((j_common_ptr) cinfo, JPOOL_IMAGE,
				    OUTPUT_BUF_SIZE * sizeof (unsigned char));
    dest->pub.next_output_byte = dest->buffer;
    dest->pub.free_in_buffer = OUTPUT_BUF_SIZE;
}

static safeboolean
empty_output_buffer (j_compress_ptr cinfo)
{
    my_dest_ptr dest = (my_dest_ptr) cinfo->dest;
    if (xgdPutBuf (dest->buffer, OUTPUT_BUF_SIZE, dest->outfile) !=
	(size_t) OUTPUT_BUF_SIZE)
	ERREXIT (cinfo, JERR_FILE_WRITE);
    dest->pub.next_output_byte = dest->buffer;
    dest->pub.free_in_buffer = OUTPUT_BUF_SIZE;
    return TRUE;
}

static void
term_destination (j_compress_ptr cinfo)
{
    my_dest_ptr dest = (my_dest_ptr) cinfo->dest;
    size_t datacount = OUTPUT_BUF_SIZE - dest->pub.free_in_buffer;
    if (datacount > 0)
      {
	  if (xgdPutBuf (dest->buffer, datacount, dest->outfile) !=
	      (int) datacount)
	      ERREXIT (cinfo, JERR_FILE_WRITE);
      }
}

static void
jpeg_xgdIOCtx_src (j_decompress_ptr cinfo, xgdIOCtx * infile)
{
    my_src_ptr src;
    if (cinfo->src == NULL)
      {
	  cinfo->src = (struct jpeg_source_mgr *)
	      (*cinfo->mem->alloc_small) ((j_common_ptr) cinfo, JPOOL_PERMANENT,
					  sizeof (my_source_mgr));
	  src = (my_src_ptr) cinfo->src;
	  src->buffer = (unsigned char *)
	      (*cinfo->mem->alloc_small) ((j_common_ptr) cinfo, JPOOL_PERMANENT,
					  INPUT_BUF_SIZE *
					  sizeof (unsigned char));

      }
    src = (my_src_ptr) cinfo->src;
    src->pub.init_source = init_source;
    src->pub.fill_input_buffer = fill_input_buffer;
    src->pub.skip_input_data = skip_input_data;
    src->pub.resync_to_restart = jpeg_resync_to_restart;
    src->pub.term_source = term_source;
    src->infile = infile;
    src->pub.bytes_in_buffer = 0;
    src->pub.next_input_byte = NULL;
}

static void
jpeg_xgdIOCtx_dest (j_compress_ptr cinfo, xgdIOCtx * outfile)
{
    my_dest_ptr dest;
    if (cinfo->dest == NULL)
      {
	  cinfo->dest = (struct jpeg_destination_mgr *)
	      (*cinfo->mem->alloc_small) ((j_common_ptr) cinfo, JPOOL_PERMANENT,
					  sizeof (my_destination_mgr));
      }
    dest = (my_dest_ptr) cinfo->dest;
    dest->pub.init_destination = init_destination;
    dest->pub.empty_output_buffer = empty_output_buffer;
    dest->pub.term_destination = term_destination;
    dest->outfile = outfile;
}

static void
xgdImageJpegCtx (rasterliteImagePtr img, xgdIOCtx * outfile, int quality,
		 int mode)
{
    struct jpeg_compress_struct cinfo;
    struct jpeg_error_mgr jerr;
    int i, j, jidx;
    volatile JSAMPROW row = 0;
    JSAMPROW rowptr[1];
    jmpbuf_wrapper jmpbufw;
    JDIMENSION nlines;
    char comment[255];
    memset (&cinfo, 0, sizeof (cinfo));
    memset (&jerr, 0, sizeof (jerr));
    cinfo.err = jpeg_std_error (&jerr);
    cinfo.client_data = &jmpbufw;
    if (setjmp (jmpbufw.jmpbuf) != 0)
      {
	  if (row)
	      free (row);
	  return;
      }
    cinfo.err->error_exit = fatal_jpeg_error;
    jpeg_create_compress (&cinfo);
    cinfo.image_width = img->sx;
    cinfo.image_height = img->sy;
    if (mode == IMAGE_JPEG_BW)
      {
	  /* GRAYSCALE */
	  cinfo.input_components = 1;
	  cinfo.in_color_space = JCS_GRAYSCALE;
      }
    else
      {
	  /* RGB */
	  cinfo.input_components = 3;
	  cinfo.in_color_space = JCS_RGB;
      }
    jpeg_set_defaults (&cinfo);
    if (quality >= 0)
	jpeg_set_quality (&cinfo, quality, TRUE);
    jpeg_xgdIOCtx_dest (&cinfo, outfile);
    row = (JSAMPROW) calloc (1, cinfo.image_width * cinfo.input_components
			     * sizeof (JSAMPLE));
    if (row == 0)
      {
	  fprintf (stderr,
		   "jpeg-wrapper: error: unable to allocate JPEG row\n");
	  jpeg_destroy_compress (&cinfo);
	  return;
      }
    rowptr[0] = row;
    jpeg_start_compress (&cinfo, TRUE);
    sprintf (comment, "CREATOR: jpeg-wrapper (using IJG JPEG v%d),",
	     JPEG_LIB_VERSION);
    if (quality >= 0)
	sprintf (comment + strlen (comment), " quality = %d\n", quality);
    else
	strcat (comment + strlen (comment), " default quality\n");
    jpeg_write_marker (&cinfo, JPEG_COM, (unsigned char *) comment,
		       (unsigned int) strlen (comment));
#if BITS_IN_JSAMPLE == 12
    fprintf (stderr,
	     "jpeg-wrapper: error: jpeg library was compiled for 12-bit\n"
	     "precision. This is mostly useless, because JPEGs on the web are\n"
	     "8-bit and such versions of the jpeg library won't read or write\n"
	     "them. GD doesn't support these unusual images. Edit your\n"
	     "jmorecfg.h file to specify the correct precision and completely\n"
	     "'make clean' and 'make install' libjpeg again. Sorry.\n");
    goto error;
#endif /* BITS_IN_JSAMPLE == 12 */
    for (i = 0; i < img->sy; i++)
      {
	  for (jidx = 0, j = 0; j < img->sx; j++)
	    {
		int val = img->pixels[i][j];
		if (mode == IMAGE_JPEG_BW)
		  {
		      /* GRAYSCALE */
		      row[jidx++] = true_color_get_red (val);
		  }
		else
		  {
		      /* RGB */
		      row[jidx++] = true_color_get_red (val);
		      row[jidx++] = true_color_get_green (val);
		      row[jidx++] = true_color_get_blue (val);
		  }
	    }
	  nlines = jpeg_write_scanlines (&cinfo, rowptr, 1);
	  if (nlines != 1)
	      fprintf (stderr, "jpeg-wrapper: warning: jpeg_write_scanlines"
		       " returns %u -- expected 1\n", nlines);
      }
    jpeg_finish_compress (&cinfo);
    jpeg_destroy_compress (&cinfo);
    free (row);
}

static int
CMYKToRGB (int c, int m, int y, int k, int inverted)
{
    if (inverted)
      {
	  c = 255 - c;
	  m = 255 - m;
	  y = 255 - y;
	  k = 255 - k;
      }
    return true_color ((255 - c) * (255 - k) / 255,
		       (255 - m) * (255 - k) / 255,
		       (255 - y) * (255 - k) / 255);
}

static rasterliteImagePtr
xgdImageCreateFromJpegCtx (xgdIOCtx * infile)
{
    struct jpeg_decompress_struct cinfo;
    struct jpeg_error_mgr jerr;
    jmpbuf_wrapper jmpbufw;
    volatile JSAMPROW row = 0;
    volatile rasterliteImagePtr img = 0;
    JSAMPROW rowptr[1];
    int i, j, retval;
    JDIMENSION nrows;
    int channels = 3;
    int inverted = 0;
    memset (&cinfo, 0, sizeof (cinfo));
    memset (&jerr, 0, sizeof (jerr));
    cinfo.err = jpeg_std_error (&jerr);
    cinfo.client_data = &jmpbufw;
    if (setjmp (jmpbufw.jmpbuf) != 0)
      {
	  if (row)
	      free (row);
	  if (img)
	      image_destroy ((rasterliteImagePtr) img);
	  return 0;
      }
    cinfo.err->error_exit = fatal_jpeg_error;
    jpeg_create_decompress (&cinfo);
    jpeg_xgdIOCtx_src (&cinfo, infile);
    jpeg_save_markers (&cinfo, JPEG_APP0 + 14, 256);
    retval = jpeg_read_header (&cinfo, TRUE);
    if (retval != JPEG_HEADER_OK)
	fprintf (stderr, "jpeg-wrapper: warning: jpeg_read_header returns"
		 " %d, expected %d\n", retval, JPEG_HEADER_OK);
    if (cinfo.image_height > INT_MAX)
	fprintf (stderr,
		 "jpeg-wrapper: warning: JPEG image height (%u) is greater than INT_MAX\n",
		 cinfo.image_height);
    if (cinfo.image_width > INT_MAX)
	fprintf (stderr,
		 "jpeg-wrapper: warning: JPEG image width (%u) is greater than INT_MAX\n",
		 cinfo.image_width);
    img = image_create ((int) cinfo.image_width, (int) cinfo.image_height);
    if (img == 0)
      {
	  fprintf (stderr, "jpeg-wrapper error: cannot allocate image\n");
	  goto error;
      }
    if ((cinfo.jpeg_color_space == JCS_CMYK) ||
	(cinfo.jpeg_color_space == JCS_YCCK))
      {
	  cinfo.out_color_space = JCS_CMYK;
      }
    else
      {
	  cinfo.out_color_space = JCS_RGB;
      }

    if (jpeg_start_decompress (&cinfo) != TRUE)
	fprintf (stderr,
		 "jpeg-wrapper: warning: jpeg_start_decompress reports suspended data source\n");
    if (cinfo.out_color_space == JCS_RGB)
      {
	  img->color_space = COLORSPACE_RGB;
	  if (cinfo.output_components != 3)
	    {
		fprintf (stderr,
			 "jpeg-wrapper: error: JPEG color output_components == %d\n",
			 cinfo.output_components);
		goto error;
	    }
	  channels = 3;
      }
    else if (cinfo.out_color_space == JCS_GRAYSCALE)
      {
	  img->color_space = COLORSPACE_GRAYSCALE;
	  if (cinfo.output_components != 1)
	    {
		fprintf (stderr,
			 "jpeg-wrapper: error: JPEG color output_components == %d\n",
			 cinfo.output_components);
		goto error;
	    }
	  channels = 1;
      }
    else if (cinfo.out_color_space == JCS_CMYK)
      {
	  jpeg_saved_marker_ptr marker;
	  img->color_space = COLORSPACE_RGB;
	  if (cinfo.output_components != 4)
	    {
		fprintf (stderr,
			 "jpeg-wrapper: error: JPEG output_components == %d\n",
			 cinfo.output_components);
		goto error;
	    }
	  channels = 4;
	  marker = cinfo.marker_list;
	  while (marker)
	    {
		if ((marker->marker == (JPEG_APP0 + 14)) &&
		    (marker->data_length >= 12)
		    && (!strncmp ((const char *) marker->data, "Adobe", 5)))
		  {
		      inverted = 1;
		      break;
		  }
		marker = marker->next;
	    }
      }
    else
      {
	  fprintf (stderr, "jpeg-wrapper: error: unexpected colorspace\n");
	  goto error;
      }
#if BITS_IN_JSAMPLE == 12
    fprintf (stderr,
	     "jpeg-wrapper: error: jpeg library was compiled for 12-bit\n"
	     "precision. This is mostly useless, because JPEGs on the web are\n"
	     "8-bit and such versions of the jpeg library won't read or write\n"
	     "them. GD doesn't support these unusual images. Edit your\n"
	     "jmorecfg.h file to specify the correct precision and completely\n"
	     "'make clean' and 'make install' libjpeg again. Sorry.\n");
    goto error;
#endif /* BITS_IN_JSAMPLE == 12 */
    row = calloc (cinfo.output_width * channels, sizeof (JSAMPLE));
    if (row == 0)
      {
	  fprintf (stderr,
		   "jpeg-wrapper: error: unable to allocate row for JPEG scanline\n");
	  goto error;
      }
    rowptr[0] = row;
    if (cinfo.out_color_space == JCS_CMYK)
      {
	  for (i = 0; i < (int) cinfo.output_height; i++)
	    {
		register JSAMPROW currow = row;
		register int *tpix = img->pixels[i];
		nrows = jpeg_read_scanlines (&cinfo, rowptr, 1);
		if (nrows != 1)
		  {
		      fprintf (stderr,
			       "jpeg-wrapper: error: jpeg_read_scanlines returns %u, expected 1\n",
			       nrows);
		      goto error;
		  }
		for (j = 0; j < (int) cinfo.output_width;
		     j++, currow += 4, tpix++)
		  {
		      *tpix =
			  CMYKToRGB (currow[0], currow[1], currow[2], currow[3],
				     inverted);
		  }
	    }
      }
    else if (cinfo.out_color_space == JCS_GRAYSCALE)
      {
	  for (i = 0; i < (int) cinfo.output_height; i++)
	    {
		register JSAMPROW currow = row;
		register int *tpix = img->pixels[i];
		nrows = jpeg_read_scanlines (&cinfo, rowptr, 1);
		if (nrows != 1)
		  {
		      fprintf (stderr,
			       "jpeg-wrapper: error: jpeg_read_scanlines returns %u, expected 1\n",
			       nrows);
		      goto error;
		  }
		for (j = 0; j < (int) cinfo.output_width; j++, currow++, tpix++)
		  {
		      *tpix = true_color (currow[0], currow[0], currow[0]);
		  }
	    }
      }
    else
      {
	  for (i = 0; i < (int) cinfo.output_height; i++)
	    {
		register JSAMPROW currow = row;
		register int *tpix = img->pixels[i];
		nrows = jpeg_read_scanlines (&cinfo, rowptr, 1);
		if (nrows != 1)
		  {
		      fprintf (stderr,
			       "jpeg-wrapper: error: jpeg_read_scanlines returns %u, expected 1\n",
			       nrows);
		      goto error;
		  }
		for (j = 0; j < (int) cinfo.output_width;
		     j++, currow += 3, tpix++)
		  {
		      *tpix = true_color (currow[0], currow[1], currow[2]);
		  }
	    }
      }
    if (jpeg_finish_decompress (&cinfo) != TRUE)
	fprintf (stderr,
		 "jpeg-wrapper: warning: jpeg_finish_decompress reports suspended data source\n");
    jpeg_destroy_decompress (&cinfo);
    free (row);
    return (rasterliteImagePtr) img;
  error:
    jpeg_destroy_decompress (&cinfo);
    if (row)
	free (row);
    if (img)
	image_destroy ((rasterliteImagePtr) img);
    return 0;
}

extern void *
image_to_jpeg (const rasterliteImagePtr img, int *size, int quality)
{
/* compressing an image as JPEG RGB */
    void *rv;
    xgdIOCtx *out = xgdNewDynamicCtx (2048, NULL);
    xgdImageJpegCtx (img, out, quality, IMAGE_JPEG_RGB);
    rv = xgdDPExtractData (out, size);
    out->xgd_free (out);
    return rv;
}

extern void *
image_to_jpeg_grayscale (const rasterliteImagePtr img, int *size, int quality)
{
/* compressing an image as JPEG GRAYSCALE */
    void *rv;
    xgdIOCtx *out = xgdNewDynamicCtx (2048, NULL);
    xgdImageJpegCtx (img, out, quality, IMAGE_JPEG_BW);
    rv = xgdDPExtractData (out, size);
    out->xgd_free (out);
    return rv;
}

extern rasterliteImagePtr
image_from_jpeg (int size, const void *data)
{
/* uncompressing a JPEG */
    rasterliteImagePtr img;
    xgdIOCtx *in = xgdNewDynamicCtxEx (size, data, 0);
    img = xgdImageCreateFromJpegCtx (in);
    in->xgd_free (in);
    return img;
}
