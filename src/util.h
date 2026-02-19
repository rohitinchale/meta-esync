/*
 * util.h
 */
#ifndef UTIL_H
#define UTIL_H

#ifdef __cplusplus
extern "C" {
#endif
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include <stdio.h>
#if HAVE_LIMITS_H
#include <linux/limits.h>
#endif

#ifdef LIBUA_VER_2_0
#include "esyncua.h"
#else
#include "xl4ua.h"
#endif //LIBUA_VER_2_0

#include <xl4clib/loggit.h>

#define PATH_MAX 4096

extern int ua_debug_lvl;

static inline const char* cut_path(const char* path)
{
	const char* aux = strrchr(path, '/');

	if (aux) { return aux + 1; }
	return path;
}

#define _ltime_ \
        char __now[24]; \
        struct tm __tmnow; \
        struct timeval __tv; \
        memset(__now, 0, 24); \
        gettimeofday(&__tv, 0); \
        localtime_r(&__tv.tv_sec, &__tmnow); \
        strftime(__now, 20, "%m-%d:%H:%M:%S.", &__tmnow); \
        sprintf(__now+15, "%03d", (int)(__tv.tv_usec/1000))

#define A_INFO_MSG_(fmt, a ...) do { \
					    _ltime_; \
					    printf("[%s] %s:%d " fmt "\n", __now, cut_path(__FILE__), __LINE__, ## a); \
					    fflush(stdout); \
				    } while (0);



#define A_INFO_MSG A_INFO_MSG_

int remove_dir(const char* dir);
int xl4_run_cmd(char* argv[]);
int copy_file(const char* src, const char* dest);
int compare_md5sum(const char* src, const char* dest);
int calculate_md5sum(const char *file_path, unsigned char *md5_sum);
int f_is_dir(char* filename);

#ifdef __cplusplus
}
#endif

#endif /* UTIL_H */
