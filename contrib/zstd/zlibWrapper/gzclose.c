/* gzclose.c contains minimal changes required to be compiled with zlibWrapper:
 * - gz_statep was converted to union to work with -Wstrict-aliasing=1      */

/* gzclose.c -- zlib gzclose() function
 * Copyright (C) 2004, 2010 Mark Adler
 * For conditions of distribution and use, see http://www.zlib.net/zlib_license.html
 */

#include "gzguts.h"

/* gzclose() is in a separate file so that it is linked in only if it is used.
   That way the other gzclose functions can be used instead to avoid linking in
   unneeded compression or decompression routines. */
int ZEXPORT gzclose(file)
    gzFile file;
{
#ifndef NO_GZCOMPRESS
    gz_statep state;

    if (file == NULL)
        return Z_STREAM_ERROR;
    state = (gz_statep)file;

    return state.state->mode == GZ_READ ? gzclose_r(file) : gzclose_w(file);
#else
    return gzclose_r(file);
#endif
}
