#pragma once
#define _WINSOCK_DEPRECATED_NO_WARNINGS // inet_addr gethostbyname 등의 deprecated 함수 이용
#pragma comment(lib, "ws2_32")
#pragma comment(lib, "mswsock.lib")
#include <WinSock2.h>
#include <Windows.h>
#include <mswsock.h>
#include <stdlib.h>
#include <process.h> //beginthreadex 등
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

//오버랩 확장
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

//라이브러리를 사용할 외부에서 건네줄 함수포인터 타입들.
typedef void (*AcceptFunc)(UINT uIndex);
typedef void (*ConnectFunc)(UINT uIndex);
typedef void (*CloseFunc)(UINT uIndex);
typedef void (*RecvFunc)(PacketInfo* pPacketInfo);


class CIocp
{
public:
	ECSType m_eCSType;
	UINT m_nBindPort; //accept용도의 bind할 포트
	bool m_isConnected; //차일드 소켓의 경우 accept/connect 체크용. 메인 소켓의 경우 루프 중 delete 타이밍을 맞추기 위해 이용.
	SOCKET m_ListenSocket; //리슨 소켓
	SOCKET m_socket; //커넥션 들이 가질 소켓. send recv용
	HANDLE m_CompletionPort; //completionport 핸들
	IODATA* m_pIoData; //오버랩 확장. 버퍼 및 리시브 센드 타입

	std::vector<CIocp*> m_ConnectionList; //커넥션 리스트
	UINT m_uConnectionIndex;
	CIocp* m_pMainConnection; //차일드 커넥션들이 메인 커넥션에 접근하기 위한 포인터

	std::string m_szRemoteIP; //외부에서 연결시도한 커넥션의 ip
	UINT m_uRemotePort; //외부에서 연결시도한 커넥션의 포트
	bool m_bWorkerThreadLive;

	//Ex함수를 이용하기 위한 함수포인터.
	LPFN_ACCEPTEX lpfnAcceptEx;
	LPFN_DISCONNECTEX lpfnDisconnectEx;
	LPFN_CONNECTEX lpfnConnectEx;

	//라이브러리를 이용할 쪽에서 recv받을 때 처리할 로직처리 함수 포인터.
	static AcceptFunc g_pOnAcceptFunc;
	static ConnectFunc g_pOnConnectFunc;
	static CloseFunc g_pOnCloseFunc;
	static RecvFunc g_pOnRecvFunc;

	std::vector<PacketInfo>* m_pReadBuff; //메인소켓에서 패킷들을 계속 처리할 버퍼
	UINT m_uReadBuffPos; //패킷 프로세스 스레드에서 읽고 있는 리드 버퍼 위치.
	std::vector<PacketInfo>* m_pWriteBuff;//차일들 소켓들이 리시브 버퍼의 내용을 계속 써넣을 메인소켓의 버퍼
	ULONG m_uInterLockWriteBuffPos; //인터락으로 관리할 라이트 버퍼의 위치.
	std::vector<PacketInfo>* m_pSendBuff;
	UINT m_uSendBuffPos;
	char* m_pTempBuff; //완전하지 않은 패킷을 임시 보관하는 버퍼
	UINT m_uTempBuffPos;
	UINT m_uIoPos;

	DWORD m_dwLockNum; //워커스레드 갯수이자 Read Write 버퍼 스왑할 때 걸 락의 갯수.
	SRWLOCK* m_pBufferSwapLock; //버퍼 스왑용 SRWLock 배열
	DWORD* m_pThreadIdArr; //워커스레드 아이디 배열

	UINT m_nChildSockNum;

public:
	CIocp();
	virtual ~CIocp();

	bool InitConnectionList(UINT nCount);

	bool InitSocket(ECSType csType, UINT port); // 클라이언트의 경우 포트는 NULL 넣어서 이용.
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
	CIocp* GetNoneConnectConnection(); //소켓 재활용해서 소켓핸들은 이미 있지만 연결은 안되어 있는 차일드 커넥션 얻을때.
	bool GetPeerName(CString& peerAdress, UINT& peerPort);

	void StopThread();
	UINT GetThreadLockNum();
	UINT GetWriteContainerSize();
	UINT GetReadContainerSize();

};

