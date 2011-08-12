/*
 * Copyright (C) 1998-9 Pancrazio `Ezio' de Mauro <p@demauro.net>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 * $Id: installwatch.c,v 0.7.0.8 2006/11/01 06:51:39 izto Exp $
 * 
 * april-15-2001 - Modifications by Felipe Eduardo Sanchez Diaz Duran
 *                                  <izto@asic-linux.com.mx>
 * Added backup() and make_path() functions.
 *
 * november-25-2002 - Modifications by Olivier Fleurigeon
 *                                  <olivier.fleurigeon@cegedim.fr>
 *
 * august-04-2011 - Modifications by Cheng Sheng
 *                                   <jeru.sheng@gmail.com>
 * Port to Darwin.
 */

#include "config.h"

#ifdef HAVE_SYS_PARAM_H
#include <sys/param.h>
#endif

#ifdef HAVE_SYS_TYPES_H
#include <sys/types.h>
#endif

#ifdef HAVE_SYS_STAT_H
#include <sys/stat.h>
#endif

#ifdef HAVE_FCNTL_H
#include <fcntl.h>
#endif

#ifdef HAVE_STDARG_H
#include <stdarg.h>
#endif

#ifdef HAVE_STDIO_H
#include <stdio.h>
#endif

#ifdef HAVE_STDLIB_H
#include <stdlib.h>
#endif

#ifdef HAVE_DLFCN_H
#include <dlfcn.h>
#endif

#ifdef HAVE_SYSLOG_H
#include <syslog.h>
#endif

#ifdef HAVE_ERRNO_H
#include <errno.h>
#endif

#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif

#ifdef HAVE_LIBGEN_H
#include <libgen.h>
#endif

#undef basename
#ifdef HAVE_STRING_H
#include <string.h>
#endif

#ifdef HAVE_TIME_H
#include <time.h>
#endif

#ifdef HAVE_UTIME_H
#include <utime.h>
#endif

#ifdef HAVE_DIRENT_H
#include <dirent.h>
#endif

#define LOGLEVEL (LOG_USER | LOG_INFO | LOG_PID)
#define BUFSIZE 1024

#define error(X) (X < 0 ? strerror(errno) : "success")

int __installwatch_refcount = 0;
int __installwatch_timecount = 0;

#define REFCOUNT __installwatch_refcount++
#define TIMECOUNT __installwatch_timecount++

static void *libc_handle=NULL;
static void initialize(void);

  /* A few defines to fix things a little */
#define INSTW_OK 0 
  /* If not set, no work with translation is allowed */
#define INSTW_INITIALIZED	(1<<0)
  /* If not set, a wrapped function only do its "real" job */
#define INSTW_OKWRAP		(1<<1)
#define INSTW_OKBACKUP		(1<<2)
#define INSTW_OKTRANSL		(1<<3)

#define INSTW_TRANSLATED	(1<<0)
  /* Indicates that a translated file is identical to original */
#define INSTW_IDENTITY  	(1<<1)

  /* The file currently exists in the root filesystem */
#define INSTW_ISINROOT		(1<<6)
  /* The file currently exists in the translated filesystem */
#define INSTW_ISINTRANSL	(1<<7)

#define _BACKUP "/BACKUP"
#define _TRANSL "/TRANSL"

  /* The root that contains all the needed metas-infos */
#define _META   "/META"
  /* We store under this subtree the translated status */
#define _MTRANSL _TRANSL
  /* We construct under this subtree fake directory listings */
#define _MDIRLS  "/DIRLS"  

  /* String cell used to chain excluded paths */
typedef struct string_t string_t;
struct string_t {
	char *string;
	string_t *next;
};	

  /* Used to keep all infos needed to cope with backup, translated fs... */
typedef struct instw_t {
	  /*
	   * global fields 
	   */
	int gstatus;
	int dbglvl;
	pid_t pid;
	char *root;
	char *backup;
	char *transl;
	char *meta;
	char *mtransl;
	char *mdirls;
	  /* the list of all the paths excluded from translation */
	string_t *exclude;
	
	  /*
	   * per instance fields
	   */
	int error;
	int status;
	  /* the public path, hiding translation */
	char path[PATH_MAX+1];
	  /* the public resolved path, hiding translation */
	char reslvpath[PATH_MAX+1];  
	  /* the real resolved path, exposing tranlsation */
	char truepath[PATH_MAX+1];
	  /* the real translated path */
	char translpath[PATH_MAX+1];
	  /* the list of all the equiv paths conducing to "reslvpath" */
	string_t *equivpaths;  
	  /* the real path used to flag translation status */
	char mtranslpath[PATH_MAX+1];
	  /* the path given to a wrapped opendir */
	char mdirlspath[PATH_MAX+1];
} instw_t;

static instw_t __instw;

static int canonicalize(const char *,char *);
static int make_path(const char *);
static int copy_path(const char *,const char *);
static inline int path_excluded(const char *);
static int unlink_recursive(const char *);

int expand_path(string_t **,const char *,const char *);
int parse_suffix(char *,char *,const char *);

  /* a lazy way to avoid sizeof */
#define mallok(T,N)  (T *)malloc((N)*sizeof(T))
  /* single method used to minimize excessive returns */
#define finalize(code) {rcod=code;goto finalize;}

#ifndef NDEBUG
static int __instw_printdirent(struct dirent*);
#ifdef INSTW_USE_LARGEFILE64
static int __instw_printdirent64(struct dirent64*);
#endif
#endif

#ifdef DEBUG
static int instw_print(instw_t *);
#endif
static int instw_init(void);
static int instw_fini(void);

static int instw_new(instw_t *);
static int instw_delete(instw_t *);

  /* references a translated file in /mtransl */
static int instw_setmetatransl(instw_t *);

static int instw_setpath(instw_t *,const char *);
static int instw_getstatus(instw_t *,int *);
static int instw_apply(instw_t *);
static int instw_filldirls(instw_t *);
static int instw_makedirls(instw_t *);

static int backup(const char *);

static int vlambda_log(const char *logname,const char *format,va_list ap);

/*
static int lambda_log(const char *logname,const char *format,...)
#ifdef __GNUC__
	__attribute__((format(printf,2,3)))
#endif 
;
*/

static inline int instw_log(const char *format,...)
#ifdef __GNUC__
	/* Tell gcc that this function behaves like printf()
	 * for parameters 1 and 2                            */
	__attribute__((format(printf, 1, 2)))
#endif /* defined __GNUC__ */
;

static inline int debug(int dbglvl,const char *format,...)
#ifdef __GNUC__
	__attribute__((format(printf, 2, 3)))
#endif 
;

#define unset_okwrap() (__instw.gstatus &= ~INSTW_OKWRAP)
#define reset_okwrap() (__instw.gstatus |= INSTW_OKWRAP)

/*
 * Injected functions: original function pointers.
 */
#ifdef HAVE_ACCESS
static int (*true_access)(const char *, int);
#endif
#ifdef HAVE_CHDIR
static int (*true_chdir)(const char *);
#endif
#ifdef HAVE_CHMOD
static int (*true_chmod)(const char *, mode_t);
#endif
#ifdef HAVE_CHOWN
static int (*true_chown)(const char *, uid_t, gid_t);
#endif
#ifdef HAVE_CHROOT
static int (*true_chroot)(const char *);
#endif
#ifdef HAVE_CREAT
static int (*true_creat)(const char *, mode_t);
#endif
#ifdef HAVE_CREAT64
static int (*true_creat64)(const char *, __mode_t);
#endif
#ifdef HAVE_FCHMOD
static int (*true_fchmod)(int, mode_t);
#endif
#ifdef HAVE_FCHOWN
static int (*true_fchown)(int, uid_t, gid_t);
#endif
#ifdef HAVE_FOPEN
static FILE *(*true_fopen)(const char *,const char*);
#endif
#ifdef HAVE_FOPEN64
static FILE *(*true_fopen64)(const char *,const char *);
#endif
#ifdef HAVE_FTRUNCATE
static int (*true_ftruncate)(int, off_t);
#endif
#ifdef HAVE_FTRUNCATE64
static int (*true_ftruncate64)(int, __off64_t);
#endif
#ifdef HAVE_GETCWD
static char *(*true_getcwd)(char*,size_t);
#endif
#ifdef HAVE_LCHOWN
static int (*true_lchown)(const char *, uid_t, gid_t);
#endif
#ifdef HAVE_LINK
static int (*true_link)(const char *, const char *);
#endif
#ifdef HAVE_LSTAT
static int (*true_lstat)(const char *,struct stat *);
#endif
#ifdef HAVE_LSTAT64
static int (*true_lstat64)(const char *, struct stat64 *);
#endif
#ifdef HAVE_MKDIR
static int (*true_mkdir)(const char *, mode_t);
#endif
#ifdef HAVE_MKNOD
static int (*true_mknod)(const char *, mode_t, dev_t);
#endif
#ifdef HAVE_OPEN
static int (*true_open)(const char *, int, ...);
#endif
#ifdef HAVE_OPEN64
static int (*true_open64)(const char *, int, ...);
#endif
#ifdef HAVE_OPENDIR
static DIR *(*true_opendir)(const char *);
#endif
#ifdef HAVE_READDIR
static struct dirent *(*true_readdir)(DIR *dir);
#endif
#ifdef HAVE_READDIR64
static struct dirent64 *(*true_readdir64)(DIR *dir);
#endif
#ifdef HAVE_READLINK
static int (*true_readlink)(const char*,char *,size_t);
#endif
#ifdef HAVE_REALPATH
static char *(*true_realpath)(const char *,char *);
#endif
#ifdef HAVE_RENAME
static int (*true_rename)(const char *, const char *);
#endif
#ifdef HAVE_RMDIR
static int (*true_rmdir)(const char *);
#endif
#ifdef HAVE_SCANDIR
static int (*true_scandir)(	const char *,struct dirent ***,
				int (*)(const struct dirent *),
				int (*)(const void *,const void *));
#endif
#ifdef HAVE_SCANDIR64
static int (*true_scandir64)(	const char *,struct dirent64 ***,
				int (*)(const struct dirent64 *),
				int (*)(const void *,const void *));
#endif
#ifdef HAVE_STAT
static int (*true_stat)(const char *,struct stat *);
#endif
#ifdef HAVE_STAT64
static int (*true_stat64)(const char *, struct stat64 *);
#endif
#ifdef HAVE_SYMLINK
static int (*true_symlink)(const char *, const char *);
#endif
#ifdef HAVE_TIME
static time_t (*true_time) (time_t *);
#endif
#ifdef HAVE_TRUNCATE
static int (*true_truncate)(const char *, off_t);
#endif
#ifdef HAVE_TRUNCATE64
static int (*true_truncate64)(const char *, __off64_t);
#endif
#ifdef HAVE_UNLINK
static int (*true_unlink)(const char *);
#endif
#ifdef HAVE_UTIME
static int (*true_utime)(const char *,const struct utimbuf *);
#endif
#ifdef HAVE_UTIMES
static int (*true_utimes)(const char *,const struct timeval *);
#endif
#ifdef HAVE___FXSTAT
static int (*true___fxstat)(int,int fd,struct stat *);
#endif
#ifdef HAVE___LXSTAT
static int (*true___lxstat)(int,const char *,struct stat *);
#endif
#ifdef HAVE___LXSTAT64
static int (*true___lxstat64)(int,const char *, struct stat64 *);
#endif
#ifdef HAVE___XMKNOD
static int (*true___xmknod)(int ver,const char *, mode_t, dev_t *);
#endif
#ifdef HAVE___XSTAT
static int (*true___xstat)(int,const char *,struct stat *);
#endif
#ifdef HAVE___XSTAT64
static int (*true___xstat64)(int,const char *, struct stat64 *);
#endif

/*
 * Some utility functions of "__instw".
 */
static inline bool instw__in_real_mode() {
	return !(__instw.gstatus & INSTW_INITIALIZED)
		|| !(__instw.gstatus & INSTW_OKWRAP);
}

static inline bool instw__ensure_init() {
	if (!libc_handle) initialize();
}

/*
 * Injected functions implementation.
 */

#ifdef HAVE_ACCESS
int access (const char *pathname, int type) {
       int result;
       instw_t instw;

       instw__ensure_init();

#ifndef NDEBUG
       debug(2,"access(%s,%d)\n",pathname,type);
#endif

       /* We were asked to work in "real" mode */
       if (instw__in_real_mode())
	       return true_access(pathname, type);

       instw_new(&instw);
       instw_setpath(&instw,pathname);

#ifndef NDEBUG
       instw_print(&instw);
#endif

       backup(instw.truepath);
       instw_apply(&instw);

       result=true_access(instw.translpath,type);
       instw_log("%d\taccess\t%s\t#%s\n",result,instw.reslvpath,error(result));

       instw_delete(&instw);

       return result;
}
#endif  /* HAVE_ACCESS */

#ifdef HAVE_CHDIR
int chdir(const char *pathname) {
	int result;
	instw_t instw;
	int status;

	instw__ensure_init();

#ifndef NDEBUG
	debug(2,"chdir(%s)n",pathname);
#endif

	/* We were asked to work in "real" mode */
       if (instw__in_real_mode())
	       return true_chdir(pathname);

	instw_new(&instw);
	instw_setpath(&instw,pathname);
	instw_getstatus(&instw,&status);

	if(status&INSTW_TRANSLATED && !(status&INSTW_ISINROOT)) {
		result=true_chdir(instw.translpath);
		debug(3,"\teffective chdir(%s)\n",instw.translpath);
	} else {
		result=true_chdir(pathname);
		debug(3,"\teffective chdir(%s)\n",pathname);
	}

	instw_delete(&instw);

	return result;
}
#endif  /* HAVE_CHDIR */

#ifdef HAVE_CHMOD
int chmod(const char *path, mode_t mode) {
	int result;
	instw_t instw;

	REFCOUNT;
	
	instw__ensure_init();

#ifndef NDEBUG
	debug(2,"chmod(%s,mode)\n",path);
#endif

	/* We were asked to work in "real" mode */
	if (instw__in_real_mode())
		return true_chmod(path,mode);

	instw_new(&instw);
	instw_setpath(&instw,path);

#ifndef NDEBUG
	instw_print(&instw);
#endif

	backup (instw.truepath);
	instw_apply(&instw);

	result = true_chmod(instw.translpath, mode);
	instw_log("%d\tchmod\t%s\t0%04o\t#%s\n",result,
	    instw.reslvpath,mode,error(result));

	instw_delete(&instw);

	return result;
}
#endif  /* HAVE_CHMOD */

