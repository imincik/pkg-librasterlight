/* 
/ rasterlite_grid.c
/
/ a tool generating a GeoTIFF from am ASCII or BINARY Grid 
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
#include <float.h>
#include <math.h>
#include <errno.h>
#include <sys/types.h>

#include "rasterlite_tiff_hdrs.h"

#if defined(_WIN32) && !defined(__MINGW32__)
#include <io.h>
#include <direct.h>
#else
#include <dirent.h>
#endif

#ifdef _WIN32
#define strcasecmp	_stricmp
#endif /* not WIN32 */

#define ARG_NONE			0
#define ARG_GRID_PATH		1
#define ARG_COLOR_PATH		2
#define ARG_TIFF_PATH		3
#define ARG_GRID_TYPE		4
#define ARG_EPSG_CODE		5
#define ARG_NODATA_COLOR	6
#define ARG_PROJ4TEXT		7
#define ARG_MONO_COLOR		9
#define ARG_Z_FACTOR		10
#define ARG_SCALE			11
#define ARG_AZIMUTH			12
#define ARG_ALTITUDE		13

#define ASCII_GRID	100
#define FLOAT_GRID	101

#define BYTE_ORDER_NONE	0
#define BYTE_ORDER_BIG	1
#define BYTE_ORDER_LITTLE	2

struct colorTable
{
/* a color table value range */
    double min;
    double max;
    unsigned char red;
    unsigned char green;
    unsigned char blue;
};

struct triple_scan
{
/* the 3-scanlines object for Shaded Relief */
    int ncols;
    double z_factor;
    double scale_factor;
    double altitude;
    double azimuth;
    int mono_color;
    unsigned char mono_red;
    unsigned char mono_green;
    unsigned char mono_blue;
    float no_data_value;
    unsigned char no_red;
    unsigned char no_green;
    unsigned char no_blue;
    float *row1;
    float *row2;
    float *row3;
    float *current_row;
};

static int
cmp_color_qsort (const void *p1, const void *p2)
{
/* compares two colorTable entries [for QSORT] */
    struct colorTable *pc1 = (struct colorTable *) p1;
    struct colorTable *pc2 = (struct colorTable *) p2;
    if (pc1->min > pc2->min)
	return 1;
    if (pc1->min < pc2->min)
	return -1;
    if (pc1->max > pc2->max)
	return 1;
    if (pc1->max < pc2->max)
	return -1;
    return 0;
}

static int
cmp_color_bsearch (const void *p1, const void *p2)
{
/* compares two colorTable entries [for BSEARCH] */
    struct colorTable *pc1 = (struct colorTable *) p1;
    struct colorTable *pc2 = (struct colorTable *) p2;
    if (pc1->min >= pc2->min && pc1->min <= pc2->max)
	return 0;
    if (pc1->min > pc2->min)
	return 1;
    if (pc1->min < pc2->min)
	return -1;
    if (pc1->min > pc2->max)
	return 1;
    if (pc1->min < pc2->max)
	return -1;
    return 0;
}

static int
match_color (struct colorTable *color_table, int colors, double value,
	     unsigned char *red, unsigned char *green, unsigned char *blue)
{
/* mapping a value into the corresponding color */
    struct colorTable *ret;
    struct colorTable src;
    src.min = value;
    ret =
	bsearch (&src, color_table, colors, sizeof (struct colorTable),
		 cmp_color_bsearch);
    if (ret)
      {
	  *red = ret->red;
	  *green = ret->green;
	  *blue = ret->blue;
	  return 1;
      }
    return 0;
}

static struct triple_scan *
triple_alloc (int ncols)
{
/* allocating the 3-scanlines object */
    struct triple_scan *p = malloc (sizeof (struct triple_scan));
    p->ncols = ncols;
    p->row1 = malloc (sizeof (float) * ncols);
    p->row2 = malloc (sizeof (float) * ncols);
    p->row3 = malloc (sizeof (float) * ncols);
    p->current_row = p->row1;
    p->mono_color = 0;
    p->mono_red = 0;
    p->mono_green = 0;
    p->mono_blue = 0;
    p->no_data_value = 0.0;
    p->no_red = 0;
    p->no_green = 0;
    p->no_blue = 0;
    p->z_factor = 1.0;
    p->scale_factor = 1.0;
    p->altitude = 45.0;
    p->azimuth = 315.0;
    return p;
}

static void
triple_free (struct triple_scan *p)
{
/* freeing the 3-scanlines object */
    free (p->row1);
    free (p->row2);
    free (p->row3);
    free (p);
}

static void
triple_set_no_data (struct triple_scan *p, float no_data_value,
		    unsigned char no_red, unsigned char no_green,
		    unsigned char no_blue)
{
/* setting NODATA params */
    p->no_data_value = no_data_value;
    p->no_red = no_red;
    p->no_green = no_green;
    p->no_blue = no_blue;
}

static void
triple_set_monochrome_params (struct triple_scan *p, int mono_color,
			      unsigned char mono_red, unsigned char mono_green,
			      unsigned char mono_blue)
{
/* setting ShadedRelief params */
    p->mono_color = mono_color;
    p->mono_red = mono_red;
    p->mono_green = mono_green;
    p->mono_blue = mono_blue;
}

static void
triple_set_shaded_relief_params (struct triple_scan *p, double z, double scale,
				 double alt, double az)
{
/* setting ShadedRelief params */
    p->z_factor = z;
    p->scale_factor = scale;
    p->altitude = alt;
    p->azimuth = az;
}

static void
triple_rotate (struct triple_scan *p)
{
/* rotating the 3-scanlines */
    if (p->current_row == p->row1)
      {
	  p->current_row = p->row2;
	  goto zero_init;
      }
    if (p->current_row == p->row2)
      {
	  p->current_row = p->row3;
	  goto zero_init;
      }
    p->current_row = p->row1;
    p->row1 = p->row2;
    p->row2 = p->row3;
    p->row3 = p->current_row;
  zero_init:
    memset (p->current_row, 0, sizeof (float) * p->ncols);
}

static void
triple_store_cell (struct triple_scan *p, int icol, float value)
{
/* storing a cell value into 3-scanlines */
    if (icol < 0 || icol >= p->ncols)
	return;
    *(p->current_row + icol) = value;
}

static int
triple_is_valid (struct triple_scan *p)
{
/* checking for validitity */
    if (p->current_row == p->row3)
	return 1;
    return 0;
}

