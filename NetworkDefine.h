#pragma once
#define _WINSOCK_DEPRECATED_NO_WARNINGS // inet_addr gethostbyname 등의 deprecated 함수 이용
#pragma comment(lib, "ws2_32")
#pragma comment(lib, "mswsock.lib")
#include <WinSock2.h>
#include <Windows.h>
#include <mswsock.h>
#include <stdlib.h>

#define __DEV_LOG__

const DWORD RECV_PACKET_MAX = 1024; //리시브 패킷 사이즈
const DWORD SEND_PACKET_MAX = 8192; //센드 패킷 사이즈

const DWORD PACKET_BUFF_MAX = 8192;

const DWORD RECV_QUEUE_MAX = 1000; //1024 * 1000 * 2 = 2Mb
const DWORD SEND_QUEUE_MAX = 1000; //8192 * 1000 = 8Mb

//const DWORD RECV_RING_BUFFER_MAX = RECV_PACKET_MAX * 32; //커넥션 마다 가지는 리시브 링버퍼 사이즈 1024 * 32 * n = 32Kb * n
//const DWORD SEND_RING_BUFFER_MAX = SEND_PACKET_MAX * 32; //커넥션 마다 가지는 센드 링버퍼 사이즈 8192 * 32 * n = 256Kb * n
const DWORD RECV_RING_BUFFER_MAX = 1024; 
const DWORD SEND_RING_BUFFER_MAX = 8192; 

const DWORD IP_BUFF_SIZE = 17;
const DWORD ADDR_BUFF_SIZE = 64;

enum class ECSType
{
	NONE = 0
	, SERVER
	, CLIENT
};

enum class IOType
{
	NONE = 0
	, ACCEPT
	, CONNECT
	, DISCONNECT
	, RECV
	, SEND
};

typedef struct
{
	OVERLAPPED overlapped;
	WSABUF wsabuff;
	DWORD dwIndex; //커넥션 인덱스
	IOType eIoType;
}OverlappedEX;

typedef struct sPacketInfo
{
	DWORD dwIndex;
	DWORD dwLength;
	char buff[RECV_PACKET_MAX];

	sPacketInfo()
	{
		dwIndex = -1;
		dwLength = 0;
		memset(buff, 0, sizeof(buff));
	}

}PacketInfo;