#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <errno.h>
#include <unistd.h>
#include <string.h>

extern int errno;

#define _ERR_IO_READ 10
#define _ERR_IO_WRITE 11
#define _ERR_MAPPING_SOURCE 12
#define _ERR_MAPPING_DEST 13

#define _ERR_ARG_COUNT 1
#define _ERR_ARG_TYPE 2
#define _ERR_NO_PERMIT 3
#define _ERR_NO_FILE 4
#define _ERR_NO_FREE_SPACE 5
#define _ERR_FIXME 666

const mode_t no_mode = -1;

typedef int fdesc;

enum filetype {
	nofile,
	dir,
	regfile,
	nosupport
};

struct fileinfo {
	char *name;
	struct stat st;
	fdesc fd;
	enum filetype type;
};

int fcopy_mmap (struct fileinfo *source_f, struct fileinfo *dest_f);
int open_fileinfo (struct fileinfo *file_f, int flags, mode_t mode, char* err_access, char* err_nofile, char* err_exist);
int stat_fileinfo (struct fileinfo *file_f);
int file_to_file_copy (struct fileinfo *source_f, struct fileinfo *dest_f);
int file_to_dir_copy (struct fileinfo *source_f, struct fileinfo *dest_f);


int main (int argc, char *argv[])
{
	//arg checking
	if (argc != 3) {
		printf ("Usage: %s <source> <destenition>\n", argv[0]);
		return _ERR_ARG_COUNT;
	}
	int ret = 0; //common var for return value

	//sorce open
	struct fileinfo source_f = {0};
	source_f.name = argv[1];
	ret = open_fileinfo (&source_f, O_RDONLY, no_mode, "no read permittion", "no such file or directory", NULL);
	if (ret)
		return ret;
	
	//dest stat
	struct fileinfo dest_f = {0};
	dest_f.name = argv[2];
	stat_fileinfo (&dest_f);

	//non-reg file processing
	if (source_f.type != regfile) {
		//recursive copying will be somewhat here
		printf ("source is not a regular file\n");
		close (source_f.fd);
		close (dest_f.fd);
		return _ERR_FIXME;
	}
	if (dest_f.type == nosupport) {
		printf ("destination is neither regular file nor directory\n");
		close (source_f.fd);
		close (dest_f.fd);
		return _ERR_ARG_TYPE;
	}
	//here source is reg file and dest is reg file of dir
	
	//plain copying
	if (dest_f.type == regfile || dest_f.type == nofile) {
		return file_to_file_copy (&source_f, &dest_f);
	}
	
	//file to dir copying
	if (dest_f.type == dir) {
		return file_to_dir_copy (&source_f, &dest_f);
	}
	//file to dir finish

	close (source_f.fd);
	close (dest_f.fd);
	return 0;
}

int file_to_dir_copy (struct fileinfo *source_f, struct fileinfo *dest_f)
{
	int ret = 0;
	//start forming new name
	char *source_filename = strrchr (source_f->name, '/');
	if (source_filename == NULL)
		source_filename = source_f->name;
	else source_filename += 1; 
		// now source_filname contains bare name of source file
	
		//directory may end on '/' or no '/'
	char *last_slash = strrchr (dest_f->name, '/');
	if (last_slash != NULL) {
		if (*(last_slash + 1) == 0) 
			*last_slash = 0;
	}
	//now dest_f.name ends not on '/'
	
	char *dest_filename = (char *) calloc (strlen(dest_f->name) + strlen(source_filename) + 1, sizeof (char)); // +1 for slash
	strcpy (dest_filename, dest_f->name);
	strcat (dest_filename, "/");
	strcat (dest_filename, source_filename);
		//printf ("formed filename: '%s'\n", dest_filename);
	dest_f->name = dest_filename;
	//end forming new name
	
	ret = file_to_file_copy (source_f, dest_f);
	free (dest_filename);
	return ret;
}

int file_to_file_copy (struct fileinfo *source_f, struct fileinfo *dest_f)
{
	int ret = 0;
	ret = open_fileinfo (dest_f, O_RDWR | O_CREAT | O_EXCL, source_f->st.st_mode, "no write permittion", NULL, "file already exists, aborting");
	switch (ret) {
		case _ERR_NO_PERMIT:
			close (source_f->fd);
			return ret;
		case EEXIST:
			close (source_f->fd);
			return _ERR_FIXME;
		default:
			break;
	}
	ret = fcopy_mmap (source_f, dest_f);
	if (ret)
		printf ("err: fcopy () returned %d\n", ret);
	close (source_f->fd);
	close (dest_f->fd);
	return ret;
}