static void
triple_shaded_relief (struct triple_scan *p, unsigned char *raster)
{
/* creating a shaded relief scanline */
    int j;
    int n;
    double x;
    double y;
    double aspect;
    double slope;
    double cang;
    int gray;
    float afWin[9];
    const double degreesToRadians = M_PI / 180.0;
    const double altRadians = p->altitude * degreesToRadians;
    const double azRadians = p->azimuth * degreesToRadians;
    double red;
    double green;
    double blue;
    double alpha;
    int bContainsNull;
    unsigned char *p_raster = raster;
    unsigned char r;
    unsigned char g;
    unsigned char b;

/*
/ Move a 3x3 pafWindow over each cell 
/ (where the cell in question is #4)
/ 
/      0 1 2
/      3 4 5
/      6 7 8
*/
    for (j = 0; j < p->ncols; j++)
      {
	  /* Exclude the edges */
	  if (j == 0 || j == p->ncols - 1)
	      continue;

	  bContainsNull = 0;

	  afWin[0] = p->row1[j - 1];
	  afWin[1] = p->row1[j];
	  afWin[2] = p->row1[j + 1];
	  afWin[3] = p->row2[j - 1];
	  afWin[4] = p->row2[j];
	  afWin[5] = p->row2[j + 1];
	  afWin[6] = p->row3[j - 1];
	  afWin[7] = p->row3[j];
	  afWin[8] = p->row3[j + 1];

	  for (n = 0; n <= 8; n++)
	    {
		if (afWin[n] == p->no_data_value)
		  {
		      bContainsNull = 1;
		      break;
		  }
	    }

	  if (bContainsNull)
	    {
		/* We have nulls so write nullValue and move on */
		r = p->no_red;
		g = p->no_green;
		b = p->no_blue;
	    }
	  else
	    {
		/* We have a valid 3x3 window. */

		/* ---------------------------------------
		 * Compute Hillshade
		 */

		/* First Slope ... */
		x = p->z_factor * ((afWin[0] + afWin[3] + afWin[3] + afWin[6]) -
				   (afWin[2] + afWin[5] + afWin[5] +
				    afWin[8])) / (8.0 * p->scale_factor);

		y = p->z_factor * ((afWin[6] + afWin[7] + afWin[7] + afWin[8]) -
				   (afWin[0] + afWin[1] + afWin[1] +
				    afWin[2])) / (8.0 * p->scale_factor);

		slope = M_PI / 2 - atan (sqrt (x * x + y * y));

		/* ... then aspect... */
		aspect = atan2 (x, y);

		/* ... then the shade value */
		cang = sin (altRadians) * sin (slope) +
		    cos (altRadians) * cos (slope) *
		    cos (azRadians - M_PI / 2 - aspect);

		if (cang <= 0.0)
		    cang = 1.0;
		else
		    cang = 1.0 + (254.0 * cang);

		if (p->mono_color)
		  {
		      /* using the monochrome base color + ALPHA */
		      alpha = cang / 255.0;
		      red = (double) (p->mono_red) * alpha;
		      green = (double) (p->mono_green) * alpha;
		      blue = (double) (p->mono_blue) * alpha;
		      if (red < 0.0)
			  red = 0.0;
		      if (green < 0.0)
			  green = 0.0;
		      if (blue < 0.0)
			  blue = 0.0;
		      if (red > 255.0)
			  red = 255.0;
		      if (green > 255.0)
			  green = 255.0;
		      if (blue > 255.0)
			  blue = 255.0;
		      r = (unsigned char) red;
		      g = (unsigned char) green;
		      b = (unsigned) blue;
		  }
		else
		  {
		      /* plain gray-scale */
		      gray = (int) cang;
		      r = (unsigned char) gray;
		      g = (unsigned char) gray;
		      b = (unsigned) gray;
		  }
	    }
	  *p_raster++ = r;
	  *p_raster++ = g;
	  *p_raster++ = b;
      }
}

static int
triple_shaded_relief_color (struct colorTable *color_table, int colors,
			    struct triple_scan *p, unsigned char *raster,
			    int row)
{
/* creating a shaded relief color scanline */
    int j;
    int n;
    double x;
    double y;
    double aspect;
    double slope;
    double cang;
    float afWin[9];
    const double degreesToRadians = M_PI / 180.0;
    const double altRadians = p->altitude * degreesToRadians;
    const double azRadians = p->azimuth * degreesToRadians;
    double red;
    double green;
    double blue;
    double alpha;
    double thisValue;
    int bContainsNull;
    unsigned char *p_raster = raster;
    unsigned char r;
    unsigned char g;
    unsigned char b;

/*
/ Move a 3x3 pafWindow over each cell 
/ (where the cell in question is #4)
/ 
/      0 1 2
/      3 4 5
/      6 7 8
*/
    for (j = 0; j < p->ncols; j++)
      {
	  /* Exclude the edges */
	  if (j == 0 || j == p->ncols - 1)
	      continue;

	  bContainsNull = 0;

	  afWin[0] = p->row1[j - 1];
	  afWin[1] = p->row1[j];
	  afWin[2] = p->row1[j + 1];
	  afWin[3] = p->row2[j - 1];
	  afWin[4] = p->row2[j];
	  afWin[5] = p->row2[j + 1];
	  afWin[6] = p->row3[j - 1];
	  afWin[7] = p->row3[j];
	  afWin[8] = p->row3[j + 1];

	  thisValue = afWin[4];

	  for (n = 0; n <= 8; n++)
	    {
		if (afWin[n] == p->no_data_value)
		  {
		      bContainsNull = 1;
		      break;
		  }
	    }

	  if (bContainsNull)
	    {
		/* We have nulls so write nullValue and move on */
		r = p->no_red;
		g = p->no_green;
		b = p->no_blue;
	    }
	  else
	    {
		/* We have a valid 3x3 window. */

		/* ---------------------------------------
		 * Compute Hillshade
		 */

		/* First Slope ... */
		x = p->z_factor * ((afWin[0] + afWin[3] + afWin[3] + afWin[6]) -
				   (afWin[2] + afWin[5] + afWin[5] +
				    afWin[8])) / (8.0 * p->scale_factor);

		y = p->z_factor * ((afWin[6] + afWin[7] + afWin[7] + afWin[8]) -
				   (afWin[0] + afWin[1] + afWin[1] +
				    afWin[2])) / (8.0 * p->scale_factor);

		slope = M_PI / 2 - atan (sqrt (x * x + y * y));

		/* ... then aspect... */
		aspect = atan2 (x, y);

		/* ... then the shade value */
		cang = sin (altRadians) * sin (slope) +
		    cos (altRadians) * cos (slope) *
		    cos (azRadians - M_PI / 2 - aspect);

		if (cang <= 0.0)
		    cang = 1.0;
		else
		    cang = 1.0 + (254.0 * cang);

		/* merging the color + ALPHA */
		if (!match_color (color_table, colors, thisValue, &r, &g, &b))
		  {
		      printf
			  ("Grid row %d: unmatched value %1.8f; color not found\n",
			   row, thisValue);
		      return 0;
		  }
		alpha = cang / 255.0;
		red = (double) r *alpha;
		green = (double) g *alpha;
		blue = (double) b *alpha;
		if (red < 0.0)
		    red = 0.0;
		if (green < 0.0)
		    green = 0.0;
		if (blue < 0.0)
		    blue = 0.0;
		if (red > 255.0)
		    red = 255.0;
		if (green > 255.0)
		    green = 255.0;
		if (blue > 255.0)
		    blue = 255.0;
		r = (unsigned char) red;
		g = (unsigned char) green;
		b = (unsigned) blue;
	    }
	  *p_raster++ = r;
	  *p_raster++ = g;
	  *p_raster++ = b;
      }
    return 1;
}

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
parse_rgb (const char *hex_color, unsigned char *red, unsigned char *green,
	   unsigned char *blue)
{
/* parsing and hexadecimal encoded color 0XRRGGBB */
    int r;
    int g;
    int b;
    if (strlen (hex_color) != 8)
	return 0;
    if (hex_color[0] && (hex_color[1] == 'x' || hex_color[1] == 'X'))
	;
    else
	return 0;
    r = parse_hex (hex_color[2], hex_color[3]);
    g = parse_hex (hex_color[4], hex_color[5]);
    b = parse_hex (hex_color[6], hex_color[7]);
    if (r < 0 || g < 0 || b < 0)
	return 0;
    *red = r;
    *green = g;
    *blue = b;
    return 1;
}

static int
ok_number (const char *buf)
{
/* cheching a floating point number for validity */
    int digit = 0;
    int comma = 0;
    int sign = 0;
    int others = 0;
    const char *p = buf;
    while (*p != '\0')
      {
	  if (*p >= '0' && *p <= '9')
	      digit++;
	  else if (*p == '+' || *p == '-')
	    {
		if (p == buf)
		    sign++;
		else
		    others++;
	    }
	  else if (*p == '.')
	      comma++;
	  else
	      others++;
	  p++;
      }
    if (digit == 0)
	return 0;
    if (others > 0)
	return 0;
    if (comma > 1)
	return 0;
    return 1;
}

