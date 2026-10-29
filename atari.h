#ifndef _ATARI_H_
#define _ATARI_H_

/* The path most recently picked with atari_file_request, consumed by
 * sdl_load_file; NULL once used or if the user cancelled */
extern char *atari_file_request_retval;

int atari_gem_driver(void);
int atari_aes_available(void);
char *atari_file_request(const char *title);
void atari_poll(void);

#endif
