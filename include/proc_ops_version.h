#ifndef _PROC_OPS_VERSION_H
#define _PROC_OPS_VERSION_H

#include <linux/version.h>

#ifdef CONFIG_COMPAT
#define __add_proc_ops_compat_ioctl(pops, fops)					\
	(pops)->proc_compat_ioctl = (fops)->compat_ioctl
#else
#define __add_proc_ops_compat_ioctl(pops, fops)
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 6, 0)
#define proc_ops_wrapper(fops, newname)	(fops)
#else
#define proc_ops_wrapper(fops, newname)						\
({										\
 	static struct proc_ops newname;						\
										\
	newname.proc_open = (fops)->open;					\
	newname.proc_read = (fops)->read;					\
	newname.proc_write = (fops)->write;					\
	newname.proc_release = (fops)->release;					\
	newname.proc_poll = (fops)->poll;					\
	newname.proc_ioctl = (fops)->unlocked_ioctl;				\
	newname.proc_mmap = (fops)->mmap;					\
	newname.proc_get_unmapped_area = (fops)->get_unmapped_area;		\
	newname.proc_lseek = (fops)->llseek;					\
	__add_proc_ops_compat_ioctl(&newname, fops);				\
	&newname;								\
})
#endif

#endif