static int
parse_color_line (const char *buf, int row_no, double *min, double *max,
		  unsigned char *red, unsigned char *green, unsigned char *blue)
{
/* checking a color line */
    int item = 0;
    char token[2048];
    char *p_out = token;
    const char *p_in = buf;
    int error = 0;
    int ok_min = 0;
    int ok_max = 0;
    int ok_rgb = 0;
    unsigned char r;
    unsigned char g;
    unsigned char b;
    while (*p_in != '\0')
      {
	  if (*p_in == ' ' || *p_in == '\t')
	    {
		p_in++;
		if (p_out != token)
		  {
		      *p_out = '\0';
		      if (item == 0)
			{
			    if (ok_number (token))
			      {
				  *min = atof (token);
				  ok_min = 1;
			      }
			    else
			      {
				  fprintf (stderr,
					   "Row %d] invalid MIN-VALUE <%s>\n",
					   row_no, token);
				  error = 1;
			      }
			}
		      else if (item == 1)
			{
			    if (ok_number (token))
			      {
				  *max = atof (token);
				  ok_max = 1;
			      }
			    else
			      {
				  fprintf (stderr,
					   "Row %d] invalid MAX-VALUE <%s>\n",
					   row_no, token);
				  error = 1;
			      }
			}
		      if (item == 2)
			{
			    if (parse_rgb (token, &r, &g, &b))
			      {
				  *red = r;
				  *green = g;
				  *blue = b;
				  ok_rgb = 1;
			      }
			    else
			      {
				  fprintf (stderr,
					   "Row %d] invalid RGB-COLOR <%s>\n",
					   row_no, token);
				  error = 1;
			      }
			}
		      item++;
		  }
		p_out = token;
		continue;
	    }
	  *p_out++ = *p_in++;
      }
    if (ok_min && ok_max && ok_rgb && !error)
      {
	  if (*max < *min)
	    {
		fprintf (stderr,
			 "Row %d] mismatching interval [%1.2f / %1.2f]\n",
			 row_no, *min, *max);
		return 0;
	    }
	  return 1;
      }
    return 0;
}

static struct colorTable *
parse_color_table (const char *path, int *count)
{
/* parsing the Color Table */
    double min;
    double max;
    unsigned char red;
    unsigned char green;
    unsigned char blue;
    int c;
    int i = 0;
    int lines = 0;
    int error = 0;
    int row_no = 0;
    struct colorTable *table = NULL;
    char buf[2048];
    char *ptr = buf;
    FILE *in = fopen (path, "rb");
    if (!in)
      {
	  printf ("Open error: %s\n", path);
	  return NULL;
      }
    while ((c = getc (in)) != EOF)
      {
	  /* step I - counting the lines */
	  if (c == '\r')
	    {
		/* ignoring Return chars */
		continue;
	    }
	  if (c == '\n')
	    {
		row_no++;
		*ptr++ = ' ';	/* appending an extra Space at the end of the line */
		*ptr = '\0';
		if (strlen (buf) > 0)
		  {
		      if (parse_color_line
			  (buf, row_no, &min, &max, &red, &green, &blue))
			  lines++;
		      else
			  error++;
		  }
		ptr = buf;
		continue;
	    }
	  *ptr++ = c;
      }
    if (lines == 0 || error > 0)
	return NULL;
    table = malloc (sizeof (struct colorTable) * (lines));
/* closing and reopening the Color Table file */
    fclose (in);
    in = fopen (path, "rb");
    if (!in)
      {
	  printf ("Re-Open error: %s\n", path);
	  return NULL;
      }
    while ((c = getc (in)) != EOF)
      {
	  /* step II - feeding the struct */
	  if (c == '\r')
	    {
		/* ignoring Return chars */
		continue;
	    }
	  if (c == '\n')
	    {
		row_no++;
		*ptr++ = ' ';	/* appending an extra Space at the end of the line */
		*ptr = '\0';
		if (strlen (buf) > 0)
		  {
		      if (parse_color_line
			  (buf, row_no, &min, &max, &red, &green, &blue))
			{
			    if (i < lines)
			      {
				  (table + i)->min = min;
				  (table + i)->max = max;
				  (table + i)->red = red;
				  (table + i)->green = green;
				  (table + i)->blue = blue;
			      }
			    i++;
			}
		  }
		ptr = buf;
		continue;
	    }
	  *ptr++ = c;
      }
    if (i != lines)
      {
	  /* unexected error */
	  fprintf (stderr, "ColorTable: unexpected read error\n");
	  free (table);
	  table = NULL;
      }
    fclose (in);
/* sorting the color table */
    *count = lines;
    qsort (table, lines, sizeof (struct colorTable), cmp_color_qsort);
    return table;
}

static int
parseIntHeader (const char *row, const char *tag, int *value)
{
/* parsing an Integer value Header */
    int len = strlen (tag);
    if (strncmp (row, tag, len) != 0)
	return 0;
    *value = atoi (row + len);
    return 1;
}

static int
parseDblHeader (const char *row, const char *tag, double *value)
{
/* parsing a Double value Header */
    int len = strlen (tag);
    if (strncmp (row, tag, len) != 0)
	return 0;
    *value = atof (row + len);
    return 1;
}

static int
parseOrderHeader (const char *row, const char *tag, int *value)
{
/* parsing the ByteOrder value Header */
    int len = strlen (tag);
    *value = BYTE_ORDER_NONE;
    if (strncmp (row, tag, len) != 0)
	return 0;
    if (strncmp (row + strlen (row) - 8, "LSBFIRST", 8) == 0)
      {
	  *value = BYTE_ORDER_LITTLE;
	  return 1;
      }
    if (strncmp (row + strlen (row) - 8, "MSBFIRST", 8) == 0)
      {
	  *value = BYTE_ORDER_BIG;
	  return 1;
      }
    return 0;
}

static int
fetch_scanline (int row, struct colorTable *color_table, int colors, FILE * asc,
		unsigned char *raster, int columns, double nodata,
		unsigned char no_red, unsigned char no_green,
		unsigned char no_blue)
{
/* feeding a TIFF scanline from an ASCII Grid */
    int c;
    int cell = 0;
    char buf[1024];
    char *ptr = buf;
    double value;
    unsigned char red;
    unsigned char green;
    unsigned char blue;
    unsigned char *p_raster = raster;
    while ((c = getc (asc)) != EOF)
      {
	  if (c == '\n')
	      break;
	  *ptr++ = c;
	  if (c == ' ')
	    {
		/* value delimiter */
		*ptr = '\0';
		cell++;
		if (cell > columns)
		  {
		      printf ("Grid row %d: exceding column\n", row);
		      return 0;
		  }
		value = atof (buf);
		if (value == nodata)
		  {
		      *p_raster++ = no_red;
		      *p_raster++ = no_green;
		      *p_raster++ = no_blue;
		  }
		else
		  {
		      if (match_color
			  (color_table, colors, value, &red, &green, &blue))
			{
			    *p_raster++ = red;
			    *p_raster++ = green;
			    *p_raster++ = blue;
			}
		      else
			{
			    printf
				("Grid row %d: unmatched value %1.8f; color not found\n",
				 row, value);
			    return 0;
			}
		  }
		ptr = buf;
	    }
      }
    return 1;
}

static float
fetch_float (const unsigned char *p, int byteorder, int endian_arch)
{
/* fetches a 32bit FLOAT from BLOB respecting declared endiannes */
    union cvt
    {
	unsigned char byte[4];
	float flt_value;
    } convert;
    if (endian_arch)
      {
	  /* this one is a LittleEndian CPU */
	  if (byteorder == BYTE_ORDER_LITTLE)
	    {
		/* Litte-Endian float */
		convert.byte[0] = *(p + 0);
		convert.byte[1] = *(p + 1);
		convert.byte[2] = *(p + 2);
		convert.byte[3] = *(p + 3);
	    }
	  else
	    {
		/* Big Endian float */
		convert.byte[0] = *(p + 3);
		convert.byte[1] = *(p + 2);
		convert.byte[2] = *(p + 1);
		convert.byte[3] = *(p + 0);
	    }
      }
    else
      {
	  /* this one is a BigEndian CPU */
	  if (byteorder == BYTE_ORDER_BIG)
	    {
		/* Big-Endian float */
		convert.byte[0] = *(p + 0);
		convert.byte[1] = *(p + 1);
		convert.byte[2] = *(p + 2);
		convert.byte[3] = *(p + 3);
	    }
	  else
	    {
		/* Little Endian float */
		convert.byte[0] = *(p + 3);
		convert.byte[1] = *(p + 2);
		convert.byte[2] = *(p + 1);
		convert.byte[3] = *(p + 0);
	    }
      }
    return convert.flt_value;
}

