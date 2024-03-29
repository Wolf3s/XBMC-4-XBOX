/*
 * Copyright (C) 2001, 2002 Billy Biggs <vektor@dumbterm.net>,
 *                          H�kan Hjort <d95hjort@dtek.chalmers.se>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111, USA.
 */

#include "config.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h> /* For the timing of dvdcss_title crack. */
#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>
#include <dirent.h>
 
#if defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__) || defined(__bsdi__)|| defined(__DARWIN__)
#define SYS_BSD 1
#endif

#if defined(__sun)
#include <sys/mnttab.h>
#elif defined(hpux)
#include </usr/conf/h/mnttab.h>
#elif defined(SYS_BSD)
#include <fstab.h>
#elif defined(__linux__)
#include <mntent.h>
#endif

#if defined(__MINGW32__) && (__MINGW32_MAJOR_VERSION <= 3) && (__MINGW32_MINOR_VERSION < 10)
#include <sys/timeb.h>
static void gettimeofday(struct timeval* t,void* timezone){
    struct timeb timebuffer;
    ftime( &timebuffer );
    t->tv_sec=timebuffer.time;
    t->tv_usec=1000*timebuffer.millitm;
}
#endif

#include "dvd_udf.h"
#include "dvd_input.h"
#include "dvd_reader.h"

struct dvd_reader_s {
    /* Basic information. */
    int isImageFile;
  
    /* Hack for keeping track of the css status. 
     * 0: no css, 1: perhaps (need init of keys), 2: have done init */
    int css_state;
    int css_title; /* Last title that we have called DVDinpute_title for. */

    /* Information required for an image file. */
    dvd_input_t dev;

    /* Information required for a directory path drive. */
    char *path_root;
};

struct dvd_file_s {
    /* Basic information. */
    dvd_reader_t *dvd;
  
    /* Hack for selecting the right css title. */
    int css_title;

    /* Information required for an image file. */
    uint32_t lb_start;
    uint32_t seek_pos;

    /* Information required for a directory path drive. */
    size_t title_sizes[ 9 ];
    dvd_input_t title_devs[ 9 ];

    /* Calculated at open-time, size in blocks. */
    ssize_t filesize;
};

/* Loop over all titles and call dvdcss_title to crack the keys. */
static int initAllCSSKeys( dvd_reader_t *dvd )
{
    struct timeval all_s, all_e;
    struct timeval t_s, t_e;
    char filename[ MAX_UDF_FILE_NAME_LEN ];
    uint32_t start, len;
    int title;
	
    fprintf( stderr, "\n" );
    fprintf( stderr, "libdvdread: Attempting to retrieve all CSS keys\n" );
    fprintf( stderr, "libdvdread: This can take a _long_ time, "
	     "please be patient\n\n" );
	
    gettimeofday(&all_s, NULL);
	
    for( title = 0; title < 100; title++ ) {
	gettimeofday( &t_s, NULL );
	if( title == 0 ) {
	    sprintf( filename, "/VIDEO_TS/VIDEO_TS.VOB" );
	} else {
	    sprintf( filename, "/VIDEO_TS/VTS_%02d_%d.VOB", title, 0 );
	}
	start = UDFFindFile( dvd, filename, &len );
	if( start != 0 && len != 0 ) {
	    /* Perform CSS key cracking for this title. */
	    fprintf( stderr, "libdvdread: Get key for %s at 0x%08x\n", 
		     filename, start );
	    if( DVDinput_title( dvd->dev, (int)start ) < 0 ) {
		fprintf( stderr, "libdvdread: Error cracking CSS key for %s (0x%08x)\n", filename, start);
	    }
	    gettimeofday( &t_e, NULL );
	    fprintf( stderr, "libdvdread: Elapsed time %ld\n",  
		     (long int) t_e.tv_sec - t_s.tv_sec );
	}
	    
	if( title == 0 ) continue;
	    
	gettimeofday( &t_s, NULL );
	sprintf( filename, "/VIDEO_TS/VTS_%02d_%d.VOB", title, 1 );
	start = UDFFindFile( dvd, filename, &len );
	if( start == 0 || len == 0 ) break;
	    
	/* Perform CSS key cracking for this title. */
	fprintf( stderr, "libdvdread: Get key for %s at 0x%08x\n", 
		 filename, start );
	if( DVDinput_title( dvd->dev, (int)start ) < 0 ) {
	    fprintf( stderr, "libdvdread: Error cracking CSS key for %s (0x%08x)!!\n", filename, start);
	}
	gettimeofday( &t_e, NULL );
	fprintf( stderr, "libdvdread: Elapsed time %ld\n",  
		 (long int) t_e.tv_sec - t_s.tv_sec );
    }
    title--;
    
    fprintf( stderr, "libdvdread: Found %d VTS's\n", title );
    gettimeofday(&all_e, NULL);
    fprintf( stderr, "libdvdread: Elapsed time %ld\n",  
	     (long int) all_e.tv_sec - all_s.tv_sec );
    
    return 0;
}


