/*
 Copyright (c) 2010 Fizians SAS. <http://www.fizians.com>
 This file is part of Rozofs.

 Rozofs is free software; you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published
 by the Free Software Foundation, version 2.

 Rozofs is distributed in the hope that it will be useful, but
 WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with this program.  If not, see
 <http://www.gnu.org/licenses/>.
 */

#define _XOPEN_SOURCE 500

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <limits.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/statfs.h>
#include <inttypes.h>
#include <glob.h>
#include <fnmatch.h>
#include <dirent.h>

#include <rozofs/rozofs.h>
#include <rozofs/common/log.h>
#include <rozofs/common/list.h>
#include <rozofs/common/xmalloc.h>
#include <rozofs/rozofs_srv.h>

#include "storage.h"

char *storage_map_distribution(storage_t * st, uint8_t layout,
        sid_t dist_set[ROZOFS_SAFE_MAX], uint8_t spare, char *path) {
    int i = 0;
    char build_path[FILENAME_MAX];

    DEBUG_FUNCTION;

    strncpy(path, st->root, FILENAME_MAX);
    strcat(path, "/");
    sprintf(build_path, "layout_%u/spare_%u/", layout, spare);
    strcat(path, build_path);
    uint8_t rozofs_safe = rozofs_get_rozofs_safe(layout);

    for (i = 0; i < rozofs_safe; i++) {
        char build_path_2[FILENAME_MAX];
        sprintf(build_path_2, "%.3u", dist_set[i]);
        strcat(path, build_path_2);
        if (i != (rozofs_safe - 1))
            strcat(path, "-");
    }
    strcat(path, "/");
    return path;
}

/*
 ** Build the path for the projection file
  @param fid: unique file identifier
  @param path : pointer to the buffer where reuslting path will be stored
  
  @retval pointer to the beginning of the path
  
 */
char *storage_map_projection(fid_t fid, char *path) {
    char str[37];

    uuid_unparse(fid, str);
    strcat(path, str);
    sprintf(str, ".bins");
    strcat(path, str);
    return path;
}

int storage_initialize(storage_t *st, cid_t cid, sid_t sid, const char *root) {
    int status = -1;
    uint8_t layout = 0;
    char path[FILENAME_MAX];
    struct stat s;

    DEBUG_FUNCTION;

    if (!realpath(root, st->root))
        goto out;
    // sanity checks
    if (stat(st->root, &s) != 0)
        goto out;
    if (!S_ISDIR(s.st_mode)) {
        errno = ENOTDIR;
        goto out;
    }

    // Build directories for each possible layout if necessary
    for (layout = 0; layout < LAYOUT_MAX; layout++) {

        // Build layout level directory
        sprintf(path, "%s/layout_%u", st->root, layout);
        if (access(path, F_OK) == -1) {
            if (errno == ENOENT) {
                // If the directory doesn't exist, create it
                if (mkdir(path, ROZOFS_ST_DIR_MODE) != 0)
                    goto out;
            } else {
                goto out;
            }
        }

        // Build spare level directories
        sprintf(path, "%s/layout_%u/spare_0", st->root, layout);
        if (access(path, F_OK) == -1) {
            if (errno == ENOENT) {
                // If the directory doesn't exist, create it
                if (mkdir(path, S_IRUSR | S_IWUSR | S_IXUSR) != 0)
                    goto out;
            } else {
                goto out;
            }
        }
        sprintf(path, "%s/layout_%u/spare_1", st->root, layout);
        if (access(path, F_OK) == -1) {
            if (errno == ENOENT) {
                // If the directory doesn't exist, create it
                if (mkdir(path, S_IRUSR | S_IWUSR | S_IXUSR) != 0)
                    goto out;
            } else {
                goto out;
            }
        }
    }

    st->sid = sid;
    st->cid = cid;

    status = 0;
out:
    return status;
}

void storage_release(storage_t * st) {

    DEBUG_FUNCTION;

    st->sid = 0;
    st->cid = 0;
    st->root[0] = 0;
}

