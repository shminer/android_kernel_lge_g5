#ifndef _SOUND_CONTROL_H
#define _SOUND_CONTROL_H

#define SOUND_CONTROL_MAJOR_VERSION 4
#define SOUND_CONTROL_MINOR_VERSION 0

unsigned int sound_control_write(unsigned int reg, int val);

#endif /* _SOUND_CONTROL_H */