static int
check_endian_arch ()
{
/* checking if target CPU is a little-endian one */
    union cvt
    {
	unsigned char byte[4];
	int int_value;
    } convert;
    convert.int_value = 1;
    if (convert.byte[0] == 0)
	return 0;
    return 1;
}

static int
fetch_float_scanline (int row, struct colorTable *color_table, int colors,
		      unsigned char *floats, unsigned char *raster, int columns,
		      double nodata, unsigned char no_red,
		      unsigned char no_green, unsigned char no_blue,
		      int byteorder)
{
/* feeding a TIFF scanline from a FLOAT Grid */
    int cell = 0;
    double value;
    unsigned char *ptr = floats;
    unsigned char red;
    unsigned char green;
    unsigned char blue;
    unsigned char *p_raster = raster;
    int endian_arch = check_endian_arch ();
    for (cell = 0; cell < columns; cell++)
      {
	  value = fetch_float (ptr, byteorder, endian_arch);
	  if (value == nodata)
	    {
		*p_raster++ = no_red;
		*p_raster++ = no_green;
		*p_raster++ = no_blue;
	    }
	  else
	    {
		if (match_color
		    (color_table, colors, value, &red, &green, &blue))
		  {
		      *p_raster++ = red;
		      *p_raster++ = green;
		      *p_raster++ = blue;
		  }
		else
		  {
		      printf
			  ("Grid row %d: unmatched value %1.8f; color not found\n",
			   row, value);
		      return 0;
		  }
	    }
	  ptr += sizeof (float);
      }
    return 1;
}

static void
export_geoTiff_float (struct colorTable *color_table, int colors,
		      const char *proj4text, const char *grid_path,
		      const char *tiff_path, unsigned char no_red,
		      unsigned char no_green, unsigned char no_blue,
		      int verbose)
{
/* exporting a FLOAT GRID as GeoTIFF */
    TIFF *tiff = NULL;
    GTIF *gtif = NULL;
    int byteorder = BYTE_ORDER_NONE;
    int c;
    int row = 0;
    char buf[1024];
    char path[1024];
    char *ptr = buf;
    int err = 0;
    int ncols = -1;
    int nrows = -1;
    double xllcorner = 0.0;
    double yllcorner = 0.0;
    double cellsize = 0.0;
    double nodata = 0.0;
    double tiepoint[6];
    double pixsize[3];
    unsigned char *raster = NULL;
    unsigned char *flt_buf = NULL;
    size_t rd;
    FILE *grid;

/* parsing the Grid Header .hdr */
    sprintf (path, "%s.hdr", grid_path);
    grid = fopen (path, "rb");
    if (!grid)
      {
	  printf ("Open error: %s\n", path);
	  return;
      }
    while ((c = getc (grid)) != EOF)
      {
	  if (c == '\r')
	    {
		/* ignoring Return chars */
		continue;
	    }
	  if (c == '\n')
	    {
		*ptr = '\0';
		switch (row)
		  {
		  case 0:
		      if (!parseIntHeader (buf, "ncols ", &ncols))
			  err = 1;
		      break;
		  case 1:
		      if (!parseIntHeader (buf, "nrows ", &nrows))
			  err = 1;
		      break;
		  case 2:
		      if (!parseDblHeader (buf, "xllcorner ", &xllcorner))
			  err = 1;
		      break;
		  case 3:
		      if (!parseDblHeader (buf, "yllcorner ", &yllcorner))
			  err = 1;
		      break;
		  case 4:
		      if (!parseDblHeader (buf, "cellsize ", &cellsize))
			  err = 1;
		      break;
		  case 5:
		      if (!parseDblHeader (buf, "NODATA_value ", &nodata))
			  err = 1;
		      break;
		  case 6:
		      if (!parseOrderHeader (buf, "byteorder ", &byteorder))
			  err = 1;
		      break;
		  };
		ptr = buf;
		row++;
		if (row == 7)
		    break;
		continue;
	    }
	  *ptr++ = c;
      }
    if (err)
      {
	  /* there was some error */
	  printf ("Invalid FLOAT Grid Header format in: %s\n", path);
	  goto stop;
      }
    fclose (grid);

/* parsing the Grid Cells .flt */
    sprintf (path, "%s.flt", grid_path);
    grid = fopen (path, "rb");
    if (!grid)
      {
	  printf ("Open error: %s\n", path);
	  return;
      }
/* creating the GeoTIFF file */
    tiff = XTIFFOpen (tiff_path, "w");
    if (!tiff)
      {
	  printf ("\tCould not open TIFF image '%s'\n", tiff_path);
	  goto stop;
      }
    gtif = GTIFNew (tiff);
    if (!gtif)
      {
	  printf ("\tCould not open GeoTIFF image '%s'\n", tiff_path);
	  goto stop;
      }

/* writing the TIFF Tags */
    TIFFSetField (tiff, TIFFTAG_IMAGEWIDTH, ncols);
    TIFFSetField (tiff, TIFFTAG_IMAGELENGTH, nrows);
    TIFFSetField (tiff, TIFFTAG_COMPRESSION, COMPRESSION_NONE);
    TIFFSetField (tiff, TIFFTAG_SAMPLEFORMAT, SAMPLEFORMAT_UINT);
    TIFFSetField (tiff, TIFFTAG_ROWSPERSTRIP, 1);
    TIFFSetField (tiff, TIFFTAG_PLANARCONFIG, PLANARCONFIG_CONTIG);
    TIFFSetField (tiff, TIFFTAG_PHOTOMETRIC, PHOTOMETRIC_RGB);
    TIFFSetField (tiff, TIFFTAG_BITSPERSAMPLE, 8);
    TIFFSetField (tiff, TIFFTAG_SAMPLESPERPIXEL, 3);

/* writing the GeoTIFF Tags */
    pixsize[0] = cellsize;
    pixsize[1] = cellsize;
    pixsize[2] = 0.0;
    TIFFSetField (tiff, GTIFF_PIXELSCALE, 3, pixsize);
    tiepoint[0] = 0.0;
    tiepoint[1] = 0.0;
    tiepoint[2] = 0.0;
    tiepoint[3] = xllcorner;
    tiepoint[4] = yllcorner + (cellsize * nrows);
    tiepoint[5] = 0.0;
    TIFFSetField (tiff, GTIFF_TIEPOINTS, 6, tiepoint);
    GTIFSetFromProj4 (gtif, proj4text);
    GTIFWriteKeys (gtif);
    raster = malloc (ncols * 3);
    flt_buf = malloc (sizeof (float) * ncols);
    for (row = 0; row < nrows; row++)
      {
	  if (verbose)
	    {
		fprintf (stderr, "writing scanline %d of %d\n", row + 1, nrows);
		fflush (stderr);
	    }
	  rd = fread (flt_buf, 1, ncols * sizeof (float), grid);
	  if (rd != (sizeof (float) * ncols))
	    {
		printf ("*** Grid read error ***\n");
		printf ("An invalid GeoTIFF was generated ... aborting ...\n");
		goto stop;
	    }
	  if (fetch_float_scanline
	      (row + 1, color_table, colors, flt_buf, raster, ncols, nodata,
	       no_red, no_green, no_blue, byteorder))
	    {
		if (TIFFWriteScanline (tiff, raster, row, 0) < 0)
		  {
		      printf ("\tTIFF write error @ row=%d\n", row);
		      printf
			  ("An invalid GeoTIFF was generated ... aborting ...\n");
		      goto stop;
		  }
	    }
	  else
	    {
		printf ("*** Grid read error ***\n");
		printf ("An invalid GeoTIFF was generated ... aborting ...\n");
		goto stop;
	    }
      }

  stop:
    if (gtif)
	GTIFFree (gtif);
    if (tiff)
	XTIFFClose (tiff);
    if (raster)
	free (raster);
    if (flt_buf)
	free (flt_buf);
    fclose (grid);
}