int storage_write(storage_t * st, uint8_t layout, sid_t * dist_set,
        uint8_t spare, fid_t fid, bid_t bid, uint32_t nb_proj, uint8_t version,
        uint64_t *file_size, const bin_t * bins) {
    int status = -1;
    char path[FILENAME_MAX];
    int fd = -1;
    size_t nb_write = 0;
    size_t length_to_write = 0;
    off_t bins_file_offset = 0;
    uint16_t rozofs_max_psize = 0;
    uint8_t write_file_hdr = 0;
    struct stat sb;

    // Build the full path of directory that contains the bins file
    storage_map_distribution(st, layout, dist_set, spare, path);

    // Check that this directory already exists, otherwise it will be create
    if (access(path, F_OK) == -1) {
        if (errno == ENOENT) {
            // If the directory doesn't exist, create it
            if (mkdir(path, ROZOFS_ST_DIR_MODE) != 0) {
	      if (errno != EEXIST) { 
	        // The directory is not created !!!
                severe("mkdir failed (%s) : %s", path, strerror(errno));
                goto out;
	      }	
	      // Well someone else has created the directory in the meantime
            }
        } else {
            goto out;
        }
    }

    // Build the path of bins file
    storage_map_projection(fid, path);

    // Check that this file already exists
    if (access(path, F_OK) == -1)
        write_file_hdr = 1; // We must write the header

    // Open bins file
    fd = open(path, ROZOFS_ST_BINS_FILE_FLAG, ROZOFS_ST_BINS_FILE_MODE);
    if (fd < 0) {
        severe("open failed (%s) : %s", path, strerror(errno));
        goto out;
    }

    // If we write the bins file for the first time, we must write the header
    if (write_file_hdr) {
        // Prepare file header
        rozofs_stor_bins_file_hdr_t file_hdr;
        memcpy(file_hdr.dist_set_current, dist_set,
                ROZOFS_SAFE_MAX * sizeof (sid_t));
        memset(file_hdr.dist_set_next, 0, ROZOFS_SAFE_MAX * sizeof (sid_t));
        file_hdr.layout = layout;
        file_hdr.version = version;

        // Write the header for this bins file
        nb_write = pwrite(fd, &file_hdr, sizeof (file_hdr), 0);
        if (nb_write != sizeof (file_hdr)) {
            severe("pwrite failed: %s", strerror(errno));
            goto out;
        }
    }

    // Compute the offset and length to write
    rozofs_max_psize = rozofs_get_max_psize(layout);
    bins_file_offset = ROZOFS_ST_BINS_FILE_HDR_SIZE + bid * (rozofs_max_psize *
            sizeof (bin_t) + sizeof (rozofs_stor_bins_hdr_t));
    length_to_write = nb_proj * (rozofs_max_psize * sizeof (bin_t)
            + sizeof (rozofs_stor_bins_hdr_t));

    // Write nb_proj * (projection + header)
    nb_write = pwrite(fd, bins, length_to_write, bins_file_offset);
    if (nb_write != length_to_write) {
        severe("pwrite failed: %s", strerror(errno));
        goto out;
    }

    // Stat file for return the size of bins file after the write operation
    if (fstat(fd, &sb) == -1) {
        severe("fstat failed: %s", strerror(errno));
        goto out;
    }

    *file_size = sb.st_size;


    // Write is successful
    status = 0;

out:
    if (fd != -1) close(fd);
    return status;
}