#ifndef HAVE_MPLAYER
 #include "get_path.c"
#else
 extern char * get_path( char * filename );
#endif

//extern char * dvdcss_cache_dir;

/**
 * Open a DVD image or block device file.
 */
static dvd_reader_t *DVDOpenImageFile( const char *location, int have_css )
{
    dvd_reader_t *dvd;
    dvd_input_t dev;

    /* setup cache dir is no longer needed, it's now implemented in libdvdcss.c
    if(!dvdcss_cache_dir){
	dvdcss_cache_dir=get_path( "" );
	if ( dvdcss_cache_dir ) { mkdir( dvdcss_cache_dir,493 ); free( dvdcss_cache_dir ); }
	dvdcss_cache_dir=get_path( "DVDKeys" );
	if(dvdcss_cache_dir) mkdir( dvdcss_cache_dir,493 );
    }
    */
    
    /* open it */
    dev = DVDinput_open( location );
    if( !dev ) {
	fprintf( stderr, "libdvdread: Can't open %s for reading\n", location );
	return 0;
    }

    dvd = (dvd_reader_t *) malloc( sizeof( dvd_reader_t ) );
    if( !dvd ) return 0;
    dvd->isImageFile = 1;
    dvd->dev = dev;
    dvd->path_root = 0;
    
    if( have_css ) {
      /* Only if DVDCSS_METHOD = title, a bit if it's disc or if
       * DVDCSS_METHOD = key but region missmatch. Unfortunaly we
       * don't have that information. */
    
      dvd->css_state = 1; /* Need key init. */
    }
    
    return dvd;
}

static dvd_reader_t *DVDOpenPath( const char *path_root )
{
    dvd_reader_t *dvd;

    dvd = (dvd_reader_t *) malloc( sizeof( dvd_reader_t ) );
    if( !dvd ) return 0;
    dvd->isImageFile = 0;
    dvd->dev = 0;
    dvd->path_root = strdup( path_root );

    return dvd;
}

#if defined(__sun)
/* /dev/rdsk/c0t6d0s0 (link to /devices/...)
   /vol/dev/rdsk/c0t6d0/??
   /vol/rdsk/<name> */
static char *sun_block2char( const char *path )
{
    char *new_path;

    /* Must contain "/dsk/" */ 
    if( !strstr( path, "/dsk/" ) ) return (char *) strdup( path );

    /* Replace "/dsk/" with "/rdsk/" */
    new_path = malloc( strlen(path) + 2 );
    strcpy( new_path, path );
    strcpy( strstr( new_path, "/dsk/" ), "" );
    strcat( new_path, "/rdsk/" );
    strcat( new_path, strstr( path, "/dsk/" ) + strlen( "/dsk/" ) );

    return new_path;
}
#endif

#if defined(SYS_BSD)
/* FreeBSD /dev/(r)(a)cd0c (a is for atapi), recomended to _not_ use r
   OpenBSD /dev/rcd0c, it needs to be the raw device
   NetBSD  /dev/rcd0[d|c|..] d for x86, c (for non x86), perhaps others
   Darwin  /dev/rdisk0,  it needs to be the raw device
   BSD/OS  /dev/sr0c (if not mounted) or /dev/rsr0c ('c' any letter will do) */
