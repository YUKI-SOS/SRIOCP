#pragma once
#define _WINSOCK_DEPRECATED_NO_WARNINGS // inet_addr gethostbyname 등의 deprecated 함수 이용
#pragma comment(lib, "ws2_32")
#pragma comment(lib, "mswsock.lib")
#include <WinSock2.h>
#include <Windows.h>
#include <mswsock.h>
#include <stdlib.h>

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

enum class EIOType
{
	NONE = 0
	, DISCONNECT
	, RECV
	, SEND
};

enum class EConnectionState 
{
	FREE = 0 //커넥션이 연결 안되어 있고, 사용 가능한 상태.
	, CONNECTED //커넥션이 연결되어 있고, 사용 중인 상태.
	, CLOSING //커넥션 종료 중인 상태. IO가 남아있어서 종료 대기 중.
};

struct OverlappedEX
{
	OVERLAPPED overlapped;
	WSABUF wsabuff;
	DWORD dwIndex; //커넥션 인덱스
	EIOType eIoType;
};

struct PacketInfo
{
	DWORD dwIndex;
	DWORD dwLength;
	char buff[RECV_PACKET_MAX];

	PacketInfo()
	{
		dwIndex = -1;
		dwLength = 0;
		memset(buff, 0, sizeof(buff));
	}
};