int storage_read(storage_t * st, uint8_t layout, sid_t * dist_set,
        uint8_t spare, fid_t fid, bid_t bid, uint32_t nb_proj,
        bin_t * bins, size_t * len_read, uint64_t *file_size) {

    int status = -1;
    char path[FILENAME_MAX];
    int fd = -1;
    size_t nb_read = 0;
    size_t length_to_read = 0;
    off_t bins_file_offset = 0;
    uint16_t rozofs_max_psize = 0;
    struct stat sb;

    // Build the full path of directory that contains the bins file
    storage_map_distribution(st, layout, dist_set, spare, path);

    // Build the path of bins file
    storage_map_projection(fid, path);

    // Check that this file already exists
    if (access(path, F_OK) == -1)
        goto out;

    // Open bins file
    fd = open(path, ROZOFS_ST_BINS_FILE_FLAG, ROZOFS_ST_BINS_FILE_MODE);
    if (fd < 0) {
        severe("open failed (%s) : %s", path, strerror(errno));
        goto out;
    }

    // Compute the offset and length to read
    rozofs_max_psize = rozofs_get_max_psize(layout);
    bins_file_offset = ROZOFS_ST_BINS_FILE_HDR_SIZE +
            bid * ((off_t) (rozofs_max_psize * sizeof (bin_t)) +
            sizeof (rozofs_stor_bins_hdr_t));
    length_to_read = nb_proj * (rozofs_max_psize * sizeof (bin_t)
            + sizeof (rozofs_stor_bins_hdr_t));

    // Read nb_proj * (projection + header)
    nb_read = pread(fd, bins, length_to_read, bins_file_offset);

    // Check error
    if (nb_read == -1) {
        severe("pread failed: %s", strerror(errno));
        goto out;
    }

    // Check the length read
    if ((nb_read % (rozofs_max_psize * sizeof (bin_t) +
            sizeof (rozofs_stor_bins_hdr_t))) != 0) {
        char fid_str[37];
        uuid_unparse(fid, fid_str);
        severe("storage_read failed (FID: %s): read inconsistent length",
                fid_str);
        errno = EIO;
        goto out;
    }

    // Update the length read
    *len_read = nb_read;

    // Stat file for return the size of bins file after the read operation
    if (fstat(fd, &sb) == -1) {
        severe("fstat failed: %s", strerror(errno));
        goto out;
    }

    *file_size = sb.st_size;

    // Read is successful
    status = 0;

out:
    if (fd != -1) close(fd);
    return status;
}
// XXX Not used

int storage_truncate(storage_t * st, uint8_t layout, sid_t * dist_set,
        uint8_t spare, fid_t fid, tid_t proj_id, bid_t bid) {
    int status = -1;
    int fd = -1;
    char path[FILENAME_MAX];

    DEBUG_FUNCTION;

    // Build the full path of directory that contains the bins file
    storage_map_distribution(st, layout, dist_set, spare, path);

    // Build the path of bins file
    storage_map_projection(fid, path);

    // Check that this file already exists
    if (access(path, F_OK) == -1)
        goto out;

    // Open bins file
    fd = open(path, ROZOFS_ST_BINS_FILE_FLAG, ROZOFS_ST_BINS_FILE_MODE);
    if (fd < 0) {
        severe("open failed (%s) : %s", path, strerror(errno));
        goto out;
    }

    status = ftruncate(fd, (bid + 1) * rozofs_get_psizes(layout, proj_id)
            * sizeof (bin_t));
out:
    if (fd != -1) close(fd);
    return status;
}

int storage_rm_file(storage_t * st, uint8_t layout, sid_t * dist_set,
        fid_t fid) {
    int status = -1;
    uint8_t spare = 0;
    char path[FILENAME_MAX];

    DEBUG_FUNCTION;

    // For spare and no spare
    for (spare = 0; spare < 2; spare++) {

        // Build the full path of directory that contains the bins file
        storage_map_distribution(st, layout, dist_set, spare, path);

        // Build the path of bins file
        storage_map_projection(fid, path);

        // Check that this file exists
        if (access(path, F_OK) == -1)
            continue;

        if (unlink(path) == -1) {
            if (errno != ENOENT) {
                severe("storage_rm_file failed: unlink file %s failed: %s",
                        path, strerror(errno));
                goto out;
            }
        } else {
            // It's not possible for one storage to store one bins file
            // in directories spare and no spare.
            status = 0;
            goto out;
        }
    }

    status = 0;
out:
    return status;
}

