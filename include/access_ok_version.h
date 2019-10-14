/*
 * @file access_ok_version.h
 * @date 10/13/2019
 * 
 */


#include <linux/uaccess.h>
#include <linux/version.h>


/*
 * Wrapper function which drops the third argument on kernel 5.0 or later
 */

inline int access_ok_wrapper(int type, void __user * arg, unsigned int cmd)
{
	int err =0;

	#if LINUX_VERSION_CODE < KERNEL_VERSION(5,0,0)

	err = access_ok(type, (void __user *)arg, cmd);

	#else

	err = access_ok((void __user *)arg, cmd);

	#endif

	return err;
}