#ifdef HAVE_CHOWN
int chown(const char *path, uid_t owner, gid_t group) {
	int result;
	instw_t instw;

	REFCOUNT;

	instw__ensure_init();

#ifndef NDEBUG
	debug(2,"chown(%s,owner,group)\n",path);
#endif

	/* We were asked to work in "real" mode */
	if (instw__in_real_mode())
		return true_chown(path,owner,group);

	instw_new(&instw);
	instw_setpath(&instw,path);

#ifndef NDEBUG
	instw_print(&instw);
#endif

	backup(instw.truepath);
	instw_apply(&instw);

	result=true_chown(instw.translpath,owner,group);
	instw_log("%d\tchown\t%s\t%d\t%d\t#%s\n",result,
	    instw.reslvpath,owner,group,error(result));

	instw_delete(&instw);

	return result;
}
#endif  /* HAVE_CHOWN */

#ifdef HAVE_CHROOT
int chroot(const char *path) {
	int result;
	char canonic[MAXPATHLEN];

	REFCOUNT;

	instw__ensure_init();

#ifndef NDEBUG
	debug(2,"chroot(%s)\n",path);
#endif

	canonicalize(path, canonic);
	result = true_chroot(path);
	  /*
	   * From now on, another log file will be written if 
	   * INSTW_LOGFILE is set                          
	   */
	instw_log("%d\tchroot\t%s\t#%s\n", result, canonic, error(result));
	return result;
}
#endif  /* HAVE_CHROOT */

#ifdef HAVE_CREAT
int creat(const char *pathname, mode_t mode) {
/* Is it a system call? */
	int result;
	instw_t instw;
	
	REFCOUNT;

	instw__ensure_init();

#ifndef NDEBUG
	debug(2,"creat(%s,mode)\n",pathname);
#endif
	
	/* We were asked to work in "real" mode */
	if (instw__in_real_mode())
		return true_creat(pathname,mode);

	instw_new(&instw);
	instw_setpath(&instw,pathname);

#ifndef NDEBUG
	instw_print(&instw);
#endif

	backup(instw.truepath);
	instw_apply(&instw);

	result = true_open(instw.translpath,O_CREAT|O_WRONLY|O_TRUNC,mode);
	instw_log("%d\tcreat\t%s\t#%s\n",result,instw.reslvpath,error(result));

	instw_delete(&instw);

	return result;
}
#endif  /* HAVE_CREAT */

#ifdef HAVE_CREAT64
int creat64(const char *pathname, __mode_t mode) {
/* Is it a system call? */
	int result;
	instw_t instw;

	REFCOUNT;

	instw__ensure_init();

#ifndef NDEBUG
	debug(2,"creat64(%s,mode)\n",pathname);
#endif

	/* We were asked to work in "real" mode */
	if (instw__in_real_mode())
		return true_creat64(pathname,mode);

	instw_new(&instw);
	instw_setpath(&instw,pathname);

#ifndef NDEBUG
	instw_print(&instw);
#endif

	backup(instw.truepath);
	instw_apply(&instw);

	result=true_open64(instw.translpath,O_CREAT | O_WRONLY | O_TRUNC, mode);
	instw_log("%d\tcreat\t%s\t#%s\n",result,instw.reslvpath,error(result));

	instw_delete(&instw);

	return result;
}
#endif  /* HAVE_CREAT64 */

#ifdef HAVE_FCHMOD
int fchmod(int filedes, mode_t mode) {
	int result;

	REFCOUNT;

	instw__ensure_init();

#ifndef NDEBUG
	debug(2,"fchmod\n");
#endif

	result = true_fchmod(filedes, mode);
	instw_log("%d\tfchmod\t%d\t0%04o\t#%s\n",result,filedes,mode,error(result));
	return result;
}
#endif  /* HAVE_FCHMOD */

#ifdef HAVE_FCHOWN
int fchown(int fd, uid_t owner, gid_t group) {
	int result;

	REFCOUNT;

	instw__ensure_init();

#ifndef NDEBUG
	debug(2,"fchown\n");
#endif

	result = true_fchown(fd, owner, group);
	instw_log("%d\tfchown\t%d\t%d\t%d\t#%s\n",result,fd,owner,group,error(result));
	return result;
}
#endif  /* HAVE_FCHOWN */

#ifdef HAVE_FOPEN
FILE *fopen(const char *pathname, const char *mode) {
	FILE *result;
	instw_t instw;
	int status=0;

	REFCOUNT;

	instw__ensure_init();

#ifndef NDEBUG
	debug(2,"fopen(%s,%s)\n",pathname,mode);
#endif

	  /* We were asked to work in "real" mode */
	if (instw__in_real_mode())
		return true_fopen(pathname,mode);

	instw_new(&instw);
	instw_setpath(&instw,pathname);
	
#ifndef NDEBUG
	instw_print(&instw);
#endif

	if(mode[0]=='w'||mode[0]=='a'||mode[1]=='+') {
		backup(instw.truepath);
		instw_apply(&instw);
		instw_log("%d\tfopen\t%s\t#%s\n",(int)result,
		    instw.reslvpath,error(result));
	}

	instw_getstatus(&instw,&status);
	
	if(status&INSTW_TRANSLATED) {
		debug(4,"\teffective fopen(%s)",instw.translpath);
		result=true_fopen(instw.translpath,mode); 
	} else {
		debug(4,"\teffective fopen(%s)",instw.path);
		result=true_fopen(instw.path,mode);
	}
	
	if(mode[0]=='w'||mode[0]=='a'||mode[1]=='+') 
		instw_log("%d\tfopen\t%s\t#%s\n",(int)result,
		    instw.reslvpath,error(result));

	instw_delete(&instw);

	return result;
}
#endif  /* HAVE_FOPEN */

#ifdef HAVE_FOPEN64
FILE *fopen64(const char *pathname, const char *mode) {
	FILE *result;
	instw_t instw;
	int status;

	REFCOUNT;

	instw__ensure_init();

#ifndef NDEBUG
	debug(2,"fopen64(%s,%s)\n",pathname,mode);
#endif
	
	/* We were asked to work in "real" mode */
	if (instw__in_real_mode())
		return true_fopen64(pathname,mode);

	instw_new(&instw);
	instw_setpath(&instw,pathname);
	
#ifndef NDEBUG
	instw_print(&instw);
#endif

	if(mode[0]=='w'||mode[0]=='a'||mode[1]=='+') {
		backup(instw.truepath);
		instw_apply(&instw);
	}

	instw_getstatus(&instw,&status);
	
	if(status&INSTW_TRANSLATED) {
		debug(4,"\teffective fopen64(%s)",instw.translpath);
		result=true_fopen64(instw.translpath,mode); 
	} else {
		debug(4,"\teffective fopen64(%s)",instw.path);
		result=true_fopen64(instw.path,mode);
	}

	if(mode[0]=='w'||mode[0]=='a'||mode[1]=='+') 
		instw_log("%d\tfopen64\t%s\t#%s\n",(int)result,
		    instw.reslvpath,error(result));

	instw_delete(&instw);

	return result;
}
#endif  /* HAVE_FOPEN64 */

#ifdef HAVE_FTRUNCATE
int ftruncate(int fd, off_t length) {
	int result;

	REFCOUNT;

	instw__ensure_init();

#ifndef NDEBUG
	debug(2,"ftruncate\n");
#endif

	result = true_ftruncate(fd, length);
	instw_log("%d\tftruncate\t%d\t%d\t#%s\n",result,fd,(int)length,error(result));
	return result;
}
#endif  /* HAVE_FTRUNCATE */

#ifdef HAVE_FTRUNCATE64
int ftruncate64(int fd, __off64_t length) {
	int result;

	REFCOUNT;

	instw__ensure_init();

#ifndef NDEBUG
	debug(2,"ftruncate64\n");
#endif

	result = true_ftruncate64(fd, length);
	instw_log("%d\tftruncate\t%d\t%d\t#%s\n",result,fd,(int)length,error(result));
	return result;
}
#endif  /* HAVE_FTRUNCATE64 */

#ifdef HAVE_GETCWD
char *getcwd(char *buffer,size_t size) {
	char wpath[PATH_MAX+1];
	char *result;
	char *wptr;
	size_t wsize;

	instw__ensure_init();

#ifndef NDEBUG
	debug(2,"getcwd(%p,%ld)\n",buffer,(long int)size);
#endif

	/* We were asked to work in "real" mode */
	if (instw__in_real_mode())
		return true_getcwd(buffer,size);

	if(	__instw.gstatus&INSTW_INITIALIZED &&
		__instw.gstatus&INSTW_OKTRANSL && 
		(NULL!=(result=true_getcwd(wpath,sizeof(wpath)))) ) {
		  /* we untranslate any translated path */
		if(strstr(wpath,__instw.transl)==wpath)	{
			wptr=wpath+strlen(__instw.transl);
			wsize=strlen(wptr)+1;
		} else {
			wptr=wpath;
			wsize=strlen(wptr)+1;
		}

		if (buffer == NULL) {
			if (size !=0 && size < wsize) {
				result=NULL;
				errno=(size<=0?EINVAL:ERANGE);
			} else {
				result=malloc(wsize);
				if(result == NULL) {
					errno=ENOMEM;
				} else {
					strcpy(result,wptr);
				}
			}
		} else {
			if(size>=wsize) {
				strcpy(buffer,wptr);
			} else {
				result=NULL;
				errno=(size<=0?EINVAL:ERANGE);
			}
		}
	} else  {
		result=true_getcwd(buffer,size);
	}

#ifndef NDEBUG
	debug(3,"\teffective getcwd(%s,%ld)\n",
	      (result?buffer:"(null)"),(long int)size);
#endif	

	return result;
}
#endif  /* HAVE_GETCWD */

#ifdef HAVE_LCHOWN
int lchown(const char *path, uid_t owner, gid_t group) {
/* Linux specific? */
	int result;
	instw_t instw;

	REFCOUNT;

	instw__ensure_init();

#ifndef NDEBUG
	debug(2,"lchown(%s,owner,group)\n",path);
#endif

	/* We were asked to work in "real" mode */
	if (instw__in_real_mode())
		return true_lchown(path,owner,group);

	instw_new(&instw);
	instw_setpath(&instw,path);
	
#ifndef NDEBUG
	instw_print(&instw);
#endif

	backup(instw.truepath);
	instw_apply(&instw);

	result=true_lchown(instw.translpath,owner,group);
	instw_log("%d\tlchown\t%s\t%d\t%d\t#%s\n",result,
	    instw.reslvpath,owner,group,error(result));
	    
	instw_delete(&instw);

	return result;
}
#endif  /* HAVE_LCHOWN */

#ifdef HAVE_LINK
int link(const char *oldpath, const char *newpath) {
	int result;
	instw_t instw_o;
	instw_t instw_n;

	REFCOUNT;

	instw__ensure_init();

#ifndef NDEBUG
	debug(2,"link(%s,%s)\n",oldpath,newpath);
#endif

	/* We were asked to work in "real" mode */
	if (instw__in_real_mode())
		return true_link(oldpath,newpath);

	instw_new(&instw_o);
	instw_new(&instw_n);
	instw_setpath(&instw_o,oldpath);
	instw_setpath(&instw_n,newpath);

#ifndef NDEBUG
	instw_print(&instw_o);
	instw_print(&instw_n);
#endif

	backup(instw_o.truepath);
	instw_apply(&instw_o);
	instw_apply(&instw_n);
	
	result=true_link(instw_o.translpath,instw_n.translpath);
	instw_log("%d\tlink\t%s\t%s\t#%s\n",result,
	    instw_o.reslvpath,instw_n.reslvpath,error(result));
	    
	instw_delete(&instw_o);
	instw_delete(&instw_n);

	return result;
}
#endif  /* HAVE_LINK */

#ifdef HAVE_LSTAT

#   ifdef HAVE___LXSTAT
int true_lstat_impl(const char *pathname,struct stat *info) {
	return true___lxstat(_STAT_VER,pathname,info);
}
#   endif

int lstat(const char *pathname,struct stat *info)
{
	int result;
	instw_t instw;
	int status;

	instw__ensure_init();

#ifndef NDEBUG
	debug(2,"lstat(%s,%p)\n",pathname,info);
#endif

	/* We were asked to work in "real" mode */
	if (instw__in_real_mode())
		return true_lstat(pathname,info);

	instw_new(&instw);
	instw_setpath(&instw,pathname);
	instw_getstatus(&instw,&status);

#ifndef NDEBUG
	instw_print(&instw);
#endif

	if(status&INSTW_TRANSLATED) {
		debug(4,"\teffective lstat(%s,%p)\n",
		      instw.translpath,info);
		result=true_lstat(instw.translpath,info);
	} else {
		debug(4,"\teffective lstat(%s,%p)\n",
		      instw.path,info);
		result=true_lstat(instw.path,info);
	}

	instw_delete(&instw);

	return result;	
}
#endif  /* HAVE_LSTAT */

#ifdef HAVE_LSTAT64

#   ifdef HAVE___LXSTAT64
int true_lstat64_impl(const char *pathname,struct stat64 *info) {
	return true___lxstat64(_STAT_VER,pathname,info);
}
#   endif

int lstat64(const char *pathname,struct stat64 *info) {
	int result;
	instw_t instw;
	int status;

#ifndef NDEBUG
	debug(2,"lstat64(%s,%p)\n",pathname,info);
#endif

	/* We were asked to work in "real" mode */
	if (instw__in_real_mode())
		return true_lstat64(pathname,info);

	instw_new(&instw);
	instw_setpath(&instw,pathname);
	instw_getstatus(&instw,&status);

#ifndef NDEBUG
	instw_print(&instw);
#endif

	if(status&INSTW_TRANSLATED) {
		debug(4,"\teffective lstat64(%s,%p)\n",
			instw.translpath,info);
		result=true_lstat64(instw.translpath,info);
	} else {
		debug(4,"\teffective lstat64(%s,%p)\n",
			instw.path,info);
		result=true_lstat64(instw.path,info);
	}

	instw_delete(&instw);

	return result;	
}
#endif  /* HAVE_LSTAT64 */

