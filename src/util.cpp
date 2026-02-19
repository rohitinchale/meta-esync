/*
 * util.c
 */
#include <unistd.h>
#include <errno.h>
#include <sys/wait.h>
#include <stdlib.h>
#include <libgen.h>
#include "util.h"
#include <dirent.h>
#include <sys/stat.h>
#include <openssl/md5.h>

#define BUFFER_SIZE 4096


int f_is_dir(char* filename)
{
	struct stat buf;
	int ret = stat(filename,&buf);

	if (0 == ret) {
		if (S_ISDIR(buf.st_mode)) {
			return 0;
		} else {
			return 1;
		}
	}

	return -1;
}

int remove_dir(const char* dirname)
{
	char chBuf[257];
	DIR* dir = NULL;
	const struct dirent* ptr;
	int ret = 0;

	A_INFO_MSG("removing %s \n", dirname);
	dir = opendir(dirname);

	if (NULL == dir) {
		return -1;
	}

	while ((ptr = readdir(dir)) != NULL) {
		ret = strcmp(ptr->d_name, ".");
		if (0 == ret) {
			continue;
		}

		ret = strcmp(ptr->d_name, "..");
		if (0 == ret) {
			continue;
		}

		snprintf(chBuf, sizeof(chBuf), "%s/%s", dirname, ptr->d_name);
		ret = f_is_dir(chBuf);
		if (0 == ret) {
			ret = remove_dir(chBuf);
			if (0 != ret) {
				continue;
			}
		} else if (1 == ret) {
			ret = remove(chBuf);
			if (0 != ret) {
				continue;
			}
		}
	}

	closedir(dir);
	ret = remove(dirname);

	if (0 != ret) {
		return -1;
	}

	return 0;
}

int xl4_run_cmd(char* argv[])
{
	int rc     = E_UA_OK;
	int status = 0;

	if (argv) {
		char* cmd  = 0;
		cmd = argv[0];
		pid_t pid=fork();

		if ( pid == -1) {
			A_INFO_MSG("fork failed: %s", strerror(errno));
			rc = E_UA_SYS;
		}else if ( pid == 0) {
			execvp(cmd, argv);
			A_INFO_MSG("execvp %s failed: %s", cmd, strerror(errno));
			rc = E_UA_SYS;
		}else {
			if (waitpid(pid, &status, 0) == -1) {
				A_INFO_MSG("waitpid failed: %s", strerror(errno));
				rc = E_UA_SYS;
			}else {
				rc = WEXITSTATUS(status);
				if (rc)
					A_INFO_MSG("command(%s) exited with status: %d", cmd, rc);
			}
		}
	}else{
		rc = E_UA_SYS;
	}
	return rc;
}

int copy_file(const char* src, const char* dest) {
    FILE *fsrc = fopen(src, "rb");
    FILE *fdest = fopen(dest, "wb");
    if (!fsrc || !fdest) return -1;

    char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), fsrc)) > 0) {
        if (fwrite(buf, 1, n, fdest) != n) {
            fclose(fsrc);
            fclose(fdest);
            return -1;
        }
    }

    fclose(fsrc);
    fclose(fdest);
    return 0;
}

int compare_md5sum(const char* src, const char* dest)
{
	unsigned char* src_md5 = static_cast<unsigned char*>(calloc(1, MD5_DIGEST_LENGTH));

	if (!src_md5)
	{
		ERR("Memory allocation failed for source MD5 buffer");
		return E_UA_ERR;
	}

	unsigned char* dest_md5 = static_cast<unsigned char*>(calloc(1, MD5_DIGEST_LENGTH));

	if (!dest_md5)
	{
		ERR("Memory allocation failed for destination MD5 buffer");
		free(src_md5);
		return E_UA_ERR;
	}

	if (calculate_md5sum(src, src_md5))
	{
		ERR("Failed to calculate MD5 checksum for source file");
		free(src_md5);
		free(dest_md5);
		return E_UA_ERR;
	}

	if (calculate_md5sum(dest, dest_md5))
	{
		ERR("Failed to calculate MD5 checksum for destination file");
		free(src_md5);
		free(dest_md5);
		return E_UA_ERR;
	}

	if (memcmp(src_md5, dest_md5, MD5_DIGEST_LENGTH))
	{
		ERR("MD5 checksum mismatch: source and destination files differ");
		free(src_md5);
		free(dest_md5);
		return E_UA_ERR;
	}

	free(src_md5);
	free(dest_md5);
	return E_UA_OK;
}


int calculate_md5sum(const char *file_path, unsigned char *md5_sum)
{
    FILE *file = fopen(file_path, "rb");
    if (!file)
    {
        ERR("Failed to open file : %s", file_path);
        return -1;
    }

    MD5_CTX md5_ctx;
    if (MD5_Init(&md5_ctx) != 1)
    {
        ERR("Failed to initialize MD5 context\n");
        fclose(file);
        return -1;
    }

    unsigned char buffer[BUFFER_SIZE];

    size_t bytes_read;
    while ((bytes_read = fread(buffer, 1, BUFFER_SIZE, file)) > 0)
    {
        if (MD5_Update(&md5_ctx, buffer, bytes_read) != 1)
        {
            ERR("Failed to update MD5 checksum\n");
            fclose(file);
            return -1;
        }
    }

    if (ferror(file))
    {
        ERR("Error reading file.\n");
        fclose(file);
        return -1;
    }

    if (MD5_Final(md5_sum, &md5_ctx) != 1)
    {
        ERR("Failed to finalize MD5 checksum.\n");
        fclose(file);
        return -1;
    }

    fclose(file);
    return 0;
}