int open_fileinfo (struct fileinfo *file_f, int flags, mode_t mode, char* err_access, char* err_nofile, char* err_exist)
{
	if (file_f->name == NULL)
		return -1;
	if (mode == no_mode)
		file_f->fd = open (file_f->name, flags);
	else
		file_f->fd = open (file_f->name, flags, mode);
	switch (errno) {
		case EACCES:
			if (err_access != NULL)
				printf ("'%s': %s\n", file_f->name, err_access);
			return _ERR_NO_PERMIT;
		case EDQUOT:
			printf ("no free space left\n");
			return _ERR_NO_FREE_SPACE;
		case EISDIR:
			file_f->type = dir;
			return EISDIR;
		case EEXIST:
			if (err_exist != NULL)
				printf ("'%s': %s\n", file_f->name, err_exist);
			return EEXIST;
	}
	if (file_f->fd  == -1) {
		if (err_nofile != NULL) 
			printf ("'%s': %s\n", file_f->name, err_nofile);
		return _ERR_NO_FILE;
	}
	
	//stat_filenifo (file_f); - эквивалентно всему что ниже но дольше

	fstat (file_f->fd, &(file_f->st));
	switch (file_f->st.st_mode & S_IFMT) {
		case S_IFREG:
			file_f->type = regfile;
			break;
		case S_IFDIR:
			file_f->type = dir;
			break;
		default:
			file_f->type = nosupport;
			break;
	}
	return 0;
}

int stat_fileinfo (struct fileinfo *file_f)
{
	if (file_f->name == NULL)
		return -1;
	if (stat (file_f->name, &(file_f->st))) {
		file_f->type = nofile;
		return _ERR_NO_FILE;	
	}
	switch (file_f->st.st_mode & S_IFMT) {
		case S_IFREG:
			file_f->type = regfile;
			break;
		case S_IFDIR:
			file_f->type = dir;
			break;
		default:
			file_f->type = nosupport;
			break;
	}
	return 0;
}

/*
int fcopy_calloc (struct fileinfo *source_f, struct fileinfo *dest_f, off_t bs)
{
	//basically copies file@fd to file@fd 
	
	#define SOURCE_SIZE source_f->st.st_size
	#define DEST_SIZE dest_f->st.st_size

	if (SOURCE_SIZE < bs) bs = SOURCE_SIZE;

	char *buffer = (char *) calloc (bs, sizeof (char));
	size_t read_bytes = 0;
	while ((read_bytes = read (source_f->fd, buffer, bs)) != 0) {
		//unimportant
		switch (errno) {
			case EIO:
				free (buffer);
				return _ERR_IO_READ;
		}

		write (dest_f->fd, buffer, read_bytes);
		//unimportant
		switch (errno) {
			case EIO:
				free (buffer);
				return _ERR_IO_WRITE;
		}
	}
	free (buffer);

	#undef SOURCE_SIZE
	#undef DEST_SIZE
	return 0;
}
*/

int fcopy_mmap (struct fileinfo *source_f, struct fileinfo *dest_f)
{
	/* basically copies file@fd to file@fd */
	
	#define SOURCE_SIZE source_f->st.st_size
	#define DEST_SIZE dest_f->st.st_size

	char *source_mmap = NULL;
	if ((source_mmap = (char *) mmap (NULL, SOURCE_SIZE, PROT_READ, MAP_SHARED, source_f->fd, 0)) == MAP_FAILED)
		return _ERR_MAPPING_SOURCE;

	if ((lseek (dest_f->fd, SOURCE_SIZE - 1, SEEK_SET) < 0) || (write (dest_f->fd, "", 1) != 1)) //set output file size
		return _ERR_IO_WRITE;

	char *dest_mmap = NULL;
	if ((dest_mmap = (char *) mmap (NULL, SOURCE_SIZE, PROT_WRITE, MAP_SHARED, dest_f->fd, 0)) == MAP_FAILED)
		return _ERR_MAPPING_DEST;
	memcpy (dest_mmap, source_mmap, SOURCE_SIZE);

	#undef SOURCE_SIZE
	#undef DEST_SIZE
	return 0;
}