#ifdef HAVE_MKDIR
int mkdir(const char *pathname, mode_t mode) {
	int result;
	instw_t instw;

	REFCOUNT;

	instw__ensure_init();

#ifndef NDEBUG
	debug(2,"mkdir(%s,mode)\n",pathname);
#endif

	/* We were asked to work in "real" mode */
	if (instw__in_real_mode())
		return true_mkdir(pathname,mode);

	instw_new(&instw);
	instw_setpath(&instw,pathname);

#ifndef NDEBUG
	instw_print(&instw);
#endif

	instw_apply(&instw);

	result=true_mkdir(instw.translpath,mode);
	instw_log("%d\tmkdir\t%s\t#%s\n",result,instw.reslvpath,error(result));

	instw_delete(&instw);

	return result;
}
#endif  /* HAVE_MKDIR */

#ifdef HAVE_MKNOD

#   ifdef HAVE___XMKNOD
int true_mknod_impl(const char* pathname, mode_t mode, dev_t dev) {
    return true___xmknod(_MKNOD_VER, pathname, mode, dev);
}
#   endif

int mknod(const char *pathname, mode_t mode,dev_t dev)
{
	int result;
	instw_t instw;
	
	REFCOUNT;

	instw__ensure_init();

#ifndef NDEBUG
	debug(2,"mknod(%s,mode,dev)\n",pathname);
#endif

	/* We were asked to work in "real" mode */
	if (instw__in_real_mode())
		return true_mknod(pathname,mode,dev);

	instw_new(&instw);
	instw_setpath(&instw,pathname);

#ifndef NDEBUG
	instw_print(&instw);
#endif

	instw_apply(&instw);
	backup(instw.truepath);

	result=true_mknod(instw.translpath,mode,dev);
	instw_log("%d\tmknod\t%s\t#%s\n",result,instw.reslvpath,error(result));

	instw_delete(&instw);
	
	return result;
}
#endif  /* HAVE_MKNOD */

#ifdef HAVE_OPEN
int open(const char *pathname, int flags, ...) {
/* Eventually, there is a third parameter: it's mode_t mode */
	va_list ap;
	mode_t mode;
	int result;
	instw_t instw;
	int status;

	REFCOUNT;

	instw__ensure_init();

#ifndef NDEBUG
	debug(2,"open(%s,%d,mode)\n",pathname,flags);
#endif

	if (flags & O_CREAT) {
		va_start(ap, flags);
#ifdef __APPLE__
		/* Apple uses int to hold the mode_t variadic. */
		mode = va_arg(ap, int);
#else
		mode = va_arg(ap, mode_t);
#endif
		va_end(ap);
	} else {
		mode = 0;
	}

	/* We were asked to work in "real" mode */
	if (instw__in_real_mode())
		return true_open(pathname,flags,mode);

	instw_new(&instw);
	instw_setpath(&instw,pathname);
	
#ifndef NDEBUG
	instw_print(&instw);
#endif

	if(flags & (O_WRONLY | O_RDWR)) {
		backup(instw.truepath);
		instw_apply(&instw);
	}

	instw_getstatus(&instw,&status);

	if(status&INSTW_TRANSLATED) 
		result=true_open(instw.translpath,flags,mode);
	else
		result=true_open(instw.path,flags,mode);
	
	if(flags & (O_WRONLY | O_RDWR)) 
		instw_log("%d\topen\t%s\t#%s\n",result,instw.reslvpath,error(result));

	instw_delete(&instw);

	return result;
}
#endif  /* HAVE_OPEN */

#ifdef HAVE_OPEN64
int open64(const char *pathname, int flags, ...) {
/* Eventually, there is a third parameter: it's mode_t mode */
	va_list ap;
	mode_t mode;
	int result;
	instw_t instw;
	int status;

	REFCOUNT;

	instw__ensure_init();

#ifndef NDEBUG
	debug(2,"open64(%s,%d,mode)\n",pathname,flags);
#endif

	va_start(ap, flags);
	mode = va_arg(ap, mode_t);
	va_end(ap);

	/* We were asked to work in "real" mode */
	if (instw__in_real_mode())
		return true_open64(pathname,flags,mode);

	instw_new(&instw);
	instw_setpath(&instw,pathname);

#ifndef NDEBUG
	instw_print(&instw);
#endif

	if(flags & (O_WRONLY | O_RDWR)) {
		backup(instw.truepath);
		instw_apply(&instw);
	}

	instw_getstatus(&instw,&status);

	if(status&INSTW_TRANSLATED) {
		debug(4,"\teffective open64(%s)",instw.translpath);
		result=true_open64(instw.translpath,flags,mode);
	} else {
		debug(4,"\teffective open64(%s)",instw.path);
		result=true_open64(instw.path,flags,mode);
	}
	
	if(flags & (O_WRONLY | O_RDWR)) 
		instw_log("%d\topen\t%s\t#%s\n",result,
		    instw.reslvpath,error(result));

	instw_delete(&instw);

	return result;
}
#endif  /* HAVE_OPEN64 */

#ifdef HAVE_OPENDIR
DIR *opendir(const char *dirname) {
	DIR *result;
	instw_t instw;

	instw__ensure_init();

#ifndef NDEBUG
	debug(2,"opendir(%s)\n",dirname);
#endif
	
	/* We were asked to work in "real" mode */
	if (instw__in_real_mode())
		return true_opendir(dirname);

	instw_new(&instw);
	instw_setpath(&instw,dirname);
	instw_makedirls(&instw);

#ifndef NDEBUG
	instw_print(&instw);
#endif

	result=true_opendir(instw.mdirlspath);

	instw_delete(&instw);

	return result;
}
#endif  /* HAVE_OPENDIR */

#ifdef HAVE_READDIR
struct dirent *readdir(DIR *dir) {
	struct dirent *result;

	instw__ensure_init();

#ifndef NDEBUG
	debug(3,"readdir(%p)\n",dir);
#endif

	/* We were asked to work in "real" mode */
	if (instw__in_real_mode())
		return true_readdir(dir);

	result=true_readdir(dir);

#ifndef NDEBUG
	__instw_printdirent(result);
#endif

	return result;
}
#endif  /* HAVE_READDIR */

#ifdef HAVE_READDIR64
struct dirent64 *readdir64(DIR *dir) {
	struct dirent64 *result;

	instw__ensure_init();

#ifndef NDEBUG
	debug(3,"readdir64(%p)\n",dir);
#endif

	/* We were asked to work in "real" mode */
	if (instw__in_real_mode())
		return true_readdir64(dir);

	result=true_readdir64(dir);

#ifndef NDEBUG
	__instw_printdirent64(result);
#endif

	return result;
}
#endif  /* HAVE_READDIR64 */

#ifdef HAVE_READLINK
ssize_t readlink(const char *path,char *buf,size_t bufsiz) {
	int result;
	instw_t instw;
	int status;

	instw__ensure_init();

#ifndef NDEBUG
	debug(2,"readlink(\"%s\",%p,%ld)\n",path,buf,(long int)bufsiz);
#endif

	/* We were asked to work in "real" mode */
	if (instw__in_real_mode())
		return true_readlink(path,buf,bufsiz);

	instw_new(&instw);
	instw_setpath(&instw,path);
	instw_getstatus(&instw,&status);
	
#ifndef NDEBUG
	instw_print(&instw);
#endif

	if(status&INSTW_TRANSLATED)
		result=true_readlink(instw.translpath,buf,bufsiz);
	else
		result=true_readlink(instw.path,buf,bufsiz);

	instw_delete(&instw);

	return result;	
}
#endif  /* HAVE_READLINK */

#ifdef HAVE_REALPATH
char *realpath(const char *file_name,char *resolved_name) {
	char *result;

	instw__ensure_init();

	/* We were asked to work in "real" mode */
	if (instw__in_real_mode())
		return true_realpath(file_name,resolved_name);

	result=true_realpath(file_name,resolved_name);

	return result;
}
#endif  /* HAVE_REALPATH */

#ifdef HAVE_RENAME
int rename(const char *oldpath, const char *newpath) {
	int result;
	instw_t oldinstw;
	instw_t newinstw;

	REFCOUNT;
	
	instw__ensure_init();

#ifndef NDEBUG
	debug(2,"rename(\"%s\",\"%s\")\n",oldpath,newpath);	
#endif

	/* We were asked to work in "real" mode */
	if (instw__in_real_mode())
		return true_rename(oldpath,newpath);

	instw_new(&oldinstw);
	instw_new(&newinstw);
	instw_setpath(&oldinstw,oldpath);
	instw_setpath(&newinstw,newpath);

#ifndef NDEBUG
	instw_print(&oldinstw);
	instw_print(&newinstw);
#endif

	backup(oldinstw.truepath);
	instw_apply(&oldinstw);
	instw_apply(&newinstw);

	result=true_rename(oldinstw.translpath,newinstw.translpath);
	instw_log("%d\trename\t%s\t%s\t#%s\n",result,
	    oldinstw.reslvpath,newinstw.reslvpath,error(result));

	instw_delete(&oldinstw);
	instw_delete(&newinstw);

	return result;
}
#endif  /* HAVE_RENAME */

#ifdef HAVE_RMDIR
int rmdir(const char *pathname) {
	int result;
	instw_t instw;

	REFCOUNT;

	instw__ensure_init();

#ifndef NDEBUG
	debug(2,"rmdir(%s)\n",pathname);
#endif

	/* We were asked to work in "real" mode */
	if (instw__in_real_mode())
		return true_rmdir(pathname);

	instw_new(&instw);
	instw_setpath(&instw,pathname);

	backup(instw.truepath);
	instw_apply(&instw);

	result=true_rmdir(instw.translpath);
	instw_log("%d\trmdir\t%s\t#%s\n",result,instw.reslvpath,error(result));
	
	instw_delete(&instw);
	
	return result;
}
#endif  /* HAVE_RMDIR */

#ifdef HAVE_SCANDIR
int scandir(const char *dir,struct dirent ***namelist,
#if !defined(_POSIX_C_SOURCE) || defined(_DARWIN_C_SOURCE)
		int (*select)(struct dirent *),
		int (*compar)(const void *,const void *)
#else
		int (*select)(const struct dirent *),
		int (*compar)(const struct dirent **,const struct dirent **)
#endif
) {
	int result;

	instw__ensure_init();

#ifndef NDEBUG
	debug(2,"scandir(%s,%p,%p,%p)\n",dir,namelist,select,compar);
#endif

	/* We were asked to work in "real" mode */
	if (instw__in_real_mode())
		return true_scandir(dir,namelist,select,compar);

	result=true_scandir(dir,namelist,select,compar);

	return result;
}		
#endif  /* HAVE_SCANDIR */

#ifdef HAVE_SCANDIR64
int scandir64(const char *dir,struct dirent64 ***namelist,
#if !defined(_POSIX_C_SOURCE) || defined(_DARWIN_C_SOURCE)
		int (*select)(struct dirent64 *),
		int (*compar)(const void *,const void *)
#else
		int (*select)(const struct dirent64 *),
		int (*compar)(const struct dirent64 **,const struct dirent64 **)
#endif
) {
	int result;

	instw__ensure_init();

#ifndef NDEBUG
	debug(2,"scandir64(%s,%p,%p,%p)\n",dir,namelist,select,compar);
#endif

	/* We were asked to work in "real" mode */
	if (instw__in_real_mode())
		return true_scandir64(dir,namelist,select,compar);

	result=true_scandir64(dir,namelist,select,compar);

	return result;
}		
#endif  /* HAVE_SCANDIR64 */

#ifdef HAVE_STAT

#   ifdef HAVE___XSTAT
int true_stat_impl(const char *pathname,struct stat *info) {
	return true___xstat(_STAT_VER,pathname,info);
}
#   endif

int stat(const char *pathname,struct stat *info)
{
	int result;
	instw_t instw;
	int status;

	instw__ensure_init();

#ifndef NDEBUG
	debug(2,"stat(%s,%p)\n",pathname,info);
#endif

	/* We were asked to work in "real" mode */
	if (instw__in_real_mode())
		return true_stat(pathname,info);

	instw_new(&instw);
	instw_setpath(&instw,pathname);
	instw_getstatus(&instw,&status);

#ifndef NDEBUG
	instw_print(&instw);
#endif

	if(status&INSTW_TRANSLATED) {
		debug(4,"\teffective stat(%s,%p)\n",
		      instw.translpath,info);
		result=true_stat(instw.translpath,info);
	} else {
		debug(4,"\teffective stat(%s,%p)\n",
		      instw.path,info);
		result=true_stat(instw.path,info);
	}

	instw_delete(&instw);

	return result;	
}
#endif  /* HAVE_STAT */

#ifdef HAVE_STAT64
#endif  /* HAVE_STAT64 */

#ifdef HAVE_SYMLINK
int symlink(const char *pathname, const char *slink) {
	int result;
	instw_t instw;
	instw_t instw_slink;

	REFCOUNT;

	instw__ensure_init();

#ifndef NDEBUG
	debug(2,"symlink(%s,%s)\n",pathname,slink);
#endif

	/* We were asked to work in "real" mode */
	if (instw__in_real_mode())
		return true_symlink(pathname,slink);

	instw_new(&instw);
	instw_new(&instw_slink);
	instw_setpath(&instw,pathname);
	instw_setpath(&instw_slink,slink);

#ifndef NDEBUG
	instw_print(&instw_slink);
#endif

	backup(instw_slink.truepath);
	instw_apply(&instw_slink);
	
	result=true_symlink(pathname,instw_slink.translpath);
	instw_log("%d\tsymlink\t%s\t%s\t#%s\n",
           result,instw.path,instw_slink.reslvpath,error(result));

	    
	instw_delete(&instw);
	instw_delete(&instw_slink);

	return result;
}
#endif  /* HAVE_SYMLINK */

