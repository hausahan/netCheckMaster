#include "pingworker.h"
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#pragma comment(lib, "Ws2_32.lib")

unsigned short calculateChecksum(unsigned short *buffer, int size) {
    unsigned long sum = 0;
    while (size > 1) {
        sum += *buffer++;
        size -= 2;
    }
    if (size) sum += *(unsigned char*)buffer;
    sum = (sum >> 16) + (sum & 0xffff);
    sum += (sum >> 16);
    return (unsigned short)(~sum);
}

PingWorker::PingWorker(const QString &ip, QObject *parent) :
    QObject(parent), targetIp(ip), running(false), timer(new QTimer(this)), sequence(1)
{
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        qDebug() << "WSAStartup failed:" << WSAGetLastError();
    }
    connect(timer, &QTimer::timeout, this, &PingWorker::ping);
}

PingWorker::~PingWorker() {
    stop();
    WSACleanup();
}

void PingWorker::startPing() {
    running = true;
    sequence = 1;
    timer->start(1000);
    ping();
}

void PingWorker::stop() {
    running = false;
    timer->stop();
}

void PingWorker::ping() {
    if (!running) return;

    SOCKET sock = socket(AF_INET, SOCK_RAW, IPPROTO_ICMP);
    if (sock == INVALID_SOCKET) {
        qDebug() << "Socket creation failed for" << targetIp << ":" << WSAGetLastError();
        emit resultReady(targetIp, -1);
        return;
    }

    int timeout = 2000;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout));

    sockaddr_in dest;
    dest.sin_family = AF_INET;
    dest.sin_addr.s_addr = inet_addr(targetIp.toStdString().c_str());

    struct IcmpHeader {
        unsigned char type;
        unsigned char code;
        unsigned short checksum;
        unsigned short id;
        unsigned short seq;
    };
    char icmpData[32] = {0};
    IcmpHeader *icmp = (IcmpHeader*)icmpData;
    icmp->type = 8;
    icmp->code = 0;
    icmp->id = static_cast<unsigned short>(reinterpret_cast<uintptr_t>(this));
    icmp->seq = sequence++;
    icmp->checksum = 0;
    icmp->checksum = calculateChecksum((unsigned short*)icmpData, sizeof(icmpData));

    LARGE_INTEGER frequency, startTime, endTime;
    QueryPerformanceFrequency(&frequency);
    QueryPerformanceCounter(&startTime);

    if (sendto(sock, icmpData, sizeof(icmpData), 0, (sockaddr*)&dest, sizeof(dest)) == SOCKET_ERROR) {
        qDebug() << "Sendto failed for" << targetIp << ":" << WSAGetLastError();
        emit resultReady(targetIp, -1);
        closesocket(sock);
        return;
    }

    char recvBuf[1024];
    sockaddr_in from;
    int addrLen = sizeof(from);
    bool received = false;
    int attempts = 0;
    const int maxAttempts = 5; // 最多尝试读取 5 次

    while (!received && attempts < maxAttempts) {
        int ret = recvfrom(sock, recvBuf, sizeof(recvBuf), 0, (sockaddr*)&from, &addrLen);
        if (ret > 0) {
            if (from.sin_addr.s_addr != dest.sin_addr.s_addr) {
                qDebug() << "Received reply from wrong IP for" << targetIp << ":" << inet_ntoa(from.sin_addr);
            } else {
                unsigned char *ipHeader = (unsigned char*)recvBuf;
                int ipHeaderLen = (ipHeader[0] & 0x0F) * 4;
                if (ret < ipHeaderLen + sizeof(IcmpHeader)) {
                    qDebug() << "Received packet too small for" << targetIp << ", size:" << ret;
                } else {
                    unsigned char *icmpReply = reinterpret_cast<unsigned char*>(recvBuf + ipHeaderLen);
                    IcmpHeader *replyHeader = (IcmpHeader*)icmpReply;

                    if (icmpReply[0] == 0 && icmpReply[1] == 0 &&
                        replyHeader->id == icmp->id && replyHeader->seq == icmp->seq) {
                        QueryPerformanceCounter(&endTime);
                        int latency = static_cast<int>((endTime.QuadPart - startTime.QuadPart) * 1000 / frequency.QuadPart);
//                        qDebug() << "Received reply from" << targetIp << ", latency:" << latency << "ms, seq:" << icmp->seq;
                        emit resultReady(targetIp, latency >= 0 ? latency : -1);
                        received = true;
                    } else if (icmpReply[0] == 3) {
//                        qDebug() << "Destination unreachable for" << targetIp << ", code:" << (int)icmpReply[1];
                        emit resultReady(targetIp, -1);
                        received = true;
                    } else {
//                        qDebug() << "Invalid ICMP reply from" << targetIp << ", type:" << (int)icmpReply[0] << ", seq:" << replyHeader->seq;
                    }
                }
            }
        } else {
//            qDebug() << "Recvfrom failed or timeout for" << targetIp << ":" << WSAGetLastError();
            break; // 超时后退出循环
        }
        attempts++;
    }

    if (!received) {
//        qDebug() << "No valid reply received for" << targetIp << "after" << maxAttempts << "attempts";
        emit resultReady(targetIp, -1);
    }

    closesocket(sock);
}
