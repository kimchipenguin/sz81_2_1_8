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
#define M_SOUND          32
#define M_GUIOPTS        33
#define M_JOYSTICK       34

#define T_SETTINGS       1    /* tree */
#define ST_TITLE         1
#define ST_PAGES         2
#define ST_BAR           3
#define ST_THW           4
#define ST_TSOUND        5
#define ST_TGUI          6
#define ST_TJOY          7
#define ST_CANCEL        8
#define ST_OK            9

#define T_HARDWARE       2    /* tree */
#define HW_FMACH         1
#define HW_ZX80          2
#define HW_ZX80_8K       3
#define HW_ZX81          4
#define HW_RAMLBL        5
#define HW_RAM           6
#define HW_M1NOT         7
#define HW_FSPEED        8
#define HW_FSKLBL        9
#define HW_FSKIP         10
#define HW_SPDLBL        11
#define HW_SPEED         12
#define HW_FGFX          13
#define HW_WRX           14
#define HW_UDGNONE       15
#define HW_UDG64         16
#define HW_UDG128        17

#define T_SOUND          3    /* tree */
#define SD_FDEV          1
#define SD_NONE          2
#define SD_QS            3
#define SD_ZONX          4
#define SD_VSYNC         5
#define SD_ACB           6
#define SD_FVOL          7
#define SD_VOLLBL        8
#define SD_VOL           9

#define T_GUIOPTS        4    /* tree */
#define GU_FREP          1
#define GU_DELAY         3
#define GU_INTERVAL      6
#define GU_FCOL          8
#define GU_INKLBL        9
#define GU_INK           10
#define GU_PAPERLBL      11
#define GU_PAPER         12

#define T_JOYSTICK       5    /* tree */
#define JS_FRAME         1
#define JS_UP            3
#define JS_LEFT          5
#define JS_FIRE          6
#define JS_RIGHT         7
#define JS_DOWN          9
#define JS_HINT          11

#define T_POPUPS         6    /* tree */
#define PU_RAM           1
#define PU_FSKIP         12
#define PU_SPEED         23

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
#define FS_JS_HINT       15
#define FS_JS_PRESS      16
#define FS_JS_NONE       17
#define AL_NOVDI         18
#define AL_NOWINDOW      19
#define AL_NOKBPIC       20
#define AL_NOABPIC       21
#define AL_NOMEM         22
#define AL_RESET         23
#define AL_HWRESET       24
#define AL_SNDRESET      25
#define AL_BADVOLUME     26
#define AL_BADREPEAT     27
#define AL_SZ81DIALOG    28

#endif
