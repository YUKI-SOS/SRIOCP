#pragma once
#define _WINSOCK_DEPRECATED_NO_WARNINGS // inet_addr gethostbyname ���� deprecated �Լ� �̿�
#pragma comment(lib, "ws2_32")
#pragma comment(lib, "mswsock.lib")
#include <WinSock2.h>
#include <Windows.h>
#include <mswsock.h>
#include <stdlib.h>
#include <process.h> //beginthreadex ��
#include <iostream>
#include <string>	//std::string
#include <atlstr.h> //CString 
#include <vector>
#include <deque>
#include <ws2tcpip.h>

const unsigned int BUFFSIZE = 512;

class CIocp;

enum class CSType
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
	OVERLAPPED overLapped;
	WSABUF dataBuff;
	CHAR Buff[BUFFSIZE];
	SOCKET connectSocket;
	IOType ioType;
}IODATA;

typedef struct
{
	CIocp* connection;
	CHAR Buff[BUFFSIZE];
}PacketInfo;


//CIocp�� ��ӹ޴� ���μ��ϰ� �Ȱ��� CIocp�� ��ӹ޴� ���ϵ����
//���ϵ���ϵ��� ���μ����� ����� ������ ����. ���ϵ� ���ϵ��� ���� ����� Ŀ�ؼ� ������ �ǹ�.
class CIocp
{
public:
	CSType m_csType;
	UINT bindPort; //accept�뵵�� bind�� ��Ʈ
	bool isConnected; //���ϵ� ������ ��� accept/connect üũ��. ���� ������ ��� ���� �� delete Ÿ�̹��� ���߱� ���� �̿�.
	SOCKET m_listenSocket; //��������
	SOCKET m_socket; //���ϵ� ���ϵ��� ���� ����. send recv��
	HANDLE completionPort; //completionport �ڵ�
	SYSTEM_INFO sysInfo; //cpu���� üũ��
	IODATA* m_ioData; //������ Ȯ��. ���� �� ���ú� ���� Ÿ��
	std::vector<CIocp*> pConnectionList; //���ϵ� ���� Ǯ.
	CIocp* m_pMainConnection;
	std::string remoteIP; //�ܺο��� ����õ��� Ŀ�ؼ��� ip
	UINT remotePort; //�ܺο��� ����õ��� Ŀ�ؼ��� ��Ʈ
	bool bIsWorkerThread;
	CRITICAL_SECTION recvCS;
	LPFN_ACCEPTEX lpfnAcceptEx;
	LPFN_DISCONNECTEX lpfnDisconnectEx;
	LPFN_CONNECTEX lpfnConnectEx;
	std::vector<PacketInfo>* readBuff; //���μ��Ͽ��� ��Ŷ���� ��� ó���� ����
	UINT readBuffPos; //��Ŷ ���μ��� �����忡�� �а� �ִ� ���� ���� ��ġ.
	std::vector<PacketInfo>* writeBuff;//���ϵ� ���ϵ��� ���ú� ������ ������ ��� ����� ���μ����� ����
	ULONG ilWriteBuffPos; //���Ͷ����� ������ ����Ʈ ������ ��ġ.
	std::vector<PacketInfo>* sendBuff;
	UINT sendBuffPos;
	char* recvBuff; //�������� ���� ��Ŷ�� �ӽ� �����ϴ� ����
	UINT recvBuffPos;
	UINT ioBuffPos;

	DWORD dwLockNum; //��Ŀ������ �������� RW���� ������ �� �� ���� ����.
	SRWLOCK* m_BufferSwapLock; //���� ���ҿ� SRWLock �迭
	DWORD* m_ThreadIdArr; //��Ŀ������ ���̵� �迭

	UINT m_ChildSockNum;

public:
	CIocp();
	virtual ~CIocp();

	//���ϵ� ���Ͽ��� OnRecv()���� �������� ����ciocp�� ��ӹ޾� ���� ���� Ŭ������ Ÿ���� �˾ƾ� �ż� ���ø�����..
	template<typename T>
	T* SetChildSockType(T* type, int count)
	{
		for (int i = 0; i < count; i++)
		{
			CIocp* child = new T();
			child->recvBuff = new char[BUFFSIZE];
			ZeroMemory(child->recvBuff, BUFFSIZE);
			child->m_pMainConnection = this;
			child->recvCS = this->recvCS;
			pConnectionList.push_back(child);
		}

		m_ChildSockNum = count;
		return type;
	};


	bool InitSocket(CSType csType, UINT port); // Ŭ���̾�Ʈ�� ��� ��Ʈ�� NULL �־ �̿�.
	void InitSocketOption(SOCKET socket);
	void SetReuseSocketOpt(SOCKET socket);
	void SetLingerOpt(SOCKET socket);
	void SetNagleOffOpt(SOCKET socket);
	bool CreateWorkerThread();
	static unsigned __stdcall WorkerThread(LPVOID CompletionPortObj);
	void PacketProcess();
	void SendPacketProcess();
	void SwapRWBuffer();
	void PushWriteBuffer(PacketInfo* packetInfo, DWORD dwLockIndex);
	bool InitAcceptPool(UINT num);
	bool InitConnectPool(UINT num);
	bool ReAcceptSocket(SOCKET socket);
	void CloseSocket(SOCKET socket);
	SOCKET Connect(LPCTSTR lpszHostAddress, UINT port);
	bool RecvSet(CIocp* pClient);
	bool RecvSet(CIocp* pClient, IOType ioType); //���ú� �����ϵ� Ÿ���� recv�� �ƴϵ��� �ޱ� ���ؼ�. ConnectEx�� 0����Ʈ�� �����µ� ���� ����� �����ϱ� ���� �̿�.
	CIocp* GetEmptyConnection();
	CIocp* GetConnection(SOCKET socket);
	CIocp* GetNoneConnectConnection(); //���� ��Ȱ���ؼ� �����ڵ��� �̹� ������ ������ �ȵǾ� �ִ� ���ϵ� Ŀ�ؼ� ������.
	bool GetPeerName(CString& peerAdress, UINT& peerPort);
	bool Send(void* lpBuff, int nBuffSize);
	void SendToBuff(void* lpBuff, int nBuffSize);

	PacketInfo GetPacket(); //read���� ��ġ�� ����Ű�� ���� ��Ŷ�� ��ȯ.
	void StopThread();
	UINT GetThreadLockNum();
	UINT GetWriteContainerSize();
	UINT GetReadContainerSize();



public:
	virtual void OnAccept(SOCKET socket) {};
	virtual void OnConnect(SOCKET socket) {};
	virtual void OnReceive() {};
	virtual void OnClose() {};
};