int storage_stat(storage_t * st, sstat_t * sstat) {
    int status = -1;
    struct statfs sfs;
    DEBUG_FUNCTION;

    if (statfs(st->root, &sfs) == -1)
        goto out;
    sstat->size = (uint64_t) sfs.f_blocks * (uint64_t) sfs.f_bsize;
    sstat->free = (uint64_t) sfs.f_bfree * (uint64_t) sfs.f_bsize;
    status = 0;
out:
    return status;
}

bins_file_rebuild_t ** storage_list_bins_file(storage_t * st, uint8_t layout,
        sid_t * dist_set, uint8_t spare, uint64_t * cookie,
        bins_file_rebuild_t ** children, uint8_t * eof,
        uint64_t * current_files_nb) {
    int i = 0;
    int j = 0;
    char path[FILENAME_MAX];
    DIR *dp = NULL;
    struct dirent *ep = NULL;
    bins_file_rebuild_t **iterator;

    DEBUG_FUNCTION;

    // Build the full path of directory that contains the bins file
    storage_map_distribution(st, layout, dist_set, spare, path);

    // Open directory
    if (!(dp = opendir(path)))
        goto out;

    // Readdir first time
    ep = readdir(dp);

    // Go to cookie index in this dir
    for (j = 0; j < *cookie; j++) {
        if (ep)
            ep = readdir(dp);
    }

    // Use iterator
    iterator = children;

    // The current nb. of bins files in the list
    i = *current_files_nb;

    // Readdir the next entries
    while (ep && i < MAX_REBUILD_ENTRIES) {

        // Pattern matching
        if (fnmatch("*.bins", ep->d_name, 0) == 0) {

            // Get the FID for this bins file
            char fid_str[37];
            if (sscanf(ep->d_name, "%36s.bins", fid_str) != 1)
                continue;

            // Alloc a new bins_file_rebuild_t
            *iterator = xmalloc(sizeof (bins_file_rebuild_t)); // XXX FREE ?
            // Copy FID
            uuid_parse(fid_str, (*iterator)->fid);
            // Copy current dist_set
            memcpy((*iterator)->dist_set_current, dist_set,
                    sizeof (sid_t) * ROZOFS_SAFE_MAX);
            // Copy layout
            (*iterator)->layout = layout;

            // Go to next entry
            iterator = &(*iterator)->next;

            // Increment the current nb. of bins files in the list
            i++;
        }

        j++;

        // Readdir for next entry
        ep = readdir(dp);
    }

    // Update current nb. of bins files in the list
    *current_files_nb = i;

    // Close directory
    if (closedir(dp) == -1)
        goto out;

    if (ep) {
        // It's not the EOF
        *eof = 0;
        *cookie = j;
    } else {
        *eof = 1;
    }

    *iterator = NULL;
out:
    return iterator;
}