static void
export_geoTiff_ascii (struct colorTable *color_table, int colors,
		      const char *proj4text, const char *grid_path,
		      const char *tiff_path, unsigned char no_red,
		      unsigned char no_green, unsigned char no_blue,
		      int verbose)
{
/* exporting an ASCII GRID as GeoTIFF */
    TIFF *tiff = NULL;
    GTIF *gtif = NULL;
    int c;
    int row = 0;
    char buf[1024];
    char *ptr = buf;
    int err = 0;
    int ncols = -1;
    int nrows = -1;
    double xllcorner = 0.0;
    double yllcorner = 0.0;
    double cellsize = 0.0;
    double nodata = 0.0;
    double tiepoint[6];
    double pixsize[3];
    unsigned char *raster = NULL;
    FILE *asc = fopen (grid_path, "rb");
    if (!asc)
      {
	  printf ("Open error: %s\n", grid_path);
	  return;
      }
    while ((c = getc (asc)) != EOF)
      {
	  if (c == '\r')
	    {
		/* ignoring Return chars */
		continue;
	    }
	  if (c == '\n')
	    {
		*ptr = '\0';
		switch (row)
		  {
		  case 0:
		      if (!parseIntHeader (buf, "ncols ", &ncols))
			  err = 1;
		      break;
		  case 1:
		      if (!parseIntHeader (buf, "nrows ", &nrows))
			  err = 1;
		      break;
		  case 2:
		      if (!parseDblHeader (buf, "xllcorner ", &xllcorner))
			  err = 1;
		      break;
		  case 3:
		      if (!parseDblHeader (buf, "yllcorner ", &yllcorner))
			  err = 1;
		      break;
		  case 4:
		      if (!parseDblHeader (buf, "cellsize ", &cellsize))
			  err = 1;
		      break;
		  case 5:
		      if (!parseDblHeader (buf, "NODATA_value ", &nodata))
			  err = 1;
		      break;
		  };
		ptr = buf;
		row++;
		if (row == 6)
		    break;
		continue;
	    }
	  *ptr++ = c;
      }
    if (err)
      {
	  /* there was some error */
	  printf ("Invalid ASCII Grid format in: %s\n", grid_path);
	  goto stop;
      }
/* creating the GeoTIFF file */
    tiff = XTIFFOpen (tiff_path, "w");
    if (!tiff)
      {
	  printf ("\tCould not open TIFF image '%s'\n", tiff_path);
	  goto stop;
      }
    gtif = GTIFNew (tiff);
    if (!gtif)
      {
	  printf ("\tCould not open GeoTIFF image '%s'\n", tiff_path);
	  goto stop;
      }

/* writing the TIFF Tags */
    TIFFSetField (tiff, TIFFTAG_IMAGEWIDTH, ncols);
    TIFFSetField (tiff, TIFFTAG_IMAGELENGTH, nrows);
    TIFFSetField (tiff, TIFFTAG_COMPRESSION, COMPRESSION_NONE);
    TIFFSetField (tiff, TIFFTAG_SAMPLEFORMAT, SAMPLEFORMAT_UINT);
    TIFFSetField (tiff, TIFFTAG_ROWSPERSTRIP, 1);
    TIFFSetField (tiff, TIFFTAG_PLANARCONFIG, PLANARCONFIG_CONTIG);
    TIFFSetField (tiff, TIFFTAG_PHOTOMETRIC, PHOTOMETRIC_RGB);
    TIFFSetField (tiff, TIFFTAG_BITSPERSAMPLE, 8);
    TIFFSetField (tiff, TIFFTAG_SAMPLESPERPIXEL, 3);

/* writing the GeoTIFF Tags */
    pixsize[0] = cellsize;
    pixsize[1] = cellsize;
    pixsize[2] = 0.0;
    TIFFSetField (tiff, GTIFF_PIXELSCALE, 3, pixsize);
    tiepoint[0] = 0.0;
    tiepoint[1] = 0.0;
    tiepoint[2] = 0.0;
    tiepoint[3] = xllcorner;
    tiepoint[4] = yllcorner + (cellsize * nrows);
    tiepoint[5] = 0.0;
    TIFFSetField (tiff, GTIFF_TIEPOINTS, 6, tiepoint);
    GTIFSetFromProj4 (gtif, proj4text);
    GTIFWriteKeys (gtif);
    raster = malloc (ncols * 3);
    for (row = 0; row < nrows; row++)
      {
	  if (verbose)
	    {
		fprintf (stderr, "writing scanline %d of %d\n", row + 1, nrows);
		fflush (stderr);
	    }
	  if (fetch_scanline
	      (row + 1, color_table, colors, asc, raster, ncols, nodata, no_red,
	       no_green, no_blue))
	    {
		if (TIFFWriteScanline (tiff, raster, row, 0) < 0)
		  {
		      printf ("\tTIFF write error @ row=%d\n", row);
		      printf
			  ("An invalid GeoTIFF was generated ... aborting ...\n");
		      goto stop;
		  }
	    }
	  else
	    {
		printf ("*** Grid read error ***\n");
		printf ("An invalid GeoTIFF was generated ... aborting ...\n");
		goto stop;
	    }
      }

  stop:
    if (gtif)
	GTIFFree (gtif);
    if (tiff)
	XTIFFClose (tiff);
    if (raster)
	free (raster);
    fclose (asc);
}

static int
fetch_float_scanline_shaded (unsigned char *floats, int columns,
			     int byteorder, struct triple_scan *triplet)
{
/* feeding a TIFF scanline from a FLOAT Grid */
    int cell = 0;
    double value;
    unsigned char *ptr = floats;
    int endian_arch = check_endian_arch ();
    for (cell = 0; cell < columns; cell++)
      {
	  value = fetch_float (ptr, byteorder, endian_arch);
	  triple_store_cell (triplet, cell, (float) value);
	  ptr += sizeof (float);
      }
    return 1;
}