#ifdef HAVE_TIME
time_t time (time_t *timer) {
	TIMECOUNT;

	instw__ensure_init();

#ifndef NDEBUG
	debug(2,"time\n");
#endif

	return true_time(timer);
}
#endif  /* HAVE_TIME */

#ifdef HAVE_TRUNCATE
int truncate(const char *path, off_t length) {
	int result;
	instw_t instw;

	REFCOUNT;

	instw__ensure_init();

#ifndef NDEBUG
	debug(2,"truncate(%s,length)\n",path);
#endif

	/* We were asked to work in "real" mode */
	if (instw__in_real_mode())
		return true_truncate(path,length);

	instw_new(&instw);
	instw_setpath(&instw,path);

#ifndef NDEBUG
	instw_print(&instw);
#endif

	backup(instw.truepath);
	instw_apply(&instw);

	result=true_truncate(instw.translpath,length);
	instw_log("%d\ttruncate\t%s\t%d\t#%s\n",result,
	    instw.reslvpath,(int)length,error(result));

	instw_delete(&instw);

	return result;
}
#endif  /* HAVE_TRUNCATE */

#ifdef HAVE_TRUNCATE64
int truncate64(const char *path, __off64_t length) {
	int result;
	instw_t instw;

	instw__ensure_init();

	REFCOUNT;

	instw__ensure_init();

#ifndef NDEBUG
	debug(2,"truncate64(%s,length)\n",path);
#endif

	/* We were asked to work in "real" mode */
	if (instw__in_real_mode())
		return true_truncate64(path,length);

	instw_new(&instw);
	instw_setpath(&instw,path);

#ifndef NDEBUG
	instw_print(&instw);
#endif

	backup(instw.truepath);
	instw_apply(&instw);

	result=true_truncate64(instw.translpath,length);
	
	instw_log("%d\ttruncate\t%s\t%d\t#%s\n",result,
	    instw.reslvpath,(int)length,error(result));

	instw_delete(&instw);

	return result;
}
#endif  /* HAVE_TRUNCATE64 */

#ifdef HAVE_UNLINK
int unlink(const char *pathname) {
	int result;
	instw_t instw;

	REFCOUNT;

	instw__ensure_init();

#ifndef NDEBUG
	debug(2,"unlink(%s)\n",pathname);
#endif

	/* We were asked to work in "real" mode */
	if (instw__in_real_mode())
		return true_unlink(pathname);

	instw_new(&instw);
	instw_setpath(&instw,pathname);

#ifndef NDEBUG
	instw_print(&instw);
#endif

	backup(instw.truepath);
	instw_apply(&instw);

	result=true_unlink(instw.translpath);
	instw_log("%d\tunlink\t%s\t#%s\n",result,instw.reslvpath,error(result));

	instw_delete(&instw);

	return result;
}
#endif  /* HAVE_UNLINK */

#ifdef HAVE_UTIME
int utime (const char *pathname, const struct utimbuf *newtimes) {
	int result;
	instw_t instw;

	instw__ensure_init();

#ifndef NDEBUG
	debug(2,"utime(%s,newtimes)\n",pathname);
#endif

	/* We were asked to work in "real" mode */
	if (instw__in_real_mode())
		return true_utime(pathname,newtimes);

	instw_new(&instw);
	instw_setpath(&instw,pathname);

#ifndef NDEBUG
	instw_print(&instw);
#endif

	backup(instw.truepath);
	instw_apply(&instw);

	result=true_utime(instw.translpath,newtimes);
	instw_log("%d\tutime\t%s\t#%s\n",result,instw.reslvpath,error(result));

	instw_delete(&instw);

	return result;
}
#endif  /* HAVE_UTIME */

#ifdef HAVE_UTIMES
int utimes (const char *pathname, const struct timeval *newtimes) {
       int result;
       instw_t instw;

	instw__ensure_init();

#ifndef NDEBUG
       debug(2,"utimes(%s,newtimes)\n",pathname);
#endif

       /* We were asked to work in "real" mode */
	if (instw__in_real_mode())
		return true_utimes(pathname,newtimes);

       instw_new(&instw);
       instw_setpath(&instw,pathname);

#ifndef NDEBUG
       instw_print(&instw);
#endif

       backup(instw.truepath);
       instw_apply(&instw);

       result=true_utimes(instw.translpath,newtimes);
       instw_log("%d\tutimes\t%s\t#%s\n",result,instw.reslvpath,error(result));

       instw_delete(&instw);

       return result;
}
#endif  /* HAVE_UTIMES */

#ifdef HAVE___FXSTAT
#endif  /* HAVE___FXSTAT */

#ifdef HAVE___LXSTAT
int __lxstat(int version,const char *pathname,struct stat *info)
{
	int result;
	instw_t instw;
	int status;

	instw__ensure_init();

#ifndef NDEBUG
	debug(2,"lstat(%s,%p)\n",pathname,info);
#endif

	/* We were asked to work in "real" mode */
	if (instw__in_real_mode())
		return true___lxstat(version,pathname,info);

	instw_new(&instw);
	instw_setpath(&instw,pathname);
	instw_getstatus(&instw,&status);

#ifndef NDEBUG
	instw_print(&instw);
#endif

	if(status&INSTW_TRANSLATED) {
		debug(4,"\teffective lstat(%s,%p)\n",
			instw.translpath,info);
		result=true___lxstat(version,instw.translpath,info);
	} else {
		debug(4,"\teffective lstat(%s,%p)\n",
			instw.path,info);
		result=true___lxstat(version,instw.path,info);
	}

	instw_delete(&instw);

	return result;	
}
#endif  /* HAVE___LXSTAT */

#ifdef  HAVE___LXSTAT64
int __lxstat64(int version,const char *pathname,struct stat64 *info) {
	int result;
	instw_t instw;
	int status;

#ifndef NDEBUG
	debug(2,"lstat64(%s,%p)\n",pathname,info);
#endif

	/* We were asked to work in "real" mode */
	if (instw__in_real_mode())
		return true___lxstat64(version,pathname,info);

	instw_new(&instw);
	instw_setpath(&instw,pathname);
	instw_getstatus(&instw,&status);

#ifndef NDEBUG
	instw_print(&instw);
#endif

	if(status&INSTW_TRANSLATED) {
		debug(4,"\teffective lstat64(%s,%p)\n",
			instw.translpath,info);
		result=true___lxstat64(version,instw.translpath,info);
	} else {
		debug(4,"\teffective lstat64(%s,%p)\n",
			instw.path,info);
		result=true___lxstat64(version,instw.path,info);
	}

	instw_delete(&instw);

	return result;	
}
#endif  /* HAVE___LXSTAT64 */

#ifdef HAVE___XMKNOD
int __xmknod(int version,const char *pathname, mode_t mode,dev_t *dev)
{
	int result;
	instw_t instw;
	
	REFCOUNT;

	instw__ensure_init();

#ifndef NDEBUG
	debug(2,"mknod(%s,mode,dev)\n",pathname);
#endif

	/* We were asked to work in "real" mode */
	if (instw__in_real_mode())
		return true___xmknod(version,pathname,mode,dev);

	instw_new(&instw);
	instw_setpath(&instw,pathname);

#ifndef NDEBUG
	instw_print(&instw);
#endif

	instw_apply(&instw);
	backup(instw.truepath);

	result=true___xmknod(version,instw.translpath,mode,dev);
	instw_log("%d\tmknod\t%s\t#%s\n",result,instw.reslvpath,error(result));

	instw_delete(&instw);
	
	return result;
}
#endif  /* HAVE___XMKNOD */

#ifdef HAVE___XSTAT
int __xstat(int version,const char *pathname,struct stat *info)
{
	int result;
	instw_t instw;
	int status;

	instw__ensure_init();

#ifndef NDEBUG
	debug(2,"xstat(%s,%p)\n",pathname,info);
#endif

	/* We were asked to work in "real" mode */
	if (instw__in_real_mode())
		return true___xstat(version,pathname,info);

	instw_new(&instw);
	instw_setpath(&instw,pathname);
	instw_getstatus(&instw,&status);

#ifndef NDEBUG
	instw_print(&instw);
#endif

	if(status&INSTW_TRANSLATED) {
		debug(4,"\teffective xstat(%s,%p)\n",
		      instw.translpath,info);
		result=true___xstat(version,instw.translpath,info);
	} else {
		debug(4,"\teffective xstat(%s,%p)\n",
		      instw.path,info);
		result=true___xstat(version,instw.path,info);
	}

	instw_delete(&instw);

	return result;	
}
#endif  /* HAVE___XSTAT */

#ifdef HAVE___XSTAT64
int __xstat64(int version,const char *pathname,struct stat64 *info) {
	int result;
	instw_t instw;
	int status;

#ifndef NDEBUG
	debug(2,"stat64(%s,%p)\n",pathname,info);
#endif

	/* We were asked to work in "real" mode */
	if (instw__in_real_mode())
		return true___xstat64(version,pathname,info);

	instw_new(&instw);
	instw_setpath(&instw,pathname);
	instw_getstatus(&instw,&status);

#ifndef NDEBUG
	instw_print(&instw);
#endif

	if(status&INSTW_TRANSLATED) {
		debug(4,"\teffective stat64(%s,%p)\n",
		      instw.translpath,info);
		result=true___xstat64(version,instw.translpath,info);
	} else {
		debug(4,"\teffective stat64(%s,%p)\n",
		      instw.path,info);
		result=true___xstat64(version,instw.path,info);
	}	

	instw_delete(&instw);

	return result;	
}
#endif  /* HAVE___XSTAT64 */



/*
 * *****************************************************************************
 */

void initialize(void) {
	if (libc_handle)
		return;

	#ifdef BROKEN_RTLD_NEXT
//        	printf ("RTLD_LAZY");
        	libc_handle = dlopen(LIBC_VERSION, RTLD_LAZY);
	#else
 //       	printf ("RTLD_NEXT");
        	libc_handle = RTLD_NEXT;
	#endif
#ifdef HAVE_ACCESS
	true_access = dlsym(libc_handle, "access");
#endif  /* HAVE_ACCESS */
#ifdef HAVE_CHDIR
	true_chdir = dlsym(libc_handle, "chdir");
#endif  /* HAVE_CHDIR */
#ifdef HAVE_CHMOD
	true_chmod = dlsym(libc_handle, "chmod");
#endif  /* HAVE_CHMOD */
#ifdef HAVE_CHOWN
	true_chown = dlsym(libc_handle, "chown");
#endif  /* HAVE_CHOWN */
#ifdef HAVE_CHROOT
	true_chroot = dlsym(libc_handle, "chroot");
#endif  /* HAVE_CHROOT */
#ifdef HAVE_CREAT
	true_creat = dlsym(libc_handle, "creat");
#endif  /* HAVE_CREAT */
#ifdef HAVE_CREAT64
	true_creat64 = dlsym(libc_handle, "creat64");
#endif  /* HAVE_CREAT64 */
#ifdef HAVE_FCHMOD
	true_fchmod = dlsym(libc_handle, "fchmod");
#endif  /* HAVE_FCHMOD */
#ifdef HAVE_FCHOWN
	true_fchown = dlsym(libc_handle, "fchown");
#endif  /* HAVE_FCHOWN */
#ifdef HAVE_FOPEN
	true_fopen = dlsym(libc_handle, "fopen");
#endif  /* HAVE_FOPEN */
#ifdef HAVE_FOPEN64
	true_fopen64 = dlsym(libc_handle, "fopen64");
#endif  /* HAVE_FOPEN64 */
#ifdef HAVE_FTRUNCATE
	true_ftruncate = dlsym(libc_handle, "ftruncate");
#endif  /* HAVE_FTRUNCATE */
#ifdef HAVE_FTRUNCATE64
	true_ftruncate64 = dlsym(libc_handle, "ftruncate64");
#endif  /* HAVE_FTRUNCATE64 */
#ifdef HAVE_GETCWD
	true_getcwd = dlsym(libc_handle, "getcwd");
#endif  /* HAVE_GETCWD */
#ifdef HAVE_LCHOWN
	true_lchown = dlsym(libc_handle, "lchown");
#endif  /* HAVE_LCHOWN */
#ifdef HAVE_LINK
	true_link = dlsym(libc_handle, "link");
#endif  /* HAVE_LINK */
#ifdef HAVE_LSTAT
#   ifdef HAVE___LXSTAT
	true_lstat = true_lstat_impl;
#   else
	true_lstat = dlsym(libc_handle, "lstat");
#   endif
#endif  /* HAVE_LSTAT */
#ifdef HAVE_LSTAT64
#   if HAVE___LXSTAT64
	true_lstat64 = true_lstat64_impl;
#   else
	true_lstat64 = dlsym(libc_handle, "lstat64");
#   endif
#endif  /* HAVE_LSTAT64 */
#ifdef HAVE_MKDIR
	true_mkdir = dlsym(libc_handle, "mkdir");
#endif  /* HAVE_MKDIR */
#ifdef HAVE_MKNOD
#   ifdef HAVE___XMKNOD
	true_mknod = true_mknod_impl;
#   else
	true_mknod = dlsym(libc_handle, "mknod");
#   endif
#endif  /* HAVE_MKNOD */
#ifdef HAVE_OPEN
	true_open = dlsym(libc_handle, "open");
#endif  /* HAVE_OPEN */
#ifdef HAVE_OPEN64
	true_open64 = dlsym(libc_handle, "open64");
#endif  /* HAVE_OPEN64 */
#ifdef HAVE_OPENDIR
	true_opendir = dlsym(libc_handle, "opendir");
#endif  /* HAVE_OPENDIR */
#ifdef HAVE_READDIR
	true_readdir = dlsym(libc_handle, "readdir");
#endif  /* HAVE_READDIR */
#ifdef HAVE_READDIR64
	true_readdir64 = dlsym(libc_handle, "readdir64");
#endif  /* HAVE_READDIR64 */
#ifdef HAVE_READLINK
	true_readlink = dlsym(libc_handle, "readlink");
#endif  /* HAVE_READLINK */
#ifdef HAVE_REALPATH
	true_realpath = dlsym(libc_handle, "realpath");
#endif  /* HAVE_REALPATH */
#ifdef HAVE_RENAME
	true_rename = dlsym(libc_handle, "rename");
#endif  /* HAVE_RENAME */
#ifdef HAVE_RMDIR
	true_rmdir = dlsym(libc_handle, "rmdir");
#endif  /* HAVE_RMDIR */
#ifdef HAVE_SCANDIR
	true_scandir = dlsym(libc_handle, "scandir");
#endif  /* HAVE_SCANDIR */
#ifdef HAVE_SCANDIR64
	true_scandir64 = dlsym(libc_handle, "scandir64");
#endif  /* HAVE_SCANDIR64 */
#ifdef HAVE_STAT
#   ifdef HAVE___XSTAT
	true_stat = true_stat_impl;
#   else
	true_stat = dlsym(libc_handle, "stat");
#   endif
#endif  /* HAVE_STAT */
#ifdef HAVE_STAT64
#   ifdef HAVE___XSTAT64
	true_stat64 = true_stat64_impl;
#   else
	true_stat64 = dlsym(libc_handle, "stat64");
#   endif
#endif  /* HAVE_STAT64 */
#ifdef HAVE_SYMLINK
	true_symlink = dlsym(libc_handle, "symlink");
#endif  /* HAVE_SYMLINK */
#ifdef HAVE_TIME
	true_time = dlsym(libc_handle, "time");
#endif  /* HAVE_TIME */
#ifdef HAVE_TRUNCATE
	true_truncate = dlsym(libc_handle, "truncate");
#endif  /* HAVE_TRUNCATE */
#ifdef HAVE_TRUNCATE64
	true_truncate64 = dlsym(libc_handle, "truncate64");
#endif  /* HAVE_TRUNCATE64 */
#ifdef HAVE_UNLINK
	true_unlink = dlsym(libc_handle, "unlink");
#endif  /* HAVE_UNLINK */
#ifdef HAVE_UTIME
	true_utime = dlsym(libc_handle, "utime");
#endif  /* HAVE_UTIME */
#ifdef HAVE_UTIMES
	true_utimes = dlsym(libc_handle, "utimes");
#endif  /* HAVE_UTIMES */
#ifdef HAVE___FXSTAT
	true___fxstat = dlsym(libc_handle, "__fxstat");
#endif  /* HAVE___FXSTAT */
#ifdef HAVE___LXSTAT
	true___lxstat = dlsym(libc_handle, "__lxstat");
#endif  /* HAVE___LXSTAT */
#ifdef HAVE___LXSTAT64
	true___lxstat64 = dlsym(libc_handle, "__lxstat64");
#endif  /* HAVE___LXSTAT64 */
#ifdef HAVE___XMKNOD
	true___xmknod = dlsym(libc_handle, "__xmknod");
#endif  /* HAVE___XMKNOD */
#ifdef HAVE___XSTAT
	true___xstat = dlsym(libc_handle, "__xstat");
#endif  /* HAVE___XSTAT */
#ifdef HAVE___XSTAT64
	true___xstat64 = dlsym(libc_handle, "__xstat64");
#endif  /* HAVE___XSTAT64 */

	if(instw_init()) exit(-1);
}

