/* 
/ rasterlite_io.c
/
/ IO helper methods
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
#include <limits.h>

#include <tiffio.h>

#ifdef SPATIALITE_AMALGAMATION
#include <spatialite/sqlite3.h>
#else
#include <sqlite3.h>
#endif

#include <spatialite/gaiageo.h>

#include "rasterlite_internals.h"

#define TRUE 1
#define FALSE 0

/* 
/
/ DISCLAIMER:
/ all the following code merely is an 'ad hoc' adaption
/ deriving from the original GD lib code
/
*/

static int
xgdReallocDynamic (dynamicPtr * dp, int required)
{
    void *newPtr;
    if ((newPtr = realloc (dp->data, required)))
      {
	  dp->realSize = required;
	  dp->data = newPtr;
	  return TRUE;
      }
    newPtr = malloc (required);
    if (!newPtr)
      {
	  dp->dataGood = FALSE;
	  return FALSE;
      }
    memcpy (newPtr, dp->data, dp->logicalSize);
    free (dp->data);
    dp->data = newPtr;
    dp->realSize = required;
    return TRUE;
}

static void
xgdFreeDynamicCtx (struct xgdIOCtx *ctx)
{
    dynamicPtr *dp;
    dpIOCtx *dctx;
    dctx = (dpIOCtx *) ctx;
    dp = dctx->dp;
    free (ctx);
    if ((dp->data != NULL) && (dp->freeOK))
      {
	  free (dp->data);
	  dp->data = NULL;
      }
    dp->realSize = 0;
    dp->logicalSize = 0;
    free (dp);
}

static int
allocDynamic (dynamicPtr * dp, int initialSize, const void *data)
{
    if (data == NULL)
      {
	  dp->logicalSize = 0;
	  dp->dataGood = FALSE;
	  dp->data = malloc (initialSize);
      }
    else
      {
	  dp->logicalSize = initialSize;
	  dp->dataGood = TRUE;
	  dp->data = (void *) data;
      }
    if (dp->data != NULL)
      {
	  dp->realSize = initialSize;
	  dp->dataGood = TRUE;
	  dp->pos = 0;
	  return TRUE;
      }
    else
      {
	  dp->realSize = 0;
	  return FALSE;
      }
}

static int
appendDynamic (dynamicPtr * dp, const void *src, int size)
{
    int bytesNeeded;
    char *tmp;
    if (!dp->dataGood)
	return FALSE;
    bytesNeeded = dp->pos + size;
    if (bytesNeeded > dp->realSize)
      {
	  if (!dp->freeOK)
	      return FALSE;
	  if (overflow2 (dp->realSize, 2))
	      return FALSE;
	  if (!xgdReallocDynamic (dp, bytesNeeded * 2))
	    {
		dp->dataGood = FALSE;
		return FALSE;
	    }
      }
    tmp = (char *) dp->data;
    memcpy ((void *) (tmp + (dp->pos)), src, size);
    dp->pos += size;
    if (dp->pos > dp->logicalSize)
	dp->logicalSize = dp->pos;
    return TRUE;
}

static int
dynamicPutbuf (struct xgdIOCtx *ctx, const void *buf, int size)
{
    dpIOCtx *dctx;
    dctx = (dpIOCtx *) ctx;
    appendDynamic (dctx->dp, buf, size);
    if (dctx->dp->dataGood)
	return size;
    else
	return -1;
}

static void
dynamicPutchar (struct xgdIOCtx *ctx, int a)
{
    unsigned char b;
    dpIOCtxPtr dctx;
    b = a;
    dctx = (dpIOCtxPtr) ctx;
    appendDynamic (dctx->dp, &b, 1);
}

static int
dynamicGetbuf (xgdIOCtxPtr ctx, void *buf, int len)
{
    int rlen, remain;
    dpIOCtxPtr dctx;
    dynamicPtr *dp;
    dctx = (dpIOCtxPtr) ctx;
    dp = dctx->dp;
    remain = dp->logicalSize - dp->pos;
    if (remain >= len)
	rlen = len;
    else
      {
	  if (remain == 0)
	      return 0;
	  rlen = remain;
      }
    memcpy (buf, (void *) ((char *) dp->data + dp->pos), rlen);
    dp->pos += rlen;
    return rlen;
}

static int
dynamicGetchar (xgdIOCtxPtr ctx)
{
    unsigned char b;
    int rv;
    rv = dynamicGetbuf (ctx, &b, 1);
    if (rv != 1)
	return EOF;
    else
	return b;
}

