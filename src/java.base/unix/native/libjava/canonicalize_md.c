/*
 * Copyright (c) 1994, 2021, Oracle and/or its affiliates. All rights reserved.
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This code is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 only, as
 * published by the Free Software Foundation.  Oracle designates this
 * particular file as subject to the "Classpath" exception as provided
 * by Oracle in the LICENSE file that accompanied this code.
 *
 * This code is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * version 2 for more details (a copy is included in the LICENSE file that
 * accompanied this code).
 *
 * You should have received a copy of the GNU General Public License version
 * 2 along with this work; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Please contact Oracle, 500 Oracle Parkway, Redwood Shores, CA 94065 USA
 * or visit www.oracle.com if you need additional information or have any
 * questions.
 */

/*
 * Pathname canonicalization for Unix file systems
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <errno.h>
#include <limits.h>

#include "jdk_util.h"
#include "path_util.h"

/* Note: The comments in this file use the terminology
         defined in the java.io.File class */

/* Convert a pathname to canonical form.  The input path is assumed to contain
   no duplicate slashes.  On Solaris we can use realpath() to do most of the
   work, though once that's done we still must collapse any remaining "." and
   ".." names by hand. */

JNIEXPORT int
JDK_Canonicalize(const char *orig, char *out, int len)
{
    if (len < PATH_MAX) {
        errno = EINVAL;
        return -1;
    }

    if (strlen(orig) >= PATH_MAX) {
        errno = ENAMETOOLONG;
        return -1;
    }

    /* First try realpath() on the entire path */
    if (realpath(orig, out)) {
        /* That worked, so return it */
        collapse(out);
        return 0;
    } else {
        if (errno == EINVAL || errno == ELOOP || errno == ENAMETOOLONG || errno == ENOMEM) {
            return -1;
        }

        /* Something's bogus in the original path, so remove names from the end
           until either some subpath works or we run out of names */
        char *p, *end, *r = NULL;
        char path[PATH_MAX];

        strncpy(path, orig, sizeof(path));
        if (path[PATH_MAX - 1] != '\0') {
            errno = ENAMETOOLONG;
            return -1;
         }
        end = path + strlen(path);

        for (p = end; p > path;) {

            /* Skip last element */
            while ((--p > path) && (*p != '/'));
            if (p == path) break;

            /* Try realpath() on this subpath */
            *p = '\0';
            r = realpath(path, out);
            *p = (p == end) ? '\0' : '/';

            if (r != NULL) {
                /* The subpath has a canonical path */
                break;
            } else if (errno == ENOENT || errno == ENOTDIR || errno == EACCES) {
                /* If the lookup of a particular subpath fails because the file
                   does not exist, because it is of the wrong type, or because
                   access is denied, then remove its last name and try again.
                   Other I/O problems cause an error return. */
                continue;
            } else {
                return -1;
            }
        }

        size_t nameMax;
        if (r != NULL) {
            /* Append unresolved subpath to resolved subpath */
            int rn = strlen(r);
            if (rn + (int)strlen(p) >= len) {
                /* Buffer overflow */
                errno = ENAMETOOLONG;
                return -1;
            }
            nameMax = pathconf(r, _PC_NAME_MAX);

            if ((rn > 0) && (r[rn - 1] == '/') && (*p == '/')) {
                /* Avoid duplicate slashes */
                p++;
            }
            strcpy(r + rn, p);
            collapse(r);
        } else {
            /* Nothing resolved, so just return the original path */
            nameMax = pathconf("/", _PC_NAME_MAX);
            strcpy(out, path);
            collapse(out);
        }

        // Ensure resolve path length is "< PATH_MAX" and collapse() did not overwrite
        // terminating null byte
        char resolvedPath[PATH_MAX];
        strncpy(resolvedPath, out, sizeof(resolvedPath));
        if (resolvedPath[PATH_MAX - 1] != '\0') {
            errno = ENAMETOOLONG;
            return -1;
        }

        // Ensure resolve path does not contain any components who length is "> NAME_MAX"
        // If pathconf call failed with -1 or returned 0 in case of permission denial
        if (nameMax < 1) {
            nameMax = NAME_MAX;
        }

        char *component;
        char *rest = resolvedPath;
        while ((component = strtok_r(rest, "/", &rest))) {
            if (strlen(component) > nameMax) {
                errno = ENAMETOOLONG;
                return -1;
            }
        }

        return 0;
    }
}
