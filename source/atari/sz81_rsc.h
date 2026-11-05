/* sz81_rsc.h - written by atari/mkrsc.py, do not edit */

#ifndef SZ81_RSC_H
#define SZ81_RSC_H

#define T_MENU           0    /* tree */
#define M_BAR            1
#define M_ACTIVE         2
#define M_T_DESK         3
#define M_T_FILE         4
#define M_T_VIEW         5
#define M_T_OPT          6
#define M_SCREEN         7
#define M_DESKBOX        8
#define M_ABOUT          9
#define M_FILEBOX        17
#define M_OPEN           18
#define M_LOADSTATE      20
#define M_SAVESTATE      21
#define M_SCREENSHOT     23
#define M_RESET          25
#define M_QUIT           26
#define M_VIEWBOX        27
#define M_INVERT         28
#define M_KEYBOARD       29
#define M_OPTBOX         30
#define M_HARDWARE       31

#define T_HARDWARE       1    /* tree */
#define HW_TITLE         1
#define HW_FMACH         2
#define HW_ZX80          3
#define HW_ZX80_8K       4
#define HW_ZX81          5
#define HW_RAMLBL        6
#define HW_RAM           7
#define HW_M1NOT         8
#define HW_FSPEED        9
#define HW_FSKLBL        10
#define HW_FSKIP         11
#define HW_SPDLBL        12
#define HW_SPEED         13
#define HW_FGFX          14
#define HW_WRX           15
#define HW_UDGNONE       16
#define HW_UDG64         17
#define HW_UDG128        18
#define HW_CANCEL        19
#define HW_OK            20

#define T_RAMPOP         2    /* tree */

#define T_FSKIPPOP       3    /* tree */

#define T_SPEEDPOP       4    /* tree */

/* free strings */
#define FS_APPNAME       0
#define FS_KB_ZX80       1
#define FS_KB_ZX81       2
#define FS_AB_TITLE      3
#define FS_AB_OK         4
#define FS_AB_LINE0      5
#define FS_AB_LINE1      6
#define FS_AB_LINE2      7
#define FS_AB_LINE3      8
#define FS_AB_LINE4      9
#define FS_AB_LINE5      10
#define FS_AB_LINE6      11
#define FS_AB_LINE7      12
#define FS_AB_LINE8      13
#define FS_AB_LINE9      14
#define AL_NOVDI         15
#define AL_NOWINDOW      16
#define AL_NOKBPIC       17
#define AL_NOABPIC       18
#define AL_NOMEM         19
#define AL_RESET         20
#define AL_HWRESET       21
#define AL_SZ81DIALOG    22

#endif