static void
export_geoTiff_shaded_float (struct colorTable *color_table, int colors,
			     const char *proj4text, const char *grid_path,
			     const char *tiff_path, unsigned char no_red,
			     unsigned char no_green, unsigned char no_blue,
			     int mono_color, unsigned char mono_red,
			     unsigned char mono_green, unsigned char mono_blue,
			     double z_factor, double scale_factor,
			     double azimuth, double altitude, int verbose)
{
/* exporting a FLOAT GRID as GeoTIFF */
    TIFF *tiff = NULL;
    GTIF *gtif = NULL;
    int byteorder = BYTE_ORDER_NONE;
    int c;
    int row = 0;
    char buf[1024];
    char path[1024];
    char *ptr = buf;
    int err = 0;
    int ncols = -1;
    int nrows = -1;
    double xllcorner = 0.0;
    double yllcorner = 0.0;
    double cellsize = 0.0;
    double nodata = 0.0;
    double tiepoint[6];
    double pixsize[3];
    struct triple_scan *triplet = NULL;
    unsigned char *raster = NULL;
    unsigned char *flt_buf = NULL;
    size_t rd;
    FILE *grid;

/* parsing the Grid Header .hdr */
    sprintf (path, "%s.hdr", grid_path);
    grid = fopen (path, "rb");
    if (!grid)
      {
	  printf ("Open error: %s\n", path);
	  return;
      }
    while ((c = getc (grid)) != EOF)
      {
	  if (c == '\r')
	    {
		/* ignoring Return chars */
		continue;
	    }
	  if (c == '\n')
	    {
		*ptr = '\0';
		switch (row)
		  {
		  case 0:
		      if (!parseIntHeader (buf, "ncols ", &ncols))
			  err = 1;
		      break;
		  case 1:
		      if (!parseIntHeader (buf, "nrows ", &nrows))
			  err = 1;
		      break;
		  case 2:
		      if (!parseDblHeader (buf, "xllcorner ", &xllcorner))
			  err = 1;
		      break;
		  case 3:
		      if (!parseDblHeader (buf, "yllcorner ", &yllcorner))
			  err = 1;
		      break;
		  case 4:
		      if (!parseDblHeader (buf, "cellsize ", &cellsize))
			  err = 1;
		      break;
		  case 5:
		      if (!parseDblHeader (buf, "NODATA_value ", &nodata))
			  err = 1;
		      break;
		  case 6:
		      if (!parseOrderHeader (buf, "byteorder ", &byteorder))
			  err = 1;
		      break;
		  };
		ptr = buf;
		row++;
		if (row == 7)
		    break;
		continue;
	    }
	  *ptr++ = c;
      }
    if (err)
      {
	  /* there was some error */
	  printf ("Invalid FLOAT Grid Header format in: %s\n", path);
	  goto stop;
      }
    fclose (grid);

/* resizing CellSize */
    cellsize = (cellsize * (double) ncols) / (double) (ncols - 2);

/* parsing the Grid Cells .flt */
    sprintf (path, "%s.flt", grid_path);
    grid = fopen (path, "rb");
    if (!grid)
      {
	  printf ("Open error: %s\n", path);
	  return;
      }
/* creating the GeoTIFF file */
    tiff = XTIFFOpen (tiff_path, "w");
    if (!tiff)
      {
	  printf ("\tCould not open TIFF image '%s'\n", tiff_path);
	  goto stop;
      }
    gtif = GTIFNew (tiff);
    if (!gtif)
      {
	  printf ("\tCould not open GeoTIFF image '%s'\n", tiff_path);
	  goto stop;
      }

/* writing the TIFF Tags */
    TIFFSetField (tiff, TIFFTAG_IMAGEWIDTH, ncols - 2);
    TIFFSetField (tiff, TIFFTAG_IMAGELENGTH, nrows - 2);
    TIFFSetField (tiff, TIFFTAG_COMPRESSION, COMPRESSION_NONE);
    TIFFSetField (tiff, TIFFTAG_SAMPLEFORMAT, SAMPLEFORMAT_UINT);
    TIFFSetField (tiff, TIFFTAG_ROWSPERSTRIP, 1);
    TIFFSetField (tiff, TIFFTAG_PLANARCONFIG, PLANARCONFIG_CONTIG);
    TIFFSetField (tiff, TIFFTAG_PHOTOMETRIC, PHOTOMETRIC_RGB);
    TIFFSetField (tiff, TIFFTAG_BITSPERSAMPLE, 8);
    TIFFSetField (tiff, TIFFTAG_SAMPLESPERPIXEL, 3);

/* writing the GeoTIFF Tags */
    pixsize[0] = cellsize;
    pixsize[1] = cellsize;
    pixsize[2] = 0.0;
    TIFFSetField (tiff, GTIFF_PIXELSCALE, 3, pixsize);
    tiepoint[0] = 0.0;
    tiepoint[1] = 0.0;
    tiepoint[2] = 0.0;
    tiepoint[3] = xllcorner;
    tiepoint[4] = yllcorner + (cellsize * nrows);
    tiepoint[5] = 0.0;
    TIFFSetField (tiff, GTIFF_TIEPOINTS, 6, tiepoint);
    GTIFSetFromProj4 (gtif, proj4text);
    GTIFWriteKeys (gtif);
    raster = malloc (ncols * 3);
    flt_buf = malloc (sizeof (float) * ncols);

/* initializing the TripleRow object */
    triplet = triple_alloc (ncols);
    triple_set_shaded_relief_params (triplet, z_factor, scale_factor, altitude,
				     azimuth);
    triple_set_monochrome_params (triplet, mono_color, mono_red, mono_green,
				  mono_blue);
    triple_set_no_data (triplet, (float) nodata, no_red, no_green, no_blue);

    for (row = 0; row < nrows; row++)
      {
	  if (verbose)
	    {
		fprintf (stderr, "writing scanline %d of %d\n", row + 1, nrows);
		fflush (stderr);
	    }
	  rd = fread (flt_buf, 1, ncols * sizeof (float), grid);
	  if (rd != (sizeof (float) * ncols))
	    {
		printf ("*** Grid read error ***\n");
		printf ("An invalid GeoTIFF was generated ... aborting ...\n");
		goto stop;
	    }
	  if (fetch_float_scanline_shaded (flt_buf, ncols, byteorder, triplet))
	    {
		if (triple_is_valid (triplet))
		  {
		      int ret = 1;
		      if (!color_table)
			  triple_shaded_relief (triplet, raster);
		      else
			  ret =
			      triple_shaded_relief_color (color_table, colors,
							  triplet, raster,
							  row + 1);
		      if (!ret)
			  goto stop;
		      if (TIFFWriteScanline (tiff, raster, row - 2, 0) < 0)
			{
			    printf ("\tTIFF write error @ row=%d\n", row);
			    printf
				("An invalid GeoTIFF was generated ... aborting ...\n");
			    goto stop;
			}
		  }
		triple_rotate (triplet);
	    }
	  else
	    {
		printf ("*** Grid read error ***\n");
		printf ("An invalid GeoTIFF was generated ... aborting ...\n");
		goto stop;
	    }
      }

  stop:
    if (gtif)
	GTIFFree (gtif);
    if (tiff)
	XTIFFClose (tiff);
    if (raster)
	free (raster);
    if (flt_buf)
	free (flt_buf);
    fclose (grid);
    if (triplet)
	triple_free (triplet);
}

static int
fetch_scanline_shaded (int row, FILE * asc, int columns,
		       struct triple_scan *triplet)
{
/* feeding a TIFF scanline from an ASCII Grid */
    int c;
    int cell = 0;
    char buf[1024];
    char *ptr = buf;
    double value;
    while ((c = getc (asc)) != EOF)
      {
	  if (c == '\n')
	      break;
	  *ptr++ = c;
	  if (c == ' ')
	    {
		/* value delimiter */
		*ptr = '\0';
		cell++;
		if (cell > columns)
		  {
		      printf ("Grid row %d: exceding column\n", row);
		      return 0;
		  }
		value = atof (buf);
		triple_store_cell (triplet, cell, (float) value);
		ptr = buf;
	    }
      }
    return 1;
}