int storage_list_bins_files_to_rebuild(storage_t * st, sid_t sid,
        uint8_t * layout, sid_t *dist_set, uint8_t * spare, uint64_t * cookie,
        bins_file_rebuild_t ** children, uint8_t * eof) {

    int status = -1;
    char **p;
    size_t cnt;
    glob_t glob_results;
    uint8_t layout_it = 0;
    uint8_t spare_it = 0;
    uint64_t current_files_nb = 0;
    bins_file_rebuild_t **iterator = NULL;
    uint8_t check_dist_set = 0;

    DEBUG_FUNCTION;

    // Use iterator
    iterator = children;

    sid_t current_dist_set[ROZOFS_SAFE_MAX];
    sid_t empty_dist_set[ROZOFS_SAFE_MAX];
    memset(empty_dist_set, 0, sizeof (sid_t) * ROZOFS_SAFE_MAX);
    memcpy(current_dist_set, dist_set, sizeof (sid_t) * ROZOFS_SAFE_MAX);

    if (memcmp(current_dist_set, empty_dist_set, sizeof (sid_t) * ROZOFS_SAFE_MAX) != 0)
        check_dist_set = 1;


    // For each possible layout
    for (layout_it = *layout; layout_it < LAYOUT_MAX; layout_it++) {

        // For spare and no spare
        for (spare_it = *spare; spare_it < 2; spare_it++) {

            // Build path directory for this layout and this spare type
            char path[FILENAME_MAX];
            sprintf(path, "%s/layout_%u/spare_%u/", st->root, layout_it,
                    spare_it);

            // Go to this directory
            if (chdir(path) != 0)
                continue;

            // Build pattern for globbing
            char pattern[FILENAME_MAX];
            sprintf(pattern, "*%.3u*", sid);

            // Globbing function
            if (glob(pattern, GLOB_ONLYDIR, 0, &glob_results) == 0) {

                // For all the directories matching pattern
                for (p = glob_results.gl_pathv, cnt = glob_results.gl_pathc;
                        cnt; p++, cnt--) {

                    // Get the dist_set for this directory
                    switch (layout_it) {
                        case LAYOUT_2_3_4:
                            if (sscanf(*p, "%" SCNu8 "-%" SCNu8 "-%" SCNu8 "-"
                                    "%" SCNu8 "",
                                    &dist_set[0], &dist_set[1],
                                    &dist_set[2], &dist_set[3]) != 4) {
                                continue;
                            }
                            break;
                        case LAYOUT_4_6_8:
                            if (sscanf(*p, "%" SCNu8 "-%" SCNu8 "-%" SCNu8 "-"
                                    "%" SCNu8 "-%" SCNu8 "-%" SCNu8 "-"
                                    "%" SCNu8 "-%" SCNu8 "",
                                    &dist_set[0], &dist_set[1], &dist_set[2],
                                    &dist_set[3], &dist_set[4], &dist_set[5],
                                    &dist_set[6], &dist_set[7]) != 8) {
                                continue;
                            }
                            break;
                        case LAYOUT_8_12_16:
                            if (sscanf(*p, "%" SCNu8 "-%" SCNu8 "-%" SCNu8 "-"
                                    "%" SCNu8 "-%" SCNu8 "-%" SCNu8 "-"
                                    "%" SCNu8 "-%" SCNu8 "-%" SCNu8 "-"
                                    "%" SCNu8 "-%" SCNu8 "-%" SCNu8 "-"
                                    "%" SCNu8 "-%" SCNu8 "-%" SCNu8 "-"
                                    "%" SCNu8 "",
                                    &dist_set[0], &dist_set[1], &dist_set[2],
                                    &dist_set[3], &dist_set[4], &dist_set[5],
                                    &dist_set[6], &dist_set[7], &dist_set[8],
                                    &dist_set[9], &dist_set[10], &dist_set[11],
                                    &dist_set[12], &dist_set[13], &dist_set[14],
                                    &dist_set[15]) != 16) {
                                continue;
                            }
                            break;
                    }

                    // Check dist_set
                    if (check_dist_set) {
                        if (memcmp(current_dist_set, dist_set, sizeof (sid_t) * ROZOFS_SAFE_MAX) != 0)
                            continue;
                    }

                    check_dist_set = 0;

                    // List the bins files for this specific directory
                    if ((iterator = storage_list_bins_file(st, layout_it,
                            dist_set, spare_it, cookie, iterator, eof,
                            &current_files_nb)) == NULL) {
                        severe("storage_list_bins_file failed: %s\n",
                                strerror(errno));
                        continue;
                    }

                    // Check if EOF
                    if (0 == *eof) {
                        status = 0;
                        *spare = spare_it;
                        *layout = layout_it;
                        goto out;
                    } else {
                        *cookie = 0;
                    }

                }
                globfree(&glob_results);
            }
        }
    }

    *eof = 1;
    status = 0;

out:
    return status;
}