void __attribute ((constructor)) init_function(void) {
	initialize();
}

void __attribute ((destructor)) fini_function(void) {
	instw_fini();	
}

/*
 * *****************************************************************************
 */

/*
 * procedure = / rc:=vlambda_log(logname,format,ap) /
 *
 * task      = / the va_list version of the lambda_log() procedure. /
 */
static int vlambda_log(const char *logname,const char *format,va_list ap) {
	char buffer[BUFSIZE];
	int count;
	int logfd;
	int rcod=0;

	count=vsnprintf(buffer,BUFSIZE,format,ap);
	if(count == -1) {
		  /* The buffer was not big enough */
		strcpy(&(buffer[BUFSIZE - 5]), "...\n");
		count=BUFSIZE-1;
	}
	
	if(logname!=NULL) {
		logfd=true_open(logname,O_WRONLY|O_CREAT|O_APPEND,0666);
		if(logfd>=0) {
			if(write(logfd,buffer,count)!=count)
				syslog(	LOGLEVEL,
					"Count not write `%s' in `%s': %s\n",
					buffer,logname,strerror(errno));
			if(close(logfd) < 0)
				syslog(	LOGLEVEL,
					"Could not close `%s': %s\n",
					logname,strerror(errno));
		} else {
			syslog(	LOGLEVEL,
				"Could not open `%s' to write `%s': %s\n",
				logname,buffer,strerror(errno));
		}
	} else {
		syslog(LOGLEVEL,buffer);
	}	

	return rcod;
}

/*
 * procedure = / rc:=lambda_log(logname,format,...) /
 *
 * task      = /   logs a message to the specified file, or via syslog
 *               if no file is specified. /
 *
 * returns   = /  0 ok. message logged / 
 *
 * note      = / 
 * 	--This *general* log procedure was justified by the debug mode 
 *  which used either stdout or stderr, thus interfering with the 
 *  observed process.
 *      --From now, we output nothing to stdout/stderr.
 * /
 *
 */
/*
static int lambda_log(const char *logname,const char *format, ...) {
	va_list ap;
	int rcod=0;;

	va_start(ap,format);
	rcod=vlambda_log(logname,format,ap);
	va_end(ap);

	return rcod;
}
*/

static inline int instw_log(const char *format,...) {
	char *logname;
	va_list ap;
	int rcod; 
	
	logname=getenv("INSTW_LOGFILE");
	va_start(ap,format);
	rcod=vlambda_log(logname,format,ap);
	va_end(ap);
	
	return rcod;
}

static inline int debug(int dbglvl,const char *format,...) {
	int rcod=0; 
#ifdef DEBUG
	char *logname;
	va_list ap;

	if(	__instw.dbglvl==0 ||
		dbglvl>__instw.dbglvl ||
		dbglvl<0	) return rcod;

	logname=getenv("INSTW_DBGFILE");
	va_start(ap,format);
	rcod=vlambda_log(logname,format,ap);
	va_end(ap);
#endif	

	return rcod;
}

/*
 * procedure = / rc:=canonicalize(path,resolved_path) /
 *
 * note      = /
 *	--We use realpath here, but this function calls __lxstat().
 *	We want to only use "real" calls in wrapping code, hence the 
 *      barrier established by unset_okwrap()/reset_okwrap().
 *      --We try to canonicalize as much as possible, considering that 
 * /
 */
int canonicalize(const char *path, char *resolved_path) {

	unset_okwrap();

	if(!true_realpath(path,resolved_path)) {
		if((path[0] != '/')) {
			/* The path could not be canonicalized, append it
		 	 * to the current working directory if it was not 
		 	 * an absolute path                               */
			true_getcwd(resolved_path, PATH_MAX-2);
			strcat(resolved_path, "/");
			strncat(resolved_path, path, MAXPATHLEN - 1);
		} else {
			strcpy(resolved_path,path);
		}
	}

	reset_okwrap();

#ifndef NDEBUG
	debug(4,"canonicalize(%s,%s)\n",path,resolved_path);
#endif

	return 0;
} 

static int make_path (const char *path) {
	char checkdir[BUFSIZ];
	struct stat inode;

	int i = 0;

#ifndef NDEBUG
	debug(2,"===== make_path: %s\n", path);
#endif

	while ( path[i] != '\0' ) {
		checkdir[i] = path[i];
		if (checkdir[i] == '/') {  /* Each time a '/' is found, check if the    */
			checkdir[i+1] = '\0';   /* path exists. If it doesn't, we create it. */
			if (true_stat (checkdir, &inode) < 0)
				true_mkdir (checkdir, S_IRWXU);
		}
		i++;
	}
	return 0;
}


/*
 * procedure = / rc:=copy_path(truepath,translroot) /
 *
 * task      = /   do an exact translation of 'truepath' under 'translroot', 
 *               the directory path to the new objet is not created / 
 *
 * returns   = /  0 ok. translation done 
 *               -1 failed. cf errno /
 *
 * note      = / 
 *	--we suppose that 'translroot' has no trailing '/' 
 *	--no overwrite is done is the target object already exists 
 *	--the copy method depends on the source object type. 
 *	--we don't fail if the source object doesn't exist. 
 *	--we don't create the directory path because that would lead in the 
 *      the translation case not to reference the newly created directories
 * /
 */
static int copy_path(const char *truepath,const char *translroot) {
	int rcod;
	char buffer[BUFSIZ];
	int bytes;
	char translpath[PATH_MAX+1];
	struct stat trueinfo;
	struct stat translinfo;
	int truefd;
	int translfd;
	struct utimbuf timbuf;
	size_t truesz;
	char linkpath[PATH_MAX+1];
	size_t linksz;

#ifndef NDEBUG
	debug(2,"copy_path(%s,%s)\n",truepath,translroot);
#endif

	rcod=true_lstat(truepath,&trueinfo);
	if(rcod<0 && errno!=ENOENT) return -1;
	if(!rcod) {
		if((truesz=strlen(truepath)+strlen(translpath))>PATH_MAX) {
			errno=ENAMETOOLONG;
			return -1;
		}
		
		strncpy(translpath,translroot,PATH_MAX);
		strncat(translpath,truepath,PATH_MAX-truesz);

		if(!true_lstat(translpath,&translinfo)) return 0;

		  /* symbolic links */
		if(S_ISLNK(trueinfo.st_mode)) {
			if((linksz=true_readlink(truepath,linkpath,PATH_MAX))<0) return -1;
			linkpath[linksz]='\0';
			if(true_symlink(linkpath,translpath)!=0) return -1;
		}

		  /* regular file */
		if(S_ISREG(trueinfo.st_mode)) {
			if((truefd=true_open(truepath,O_RDONLY))<0) return -1;
			if((translfd=true_open(	translpath,
						O_WRONLY|O_CREAT|O_TRUNC,
						trueinfo.st_mode))<0	) {
				close(truefd);
				return -1;
			}			
			
			while((bytes=read(truefd,buffer,BUFSIZ))>0)
				write(translfd,buffer,bytes);
	
			close(truefd);
			close(translfd);
		}
	
		  /* directory */
		if(S_ISDIR(trueinfo.st_mode)) {
			if(true_mkdir(translpath,trueinfo.st_mode)) return -1;
		}
	
		  /* block special file */
		if(S_ISBLK(trueinfo.st_mode)) {
			if(true_mknod(	translpath,trueinfo.st_mode|S_IFBLK,
					trueinfo.st_rdev	)) return -1;
		}
	
		  /* character special file */
		if(S_ISCHR(trueinfo.st_mode)) {
			if(true_mknod(	translpath,trueinfo.st_mode|S_IFCHR,
					trueinfo.st_rdev	)) return -1;
		}
		 
		  /* fifo special file */
		if(S_ISFIFO(trueinfo.st_mode)) {
			if(true_mknod(translpath,trueinfo.st_mode|S_IFIFO,0))
				return -1;
		}
		
		timbuf.actime=trueinfo.st_atime;
		timbuf.modtime=trueinfo.st_mtime;
		true_utime(translpath,&timbuf);
		
		if(!S_ISLNK(trueinfo.st_mode)) {
			true_chown(translpath,trueinfo.st_uid,trueinfo.st_gid);
			true_chmod(translpath,trueinfo.st_mode); 
		}	
	}

	return 0;
}

/*
 * procedure = / rc:=path_excluded(truepath) /
 *
 * task      = /   indicates if the given path is or is hosted under any
 *               of the exclusion list members. /
 *
 * returns   = /  0 is not a member
 *                1 is a member /
 *
 * note      = /   __instw.exclude must be initialized / 
 * 
 */
static inline int path_excluded(const char *truepath) {
	string_t *pnext;
	int result;

	result=0;
	pnext=__instw.exclude;

	while(pnext!=NULL) {
		if(strstr(truepath,pnext->string)==truepath) {
			result=1;
			break;
		}
		pnext=pnext->next;	
	}

	return result;
}

/*
 * procedure = / rc:=unlink_recursive(truepath) /
 *
 * task      = /   dangerous function that unlink either a file or 
 *               an entire subtree. / 
 *
 * returns   = /  0 ok. recursive unlink done 
 *               -1 failed. cf errno /
 *
 * note      = / 
 *	--this procedure was needed by instw_makedirls(), in order to 
 *	erase a previously created temporary subtree.
 *      --it must be called with an absolute path, and only to delete 
 *      well identified trees.
 *      --i think it is a very weak implementation, so avoid using it
 *      to unlink too deep trees, or rewrite it to avoid recursivity.
 * /
 * 
 */
static int unlink_recursive(const char *truepath) {
	int rcod;
	struct stat trueinfo;
	DIR *wdir;
	struct dirent *went;
	char wpath[PATH_MAX+1];
	struct stat winfo;

#ifndef NDEBUG
	debug(2,"unlink_recursive(%s)\n",truepath);
#endif

	rcod=true_lstat(truepath,&trueinfo);
	if(rcod<0 && errno!=ENOENT) return -1;
	if(rcod!=0) return 0;

	if(S_ISDIR(trueinfo.st_mode)) {
		wdir=true_opendir(truepath);
		if(wdir==NULL) return -1;
		while((went=true_readdir(wdir))!=NULL) {
			  /* we avoid big inifinite recursion troubles */
			if( 	went->d_name[0]=='.' && 
				(	(went->d_name[1]=='\0') ||
					(	went->d_name[1]=='.' &&
						went->d_name[2]=='\0') ) ) 
				{ continue; }	
			
			  /* let's get the absolute path to this entry */
			strcpy(wpath,truepath);	
			strcat(wpath,"/");
			strcat(wpath,went->d_name);
			rcod=true_lstat(wpath,&winfo);
			if(rcod!=0) {
				closedir(wdir);
				return -1;
			}	
		
			if(S_ISDIR(winfo.st_mode)) {
				unlink_recursive(wpath);
				true_rmdir(wpath);
			} else {
				true_unlink(wpath);
			}
		}
		closedir(wdir);
		true_rmdir(truepath);
	} else {
		true_unlink(truepath);
	}

	return rcod;
}

