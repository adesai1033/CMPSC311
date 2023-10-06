
#ifndef MDADM_H_
#define MDADM_H_

#include <stdint.h>
#include "jbod.h"

/* Return 1 on success and -1 on failure */
int mdadm_mount(void);
/* Return 1 on success and -1 on failure */
int mdadm_unmount(void);
/*returns number of bytes read on succes, -1 on failure*/
int mdadm_read(uint32_t addr, uint32_t len, uint8_t *buf);
/*returns minimum between 2 integer values*/
int min(int x, int y);
/* Return the number of bytes written on success, -1 on failure. */
int mdadm_write(uint32_t addr, uint32_t len, const uint8_t *buf);

#endif