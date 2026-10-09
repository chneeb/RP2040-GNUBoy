#include "pico/stdlib.h"
#include <time.h>

#include "rc.h"

void *sys_timer()
{
	return (void *)time_us_32();
}

int sys_elapsed(int *cl)
{
	uint32_t now;
	uint32_t usecs;

	now = time_us_32();
	usecs = now - *cl;
	*cl = now;

	return usecs;
}

void sys_sleep(int us)
{
	if(us <= 0) return;

	sleep_us(us);
}

void sys_sanitize(char *s)
{
}

void sys_initpath()
{

    char *buf = ".";

    if (rc_getstr("rcpath") == NULL)
        rc_setvar("rcpath", 1, &buf);

    if (rc_getstr("savedir") == NULL)
        rc_setvar("savedir", 1, &buf);
}

void sys_checkdir(char *path, int wr)
{
}
