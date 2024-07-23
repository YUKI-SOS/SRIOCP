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

class CIocp;
class CConnection;

//라이브러리를 사용할 외부에서 건네줄 함수포인터
typedef void (*AcceptFunc)(DWORD dwIndex);
typedef void (*ConnectFunc)(DWORD dwIndex);
typedef void (*CloseFunc)(DWORD dwIndex);
typedef void (*RecvFunc)(DWORD dwIndex, char* pMsg, DWORD dwLength);


class CIocp
{

public:
	CIocp();
	virtual ~CIocp();

public:
	bool InitConnectionList(DWORD dwCount);
	bool GetIoExFuncPointer();
	bool InitNetwork(ECSType csType, UINT port); // 클라이언트의 경우 포트는 NULL 넣어서 이용.
	
	void InitSocketOption(SOCKET socket);
	void SetReuseSocketOpt(SOCKET socket);
	void SetLingerOpt(SOCKET socket);
	void SetNagleOffOpt(SOCKET socket);
	int SetAcceptContextOpt(SOCKET socket); //getpeername 및 getsockname 정상 작동하기 위해서 필요
	int SetConnectContextOpt(SOCKET socket);

	bool InitAcceptPool(DWORD dwNum);
	//bool InitConnectPool(UINT num);

	static void SetOnAcceptFunc(AcceptFunc pFunc) { g_pOnAcceptFunc = pFunc; };
	static void OnAccept(DWORD dwIndex) { g_pOnAcceptFunc(dwIndex); };
	
	static void SetOnConnectFunc(ConnectFunc pFunc) { g_pOnConnectFunc = pFunc; };
	static void OnConnect(DWORD dwIndex) { g_pOnConnectFunc(dwIndex); };
	
	static void SetOnCloseFunc(CloseFunc pFunc) { g_pOnCloseFunc = pFunc; };
	static void OnClose(DWORD dwIndex) { g_pOnCloseFunc(dwIndex); };
	
	static void SetOnRecvFunc(RecvFunc pFunc) { g_pOnRecvFunc = pFunc; };
	static void OnRecv(DWORD dwIndex, char* pMsg, DWORD dwLength) { g_pOnRecvFunc(dwIndex, pMsg, dwLength); };

	bool CreateWorkerThread();
	static unsigned __stdcall WorkerThread(LPVOID CompletionPortObj);

	void PacketProcess();
	//void SendPacketProcess();
	void SwapRecvQueue();
	void PushWriteQueue(DWORD dwIndex, char * pMsg, DWORD dwMsgNum, DWORD dwMsgBytes, DWORD dwLockIndex);

	bool ReAcceptSocket(DWORD dwIndex);
	bool CloseConnection(DWORD dwIndex);

	SOCKET Connect(char* pAddress, UINT port);

	bool Send(DWORD dwIndex, char* pMsg, DWORD dwBytes);
	//void SendToBuff(void* lpBuff, int nBuffSize);

	CConnection* GetConnection(DWORD dwIndex);
	CConnection* GetFreeConnection();

	void StopThread();
	UINT GetThreadLockNum();

public:
	ECSType m_eCSType; //서버용/클라용 네트워크 
	UINT m_nBindPort; //accept용도의 bind할 포트
	SOCKET m_ListenSocket; //리슨 소켓
	HANDLE m_CompletionPort; //completionport 핸들

	std::vector<CConnection*> m_ConnectionList; //커넥션 리스트

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

	DWORD m_dwConnectionMax; //커넥션 맥스 최대 개수
	DWORD m_dwConnectionSize; //커넥션 현재 개수
};

