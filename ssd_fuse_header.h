/*
  FUSE-ioctl: ioctl support for FUSE
  Copyright (C) 2008       SUSE Linux Products GmbH
  Copyright (C) 2008       Tejun Heo <teheo@suse.de>
  This program can be distributed under the terms of the GNU GPLv2.
  See the file COPYING.
*/
#include <sys/types.h>
#include <sys/uio.h>
#include <sys/ioctl.h>


#define PHYSICAL_NAND_NUM (8)
#define LOGICAL_NAND_NUM (5)

#define NAND_SIZE_KB (10)
#define KB (1024)
#define PAGE_SIZE (512)
#define PAGE_PER_BLOCK (NAND_SIZE_KB * 1024 / PAGE_SIZE) // 20
#define L2P_SIZE (LOGICAL_NAND_NUM * PAGE_PER_BLOCK) // 100

#define INVALID_PCA     (0xFFFFFFFF)
#define FULL_PCA     (0xFFFFFFFE)
#define NAND_LOCATION  "/home/nao/SSD"

enum
{
    SSD_GET_LOGIC_SIZE   = _IOR('E', 0, size_t),
    SSD_GET_PHYSIC_SIZE   = _IOR('E', 1, size_t),
    SSD_GET_WA            = _IOR('E', 2, size_t),
};
