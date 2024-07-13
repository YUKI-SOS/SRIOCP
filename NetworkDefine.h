#pragma once
#pragma comment(lib, "ws2_32")
#pragma comment(lib, "mswsock.lib")
#include <WinSock2.h>
#include <Windows.h>
#include <mswsock.h>
#include <stdlib.h>

const DWORD RECV_PACKET_MAX = 1024; //���ú� ��Ŷ ������
const DWORD SEND_PACKET_MAX = 8192; //���� ��Ŷ ������

const DWORD PACKET_BUFF_MAX = 8192;

const DWORD RECV_QUEUE_MAX = 1000; //1024 * 1000 * 2 = 2Mb
const DWORD SEND_QUEUE_MAX = 1000; //8192 * 1000 = 8Mb

const DWORD RECV_RING_BUFFER_MAX = RECV_PACKET_MAX * 32; //Ŀ�ؼ� ���� ������ ���ú� ������ ������ 1024 * 32 * n = 32Kb * n
const DWORD SEND_RING_BUFFER_MAX = SEND_PACKET_MAX * 32; //Ŀ�ؼ� ���� ������ ���� ������ ������ 8192 * 32 * n = 256Kb * n

const DWORD IP_BUFF_SIZE = 17;
const DWORD ADDR_BUFF_SIZE = 64;

enum class ECSType
{
	SERVER = 0,
	CLIENT
};

enum class IOType
{
	NONE = 0,
	ACCEPT,
	CONNECT,
	DISCONNECT,
	RECV,
	SEND
};

//������ Ȯ��
typedef struct
{
	OVERLAPPED overlapped;
	WSABUF WSABuff;
	//CHAR buff[PACKET_BUFF_MAX];
	//SOCKET socket;
	DWORD dwIndex;
	IOType ioType;
}IODATA;

typedef struct
{
	OVERLAPPED overlapped;
	WSABUF wsabuff;
	DWORD dwIndex; //Ŀ�ؼ� �ε���
	IOType eIoType;
}OverlappedEX;

typedef struct _PacketInfo
{
	DWORD dwIndex;
	DWORD dwLength;
	char buff[RECV_PACKET_MAX];

	_PacketInfo()
	{
		dwIndex = -1;
		dwLength = 0;
		memset(buff, 0, sizeof(buff));
	}

}PacketInfo;