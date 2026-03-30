#include "ipc_fd.h"
#include <sys/socket.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

/* ZERO-COPY FILE DESCRIPTOR PASSING: transfer an open file descriptor from
   master process to worker process using unix domain socket ancillary data (SCM_RIGHTS).
   the kernel handles the fd duplication internally; only the fd number travels the socket.
   this avoids any userspace copying of file content and is the standard mechanism used
   by systemd socket activation for service socket handoff. */
int send_fd(int sock, int fd) {
    struct msghdr msg = {0};
    char buf[CMSG_SPACE(sizeof(int))];
    memset(buf, 0, sizeof(buf));

    /* attach dummy iovec (msghdr requires at least one iov) */
    struct iovec io = { .iov_base = (void*)"F", .iov_len = 1 };
    msg.msg_iov = &io;
    msg.msg_iovlen = 1;

    /* set up control message area to hold the fd */
    msg.msg_control = buf;
    msg.msg_controllen = sizeof(buf);

    /* build cmsg header with SCM_RIGHTS type (for fd passing) */
    struct cmsghdr *cmsg = CMSG_FIRSTHDR(&msg);
    cmsg->cmsg_level = SOL_SOCKET;
    cmsg->cmsg_type = SCM_RIGHTS;  /* special ancillary type for fd passing */
    cmsg->cmsg_len = CMSG_LEN(sizeof(int));

    /* copy the fd into the control message data area */
    memcpy(CMSG_DATA(cmsg), &fd, sizeof(int));

    /* sendmsg sends both the iovec and the ancillary data in one atomic operation */
    if (sendmsg(sock, &msg, 0) < 0) return -1;
    return 0;
}

/* RECEIVE FILE DESCRIPTOR PASSED VIA SCM_RIGHTS: the worker process receives
   a NEW fd number (kernel-allocated in the receiver's process table) that points to
   the SAME underlying open file as the sender's original fd. this creates a file descriptor
   duplicate across process boundaries without copying any file data. */
int recv_fd(int sock) {
    struct msghdr msg = {0};
    char mbuf[1];
    struct iovec io = { .iov_base = mbuf, .iov_len = sizeof(mbuf) };
    msg.msg_iov = &io;
    msg.msg_iovlen = 1;

    /* allocate buffer for incoming ancillary data */
    char cbuf[CMSG_SPACE(sizeof(int))];
    msg.msg_control = cbuf;
    msg.msg_controllen = sizeof(cbuf);

    /* recvmsg receives both the iovec and the ancillary data together */
    ssize_t n = recvmsg(sock, &msg, 0);
    if (n <= 0) return -1;

    /* extract the cmsg header from the received ancillary data */
    struct cmsghdr *cmsg = CMSG_FIRSTHDR(&msg);
    if (!cmsg) return -1;
    /* verify this is indeed an SCM_RIGHTS message (fd passing) */
    if (cmsg->cmsg_level != SOL_SOCKET || cmsg->cmsg_type != SCM_RIGHTS) return -1;

    /* extract the fd number from the cmsg data area */
    int fd = -1;
    memcpy(&fd, CMSG_DATA(cmsg), sizeof(int));
    return fd;
}