static void
export_geoTiff_shaded_ascii (struct colorTable *color_table, int colors,
			     const char *proj4text, const char *grid_path,
			     const char *tiff_path, unsigned char no_red,
			     unsigned char no_green, unsigned char no_blue,
			     int mono_color, unsigned char mono_red,
			     unsigned char mono_green, unsigned char mono_blue,
			     double z_factor, double scale_factor,
			     double azimuth, double altitude, int verbose)
{
/* exporting an ASCII GRID as GeoTIFF */
    TIFF *tiff = NULL;
    GTIF *gtif = NULL;
    int c;
    int row = 0;
    char buf[1024];
    char *ptr = buf;
    int err = 0;
    int ncols = -1;
    int nrows = -1;
    double xllcorner = 0.0;
    double yllcorner = 0.0;
    double cellsize = 0.0;
    double nodata = 0.0;
    double tiepoint[6];
    double pixsize[3];
    unsigned char *raster = NULL;
    struct triple_scan *triplet = NULL;
    FILE *asc = fopen (grid_path, "rb");
    if (!asc)
      {
	  printf ("Open error: %s\n", grid_path);
	  return;
      }
    while ((c = getc (asc)) != EOF)
      {
	  if (c == '\r')
	    {
		/* ignoring Return chars */
		continue;
	    }
	  if (c == '\n')
	    {
		*ptr = '\0';
		switch (row)
		  {
		  case 0:
		      if (!parseIntHeader (buf, "ncols ", &ncols))
			  err = 1;
		      break;
		  case 1:
		      if (!parseIntHeader (buf, "nrows ", &nrows))
			  err = 1;
		      break;
		  case 2:
		      if (!parseDblHeader (buf, "xllcorner ", &xllcorner))
			  err = 1;
		      break;
		  case 3:
		      if (!parseDblHeader (buf, "yllcorner ", &yllcorner))
			  err = 1;
		      break;
		  case 4:
		      if (!parseDblHeader (buf, "cellsize ", &cellsize))
			  err = 1;
		      break;
		  case 5:
		      if (!parseDblHeader (buf, "NODATA_value ", &nodata))
			  err = 1;
		      break;
		  };
		ptr = buf;
		row++;
		if (row == 6)
		    break;
		continue;
	    }
	  *ptr++ = c;
      }
    if (err)
      {
	  /* there was some error */
	  printf ("Invalid ASCII Grid format in: %s\n", grid_path);
	  goto stop;
      }

/* resizing CellSize */
    cellsize = (cellsize * (double) ncols) / (double) (ncols - 2);

/* creating the GeoTIFF file */
    tiff = XTIFFOpen (tiff_path, "w");
    if (!tiff)
      {
	  printf ("\tCould not open TIFF image '%s'\n", tiff_path);
	  goto stop;
      }
    gtif = GTIFNew (tiff);
    if (!gtif)
      {
	  printf ("\tCould not open GeoTIFF image '%s'\n", tiff_path);
	  goto stop;
      }

/* writing the TIFF Tags */
    TIFFSetField (tiff, TIFFTAG_IMAGEWIDTH, ncols - 2);
    TIFFSetField (tiff, TIFFTAG_IMAGELENGTH, nrows - 2);
    TIFFSetField (tiff, TIFFTAG_COMPRESSION, COMPRESSION_NONE);
    TIFFSetField (tiff, TIFFTAG_SAMPLEFORMAT, SAMPLEFORMAT_UINT);
    TIFFSetField (tiff, TIFFTAG_ROWSPERSTRIP, 1);
    TIFFSetField (tiff, TIFFTAG_PLANARCONFIG, PLANARCONFIG_CONTIG);
    TIFFSetField (tiff, TIFFTAG_PHOTOMETRIC, PHOTOMETRIC_RGB);
    TIFFSetField (tiff, TIFFTAG_BITSPERSAMPLE, 8);
    TIFFSetField (tiff, TIFFTAG_SAMPLESPERPIXEL, 3);

/* writing the GeoTIFF Tags */
    pixsize[0] = cellsize;
    pixsize[1] = cellsize;
    pixsize[2] = 0.0;
    TIFFSetField (tiff, GTIFF_PIXELSCALE, 3, pixsize);
    tiepoint[0] = 0.0;
    tiepoint[1] = 0.0;
    tiepoint[2] = 0.0;
    tiepoint[3] = xllcorner;
    tiepoint[4] = yllcorner + (cellsize * nrows);
    tiepoint[5] = 0.0;
    TIFFSetField (tiff, GTIFF_TIEPOINTS, 6, tiepoint);
    GTIFSetFromProj4 (gtif, proj4text);
    GTIFWriteKeys (gtif);
    raster = malloc (ncols * 3);

/* initializing the TripleRow object */
    triplet = triple_alloc (ncols);
    triple_set_shaded_relief_params (triplet, z_factor, scale_factor, altitude,
				     azimuth);
    triple_set_monochrome_params (triplet, mono_color, mono_red, mono_green,
				  mono_blue);
    triple_set_no_data (triplet, (float) nodata, no_red, no_green, no_blue);

    for (row = 0; row < nrows; row++)
      {
	  if (verbose)
	    {
		fprintf (stderr, "writing scanline %d of %d\n", row + 1, nrows);
		fflush (stderr);
	    }
	  if (fetch_scanline_shaded (row + 1, asc, ncols, triplet))
	    {
		if (triple_is_valid (triplet))
		  {
		      int ret = 1;
		      if (!color_table)
			  triple_shaded_relief (triplet, raster);
		      else
			  ret =
			      triple_shaded_relief_color (color_table, colors,
							  triplet, raster,
							  row + 1);
		      if (!ret)
			  goto stop;
		      if (TIFFWriteScanline (tiff, raster, row - 2, 0) < 0)
			{
			    printf ("\tTIFF write error @ row=%d\n", row);
			    printf
				("An invalid GeoTIFF was generated ... aborting ...\n");
			    goto stop;
			}
		  }
		triple_rotate (triplet);
	    }
	  else
	    {
		printf ("*** Grid read error ***\n");
		printf ("An invalid GeoTIFF was generated ... aborting ...\n");
		goto stop;
	    }
      }

  stop:
    if (gtif)
	GTIFFree (gtif);
    if (tiff)
	XTIFFClose (tiff);
    if (raster)
	free (raster);
    fclose (asc);
    if (triplet)
	triple_free (triplet);
}

static void
do_help ()
{
/* printing the argument list */
    fprintf (stderr, "\n\nusage: rasterlite_grid ARGLIST\n");
    fprintf (stderr,
	     "==============================================================\n");
    fprintf (stderr,
	     "-? or --help                      print this help message\n");
    fprintf (stderr,
	     "-g or --grid-path     pathname    the Grid path (input)\n");
    fprintf (stderr,
	     "-c or --color-path    pathname    the ColorTable path (input)\n");
    fprintf (stderr,
	     "-t or --tiff-path     pathname    the GeoTIFF path (output)\n");
    fprintf (stderr,
	     "-p or --proj4text     proj4text   the PROJ.4 parameters\n");
    fprintf (stderr, "-f or --grid-format   grid-type   [ASCII | FLOAT]\n");
    fprintf (stderr,
	     "-n or --nodata-color  0xRRGGBB    [default = 0x000000]\n");
    fprintf (stderr, "-v or --verbose                   verbose output\n\n");
    fprintf (stderr, "Shaded Relief specific arguments:\n");
    fprintf (stderr, "---------------------------------\n");
    fprintf (stderr,
	     "-sr or --shaded-relief            *disabled by default*\n");
    fprintf (stderr, "-m or --monochrome    0xRRGGBB    [default = none]\n");
    fprintf (stderr, "-z or --z-factor      numeric     [default = 1.0]\n");
    fprintf (stderr, "-s or --scale-factor  numeric     [default = 1.0]\n");
    fprintf (stderr, "-az or --azimuth      numeric     [default = 315.0]\n");
    fprintf (stderr, "-al or --altitude     numeric     [default = 45.0]\n\n");
    fprintf (stderr, "Please note: --monochrome and --color-path are\n");
    fprintf (stderr, "mutually exclusive options\n");
}

