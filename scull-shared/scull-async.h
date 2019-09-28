/*
 * scull-async.h
 *
 * Copyright (C) 2019 Dan Walkes
 * Copyright (C) 2001 Alessandro Rubini and Jonathan Corbet
 * Copyright (C) 2001 O'Reilly & Associates
 *
 * The source code in this file can be freely used, adapted,
 * and redistributed in source or binary form, so long as an
 * acknowledgment appears in derived source files.  The citation
 * should list that the code comes from the book "Linux Device
 * Drivers" by Alessandro Rubini and Jonathan Corbet, published
 * by O'Reilly & Associates.   No warranty is attached;
 * we cannot take responsibility for errors or fitness for use.
 */

#ifndef SCULL_SHARED_SCULL_ASYNC_H_
#define SCULL_SHARED_SCULL_ASYNC_H_


ssize_t scull_write_iter(struct kiocb *iocb, struct iov_iter *from);
ssize_t scull_read_iter(struct kiocb *iocb, struct iov_iter *to);


#endif /* SCULL_SHARED_SCULL_ASYNC_H_ */