/* 
 * procedure = / rc:=expand_path(&list,prefix,suffix) /
 *
 * task      = /   from a given path, generates all the paths that could 
 *               be derived from it, through symlinks expansion. /
 * 
 * note      = /
 *	--this procedure has been implemented to enhance the method used
 *	to reference files that have been translated.
 *	--briefly, it is necessary to reference all the paths that could
 *	lead to a file, not only the path and the associated real path.
 * /
 */
int expand_path(string_t **list,const char *prefix,const char *suffix) {
	char nprefix[PATH_MAX+1];
	char nwork[PATH_MAX+1];
	char nsuffix[PATH_MAX+1];
	char lnkpath[PATH_MAX+1];
	size_t lnksz=0;
	string_t *pthis=NULL;
	string_t *list1=NULL;
	string_t *list2=NULL;
	struct stat ninfo;
	int rcod=0;
	char pnp[PATH_MAX+1];
	char pns[PATH_MAX+1];
	size_t len;

#ifndef NDEBUG
	debug(4,"expand_path(%p,%s,%s)\n",list,prefix,suffix);
#endif

	  /* nothing more to expand, stop condition */
	if(suffix[0]=='\0') {
		(*list)=mallok(string_t,1);
		(*list)->string=malloc(strlen(prefix)+1);
		strcpy((*list)->string,prefix);
		(*list)->next=NULL;
		finalize(0);	
	}

	  /* we parse the next suffix subscript */	
	parse_suffix(pnp,pns,suffix);	
	strcpy(nprefix,prefix);
	strcat(nprefix,pnp);
	strcpy(nsuffix,pns);

	rcod=true_lstat(nprefix,&ninfo);
	if( (rcod!=0) ||
	    (rcod==0 && !S_ISLNK(ninfo.st_mode))) {
		expand_path(list,nprefix,nsuffix);	
	} else {
		expand_path(&list1,nprefix,nsuffix);
		
		lnksz=true_readlink(nprefix,lnkpath,PATH_MAX);
		lnkpath[lnksz]='\0';
		if(lnkpath[0]!='/') {
			strcpy(nprefix,prefix);
			len=strlen(lnkpath);
			if(lnkpath[len-1]=='/') {lnkpath[len-1]='\0';}
			strcpy(nwork,"/");
			strcat(nwork,lnkpath);
			strcat(nwork,nsuffix);
			strcpy(nsuffix,nwork);
			expand_path(&list2,nprefix,nsuffix);
		} else {
			len=strlen(lnkpath);
			if(lnkpath[len-1]=='/') {lnkpath[len-1]='\0';}
			strcpy(nprefix,"");
			strcpy(nwork,lnkpath);
			strcat(nwork,nsuffix);	
			strcpy(nsuffix,nwork);
			expand_path(&list2,nprefix,nsuffix);
		}

		*list=list1;
		pthis=*list;
		while(pthis->next!=NULL) {pthis=pthis->next;}
		pthis->next=list2;
	}	

	finalize:

	return rcod;		
}

int parse_suffix(char *pnp,char *pns,const char *suffix) {
	int rcod=0;
	char *p;

	strcpy(pnp,suffix);
	strcpy(pns,"");

	p=pnp;
	
	if(*p=='\0') {
		strcpy(pns,"");
	} else {
		p++;
		while((*p)!='\0') {
			if(*p=='/') {
				strcpy(pns,p);
				*p='\0';
				break;
			}
			p++;	
		}
	}

	return rcod;
}

/*
 * *****************************************************************************
 */

static int __instw_printdirent(struct dirent *entry) {

	if(entry!=NULL) {
		debug(	4,
			"entry(%p) {\n"
			"\td_ino     : %ld\n"
#ifdef linux
			"\td_off     : %ld\n"
			"\td_reclen  : %d\n"
			"\td_type    : %d\n"
			"\td_name    : \"%.*s\"\n",
#else
			"\td_name    : \"%s\"\n",
#endif
			entry,
			entry->d_ino,
#ifdef linux
			entry->d_off,
			entry->d_reclen,
			(int)entry->d_type,
			(int)entry->d_reclen,(char*)(entry->d_name)
#else
			(char*)(entry->d_name)
#endif
			
			);
	} else {
		debug(	4,"entry(null) \n");
	}

	return 0;
}

#ifdef INSTW_USE_LARGEFILE64
static int __instw_printdirent64(struct dirent64 *entry) {

	if(entry!=NULL) {
		debug(	4,
			"entry(%p) {\n"
			"\td_ino     : %lld\n"
#ifdef linux
			"\td_off     : %lld\n"
			"\td_reclen  : %d\n"
			"\td_type    : %d\n"
			"\td_name    : \"%.*s\"\n",
#else
			"\td_name    : \"%s\"\n",
#endif
			entry,
			entry->d_ino,
#ifdef linux
			entry->d_off,
			entry->d_reclen,
			(int)entry->d_type,
			(int)entry->d_reclen,(char*)(entry->d_name)
#else
			(char*)(entry->d_name)
#endif
			);
	} else {
		debug(	4,"entry(null) \n");
	}

	return 0;
}
#endif  /* INSTW_USE_LARGEFILE64 */

/*
 * *****************************************************************************
 */

#ifdef DEBUG
static int instw_print(instw_t *instw) {
	string_t *pnext;
	int i;
	
	debug(	4,
		"instw(%p) {\n"
		"\tgstatus     : %d\n"
		"\terror       : %d\n"
		"\tstatus      : %d\n"
		"\tdbglvl      : %d\n"
		"\tpid         : %d\n"
		"\troot        : \"%.*s\"\n"
		"\tbackup      : \"%.*s\"\n"
		"\ttransl      : \"%.*s\"\n"
		"\tmeta        : \"%.*s\"\n"
		"\tmtransl     : \"%.*s\"\n"
		"\tmdirls      : \"%.*s\"\n",
		instw,
		instw->gstatus,
		instw->error,
		instw->status,
		instw->dbglvl,
		instw->pid,
		PATH_MAX,(char*)((instw->root)?:"(null)"),
		PATH_MAX,(char*)((instw->backup)?:"(null)"),
		PATH_MAX,(char*)((instw->transl)?:"(null)"),
		PATH_MAX,(char*)((instw->meta)?:"(null)"),
		PATH_MAX,(char*)((instw->mtransl)?:"(null)"),
		PATH_MAX,(char*)((instw->mdirls)?:"(null)")
		);

	pnext=instw->exclude;
	i=0;
	while(pnext!=NULL) {
		debug(	4,
			"\texclude     : (%02d) \"%.*s\"\n",
			++i,PATH_MAX,pnext->string	);
		pnext=pnext->next;	
	}

	debug(	4,
		"\tpath        : \"%.*s\"\n"
		"\treslvpath   : \"%.*s\"\n"
		"\ttruepath    : \"%.*s\"\n"
		"\ttranslpath  : \"%.*s\"\n",
		PATH_MAX,(char*)(instw->path),
		PATH_MAX,(char*)(instw->reslvpath),
		PATH_MAX,(char*)(instw->truepath),
		PATH_MAX,(char*)(instw->translpath)
		);

	pnext=instw->equivpaths;
	i=0;
	while(pnext!=NULL) {
		debug(	4,
			"\tequivpaths  : (%02d) \"%.*s\"\n",
			++i,PATH_MAX,pnext->string	);
		pnext=pnext->next;	
	}

	debug(	4,
		"\tmtranslpath : \"%.*s\"\n"
		"\tmdirlspath  : \"%.*s\"\n"
		"}\n",
		PATH_MAX,(char*)(instw->mtranslpath),
		PATH_MAX,(char*)(instw->mdirlspath)
		);

	return 0;	
}
#endif

/*
 * procedure = / rc:=instw_init() /
 *
 * task      = /   initializes the '__transl' fields, and fills the fields 
 *		 provided by the environment. 
 *                 this structure is a reference enabling faster 
 *		 local structure creations. /
 *
 * returns   = /  0 ok. env set 
 *               -1 failed. /
 */
static int instw_init(void) {
	char *proot;
	char *pbackup;
	char *ptransl;
	char *pdbglvl;
	struct stat info;
	char wrkpath[PATH_MAX+1];
	char *pexclude;
	char *exclude;
	string_t **ppnext;
	int okinit;
	int okbackup;
	int oktransl;
	int okwrap;

#ifndef NDEBUG
	  /*
	   * We set the requested dynamic debug level
	   */
	__instw.dbglvl=0;
	if((pdbglvl=getenv("INSTW_DBGLVL"))) {
		__instw.dbglvl=atoi(pdbglvl);
		if(__instw.dbglvl>4) { __instw.dbglvl=4; }
		if(__instw.dbglvl<0) { __instw.dbglvl=0; }
	}

	debug(2,"instw_init()\n");
#endif

	okinit=0;
	okbackup=0;
	oktransl=0;
	okwrap=0;
	
	__instw.gstatus=0;
	__instw.error=0;
	__instw.status=0;
	__instw.pid=getpid();
	__instw.root=NULL;
	__instw.backup=NULL;
	__instw.transl=NULL;
	__instw.meta=NULL;
	__instw.mtransl=NULL;
	__instw.mdirls=NULL;
	__instw.exclude=NULL;

	__instw.path[0]='\0';
	__instw.reslvpath[0]='\0';
	__instw.truepath[0]='\0';
	__instw.translpath[0]='\0';

	__instw.equivpaths=NULL;

	__instw.mtranslpath[0]='\0';
	__instw.mdirlspath[0]='\0';

	  /* nothing can be activated without that, anyway */
	if((proot=getenv("INSTW_ROOTPATH"))) {
		realpath(proot,wrkpath);
		if(wrkpath[strlen(wrkpath)-1]=='/')
			wrkpath[strlen(wrkpath)-1]='\0';
		__instw.root=malloc(strlen(wrkpath)+1);
		if(NULL==__instw.root) return -1;
		strcpy(__instw.root,wrkpath);

		  /* this root path must exist */
		if(__instw.root[0]=='\0' || true_stat(__instw.root,&info)) {
			fprintf(stderr,
				"Please check the INSTW_ROOTPATH and "
				"be sure that it does exist please !\n"
				"given value : %s\n", __instw.root);
			return -1;	
		}

		if((pbackup=getenv("INSTW_BACKUP"))) {
			if(	!strcmp(pbackup,"1") ||
				!strcmp(pbackup,"yes") ||
				!strcmp(pbackup,"true")	) {
			
				if((strlen(__instw.root)+strlen(_BACKUP))>PATH_MAX) {
					fprintf(stderr,
						"Backup path would exceed PATH_MAX. abending.\n");
					return -1;	
				}
				__instw.backup=malloc(strlen(__instw.root)+strlen(_BACKUP)+1);
				if(NULL==__instw.backup) return -1;
				strcpy(__instw.backup,__instw.root);
				strcat(__instw.backup,_BACKUP);

				  /* we create the path that precautiously shouldn't exist */
				true_mkdir(__instw.backup,S_IRWXU);  

				okbackup=1;
			} 
			else if(	strcmp(pbackup,"0") && 
					strcmp(pbackup,"no") &&
					strcmp(pbackup,"false")	) {
				fprintf(stderr,
					"Please check the INSTW_BACKUP value please !\n"
					"Recognized values are : 1/0,yes/no,true/false.\n");
				return -1;	
			}		
		}

		if((ptransl=getenv("INSTW_TRANSL"))) {
			if(	!strcmp(ptransl,"1") ||
				!strcmp(ptransl,"yes") ||
				!strcmp(ptransl,"true")	) {
		
				if((strlen(__instw.root)+strlen(_TRANSL))>PATH_MAX) {
					fprintf(stderr,
						"Transl path would exceed PATH_MAX. abending.\n");
					return -1;	
				}
				__instw.transl=malloc(strlen(__instw.root)+strlen(_TRANSL)+1);
				if(NULL==__instw.transl) return -1;
				strcpy(__instw.transl,__instw.root);
				strcat(__instw.transl,_TRANSL);
			
				  /* we create the path that precautiously shouldn't exist */
				true_mkdir(__instw.transl,S_IRWXU);  
			
				if((strlen(__instw.root)+strlen(_META))>PATH_MAX) {
					fprintf(stderr,
						"Meta path would exceed PATH_MAX. abending.\n");
					return -1;	
				}
				
				__instw.meta=malloc(strlen(__instw.root)+strlen(_META)+1);
				if(NULL==__instw.meta) return -1;
				strcpy(__instw.meta,__instw.root);
				strcat(__instw.meta,_META);
			
				  /* we create the path that precautiously shouldn't exist */
				true_mkdir(__instw.meta,S_IRWXU); 
	
				__instw.mtransl=malloc(strlen(__instw.meta)+strlen(_MTRANSL)+1);
				if(NULL==__instw.mtransl) return -1;
				strcpy(__instw.mtransl,__instw.meta);
				strcat(__instw.mtransl,_MTRANSL);
			
				  /* we create the path that precautiously shouldn't exist */
				true_mkdir(__instw.mtransl,S_IRWXU); 

				__instw.mdirls=malloc(strlen(__instw.meta)+strlen(_MDIRLS)+1);
				if(NULL==__instw.mdirls) return -1;
				strcpy(__instw.mdirls,__instw.meta);
				strcat(__instw.mdirls,_MDIRLS);
			
				  /* we create the path that precautiously shouldn't exist */
				true_mkdir(__instw.mdirls,S_IRWXU); 

				oktransl=1;
			} 
			else if(	strcmp(ptransl,"0") && 
					strcmp(ptransl,"no") &&
					strcmp(ptransl,"false")	) {
				fprintf(stderr,
					"Please check the INSTW_TRANSL value please !\n"
					"Recognized values are : 1/0,yes/no,true/false.\n");
				return -1;	
			}		
		}
	}

	  /*
	   * we end up constructing the exclusion list
	   */

	ppnext=&__instw.exclude;

	  /* we systematically add the root directory */
	if(__instw.gstatus&INSTW_OKTRANSL) {
		*ppnext=mallok(string_t,1);
		if(*ppnext==NULL) return -1;
		(*ppnext)->string=NULL;
		(*ppnext)->next=NULL;
		realpath(__instw.root,wrkpath);
		(*ppnext)->string=malloc(strlen(wrkpath)+1);
		strcpy((*ppnext)->string,wrkpath);
		ppnext=&(*ppnext)->next;
	}	
	   
	if((pexclude=getenv("INSTW_EXCLUDE"))) {
		exclude=malloc(strlen(pexclude)+1);
		strcpy(exclude,pexclude);
		pexclude=strtok(exclude,",");

		while(pexclude!=NULL) {
			*ppnext=malloc(sizeof(string_t));
			if(*ppnext==NULL) return -1;
			(*ppnext)->string=NULL;
			(*ppnext)->next=NULL;
			  /* let's store the next excluded path */
			if(strlen(pexclude)>PATH_MAX) return -1;
			realpath(pexclude,wrkpath);
			(*ppnext)->string=malloc(strlen(wrkpath)+1);
			strcpy((*ppnext)->string,wrkpath);
			ppnext=&(*ppnext)->next;
			pexclude=strtok(NULL,",");
		}
	
	}


	okinit=1;
	okwrap=1;

	if(okinit) __instw.gstatus |= INSTW_INITIALIZED;
	if(okwrap) __instw.gstatus |= INSTW_OKWRAP;
	if(okbackup) __instw.gstatus |= INSTW_OKBACKUP;
	if(oktransl) __instw.gstatus |= INSTW_OKTRANSL;	
	
#ifndef NDEBUG
	debug(4,"__instw(%p)\n",&__instw);
	instw_print(&__instw);
#endif

	return 0;
}

