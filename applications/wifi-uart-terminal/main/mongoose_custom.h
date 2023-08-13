#pragma once

// See https://mongoose.ws/documentation/#build-options
#define MG_ARCH MG_ARCH_FREERTOS
#define MG_ENABLE_LWIP 1
#define MG_IO_SIZE 256
#define MG_ENABLE_FILE 1

#include <vfs.h>
#define MG_STAT_FUNC aos_stat