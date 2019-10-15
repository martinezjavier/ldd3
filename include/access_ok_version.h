/*
 * @file access_ok_version.h
 * @date 10/13/2019
 *
 */

#include <linux/version.h>

#if LINUX_VERSION_CODE < KERNEL_VERSION(5,0,0)
#define access_ok_wrapper(type,arg,cmd) \
	access_ok(type, arg, cmd)
#else
#define access_ok_wrapper(type,arg,cmd) \
	access_ok(arg, cmd)
#endif
