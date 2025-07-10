#pragma once
#include <iostream>
#include <functional>
#include <thread>
#include <cstring>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/time.h>
#include "ikcp.h"
#include "udp.h"

class KcpSocket
{
public:
    KcpSocket()
    {
        kcp = ikcp_create(2001, (void *)this);

        int result = ikcp_nodelay(kcp, 0, 10, 2, 1);
        if (result != 0){
            printf("ikcp_nodelay error! \n");
        }

        kcp->output = output;
    }

    ~KcpSocket()
    {
        ikcp_release(kcp);
    }

    void send(const char *data, int len)
    {
        ikcp_send(kcp, data, len);
        ikcp_flush(kcp);
    }

    void receive(const char *data, int len)
    {
        ikcp_input(kcp, data, len);
        char buffer[4096];
        int recv_len = ikcp_recv(kcp, buffer, sizeof(buffer));
        if (recv_len > 0)
        {
            // 处理接收到的数据
            if (dataHandler)
            {
                dataHandler(buffer, recv_len);
            }
        }
    }

    void setDataHandler(std::function<void(const char *, int)> handler)
    {
        dataHandler = handler;
    }

    void update()
    {
        ikcp_update(kcp, iclock());
    }

    void set_send_io(std::function<bool(const char *, int)> io)
    {
        this->send_io = io;
    }

private:
    std::function<bool(const char *, int)> send_io;
    ikcpcb *kcp;
    std::function<void(const char *, int)> dataHandler;

    static int output(const char *buf, int len, ikcpcb *kcp, void *user)
    {
        printf("调用send io \n");
        KcpSocket *ks = (KcpSocket *)user;
        if (!ks->send_io(buf, len))
        {
            printf("发送到io失败! \n");
        }
        return 0;
    }

    static inline IUINT32 iclock()
    {
        struct timeval time;
        gettimeofday(&time, NULL);
        return ((IUINT32)time.tv_sec) * 1000 + (time.tv_usec / 1000);
    }
};