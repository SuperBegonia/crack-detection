#pragma once
#include <iostream>
#include <cstring>
#include <functional>
#include <thread>
#include <netinet/in.h>
#include <sys/socket.h>
#include <arpa/inet.h>

#include <unistd.h>

class UDPSender
{
public:
    UDPSender(const std::string &ip, int port)
    {
        // 创建套接字
        sockfd = socket(AF_INET, SOCK_DGRAM, 0);
        if (sockfd < 0)
        {
            perror("socket creation failed");
            exit(EXIT_FAILURE);
        }

        // 填充服务器信息
        memset(&servaddr, 0, sizeof(servaddr));
        servaddr.sin_family = AF_INET;
        servaddr.sin_port = htons(port);

        // 使用 inet_pton 将IP地址转换为网络字节序
        if (inet_pton(AF_INET, ip.c_str(), &servaddr.sin_addr) <= 0)
        {
            perror("inet_pton failed");
            close(sockfd);
            exit(EXIT_FAILURE);
        }
    }

    ~UDPSender()
    {
        close(sockfd);
    }

    bool send_data(const char *data, size_t len)
    {
        ssize_t sent_len = sendto(sockfd, data, len, 0, (const struct sockaddr *)&servaddr, sizeof(servaddr));
        if (sent_len == -1)
        {
            perror("sendto failed");
            return false;
        }
        return true;
    }
    bool send_data_in_chunks(const char *data, size_t len, size_t chunk_size = 1470)
    {
        size_t offset = 0;
        while (offset < len)
        {
            size_t current_chunk_size = std::min(chunk_size, len - offset);
            if (!send_data(data + offset, current_chunk_size))
            {
                return false;
            }
            offset += current_chunk_size;
        }
        return true;
    }

private:
    int sockfd;
    struct sockaddr_in servaddr;
};

class UdpSocket
{
public:
    UdpSocket(std::function<void(const char *, int)> callback) : dataHandler(callback)
    {
        serverSocket = socket(AF_INET, SOCK_DGRAM, 0);
        if (serverSocket == -1)
        {
            std::cerr << "Socket creation failed\n";
            return;
        }
    }

    UdpSocket(std::function<void(const char *, int, UdpSocket *onwer, struct sockaddr *)> callback) : dataHandler1(callback)
    {
        serverSocket = socket(AF_INET, SOCK_DGRAM, 0);
        if (serverSocket == -1)
        {
            std::cerr << "Socket creation failed\n";
            return;
        }
    }

    ~UdpSocket()
    {
        close(serverSocket);
    }

    void bindSocket(int port)
    {
        serverAddr.sin_family = AF_INET;
        serverAddr.sin_port = htons(port);
        serverAddr.sin_addr.s_addr = INADDR_ANY;

        if (bind(serverSocket, (struct sockaddr *)&serverAddr, sizeof(serverAddr)) == -1)
        {
            std::cerr << "Bind failed\n";
            close(serverSocket);
            return;
        }
    }

    void startReceiving()
    {
        std::thread receiveThread(&UdpSocket::receiveData, this);
        receiveThread.detach();
    }

    void startReceiving(int port)
    {
        if (port > 0)
        {
            bindSocket(port);
        }
        std::thread receiveThread(&UdpSocket::receiveData, this);
        receiveThread.detach();
    }
    bool send_data_in_chunks(const char *data, size_t len, const struct sockaddr *client, size_t chunk_size = 1470)
    {
        size_t offset = 0;
        while (offset < len)
        {
            size_t current_chunk_size = std::min(chunk_size, len - offset);
            if (!send_data(data + offset, current_chunk_size, client))
            {
                return false;
            }
            offset += current_chunk_size;
        }
        return true;
    }

    bool send_data(const char *data, size_t len, const struct sockaddr *client)
    {
        ssize_t sent_len = sendto(serverSocket, data, len, 0, client, sizeof(*client));
        if (sent_len == -1)
        {
            perror("sendto failed");
            return false;
        }
        return true;
    }
    bool send_data(const char *data, size_t len, std::string ip, int port)
    {
        struct sockaddr_in client;
        client.sin_family = AF_INET;
        client.sin_port = htons(port);

        // Convert IP address from string to binary form
        if (inet_pton(AF_INET, ip.c_str(), &client.sin_addr) <= 0)
        {
            perror("inet_pton failed");
            return false;
        }

        return send_data(data, len, (struct sockaddr *)&client);
    }

private:
    struct sockaddr_in serverAddr;
    void *user;
    int serverSocket;
    std::function<void(const char *, int)> dataHandler;
    std::function<void(const char *, int, UdpSocket *, struct sockaddr *)> dataHandler1;
    void receiveData()
    {
        char buffer[4096];
        struct sockaddr_in clientAddr;
        socklen_t addrLen = sizeof(clientAddr);

        while (true)
        {
            int len = recvfrom(serverSocket, buffer, sizeof(buffer) - 1, 0, (struct sockaddr *)&clientAddr, &addrLen);
            if (len > 0)
            {
                buffer[len] = '\0';
                if (dataHandler)
                {
                    dataHandler(buffer, len);
                }
                if (dataHandler1)
                {
                    // sendto(serverSocket, buffer, len, 0,(struct sockaddr *)& clientAddr, sizeof(clientAddr));
                    dataHandler1(buffer, len, this, (struct sockaddr *)&clientAddr);
                }
            }
        }
    }
};
