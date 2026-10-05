/* Shared kernel/user ABI for the virtual sensor device */
#ifndef VSENSOR_H
#define VSENSOR_H
#include <linux/types.h>
#include <linux/ioctl.h>

struct vs_sample { __u64 ts_ns; __s32 value; __u32 alert; };   /* value in milli-degC */
struct vs_stats  { __u64 produced, dropped, consumed, alerts; };

#define VS_MAGIC 'v'
#define VS_IOC_START      _IO(VS_MAGIC, 1)
#define VS_IOC_STOP       _IO(VS_MAGIC, 2)
#define VS_IOC_SET_RATE   _IOW(VS_MAGIC, 3, __u32)   /* period in ms */
#define VS_IOC_SET_THRESH _IOW(VS_MAGIC, 4, __s32)   /* milli-degC   */
#define VS_IOC_GET_STATS  _IOR(VS_MAGIC, 5, struct vs_stats)
#define VS_IOC_CLEAR      _IO(VS_MAGIC, 6)
#endif