int
main (int argc, char *argv[])
{
/* the MAIN function simply perform arguments checking */
    int i;
    int next_arg = ARG_NONE;
    const char *grid_path = NULL;
    const char *color_path = NULL;
    const char *tiff_path = NULL;
    const char *proj4text = NULL;
    int grid_type = -1;
    int verbose = 0;
    int error = 0;
    struct colorTable *color_table = NULL;
    unsigned char no_red = 0;
    unsigned char no_green = 0;
    unsigned char no_blue = 0;
    char error_nodata_color[1024];
    int shaded_relief = 0;
    int mono_color = 0;
    unsigned char mono_red = 0;
    unsigned char mono_green = 0;
    unsigned char mono_blue = 0;
    char error_mono_color[1024];
    double z_factor = 1.0;
    double scale_factor = 1.0;
    double azimuth = 315.0;
    double altitude = 45.0;
    int colors;
    *error_nodata_color = '\0';
    *error_mono_color = '\0';
    for (i = 1; i < argc; i++)
      {
	  /* parsing the invocation arguments */
	  if (next_arg != ARG_NONE)
	    {
		switch (next_arg)
		  {
		  case ARG_GRID_PATH:
		      grid_path = argv[i];
		      break;
		  case ARG_COLOR_PATH:
		      color_path = argv[i];
		      break;
		  case ARG_TIFF_PATH:
		      tiff_path = argv[i];
		      break;
		  case ARG_PROJ4TEXT:
		      proj4text = argv[i];
		      break;
		  case ARG_GRID_TYPE:
		      if (strcasecmp (argv[i], "ASCII") == 0)
			  grid_type = ASCII_GRID;
		      if (strcasecmp (argv[i], "FLOAT") == 0)
			  grid_type = FLOAT_GRID;
		      break;
		  case ARG_NODATA_COLOR:
		      if (!parse_rgb (argv[i], &no_red, &no_green, &no_blue))
			  strcpy (error_nodata_color, argv[i]);
		      break;
		  case ARG_MONO_COLOR:
		      if (!parse_rgb
			  (argv[i], &mono_red, &mono_green, &mono_blue))
			  strcpy (error_mono_color, argv[i]);
		      else
			  mono_color = 1;
		      break;
		  case ARG_Z_FACTOR:
		      z_factor = atof (argv[i]);
		      break;
		  case ARG_SCALE:
		      scale_factor = atof (argv[i]);
		      break;
		  case ARG_AZIMUTH:
		      azimuth = atof (argv[i]);
		      break;
		  case ARG_ALTITUDE:
		      altitude = atof (argv[i]);
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
	  if (strcasecmp (argv[i], "--grid-path") == 0)
	    {
		next_arg = ARG_GRID_PATH;
		continue;
	    }
	  if (strcmp (argv[i], "-g") == 0)
	    {
		next_arg = ARG_GRID_PATH;
		continue;
	    }
	  if (strcasecmp (argv[i], "--tiff-path") == 0)
	    {
		next_arg = ARG_TIFF_PATH;
		continue;
	    }
	  if (strcmp (argv[i], "-t") == 0)
	    {
		next_arg = ARG_TIFF_PATH;
		continue;
	    }
	  if (strcasecmp (argv[i], "--proj4text") == 0)
	    {
		next_arg = ARG_PROJ4TEXT;
		continue;
	    }
	  if (strcmp (argv[i], "-p") == 0)
	    {
		next_arg = ARG_PROJ4TEXT;
		continue;
	    }
	  if (strcasecmp (argv[i], "--color-path") == 0)
	    {
		next_arg = ARG_COLOR_PATH;
		continue;
	    }
	  if (strcmp (argv[i], "-c") == 0)
	    {
		next_arg = ARG_COLOR_PATH;
		continue;
	    }
	  if (strcmp (argv[i], "-f") == 0)
	    {
		next_arg = ARG_GRID_TYPE;
		continue;
	    }
	  if (strcasecmp (argv[i], "--grid-format") == 0)
	    {
		next_arg = ARG_GRID_TYPE;
		continue;
	    }
	  if (strcmp (argv[i], "-n") == 0)
	    {
		next_arg = ARG_NODATA_COLOR;
		continue;
	    }
	  if (strcasecmp (argv[i], "--nodata-color") == 0)
	    {
		next_arg = ARG_NODATA_COLOR;
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
	  if (strcasecmp (argv[i], "--shared-relief") == 0)
	    {
		shaded_relief = 1;
		continue;
	    }
	  if (strcmp (argv[i], "-sr") == 0)
	    {
		shaded_relief = 1;
		continue;
	    }
	  if (strcmp (argv[i], "-m") == 0)
	    {
		next_arg = ARG_MONO_COLOR;
		continue;
	    }
	  if (strcasecmp (argv[i], "--monochrome") == 0)
	    {
		next_arg = ARG_MONO_COLOR;
		continue;
	    }
	  if (strcmp (argv[i], "-z") == 0)
	    {
		next_arg = ARG_Z_FACTOR;
		continue;
	    }
	  if (strcasecmp (argv[i], "--z-factor") == 0)
	    {
		next_arg = ARG_Z_FACTOR;
		continue;
	    }
	  if (strcmp (argv[i], "-s") == 0)
	    {
		next_arg = ARG_SCALE;
		continue;
	    }
	  if (strcasecmp (argv[i], "--scale-factor") == 0)
	    {
		next_arg = ARG_SCALE;
		continue;
	    }
	  if (strcmp (argv[i], "-az") == 0)
	    {
		next_arg = ARG_AZIMUTH;
		continue;
	    }
	  if (strcasecmp (argv[i], "--azimuth") == 0)
	    {
		next_arg = ARG_AZIMUTH;
		continue;
	    }
	  if (strcmp (argv[i], "-al") == 0)
	    {
		next_arg = ARG_ALTITUDE;
		continue;
	    }
	  if (strcasecmp (argv[i], "--altitude") == 0)
	    {
		next_arg = ARG_ALTITUDE;
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
    if (!grid_path)
      {
	  fprintf (stderr,
		   "did you forget setting the --grid-path argument ?\n");
	  error = 1;
      }
    if (!shaded_relief)
      {
	  if (!color_path)
	    {
		fprintf (stderr,
			 "did you forget setting the --color-path argument ?\n");
		error = 1;
	    }
      }
    if (!tiff_path)
      {
	  fprintf (stderr,
		   "did you forget setting the --tiff-path argument ?\n");
	  error = 1;
      }
    if (!proj4text)
      {
	  fprintf (stderr,
		   "did you forget setting the --proj4text argument ?\n");
	  error = 1;
      }
    if (grid_type < 0)
      {
	  fprintf (stderr,
		   "did you forget setting the --grid-format argument ?\n");
	  error = 1;
      }
    if (strlen (error_nodata_color))
      {
	  printf ("invalid NODATA color '%s'\n", error_nodata_color);
	  error = 1;
      }
    if (strlen (error_mono_color))
      {
	  printf ("invalid Shaded Relief Monochrome color '%s'\n",
		  error_mono_color);
	  error = 1;
      }
    if (color_path && mono_color)
      {
	  printf ("--monochrome and --color-path are mutually exclusive\n");
	  error = 1;
      }
    if (error)
      {
	  do_help ();
	  return -1;
      }
    printf ("=====================================================\n");
    printf ("             Arguments Summary\n");
    printf ("=====================================================\n");
    printf ("Grid       pathname: '%s'\n", grid_path);
    if (color_path)
	printf ("ColorTable pathname: '%s'\n", color_path);
    printf ("GeoTIFF    pathname: '%s'\n", tiff_path);
    printf ("PROJ.4       string: '%s'\n", proj4text);
    printf ("NoData        color: 0x%02x%02x%02x\n", no_red, no_green, no_blue);
    switch (grid_type)
      {
      case ASCII_GRID:
	  printf ("Grid Format: ASCII\n");
	  break;
      case FLOAT_GRID:
	  printf ("Grid Format: FLOAT\n");
	  break;
      default:
	  printf ("Grid Format: UNKNOWN\n");
	  break;
      }
    if (shaded_relief)
      {
	  printf ("\n           Shaded Relief arguments:\n");
	  printf ("-----------------------------------------------------\n");
	  printf ("Z-factor     : %1.4f\n", z_factor);
	  printf ("Scale-factor : %1.4f\n", scale_factor);
	  printf ("Azimuth      : %1.4f\n", azimuth);
	  printf ("Altitude     : %1.4f\n", altitude);
	  if (mono_color)
	      printf ("Monochrome   : 0x%02x%02x%02x\n", mono_red, mono_green,
		      mono_blue);
      }
    printf ("=====================================================\n\n");
    if (color_path)
      {
	  color_table = parse_color_table (color_path, &colors);
	  if (!color_table)
	    {
		fprintf (stderr, "\n*********** Invalid Color Table\n");
		return -1;
	    }
      }
    if (shaded_relief)
      {
	  if (grid_type == FLOAT_GRID)
	      export_geoTiff_shaded_float (color_table, colors, proj4text,
					   grid_path, tiff_path, no_red,
					   no_green, no_blue, mono_color,
					   mono_red, mono_green, mono_blue,
					   z_factor, scale_factor, azimuth,
					   altitude, verbose);
	  else
	      export_geoTiff_shaded_ascii (color_table, colors, proj4text,
					   grid_path, tiff_path, no_red,
					   no_green, no_blue, mono_color,
					   mono_red, mono_green, mono_blue,
					   z_factor, scale_factor, azimuth,
					   altitude, verbose);
      }
    else
      {
	  if (grid_type == FLOAT_GRID)
	      export_geoTiff_float (color_table, colors, proj4text, grid_path,
				    tiff_path, no_red, no_green, no_blue,
				    verbose);
	  else
	      export_geoTiff_ascii (color_table, colors, proj4text, grid_path,
				    tiff_path, no_red, no_green, no_blue,
				    verbose);
      }
    if (color_table)
	free (color_table);
    return 0;
}