static char *bsd_block2char( const char *path )
{
    char *new_path;

    /* If it doesn't start with "/dev/" or does start with "/dev/r" exit */ 
    if( strncmp( path, "/dev/",  5 ) || !strncmp( path, "/dev/r", 6 ) ) 
      return (char *) strdup( path );

    /* Replace "/dev/" with "/dev/r" */
    new_path = malloc( strlen(path) + 2 );
    strcpy( new_path, "/dev/r" );
    strcat( new_path, path + strlen( "/dev/" ) );

    return new_path;
}
#endif

dvd_reader_t *DVDOpen( const char *path )
{
    struct stat fileinfo;
    int ret, have_css;
    char *dev_name = 0;

    if( !path ) return 0;

#ifdef WIN32
    /* Stat doesn't work on devices under mingwin/cygwin. */
    if( path[0] && path[1] == ':' && path[2] == '\0' )
    {
        /* Don't try to stat the file */
        fileinfo.st_mode = S_IFBLK;
    }
    else
#endif
    {
#ifndef _XBOX
    ret = stat( path, &fileinfo );
    if( ret < 0 ) {
	/* If we can't stat the file, give up */
	fprintf( stderr, "libdvdread: Can't stat %s\n", path );
	perror("");
	return 0;
    }
#endif //!_XBOX
    }

    /* Try to open libdvdcss or fall back to standard functions */
    have_css = DVDInputSetup();

#ifdef _XBOX
	if (strstr(path, ".img") || strstr(path,".IMG") )
	{
#else
    /* First check if this is a block/char device or a file*/
    if( S_ISBLK( fileinfo.st_mode ) || 
	S_ISCHR( fileinfo.st_mode ) || 
	S_ISREG( fileinfo.st_mode ) ) {
#endif //!_XBOX
	/**
	 * Block devices and regular files are assumed to be DVD-Video images.
	 */
#if defined(__sun)
	return DVDOpenImageFile( sun_block2char( path ), have_css );
#elif defined(SYS_BSD)
	return DVDOpenImageFile( bsd_block2char( path ), have_css );
#else
	return DVDOpenImageFile( path, have_css );
#endif

#ifdef _XBOX
	} else {
#else
    } else if( S_ISDIR( fileinfo.st_mode ) ) {
#endif //!_XBOX
	dvd_reader_t *auth_drive = 0;
	char *path_copy;
#if defined(SYS_BSD)
	struct fstab* fe;
#elif defined(__sun) || defined(__linux__)
	FILE *mntfile;
#endif

	/* XXX: We should scream real loud here. */
	if( !(path_copy = strdup( path ) ) ) return 0;

#ifndef _XBOX
	/* Resolve any symlinks and get the absolut dir name. */
	{
	    char *new_path;
	    int cdir = open( ".", O_RDONLY );
	    
	    if( cdir >= 0 ) {
		chdir( path_copy );
		new_path = getcwd( NULL, PATH_MAX );
#ifndef __MINGW32__       
		fchdir( cdir );
#endif       
		close( cdir );
		if( new_path ) {
		    free( path_copy );
		    path_copy = new_path;
		}
	    }
	}
#endif //!_XBOX
	/**
	 * If we're being asked to open a directory, check if that directory
	 * is the mountpoint for a DVD-ROM which we can use instead.
	 */

	if( strlen( path_copy ) > 1 ) {
	    if( path_copy[ strlen( path_copy ) - 1 ] == '/' ) 
		path_copy[ strlen( path_copy ) - 1 ] = '\0';
	}

	if( strlen( path_copy ) > 9 ) {
	    if( !strcasecmp( &(path_copy[ strlen( path_copy ) - 9 ]), 
			     "/video_ts" ) ) {
	      path_copy[ strlen( path_copy ) - 9 ] = '\0';
	    }
	}

#if defined(SYS_BSD)
	if( ( fe = getfsfile( path_copy ) ) ) {
	    dev_name = bsd_block2char( fe->fs_spec );
	    fprintf( stderr,
		     "libdvdread: Attempting to use device %s"
		     " mounted on %s for CSS authentication\n",
		     dev_name,
		     fe->fs_file );
	    auth_drive = DVDOpenImageFile( dev_name, have_css );
	}
#elif defined(__sun)
	mntfile = fopen( MNTTAB, "r" );
	if( mntfile ) {
	    struct mnttab mp;
	    int res;

	    while( ( res = getmntent( mntfile, &mp ) ) != -1 ) {
		if( res == 0 && !strcmp( mp.mnt_mountp, path_copy ) ) {
		    dev_name = sun_block2char( mp.mnt_special );
		    fprintf( stderr, 
			     "libdvdread: Attempting to use device %s"
			     " mounted on %s for CSS authentication\n",
			     dev_name,
			     mp.mnt_mountp );
		    auth_drive = DVDOpenImageFile( dev_name, have_css );
		    break;
		}
	    }
	    fclose( mntfile );
	}
#elif defined(__linux__)
        mntfile = fopen( MOUNTED, "r" );
        if( mntfile ) {
            struct mntent *me;
 
            while( ( me = getmntent( mntfile ) ) ) {
                if( !strcmp( me->mnt_dir, path_copy ) ) {
		    fprintf( stderr, 
			     "libdvdread: Attempting to use device %s"
			     " mounted on %s for CSS authentication\n",
			     me->mnt_fsname,
			     me->mnt_dir );
                    auth_drive = DVDOpenImageFile( me->mnt_fsname, have_css );
		    dev_name = strdup(me->mnt_fsname);
                    break;
                }
            }
            fclose( mntfile );
	}
#elif defined(WIN32)	
	dev_name = strdup(path);
	auth_drive = DVDOpenImageFile( path, have_css );
#endif
	if( !dev_name ) {
	  fprintf( stderr, "libdvdread: Couldn't find device name.\n" );
	} else if( !auth_drive ) {
	    fprintf( stderr, "libdvdread: Device %s inaccessible, "
		     "CSS authentication not available.\n", dev_name );
	}

	free( dev_name );
	free( path_copy );

        /**
         * If we've opened a drive, just use that.
         */
        if( auth_drive ) return auth_drive;

        /**
         * Otherwise, we now try to open the directory tree instead.
         */
        return DVDOpenPath( path );
    }

    /* If it's none of the above, screw it. */
    fprintf( stderr, "libdvdread: Could not open %s\n", path );
    return 0;
}

void DVDClose( dvd_reader_t *dvd )
{
    if( dvd ) {
        if( dvd->dev ) DVDinput_close( dvd->dev );
        if( dvd->path_root ) free( dvd->path_root );
        free( dvd );
        dvd = 0;
    }
}

/**
 * Open an unencrypted file on a DVD image file.
 */
static dvd_file_t *DVDOpenFileUDF( dvd_reader_t *dvd, char *filename )
{
    uint32_t start, len;
    dvd_file_t *dvd_file;

    start = UDFFindFile( dvd, filename, &len );
    if( !start ) return 0;

    dvd_file = (dvd_file_t *) malloc( sizeof( dvd_file_t ) );
    if( !dvd_file ) return 0;
    dvd_file->dvd = dvd;
    dvd_file->lb_start = start;
    dvd_file->seek_pos = 0;
    memset( dvd_file->title_sizes, 0, sizeof( dvd_file->title_sizes ) );
    memset( dvd_file->title_devs, 0, sizeof( dvd_file->title_devs ) );
    dvd_file->filesize = len / DVD_VIDEO_LB_LEN;

    return dvd_file;
}

/**
 * Searches for <file> in directory <path>, ignoring case.
 * Returns 0 and full filename in <filename>.
 *     or -1 on file not found.
 *     or -2 on path not found.
 */
static int findDirFile( const char *path, const char *file, char *filename ) 
{
#ifdef _XBOX
	if(path[strlen(path)-1]=='\\' || path[strlen(path)-1]=='/')
		sprintf(filename, "%s%s", path, file);
	else
		sprintf(filename, "%s/%s", path, file);

	struct stat fileinfo;

	if(stat(filename, &fileinfo)==0)
		return 0;
#else
    DIR *dir;
    struct dirent *ent;

    dir = opendir( path );
    if( !dir ) return -2;

    while( ( ent = readdir( dir ) ) != NULL ) {
        if( !strcasecmp( ent->d_name, file ) ) {
            sprintf( filename, "%s%s%s", path,
                     ( ( path[ strlen( path ) - 1 ] == '/' ) ? "" : "/" ),
                     ent->d_name );
            return 0;
        }
    }
#endif //!_XBOX
    return -1;
}

static int findDVDFile( dvd_reader_t *dvd, const char *file, char *filename )
{
    char video_path[ PATH_MAX + 1 ];
    const char *nodirfile;
    int ret;

    /* Strip off the directory for our search */
    if( !strncasecmp( "/VIDEO_TS/", file, 10 ) ) {
        nodirfile = &(file[ 10 ]);
    } else {
        nodirfile = file;
    }

    ret = findDirFile( dvd->path_root, nodirfile, filename );
    if( ret < 0 ) {
        /* Try also with adding the path, just in case. */
        sprintf( video_path, "%s/VIDEO_TS/", dvd->path_root );
        ret = findDirFile( video_path, nodirfile, filename );
        if( ret < 0 ) {
            /* Try with the path, but in lower case. */
            sprintf( video_path, "%s/video_ts/", dvd->path_root );
            ret = findDirFile( video_path, nodirfile, filename );
            if( ret < 0 ) {
                return 0;
            }
        }
    }

    return 1;
}

/**
 * Open an unencrypted file from a DVD directory tree.
 */
static dvd_file_t *DVDOpenFilePath( dvd_reader_t *dvd, char *filename )
{
    char full_path[ PATH_MAX + 1 ];
    dvd_file_t *dvd_file;
    struct stat fileinfo;
    dvd_input_t dev;

    /* Get the full path of the file. */
    if( !findDVDFile( dvd, filename, full_path ) ) return 0;

    dev = DVDinput_open( full_path );
    if( !dev ) return 0;

    dvd_file = (dvd_file_t *) malloc( sizeof( dvd_file_t ) );
    if( !dvd_file ) return 0;
    dvd_file->dvd = dvd;
    dvd_file->lb_start = 0;
    dvd_file->seek_pos = 0;
    memset( dvd_file->title_sizes, 0, sizeof( dvd_file->title_sizes ) );
    memset( dvd_file->title_devs, 0, sizeof( dvd_file->title_devs ) );
    dvd_file->filesize = 0;

    if( stat( full_path, &fileinfo ) < 0 ) {
        fprintf( stderr, "libdvdread: Can't stat() %s.\n", filename );
        free( dvd_file );
        return 0;
    }
    dvd_file->title_sizes[ 0 ] = fileinfo.st_size / DVD_VIDEO_LB_LEN;
    dvd_file->title_devs[ 0 ] = dev;
    dvd_file->filesize = dvd_file->title_sizes[ 0 ];

    return dvd_file;
}

static dvd_file_t *DVDOpenVOBUDF( dvd_reader_t *dvd, int title, int menu )
{
    char filename[ MAX_UDF_FILE_NAME_LEN ];
    uint32_t start, len;
    dvd_file_t *dvd_file;

    if( title == 0 ) {
        sprintf( filename, "/VIDEO_TS/VIDEO_TS.VOB" );
    } else {
        sprintf( filename, "/VIDEO_TS/VTS_%02d_%d.VOB", title, menu ? 0 : 1 );
    }
    start = UDFFindFile( dvd, filename, &len );
    if( start == 0 ) return 0;

    dvd_file = (dvd_file_t *) malloc( sizeof( dvd_file_t ) );
    if( !dvd_file ) return 0;
    dvd_file->dvd = dvd;
    /*Hack*/ dvd_file->css_title = title << 1 | menu;
    dvd_file->lb_start = start;
    dvd_file->seek_pos = 0;
    memset( dvd_file->title_sizes, 0, sizeof( dvd_file->title_sizes ) );
    memset( dvd_file->title_devs, 0, sizeof( dvd_file->title_devs ) );
    dvd_file->filesize = len / DVD_VIDEO_LB_LEN;

    /* Calculate the complete file size for every file in the VOBS */
    if( !menu ) {
        int cur;

        for( cur = 2; cur < 10; cur++ ) {
            sprintf( filename, "/VIDEO_TS/VTS_%02d_%d.VOB", title, cur );
            if( !UDFFindFile( dvd, filename, &len ) ) break;
            dvd_file->filesize += len / DVD_VIDEO_LB_LEN;
        }
    }
    
    if( dvd->css_state == 1 /* Need key init */ ) {
//        initAllCSSKeys( dvd );
//	dvd->css_state = 2;
    }
    /*    
    if( DVDinput_seek( dvd_file->dvd->dev, 
		       (int)start, DVDINPUT_SEEK_KEY ) < 0 ) {
        fprintf( stderr, "libdvdread: Error cracking CSS key for %s\n",
		 filename );
    }
    */
    
    return dvd_file;
}

static dvd_file_t *DVDOpenVOBPath( dvd_reader_t *dvd, int title, int menu )
{
    char filename[ MAX_UDF_FILE_NAME_LEN ];
    char full_path[ PATH_MAX + 1 ];
    struct stat fileinfo;
    dvd_file_t *dvd_file;
    int i;

    dvd_file = (dvd_file_t *) malloc( sizeof( dvd_file_t ) );
    if( !dvd_file ) return 0;
    dvd_file->dvd = dvd;
    /*Hack*/ dvd_file->css_title = title << 1 | menu;
    dvd_file->lb_start = 0;
    dvd_file->seek_pos = 0;
    memset( dvd_file->title_sizes, 0, sizeof( dvd_file->title_sizes ) );
    memset( dvd_file->title_devs, 0, sizeof( dvd_file->title_devs ) );
    dvd_file->filesize = 0;

    if( menu ) {
        dvd_input_t dev;

        if( title == 0 ) {
            sprintf( filename, "VIDEO_TS.VOB" );
        } else {
            sprintf( filename, "VTS_%02i_0.VOB", title );
        }
        if( !findDVDFile( dvd, filename, full_path ) ) {
            free( dvd_file );
            return 0;
        }

        dev = DVDinput_open( full_path );
        if( dev == NULL ) {
            free( dvd_file );
            return 0;
        }

        if( stat( full_path, &fileinfo ) < 0 ) {
            fprintf( stderr, "libdvdread: Can't stat() %s.\n", filename );
            free( dvd_file );
            return 0;
        }
        dvd_file->title_sizes[ 0 ] = fileinfo.st_size / DVD_VIDEO_LB_LEN;
        dvd_file->title_devs[ 0 ] = dev;
	DVDinput_seek( dvd_file->title_devs[0], 0, DVDINPUT_SEEK_KEY );
        dvd_file->filesize = dvd_file->title_sizes[ 0 ];

    } else {
        for( i = 0; i < 9; ++i ) {

            sprintf( filename, "VTS_%02i_%i.VOB", title, i + 1 );
            if( !findDVDFile( dvd, filename, full_path ) ) {
                break;
            }

            if( stat( full_path, &fileinfo ) < 0 ) {
                fprintf( stderr, "libdvdread: Can't stat() %s.\n", filename );
                break;
            }

            dvd_file->title_sizes[ i ] = fileinfo.st_size / DVD_VIDEO_LB_LEN;
            dvd_file->title_devs[ i ] = DVDinput_open( full_path );
            dvd_file->filesize += dvd_file->title_sizes[ i ];
            DVDinput_seek( dvd_file->title_devs[ i ], 0, DVDINPUT_SEEK_KEY );
        }
        if( !dvd_file->title_devs[ 0 ] ) {
            free( dvd_file );
            return 0;
        }
    }

    return dvd_file;
}

dvd_file_t *DVDOpenFile( dvd_reader_t *dvd, int titlenum, 
			 dvd_read_domain_t domain )
{
    char filename[ MAX_UDF_FILE_NAME_LEN ];

    switch( domain ) {
    case DVD_READ_INFO_FILE:
        if( titlenum == 0 ) {
            sprintf( filename, "/VIDEO_TS/VIDEO_TS.IFO" );
        } else {
            sprintf( filename, "/VIDEO_TS/VTS_%02i_0.IFO", titlenum );
        }
        break;
    case DVD_READ_INFO_BACKUP_FILE:
        if( titlenum == 0 ) {
            sprintf( filename, "/VIDEO_TS/VIDEO_TS.BUP" );
        } else {
            sprintf( filename, "/VIDEO_TS/VTS_%02i_0.BUP", titlenum );
        }
        break;
    case DVD_READ_MENU_VOBS:
        if( dvd->isImageFile ) {
            return DVDOpenVOBUDF( dvd, titlenum, 1 );
        } else {
            return DVDOpenVOBPath( dvd, titlenum, 1 );
        }
        break;
    case DVD_READ_TITLE_VOBS:
        if( titlenum == 0 ) return 0;
        if( dvd->isImageFile ) {
            return DVDOpenVOBUDF( dvd, titlenum, 0 );
        } else {
            return DVDOpenVOBPath( dvd, titlenum, 0 );
        }
        break;
    default:
        fprintf( stderr, "libdvdread: Invalid domain for file open.\n" );
        return 0;
    }
    
    if( dvd->isImageFile ) {
        return DVDOpenFileUDF( dvd, filename );
    } else {
        return DVDOpenFilePath( dvd, filename );
    }
}

void DVDCloseFile( dvd_file_t *dvd_file )
{
    int i;

    if( dvd_file ) {
        if( dvd_file->dvd->isImageFile ) {
	    ;
	} else {
            for( i = 0; i < 9; ++i ) {
                if( dvd_file->title_devs[ i ] ) {
                    DVDinput_close( dvd_file->title_devs[i] );
                }
            }
        }

        free( dvd_file );
        dvd_file = 0;
    }
}

/* Internal, but used from dvd_udf.c */
int DVDReadBlocksUDFRaw( dvd_reader_t *device, uint32_t lb_number,
			 size_t block_count, unsigned char *data, 
			 int encrypted )
{
   int ret;

   if( !device->dev ) {
     	fprintf( stderr, "libdvdread: Fatal error in block read.\n" );
	return 0;
   }

   ret = DVDinput_seek( device->dev, (int) lb_number, DVDINPUT_NOFLAGS );
   if( ret != (int) lb_number ) {
     	fprintf( stderr, "libdvdread: Can't seek to block %u\n", lb_number );
	return 0;
   }

   return DVDinput_read( device->dev, (char *) data, 
			 (int) block_count, encrypted );
}

/* This is using a single input and starting from 'dvd_file->lb_start' offset.
 *
 * Reads 'block_count' blocks from 'dvd_file' at block offset 'offset'
 * into the buffer located at 'data' and if 'encrypted' is set
 * descramble the data if it's encrypted.  Returning either an
 * negative error or the number of blocks read. */
static int DVDReadBlocksUDF( dvd_file_t *dvd_file, uint32_t offset,
			     size_t block_count, unsigned char *data,
			     int encrypted )
{
    return DVDReadBlocksUDFRaw( dvd_file->dvd, dvd_file->lb_start + offset,
				block_count, data, encrypted );
}

/* This is using possibly several inputs and starting from an offset of '0'.
 *
 * Reads 'block_count' blocks from 'dvd_file' at block offset 'offset'
 * into the buffer located at 'data' and if 'encrypted' is set
 * descramble the data if it's encrypted.  Returning either an
 * negative error or the number of blocks read. */
static int DVDReadBlocksPath( dvd_file_t *dvd_file, unsigned int offset,
			      size_t block_count, unsigned char *data,
			      int encrypted )
{
    int i;
    int ret, ret2, off;

    ret = 0;
    ret2 = 0;
    for( i = 0; i < 9; ++i ) {
      if( !dvd_file->title_sizes[ i ] ) return 0; /* Past end of file */

        if( offset < dvd_file->title_sizes[ i ] ) {
            if( ( offset + block_count ) <= dvd_file->title_sizes[ i ] ) {
		off = DVDinput_seek( dvd_file->title_devs[ i ], 
				     (int)offset, DVDINPUT_NOFLAGS );
                if( off < 0 || off != (int)offset ) {
		    fprintf( stderr, "libdvdread: Can't seek to block %d\n", 
			     offset );
		    return off < 0 ? off : 0;
		}
                ret = DVDinput_read( dvd_file->title_devs[ i ], data,
				     (int)block_count, encrypted );
                break;
            } else {
                size_t part1_size = dvd_file->title_sizes[ i ] - offset;
		/* FIXME: Really needs to be a while loop.
                 * (This is only true if you try and read >1GB at a time) */
		
                /* Read part 1 */
                off = DVDinput_seek( dvd_file->title_devs[ i ], 
				     (int)offset, DVDINPUT_NOFLAGS );
                if( off < 0 || off != (int)offset ) {
		    fprintf( stderr, "libdvdread: Can't seek to block %d\n", 
			     offset );
		    return off < 0 ? off : 0;
		}
                ret = DVDinput_read( dvd_file->title_devs[ i ], data,
				     (int)part1_size, encrypted );
		if( ret < 0 ) return ret;
		/* FIXME: This is wrong if i is the last file in the set. 
                 * also error from this read will not show in ret. */

                /* Does the next part exist? If not then return now. */
                if( !dvd_file->title_devs[ i + 1 ] ) return ret;

                /* Read part 2 */
                off = DVDinput_seek( dvd_file->title_devs[ i + 1 ], 
				     0, DVDINPUT_NOFLAGS );
                if( off < 0 || off != 0 ) {
		    fprintf( stderr, "libdvdread: Can't seek to block %d\n", 
			     0 );
		    return off < 0 ? off : 0;
		}
                ret2 = DVDinput_read( dvd_file->title_devs[ i + 1 ], 
				      data + ( part1_size
					       * (int64_t)DVD_VIDEO_LB_LEN ),
				      (int)(block_count - part1_size),
				      encrypted );
                if( ret2 < 0 ) return ret2;
		break;
            }
        } else {
            offset -= dvd_file->title_sizes[ i ];
        }
    }

    return ret + ret2;
}

/* This is broken reading more than 2Gb at a time is ssize_t is 32-bit. */
ssize_t DVDReadBlocks( dvd_file_t *dvd_file, int offset, 
		       size_t block_count, unsigned char *data )
{
    int ret;
    
    /* Hack, and it will still fail for multiple opens in a threaded app ! */
    if( dvd_file->dvd->css_title != dvd_file->css_title ) {
      dvd_file->dvd->css_title = dvd_file->css_title;
      if( dvd_file->dvd->isImageFile ) {
	DVDinput_title( dvd_file->dvd->dev, (int)dvd_file->lb_start );
      } else {
	DVDinput_title( dvd_file->title_devs[ 0 ], (int)dvd_file->lb_start );
      }
    }
    
    if( dvd_file->dvd->isImageFile ) {
	ret = DVDReadBlocksUDF( dvd_file, (uint32_t)offset, 
				block_count, data, DVDINPUT_READ_DECRYPT );
    } else {
	ret = DVDReadBlocksPath( dvd_file, (unsigned int)offset, 
				 block_count, data, DVDINPUT_READ_DECRYPT );
    }
    
    return (ssize_t)ret;
}

int DVDFileSeek( dvd_file_t *dvd_file, int offset )
{
   if( offset > dvd_file->filesize * DVD_VIDEO_LB_LEN ) {
       return -1;
   }
   dvd_file->seek_pos = (uint32_t) offset;
   return offset;
}

ssize_t DVDReadBytes( dvd_file_t *dvd_file, void *data, size_t byte_size )
{
    unsigned char *secbuf;
    unsigned int numsec, seek_sector, seek_byte;
    int ret;
    
    seek_sector = dvd_file->seek_pos / DVD_VIDEO_LB_LEN;
    seek_byte   = dvd_file->seek_pos % DVD_VIDEO_LB_LEN;

    numsec = ( ( seek_byte + byte_size ) / DVD_VIDEO_LB_LEN ) + 1;
    secbuf = (unsigned char *) malloc( numsec * DVD_VIDEO_LB_LEN );
    if( !secbuf ) {
	fprintf( stderr, "libdvdread: Can't allocate memory " 
		 "for file read!\n" );
        return 0;
    }
    
    if( dvd_file->dvd->isImageFile ) {
	ret = DVDReadBlocksUDF( dvd_file, (uint32_t) seek_sector, 
				(size_t) numsec, secbuf, DVDINPUT_NOFLAGS );
    } else {
	ret = DVDReadBlocksPath( dvd_file, seek_sector, 
				 (size_t) numsec, secbuf, DVDINPUT_NOFLAGS );
    }

    if( ret != (int) numsec ) {
        free( secbuf );
        return ret < 0 ? ret : 0;
    }

    memcpy( data, &(secbuf[ seek_byte ]), byte_size );
    free( secbuf );

    dvd_file->seek_pos += byte_size;
    return byte_size;
}

ssize_t DVDFileSize( dvd_file_t *dvd_file )
{
    return dvd_file->filesize;
}
