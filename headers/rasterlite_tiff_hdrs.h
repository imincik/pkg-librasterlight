/*
/ rasterlite_tiff_hrds.h
/
/ internal declarations supporting geotiff
/
/ version 1.1a, 2011 November 12
/
/ Author: Brad Hards bradh@frogmouth.net
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
/ Portions created by the Initial Developer are Copyright (C) 2011
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

#ifndef _RASTERLITE_TIFF_HDRS_H
#define _RASTERLITE_TIFF_HDRS_H

#include "config.h"

#ifdef HAVE_GEOTIFF_GEOTIFF_H
#include <geotiff/geotiff.h>
#include <geotiff/xtiffio.h>
#include <geotiff/geo_tiffp.h>
#include <geotiff/geo_keyp.h>
#include <geotiff/geovalues.h>
#include <geotiff/geo_normalize.h>
#elif HAVE_LIBGEOTIFF_GEOTIFF_H
#include <libgeotiff/geotiff.h>
#include <libgeotiff/xtiffio.h>
#include <libgeotiff/geo_tiffp.h>
#include <libgeotiff/geo_keyp.h>
#include <libgeotiff/geovalues.h>
#include <libgeotiff/geo_normalize.h>
#else
#include <geotiff.h>
#include <xtiffio.h>
#include <geo_tiffp.h>
#include <geo_keyp.h>
#include <geovalues.h>
#include <geo_normalize.h>
#endif

#endif