/*
 * procedure = / rc:=instw_fini() /
 *
 * task      = /   properly finalizes the instw job /  
 *
 * returns   = /  0 ok. env set 
 *               -1 failed. /
 */
static int instw_fini(void) {
	int rcod=0;
	string_t *pnext;
	string_t *pthis;

#ifndef NDEBUG
	debug(2,"instw_fini()\n");
#endif

	if( !(__instw.gstatus & INSTW_INITIALIZED) ) finalize(0);

	__instw.gstatus &= ~INSTW_INITIALIZED;

	if(__instw.root != NULL) {free(__instw.root);__instw.root=NULL;}	
	if(__instw.backup != NULL) {free(__instw.backup);__instw.backup=NULL;}	
	if(__instw.transl != NULL) {free(__instw.transl);__instw.transl=NULL;}	
	if(__instw.meta != NULL) {free(__instw.meta);__instw.meta=NULL;}	
	if(__instw.mtransl != NULL) {free(__instw.mtransl);__instw.mtransl=NULL;}	
	if(__instw.mdirls != NULL) {free(__instw.mdirls);__instw.mdirls=NULL;}	

	pthis=__instw.exclude;
	while(pthis != NULL) {
		free(pthis->string);
		pnext=pthis->next;
		free(pthis);
		pthis=pnext;
	}
	__instw.exclude=NULL;

	finalize:

	return rcod;
}

/*
 * procedure = / rc:=instw_new(instw) /
 *
 * task      = / Initializes a new instw_t structure before any work on it /
 *
 * returns   = /  0 ok. ready to be used
 *               -1 failed. /
 */
static int instw_new(instw_t *instw) {
	int rcod=0;

	*instw=__instw;

	instw->error=0;
	instw->status=0;
	instw->path[0]='\0';
	instw->reslvpath[0]='\0';
	instw->truepath[0]='\0';
	instw->translpath[0]='\0';
	instw->equivpaths=NULL;
	instw->mtranslpath[0]='\0';
	instw->mdirlspath[0]='\0';

	return rcod;
}

/*
 * procedure = / rc:=instw_delete(instw) /
 *
 * task      = / properly finalizes an instw structure /
 *
 * returns   = /  0 ok. ready to be used
 *               -1 failed. /
 */
static int instw_delete(instw_t *instw) {
	int rcod=0;
	string_t *pnext;
	string_t *pthis;

	pthis=instw->equivpaths;
	while(pthis != NULL) {
		free(pthis->string);
		pnext=pthis->next;
		free(pthis);
		pthis=pnext;
	}

	instw->status=0;

	return rcod;
}

/* 
 * procedure = / rc:=instw_setmetatransl(instw) /
 *
 * task      = / Refreshes as mush as possible the translation 
 *               status of a translated file /
 *
 * note      = /
 *	--this procedure is meant to be called after the various
 *   translation status flags have been setted.
 *        the only thing it does is referencing a file that has been
 *   flagged as "translated".
 *        if it is, we musn't try to use the eventual real version 
 *   of the file anymore, hence the full referencement under /mtransl.
 *
 *      --in some cases, for example when you create manually a subtree 
 *  and a file in this subtree directly directly in the translated fs 
 *  (yes, it is possible) the meta infos won't exist.
 *        so, to be able to cope with this case, we firstly try to
 *  create the full reference to the file, and if this fails, we try
 *  to reference all the traversed directories.
 * /
 */
static int instw_setmetatransl(instw_t *instw) {
	int rcod=0;
	struct stat info;
	char mtransldir[PATH_MAX+1];
	char mtranslpath[PATH_MAX+1];
	char reslvpath[PATH_MAX+1];
	size_t mesz=0;
	int i=0;
	string_t *pthis;

#ifndef NDEBUG
	debug(3,"instw_setmetatransl(%p)\n",instw);
	instw_print(instw);
#endif

	if( !(instw->gstatus & INSTW_INITIALIZED) ||
	    !(instw->gstatus & INSTW_OKTRANSL) ) finalize(0); 

	if(!(instw->status & INSTW_TRANSLATED) ) finalize(0);

	if(instw->equivpaths==NULL) { 	
		expand_path(&(instw->equivpaths),"",instw->reslvpath);
	}	

#ifndef NDEBUG
	instw_print(instw);
#endif

	pthis=instw->equivpaths;	
	while(pthis!=NULL) {
		strcpy(mtranslpath,instw->mtransl);
		strcat(mtranslpath,pthis->string);
		strcpy(reslvpath,pthis->string);

		if( (true_stat(mtranslpath,&info)) && 
		    (true_mkdir(mtranslpath,S_IRWXU)) ) {
			strcpy(mtransldir,mtranslpath);
			mesz=strlen(instw->mtransl);
	
			for(i=0;reslvpath[i]!='\0';i++) {
				mtransldir[mesz+i]=reslvpath[i];
				if(reslvpath[i]=='/') {
					mtransldir[mesz+i+1]='\0';
					true_mkdir(mtransldir,S_IRWXU);
				}
			}
	
			true_mkdir(mtranslpath,S_IRWXU);
		}

		pthis=pthis->next;
	}

	finalize: 

	return rcod;
}

/*
 * procedure = / rc:=instw_setpath(instw,path) /
 *
 * task      = /   sets the 'instw->path' field and updates all the fields that 
 *               can be deduced from 'path', such as 'instw->translpath'. /
 *
 * inputs    = / path               The given path, as is 
 * outputs   = / instw->path        A stored copy of 'path'
 *               instw->truepath    The given path, canonicalized
 *               instw->translpath  The real translated path 
 *               instw->mtranslpath The translation status path  /
 *
 * returns   = /  0 ok. path set
 *               -1 failed. cf errno /
 */
static int instw_setpath(instw_t *instw,const char *path) {
	size_t relen;
	size_t trlen;
	size_t melen;

#ifndef NDEBUG
	debug(2,"instw_setpath(%p,%s)\n",instw,path);
#endif

	instw->status=0;

	strncpy(instw->path,path,PATH_MAX);
	instw->truepath[0]='\0';

	if(instw->path[0]!='/') {
		true_getcwd(instw->truepath,PATH_MAX+1);
		if(instw->truepath[strlen(instw->truepath)-1]!='/'){
			strcat(instw->truepath,"/");
		}
		strcat(instw->truepath,instw->path);
	} else {
		strcpy(instw->truepath,instw->path);
	}
	relen=strlen(instw->truepath);

	  /* 
	   *   if library is not completely initialized, or if translation 
	   * is not active, we make things so it is equivalent to the
	   * to the identity, this avoid needs to cope with special cases.
	   */
	if(	!(instw->gstatus&INSTW_INITIALIZED) || 
		!(instw->gstatus&INSTW_OKTRANSL)) {
		strncpy(instw->reslvpath,instw->truepath,PATH_MAX);
		strncpy(instw->translpath,instw->truepath,PATH_MAX);
		return 0;
	}
	
	  /*
	   *   we fill instw->reslvpath , applying the inversed translation
	   * if truepath is inside /transl.
	   */
	if(strstr(instw->truepath,instw->transl)==instw->truepath) {
		strcpy(instw->reslvpath,instw->truepath+strlen(instw->transl));
	} else {
		strcpy(instw->reslvpath,instw->truepath);
	}

	  /*
	   *   if instw->path is relative, no troubles.
	   *   but if it is absolute and located under /transl, we have 
	   * to untranslate it.
	   */
	if( (instw->path[0]=='/') &&
	    (strstr(instw->path,instw->transl)==instw->path)) {
		strcpy(instw->path,instw->reslvpath);
	} 

	  /*
	   * We must detect early 'path' matching with already translated files  
	   */
	if(path_excluded(instw->truepath)) {
		strncpy(instw->translpath,instw->truepath,PATH_MAX);
		instw->status |= ( INSTW_TRANSLATED | INSTW_IDENTITY);
	} else {
		  /* Building the real translated path */
		strncpy(instw->translpath,instw->transl,PATH_MAX);
		trlen=strlen(instw->translpath);
		if((trlen+relen)>PATH_MAX) {
			instw->error=errno=ENAMETOOLONG;
			return -1;
		}
		strncat(instw->translpath,instw->reslvpath,PATH_MAX-trlen);
		instw->translpath[PATH_MAX]='\0';
	}	

	  /* Building the translation status path */
	strncpy(instw->mtranslpath,instw->mtransl,PATH_MAX);
	instw->mtranslpath[PATH_MAX]='\0';
	melen=strlen(instw->mtranslpath);
	if((melen+relen)>PATH_MAX) {
		instw->error=errno=ENAMETOOLONG;
		return -1;
	}
	strncat(instw->mtranslpath,instw->reslvpath,PATH_MAX-trlen);
	instw->mtranslpath[PATH_MAX]='\0';

	return 0;
}

/*
 * procedure = / rc:=instw_getstatus(instw,status) /
 *
 * outputs   = / status  instw->path flags field status in the translated fs
 *                 INSTW_ISINROOT   file exists in the real fs 
 *                 INSTW_ISINTRANSL file exists in the translated fs
 *                 INSTW_TRANSLATED file has been translated /
 *
 * returns   = /  0 ok. stated 
 *               -1 failed. cf errno /
 */
static int instw_getstatus(instw_t *instw,int *status) {
	struct stat inode;
	struct stat rinode;
	struct stat tinode;

#ifndef NDEBUG
	debug(2,"instw_getstatus(%p,%p)\n",instw,status);
#endif

	  /* 
	   * is the file referenced as being translated ?
	   */
	if( (instw->gstatus&INSTW_INITIALIZED) &&
	    (instw->gstatus&INSTW_OKTRANSL) &&
	   !(instw->status&INSTW_TRANSLATED) &&  
	   !true_stat(instw->mtranslpath,&inode) ) {
		instw->status |= INSTW_TRANSLATED;
	}

	  /*
	   * do the file currently exist in the translated fs ?
	   */
	if( (instw->gstatus&INSTW_INITIALIZED) &&
	     (instw->gstatus&INSTW_OKTRANSL) && 
	     !true_stat(instw->translpath,&tinode) ) {
		instw->status |= INSTW_ISINTRANSL;
	}	

	  /*
	   * is it a newly created file, or a modified one ?
	   */
	if( instw->gstatus&INSTW_INITIALIZED &&
	    !true_stat(instw->reslvpath,&rinode) ) {
		instw->status |= INSTW_ISINROOT;
	}
	
	  /*
	   *   if the file exists, why is it not referenced as 
	   * being translated ?
	   *   we have to reference it and all the traversed 
	   * directories leading to it.
	   */
	if( (instw->gstatus&INSTW_INITIALIZED) &&
	    (instw->gstatus&INSTW_OKTRANSL) &&
	    (instw->status&INSTW_ISINTRANSL) &&
	    !(instw->status&INSTW_TRANSLATED) ) {
		instw->status |= INSTW_TRANSLATED;
		instw_setmetatransl(instw);
	}    
	  
	  /*
	   *   are the public resolved path and its translated counterpart 
	   * identical ? if so, we flag it
	   */
	if( (instw->gstatus & INSTW_INITIALIZED) &&
	    (instw->gstatus & INSTW_OKTRANSL) && 
	    (instw->status & INSTW_TRANSLATED) && 
	    (0==(strcmp(instw->truepath,instw->translpath))) ) {
		instw->status |= INSTW_IDENTITY;  
	}    

	*status=instw->status;

	return 0;
}

/*
 * procedure = / rc:=instw_apply(instw) /
 *
 * task      = /   actually do the translation prepared in 'transl' /
 *
 * note      = /   --after a call to instw_apply(), the translation related 
 *                 status flags are updated. 
 *                 --if a translation is requested and if the original file 
 *                 exists, all parent directories are created and referenced
 *                 if necessary.
 *                   if the original file does not exist, we translate at
 *                 least the existing path. /
 *
 * returns   = /  0 ok. translation done 
 *               -1 failed. cf errno     /
 */
