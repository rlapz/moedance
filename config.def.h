#ifndef __CONFIG_H__
#define __CONFIG_H__


#define CFG_HEADER_LABEL            "~MoeDance~"
#define CFG_PLAYLIST_SHOW_FULL_PATH (0)


/* allowed file types */
#define CFG_FILE_TYPES {	\
	"mp3",			\
	"flac",			\
	"wav",			\
	"m4a",			\
	"opus",			\
	/* "mp4", */			\
	/* "aac", */			\
	/* "amr", */			\
	/* "mkv", */			\
}


/*
 * metadata
 * enable; 1 = true, otherwise false
 */
#define CFG_META_TITLE_ENABLE  (1)
#define CFG_META_TITLE_WIDTH   (45)

#define CFG_META_ARTIST_ENABLE (1)
#define CFG_META_ARTIST_WIDTH  (35)

#define CFG_META_ALBUM_ENABLE  (1)
#define CFG_META_ALBUM_WIDTH   (35)

#define CFG_META_GENRE_ENABLE  (1)
#define CFG_META_GENRE_WIDTH   (30)


/*
 * colors
 * see: https://en.wikipedia.org/wiki/ANSI_escape_code
 */
#define CFG_HEADER_COLOR_FG   "97"
#define CFG_HEADER_COLOR_BG   "44"
//#define CFG_HEADER_COLOR_BG   "45"

#define CFG_BODY_COLOR_FG     "39"
#define CFG_BODY_COLOR_BG     "49"
#define CFG_BODY_SEL_COLOR_FG "30"
#define CFG_BODY_SEL_COLOR_BG "47"

#define CFG_FOOTER_COLOR_FG   "97"
#define CFG_FOOTER_COLOR_BG   "44"
//#define CFG_FOOTER_COLOR_BG   "45"


#define CFG_DIR_RECURSIVE_SIZE (8)
#define CFG_LOG_FILE           "/tmp/moedance.log"
#define CFG_PROBE_SCORE_MIN    (1)


#define CFG_INPUT_BUFFER_SIZE  (64)
#define CFG_CMD_ARGS_SIZE      (8)


/*
 * -1: default CPU count
 */
#define CFG_FILE_META_THREADS_NUM (1)


#endif