static long
dynamicTell (struct xgdIOCtx *ctx)
{
    dpIOCtx *dctx;
    dctx = (dpIOCtx *) ctx;
    return (dctx->dp->pos);
}

static int
dynamicSeek (struct xgdIOCtx *ctx, const int pos)
{
    int bytesNeeded;
    dynamicPtr *dp;
    dpIOCtx *dctx;
    dctx = (dpIOCtx *) ctx;
    dp = dctx->dp;
    if (!dp->dataGood)
	return FALSE;
    bytesNeeded = pos;
    if (bytesNeeded > dp->realSize)
      {
	  if (!dp->freeOK)
	      return FALSE;
	  if (overflow2 (dp->realSize, 2))
	      return FALSE;
	  if (!xgdReallocDynamic (dp, dp->realSize * 2))
	    {
		dp->dataGood = FALSE;
		return FALSE;
	    }
      }
    if (pos > dp->logicalSize)
	dp->logicalSize = pos;
    dp->pos = pos;
    return TRUE;
}

static dynamicPtr *
newDynamic (int initialSize, const void *data, int freeOKFlag)
{
    dynamicPtr *dp;
    dp = malloc (sizeof (dynamicPtr));
    if (dp == NULL)
	return NULL;
    if (!allocDynamic (dp, initialSize, data))
	return NULL;
    dp->pos = 0;
    dp->freeOK = freeOKFlag;
    return dp;
}

static int
trimDynamic (dynamicPtr * dp)
{
    if (!dp->freeOK)
	return TRUE;
    return xgdReallocDynamic (dp, dp->logicalSize);
}

extern int
overflow2 (int a, int b)
{
    if (a < 0 || b < 0)
      {
	  fprintf (stderr,
		   "warning: one parameter to a memory allocation multiplication is negative, failing operation gracefully\n");
	  return 1;
      }
    if (b == 0)
	return 0;
    if (a > INT_MAX / b)
      {
	  fprintf (stderr,
		   "warning: product of memory allocation multiplication would exceed INT_MAX, failing operation gracefully\n");
	  return 1;
      }
    return 0;
}

extern void *
xgdDPExtractData (struct xgdIOCtx *ctx, int *size)
{
    dynamicPtr *dp;
    dpIOCtx *dctx;
    void *data;
    dctx = (dpIOCtx *) ctx;
    dp = dctx->dp;
    if (dp->dataGood)
      {
	  trimDynamic (dp);
	  *size = dp->logicalSize;
	  data = dp->data;
      }
    else
      {
	  *size = 0;
	  data = NULL;
	  if ((dp->data != NULL) && (dp->freeOK))
	      free (dp->data);
      }
    dp->data = NULL;
    dp->realSize = 0;
    dp->logicalSize = 0;
    return data;
}

extern xgdIOCtx *
xgdNewDynamicCtx (int initialSize, const void *data)
{
    return xgdNewDynamicCtxEx (initialSize, data, 1);
}

extern xgdIOCtx *
xgdNewDynamicCtxEx (int initialSize, const void *data, int freeOKFlag)
{
    dpIOCtx *ctx;
    dynamicPtr *dp;
    ctx = malloc (sizeof (dpIOCtx));
    if (ctx == NULL)
      {
	  return NULL;
      }
    dp = newDynamic (initialSize, data, freeOKFlag);
    if (!dp)
      {
	  free (ctx);
	  return NULL;
      };
    ctx->dp = dp;
    ctx->ctx.getC = dynamicGetchar;
    ctx->ctx.putC = dynamicPutchar;
    ctx->ctx.getBuf = dynamicGetbuf;
    ctx->ctx.putBuf = dynamicPutbuf;
    ctx->ctx.seek = dynamicSeek;
    ctx->ctx.tell = dynamicTell;
    ctx->ctx.xgd_free = xgdFreeDynamicCtx;
    return (xgdIOCtx *) ctx;
}

extern int
palette_set (int *mapping, int color)
{
    int i;
    for (i = 0; i < 256; i++)
      {
	  if (mapping[i] == color)
	      return i;
	  if (mapping[i] == -1)
	    {
		mapping[i] = color;
		return i;
	    }
      }
    return 0;
}

extern int
xgdPutBuf (const void *buf, int size, xgdIOCtx * ctx)
{
    return (ctx->putBuf) (ctx, buf, size);
}

extern int
xgdGetBuf (void *buf, int size, xgdIOCtx * ctx)
{
    return (ctx->getBuf) (ctx, buf, size);
}