static int instw_apply(instw_t *instw) {
	int rcod=0;
	int status=0;
	
	char dirpart[PATH_MAX+1];
	char basepart[PATH_MAX+1];
	char *pdir;
	char *pbase;
	struct stat reslvinfo;
	instw_t iw;
	char wpath[PATH_MAX+1];
	size_t wsz=0;
	char linkpath[PATH_MAX+1];


#ifndef NDEBUG
	debug(2,"instw_apply(%p)\n",instw);
	instw_print(instw);
#endif

	  /* 
	   * if library incompletely initialized or if translation 
	   * is inactive, nothing to apply 
	   */
	if( !(instw->gstatus&INSTW_INITIALIZED) ||
	    !(instw->gstatus&INSTW_OKTRANSL) )  finalize(0); 
	 
	  /* let's get the file translation status */
	if(instw_getstatus(instw,&status)) finalize(-1);

	  /* we ignore files already translated */
	if(status & INSTW_TRANSLATED) return 0;

	strcpy(basepart,instw->reslvpath);
	strcpy(dirpart,instw->reslvpath);
	
	pbase=basename(basepart);
	pdir=dirname(dirpart);

	  /* recursivity termination test, */
	if(pdir[0]=='/' && pdir[1]=='\0' && pbase[0]=='\0') {
		instw->status|=INSTW_TRANSLATED;
		finalize(0);
	}	
	
	instw_new(&iw);	
	instw_setpath(&iw,pdir);
	instw_apply(&iw);
	instw_delete(&iw);

	  /* will we have to copy the original file ? */
	if(!true_lstat(instw->reslvpath,&reslvinfo)) {
		copy_path(instw->reslvpath,instw->transl);

		  /* a symlink ! we have to translate the target */
		if(S_ISLNK(reslvinfo.st_mode)) {
			wsz=true_readlink(instw->reslvpath,wpath,PATH_MAX);
			wpath[wsz]='\0';

			instw_new(&iw);
			if(wpath[0]!='/') { 
				strcpy(linkpath,pdir);
				strcat(linkpath,"/");
				strcat(linkpath,wpath);
			} else {
				strcpy(linkpath,wpath);
			}

			instw_setpath(&iw,linkpath);
			instw_apply(&iw);
			instw_delete(&iw);
		}
	}

	
	instw->status|=INSTW_TRANSLATED;
	instw_setmetatransl(instw);

	finalize:

	return rcod;
}

/*
 * procedure = / rc:=instw_filldirls(instw) /
 *
 * task      = /   used to create dummy entries in the mdirlspath reflecting 
 *               the content that would have been accessible with no
 *               active translation. /
 *
 * note      = / 
 *	--This procedure must be called after instw_makedirls() has been 
 *	called itself.
 *	--It implies that the translated directory and the real one are
 *      distincts, but it does not matter if one of them, or both is empty
 * /
 */
static int instw_filldirls(instw_t *instw) {
	int rcod=0;
	DIR *wdir;
	struct dirent *went;
	char spath[PATH_MAX+1];
	char dpath[PATH_MAX+1];
	char lpath[PATH_MAX+1];
	struct stat sinfo;
	struct stat dinfo;
	int wfd;
	size_t wsz;
	instw_t iw_entry;
	int status=0;

#ifndef NDEBUG
	debug(2,"instw_filldirls(%p)\n",instw);
#endif

	if((wdir=true_opendir(instw->translpath))==NULL) { return -1; }	
	while((went=true_readdir(wdir))!=NULL) {
		if( 	went->d_name[0]=='.' && 
			(	(went->d_name[1]=='\0') ||
				(	went->d_name[1]=='.' &&
					went->d_name[2]=='\0') ) ) 
			{ continue; }	

		strcpy(spath,instw->translpath);
		strcat(spath,"/");
		strcat(spath,went->d_name);
		
		if(true_lstat(spath,&sinfo)) { continue; }

		strcpy(dpath,instw->mdirlspath);
		strcat(dpath,"/");
		strcat(dpath,went->d_name);

		  /* symbolic links */
		if(S_ISLNK(sinfo.st_mode)) {
			if((wsz=true_readlink(spath,lpath,PATH_MAX))>=0) { 
				lpath[wsz]='\0';
				true_symlink(lpath,dpath); 
#ifndef NDEBUG
				debug(4,"\tfilled symlink       : %s\n",dpath);
#endif
			}
				
		}

		  /* regular file */
		if(S_ISREG(sinfo.st_mode)) {
			if((wfd=true_creat(dpath,sinfo.st_mode))>=0) {
				close(wfd); 
#ifndef NDEBUG
				debug(4,"\tfilled regular file  : %s\n",dpath);
#endif
			}
		}
	
		  /* directory */
		if(S_ISDIR(sinfo.st_mode)) {
			true_mkdir(dpath,sinfo.st_mode);
#ifndef NDEBUG
			debug(4,"\tfilled directory     : %s\n",dpath);
#endif

		}
	
		  /* block special file */
		if(S_ISBLK(sinfo.st_mode)) {
			true_mknod(dpath,sinfo.st_mode|S_IFBLK,sinfo.st_rdev);
#ifndef NDEBUG
			debug(4,"\tfilled special block : %s\n",dpath);
#endif

		}
	
		  /* character special file */
		if(S_ISCHR(sinfo.st_mode)) {
			true_mknod(dpath,sinfo.st_mode|S_IFCHR,sinfo.st_rdev);
#ifndef NDEBUG
			debug(4,"\tfilled special char  : %s\n",dpath);
#endif
		}
		 
		  /* fifo special file */
		if(S_ISFIFO(sinfo.st_mode)) {
			true_mknod(dpath,sinfo.st_mode|S_IFIFO,0);
#ifndef NDEBUG
			debug(4,"\tfilled special fifo  : %s\n",dpath);
#endif
		}
			
	}
	closedir(wdir);

	if((wdir=true_opendir(instw->reslvpath))==NULL) return -1;
	while((went=true_readdir(wdir))!=NULL) {
		if( 	went->d_name[0]=='.' && 
			(	(went->d_name[1]=='\0') ||
				(	went->d_name[1]=='.' &&
					went->d_name[2]=='\0') ) ) 
			{ continue; }	

		strcpy(spath,instw->reslvpath);
		strcat(spath,"/");
		strcat(spath,went->d_name);
		if(true_lstat(spath,&sinfo)) { continue; }

		instw_new(&iw_entry);
		instw_setpath(&iw_entry,spath);
		instw_getstatus(&iw_entry,&status);

		  /*
		   *   This entry exists in the real fs, but has been 
		   * translated and destroyed in the translated fs.
		   *   So, we mustn't present it !!!
		   */
		if( (status & INSTW_TRANSLATED) &&
		    !(status & INSTW_ISINTRANSL) ) { continue; }

		strcpy(dpath,instw->mdirlspath);
		strcat(dpath,"/");
		strcat(dpath,went->d_name);
	
		  /* already exists in the translated fs, we iterate */
		if(!true_lstat(dpath,&dinfo)) { continue; }

		  /* symbolic links */
		if(S_ISLNK(sinfo.st_mode)) {
			if((wsz=true_readlink(spath,lpath,PATH_MAX))>=0) {
				lpath[wsz]='\0';
				true_symlink(lpath,dpath);
#ifndef NDEBUG
				debug(4,"\tfilled symlink       : %s\n",dpath);
#endif
			}
		}

		  /* regular file */
		if(S_ISREG(sinfo.st_mode)) {
			if((wfd=true_creat(dpath,sinfo.st_mode))>=0) {
				close(wfd); 
#ifndef NDEBUG
				debug(4,"\tfilled regular file  : %s\n",dpath);
#endif
			}	
		}
	
		  /* directory */
		if(S_ISDIR(sinfo.st_mode)) {
			true_mkdir(dpath,sinfo.st_mode);
#ifndef NDEBUG
			debug(4,"\tfilled directory     : %s\n",dpath);
#endif
		}
	
		  /* block special file */
		if(S_ISBLK(sinfo.st_mode)) {
			true_mknod(dpath,sinfo.st_mode|S_IFBLK,sinfo.st_rdev);
#ifndef NDEBUG
			debug(4,"\tfilled special block : %s\n",dpath);
#endif
		}
	
		  /* character special file */
		if(S_ISCHR(sinfo.st_mode)) {
			true_mknod(dpath,sinfo.st_mode|S_IFCHR,sinfo.st_rdev);
#ifndef NDEBUG
			debug(4,"\tfilled special char  : %s\n",dpath);
#endif
		}
		 
		  /* fifo special file */
		if(S_ISFIFO(sinfo.st_mode)) {
			true_mknod(dpath,sinfo.st_mode|S_IFIFO,0);
#ifndef NDEBUG
			debug(4,"\tfilled special fifo  : %s\n",dpath);
#endif
		}
		
		instw_delete(&iw_entry);
	}
	closedir(wdir);

	return rcod;
}

/*
 * procedure = / rc:=instw_makedirls(instw) /
 *
 * task      = /   eventually prepares a fake temporary directory used to 
 *               present 'overlaid' content to opendir(),readdir()... /
 *
 * note      = /
 *      --This procedure must be called after instw_setpath(). 
 *
 * 	--The "fake" temporary directories are created and...forgotten.
 *      If we need to reuse later the same directory, it is previously 
 *      erased, which ensures that it is correclty refreshed.
 * /
 *
 * returns   = /  0 ok. makedirls done 
 *               -1 failed. cf errno     /
 */
static int instw_makedirls(instw_t *instw) {
	int rcod=0;
	int status=0;
	struct stat translinfo;
	struct stat dirlsinfo;
	char wdirname[NAME_MAX+1];

#ifndef NDEBUG
	debug(2,"instw_makedirls(%p)\n",instw);
#endif

	  /* 
	   * if library incompletely initialized or if translation 
	   * is inactive, nothing to do 
	   */
	if( !(instw->gstatus&INSTW_INITIALIZED) ||
	    !(instw->gstatus&INSTW_OKTRANSL)) {
	    strcpy(instw->mdirlspath,instw->path);
	    return 0; 
	} 
	 
	  /* let's get the file translation status */
	if(instw_getstatus(instw,&status)) return -1;

	if( !(status&INSTW_TRANSLATED) ||
	    ((status&INSTW_TRANSLATED) && (status&INSTW_IDENTITY)) ) {
		strcpy(instw->mdirlspath,instw->path);
	} else {
		  /*   if it's a new directory, we open it in 
		   * the translated fs .
		   *   otherwise, it means that we will have to construct a
		   * merged directory.
		   */
		if(!(status & INSTW_ISINROOT)) {
			strcpy(instw->mdirlspath,instw->translpath);
		} else {
			rcod=true_stat(instw->translpath,&translinfo);

			sprintf(wdirname,"/%d_%lld_%lld",
					instw->pid,
					(long long int) translinfo.st_dev,
					(long long int) translinfo.st_ino);
		
			strcpy(instw->mdirlspath,instw->mdirls);
			strcat(instw->mdirlspath,wdirname);

			  /* we erase a previous identical dirls */
			if(!true_stat(instw->mdirlspath,&dirlsinfo)) {
				unlink_recursive(instw->mdirlspath);
			}
			true_mkdir(instw->mdirlspath,S_IRWXU);
	
			  /* we construct the merged directory here */
			instw_filldirls(instw);
		}
	}

#ifndef NDEBUG
	instw_print(instw);
#endif

	return rcod;
}

/* 
 *
 */
static int backup(const char *path) {
	char checkdir[BUFSIZ];
	char backup_path[BUFSIZ];
	int	placeholder,
		i,
		blen;
	struct stat inode,backup_inode;
	struct utimbuf timbuf;

#ifndef NDEBUG
	debug(2,"========= backup () =========  path: %s\n", path); 
#endif

	  /* INSTW_OKBACKUP not set, we won't do any backups */
	if (!(__instw.gstatus&INSTW_OKBACKUP)) {
		#ifdef DEBUG
		debug(3,"Backup not enabled, path: %s\n", path);
		#endif
		return 0;
	}

	/* Check if this is inside /dev */
	if (strstr (path, "/dev") == path) {
		#ifndef NDEBUG
		debug(3,"%s is inside /dev. Ignoring.\n", path);
		#endif
		return 0; 
	}

	/* Now check for /tmp */
	if (strstr (path, "/tmp") == path) {
		#ifndef NDEBUG
		debug(3,"%s is inside /tmp. Ignoring.\n", path);
		#endif
		return 0; 
	}

	/* Finally, the backup path itself */
	if (strstr (path,__instw.backup ) == path) {
		#ifndef NDEBUG
		debug(3,"%s is inside the backup path. Ignoring.\n", path);
		#endif
		return 0; 
	}

	/* Does it exist already? */
	#ifndef NDEBUG
	debug(3,"Exists %s?\n", path);
	#endif
	if (true_stat(path, &inode) < 0) {

		/* It doesn't exist, we'll tag it so we won't back it up  */
		/* if we run into it later                                */

		strcpy(backup_path,__instw.backup );
		strncat(backup_path, "/no-backup", 11);
		strcat(backup_path, path);
		make_path(backup_path);

		/* This one's just a placeholder */
		placeholder = true_creat(backup_path, S_IREAD);  
		if (!(placeholder < 0)) close (placeholder);

		#ifndef NDEBUG
		debug(3,"does not exist\n");
		#endif
		return 0;
	}


	/* Is this one tagged for no backup (i.e. it didn't previously exist)? */
	strcpy (backup_path,__instw.backup);
	strncat (backup_path, "/no-backup", 11);
	strcat (backup_path, path);

	if (true_stat (backup_path, &backup_inode) >= 0) {
		#ifndef NDEBUG
		debug(3,"%s must not be backed up\n", backup_path);
		#endif
		return 0;
	}


	#ifndef NDEBUG
	debug(3,"Si existe, veamos de que tipo es.\n");
	#endif

	/* Append the path to the backup_path */
	strcpy (backup_path,__instw.backup);
	strcat (backup_path, path);

	/* Create the directory tree for this file in the backup dir */
	make_path (backup_path);

	  /* let's backup the source file */
	if(copy_path(path,__instw.backup))
		return -1;

	  /* Check the owner and permission of the created directories */
 	i=0;
	blen = strlen (__instw.backup);
	while ( path[i] != '\0' ) {
		checkdir[i] = backup_path[blen+i] = path[i];
		if (checkdir[i] == '/') {  /* Each time a '/' is found, check if the    */
			checkdir[i+1] = '\0';   /* path exists. If it does, set it's perms.  */
			if (!true_stat (checkdir, &inode)) {
			backup_path[blen+i+1]='\0';
			timbuf.actime=inode.st_atime;
			timbuf.modtime=inode.st_mtime;
			true_utime(backup_path, &timbuf);
			true_chmod(backup_path, inode.st_mode);
			true_chown(backup_path, inode.st_uid, inode.st_gid);
			}
		}
		i++;
	}
	
	return 0;
}
