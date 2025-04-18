#pragma once
#define _WINSOCK_DEPRECATED_NO_WARNINGS // inet_addr gethostbyname 등의 deprecated 함수 이용

#include <process.h> //beginthreadex 등
#include <iostream>
#include <string>	//std::string
#include <atlstr.h> //CString 
#include <vector>
#include <map>
#include <ws2tcpip.h>

#include "NetworkDefine.h"
#include "CConnection.h"

class CIocpManager;
class CConnection;

//라이브러리를 사용할 외부에서 건네줄 함수포인터
typedef void (*AcceptFunc)(DWORD dwIndex);
typedef void (*ConnectFunc)(DWORD dwIndex);
typedef void (*CloseFunc)(DWORD dwIndex);
typedef void (*RecvFunc)(DWORD dwIndex, char* pMsg, DWORD dwBytes);


class CIocpManager
{

public:
	CIocpManager();
	virtual ~CIocpManager();

public:
	//초기화
	bool Initialize(ECSType eCSType, UINT uPort, UINT dwNum);
	bool InitNetwork(ECSType csType, UINT port); // 클라이언트의 경우 포트는 NULL 넣어서 이용.
	bool InitAcceptConnection(DWORD dwNum);
	bool InitAccepIoCompletionPort(DWORD dwNum);
	bool InitConnectConnection(DWORD dwNum);
	bool InitConnectIoCompletionPort(DWORD dwNum);

	bool GetIoExFuncPointer();
	
	//워커스레드
	bool CreateWorkerThread();
	static unsigned __stdcall WorkerThread(LPVOID CompletionPortObj);
	void StopWorkerThread();
	UINT GetThreadLockNum();

	//함수포인터
	static void SetOnAcceptFunc(AcceptFunc pFunc) { g_pOnAcceptFunc = pFunc; };
	static void OnAccept(DWORD dwIndex) { g_pOnAcceptFunc(dwIndex); };
	
	static void SetOnConnectFunc(ConnectFunc pFunc) { g_pOnConnectFunc = pFunc; };
	static void OnConnect(DWORD dwIndex) { g_pOnConnectFunc(dwIndex); };
	
	static void SetOnCloseFunc(CloseFunc pFunc) { g_pOnCloseFunc = pFunc; };
	static void OnClose(DWORD dwIndex) { g_pOnCloseFunc(dwIndex); };
	
	static void SetOnRecvFunc(RecvFunc pFunc) { g_pOnRecvFunc = pFunc; };
	static void OnRecv(DWORD dwIndex, char* pMsg, DWORD dwBytes) { g_pOnRecvFunc(dwIndex, pMsg, dwBytes); };

	//소켓 옵션
	void InitSocketOption(SOCKET socket);
	void SetReuseSocketOption(SOCKET socket);
	void SetLingerSocketOption(SOCKET socket);
	void SetNagleOffSocketOption(SOCKET socket);

	//커넥션 관리
	CConnection* GetConnection(DWORD dwIndex);
	CConnection* GetServerConnection(DWORD dwIndex);
	CConnection* GetClientConnection(DWORD dwIndex);
	CConnection* GetFreeClientConnection();
	
	//IO통보 후 처리
	void PostAccept(DWORD dwIndex);
	void PostConnect(DWORD dwIndex);
	void PostRecv(DWORD dwIndex, DWORD dwRecvBytes, char** ppMsg, DWORD* pdwMsgBytes, DWORD* pdwMsgNum, DWORD dwLockIndex);
	void PostSend(DWORD dwIndex, DWORD dwBytes);

	//패킷 처리
	void PacketProcess();
	void SwapRecvQueue();
	void PushWriteQueue(DWORD dwIndex, char * pMsg, DWORD dwMsgNum, DWORD dwMsgBytes, DWORD dwLockIndex);

	//소켓 관리
	bool ReuseSocket(DWORD dwIndex);
	bool CloseConnection(DWORD dwIndex);
	
	CConnection* Connect(char* pAddress, u_short port);

	bool Send(DWORD dwIndex, char* pMsg, DWORD dwBytes);


public:
	ECSType m_eCSType; //서버 클라이언트 구분.
	u_short m_nBindPort; //accept용도의 bind할 포트
	SOCKET m_ListenSocket; //리슨 소켓
	HANDLE m_CompletionPort; //completionport 핸들

	std::vector<CConnection*> m_ServerConnectionList; //서버 입장으로 accept한 커넥션 리스트
	std::vector<CConnection*> m_ClientConnectionList; //클라이언트 입장으로 connect한 커넥션 리스트. 클라이언트에서 쓸 수도 있고, 서버 간 통신으로 서버가 다른 서버에 클라이언트 입장으로 붙을 때도 쓸 수 있다.

	std::string m_szRemoteIP; //외부에서 연결시도한 커넥션의 ip
	u_short m_nRemotePort; //외부에서 연결시도한 커넥션의 포트
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

	std::vector<PacketInfo> m_RecvQueue1;
	std::vector<PacketInfo> m_RecvQueue2;

	std::vector<PacketInfo>* m_pReadQueue; //패킷을 처리할 리드 큐(더블 버퍼링 프론트 버퍼)
	DWORD m_dwReadQueuePos; //리드 큐 현재 위치
	DWORD m_dwReadQueueSize; //리드 큐 요소 개수
	std::vector<PacketInfo>* m_pWriteQueue;//리시브 받은 데이터가 패킷이 되면 써넣을 큐(더블 버퍼링 백 버퍼)
	DWORD m_dwWriteQueuePos; //인터락으로 관리할 라이트 큐 현재 위치
	DWORD m_dwWriteQueueSize; //라이트 큐 요소 개수

	HANDLE m_QueueSwapWaitEvent; //라이트 큐가 넘치면 스왑할 때 까지 대기하기 위한 이벤트

	std::vector<PacketInfo> m_pSendQueue;
	DWORD m_dwSendQueuePos;

	DWORD m_dwLockNum; //워커스레드 갯수이자 Read Write 버퍼 스왑할 때 걸 락의 갯수.
	SRWLOCK* m_pBufferSwapLock; //버퍼 스왑용 SRWLock 배열
	DWORD* m_pThreadIdArr; //워커스레드 아이디 배열

	DWORD m_dwAcceptConnectionMax; //accept커넥션 맥스 최대 개수
	DWORD m_dwConnectConnectionMax; //connect커넥션 맥스 최대 개수
	DWORD m_dwConnectionSize; //커넥션 현재 개수
};

