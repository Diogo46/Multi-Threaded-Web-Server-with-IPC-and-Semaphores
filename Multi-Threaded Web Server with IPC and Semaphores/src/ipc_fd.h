// ipc_fd.h
#ifndef IPC_FD_H
#define IPC_FD_H

int send_fd(int sock, int fd);
int recv_fd(int sock);

#endif
