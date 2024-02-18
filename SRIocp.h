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
#include <map>
#include <ws2tcpip.h>

class CIocp;

const unsigned int BUFFSIZE = 512;

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
	WSABUF dataBuff;
	CHAR buff[BUFFSIZE];
	SOCKET socket;
	UINT uIndex;
	IOType ioType;
}IODATA;

typedef struct
{
	CIocp* pConnection;
	CHAR Buff[BUFFSIZE];
}PacketInfo;

//���̺귯���� ����� �ܺο��� �ǳ��� �Լ������� Ÿ�Ե�.
typedef void (*AcceptFunc)(UINT uIndex);
typedef void (*ConnectFunc)(UINT uIndex);
typedef void (*CloseFunc)(UINT uIndex);
typedef void (*RecvFunc)(PacketInfo* pPacketInfo);


class CIocp
{
public:
	ECSType m_eCSType;
	UINT m_nBindPort; //accept�뵵�� bind�� ��Ʈ
	bool m_isConnected; //���ϵ� ������ ��� accept/connect üũ��. ���� ������ ��� ���� �� delete Ÿ�̹��� ���߱� ���� �̿�.
	SOCKET m_ListenSocket; //���� ����
	SOCKET m_socket; //Ŀ�ؼ� ���� ���� ����. send recv��
	HANDLE m_CompletionPort; //completionport �ڵ�
	IODATA* m_pIoData; //������ Ȯ��. ���� �� ���ú� ���� Ÿ��

	std::vector<CIocp*> m_ConnectionList; //Ŀ�ؼ� ����Ʈ
	UINT m_uConnectionIndex;
	CIocp* m_pMainConnection; //���ϵ� Ŀ�ؼǵ��� ���� Ŀ�ؼǿ� �����ϱ� ���� ������

	std::string m_szRemoteIP; //�ܺο��� ����õ��� Ŀ�ؼ��� ip
	UINT m_uRemotePort; //�ܺο��� ����õ��� Ŀ�ؼ��� ��Ʈ
	bool m_bWorkerThreadLive;

	//Ex�Լ��� �̿��ϱ� ���� �Լ�������.
	LPFN_ACCEPTEX lpfnAcceptEx;
	LPFN_DISCONNECTEX lpfnDisconnectEx;
	LPFN_CONNECTEX lpfnConnectEx;

	//���̺귯���� �̿��� �ʿ��� recv���� �� ó���� ����ó�� �Լ� ������.
	static AcceptFunc g_pOnAcceptFunc;
	static ConnectFunc g_pOnConnectFunc;
	static CloseFunc g_pOnCloseFunc;
	static RecvFunc g_pOnRecvFunc;

	std::vector<PacketInfo>* m_pReadBuff; //���μ��Ͽ��� ��Ŷ���� ��� ó���� ����
	UINT m_uReadBuffPos; //��Ŷ ���μ��� �����忡�� �а� �ִ� ���� ���� ��ġ.
	std::vector<PacketInfo>* m_pWriteBuff;//���ϵ� ���ϵ��� ���ú� ������ ������ ��� ����� ���μ����� ����
	ULONG m_uInterLockWriteBuffPos; //���Ͷ����� ������ ����Ʈ ������ ��ġ.
	std::vector<PacketInfo>* m_pSendBuff;
	UINT m_uSendBuffPos;
	char* m_pTempBuff; //�������� ���� ��Ŷ�� �ӽ� �����ϴ� ����
	UINT m_uTempBuffPos;
	UINT m_uIoPos;

	DWORD m_dwLockNum; //��Ŀ������ �������� Read Write ���� ������ �� �� ���� ����.
	SRWLOCK* m_pBufferSwapLock; //���� ���ҿ� SRWLock �迭
	DWORD* m_pThreadIdArr; //��Ŀ������ ���̵� �迭

	UINT m_nChildSockNum;

public:
	CIocp();
	virtual ~CIocp();

	bool InitConnectionList(UINT nCount);

	bool InitSocket(ECSType csType, UINT port); // Ŭ���̾�Ʈ�� ��� ��Ʈ�� NULL �־ �̿�.
	void InitSocketOption(SOCKET socket);
	void SetReuseSocketOpt(SOCKET socket);
	void SetLingerOpt(SOCKET socket);
	void SetNagleOffOpt(SOCKET socket);

	bool InitAcceptPool(UINT num);
	bool InitConnectPool(UINT num);

	static void SetOnAcceptFunc(AcceptFunc pFunc) { g_pOnAcceptFunc = pFunc; };
	static void OnAccept(UINT uIndex) { g_pOnAcceptFunc(uIndex); };
	
	static void SetOnConnectFunc(ConnectFunc pFunc) { g_pOnConnectFunc = pFunc; };
	static void OnConnect(UINT uIndex) { g_pOnConnectFunc(uIndex); };
	
	static void SetOnCloseFunc(CloseFunc pFunc) { g_pOnCloseFunc = pFunc; };
	static void OnClose(UINT uIndex) { g_pOnCloseFunc(uIndex); };
	
	static void SetOnRecvFunc(RecvFunc pFunc) { g_pOnRecvFunc = pFunc; };
	static void OnRecv(PacketInfo* pPacketInfo) { g_pOnRecvFunc(pPacketInfo); };

	bool CreateWorkerThread();
	static unsigned __stdcall WorkerThread(LPVOID CompletionPortObj);
	void PacketProcess();
	void SendPacketProcess();
	void SwapRWBuffer();
	void PushWriteBuffer(PacketInfo* packetInfo, DWORD dwLockIndex);

	bool ReAcceptSocket(UINT uIndex);
	void CloseSocket(UINT uIndex);
	SOCKET Connect(LPCTSTR lpszHostAddress, UINT port);

	bool RecvSet(CIocp* pConnection);
	bool Send(UINT uIndex, void* lpBuff, int nBuffSize);
	void SendToBuff(void* lpBuff, int nBuffSize);

	CIocp* GetEmptyConnection();
	CIocp* GetConnection(UINT uIndex);
	CIocp* GetNoneConnectConnection(); //���� ��Ȱ���ؼ� �����ڵ��� �̹� ������ ������ �ȵǾ� �ִ� ���ϵ� Ŀ�ؼ� ������.
	bool GetPeerName(CString& peerAdress, UINT& peerPort);

	void StopThread();
	UINT GetThreadLockNum();
	UINT GetWriteContainerSize();
	UINT GetReadContainerSize();

};

