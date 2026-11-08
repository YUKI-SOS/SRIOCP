#pragma once
#include "NetworkDefine.h"
#include "SRRecvRingBuffer.h"
#include "SRSendRingBuffer.h"

class CIocpManager;

class CConnection
{
public:
	CConnection();
	virtual ~CConnection();

public:
	bool Initialize(EConnectionType eConnectionType, DWORD dwConnectionIndex, SOCKET socket, DWORD dwRecvRingBuffSize, DWORD dwSendRingBuffSize);
	void Recycle();

	int SetAcceptContextOpt(); //getpeername 및 getsockname 정상 작동하기 위해서 필요
	int SetConnectContextOpt();

	bool CloseSocket();

	//recv
	bool PrepareRecv();
	bool RecvProcess(DWORD dwRecvBytes, char** ppMsg, DWORD* pdwMsgBytes, DWORD* pdwMsgNum);
	void CheckReset();

	//send
	bool Send(char* pMsg, DWORD dwBytes); //네트워크 쪽에서 호출
	bool PushSend(char* pMsg, DWORD dwBytes); //보내기 전까지 메세지 넣기
	bool PrepareSend(); //실제 WSASend가 호출되어 send 를 대기

	void LockSend();
	void UnLockSend();


public:
	static CIocpManager* GetNetworkManager() { return m_pIOCPManager; }

	EConnectionType GetConnectionType() { return m_eConnectionType; }
	DWORD GetConnectionIndex() { return m_dwConnectionIndex; }
	SOCKET GetSocket() { return m_socket; }
	void SetSocket(SOCKET socket);
	EConnectionState GetConnectionState() { return m_eConnectionState; }
	void SetConnectionState(EConnectionState eState);

	void SetRemoteIP(char* szIP, DWORD dwLength);
	bool GetPeerName(char* pAddress, DWORD* pPort);

	char* GetAddrBuff();

	DWORD GetRemotePort();
	void SetRemotePort(DWORD dwPort);

	OverlappedEX* GetRecvOverlapped();
	OverlappedEX* GetSendOverlapped();

	SRRecvRingBuffer* GetRecvRingBuff();
	SRSendRingBuffer* GetSendRingBuff();

public:
	void IOLock();
	void IOUnLock();

	DWORD GetRecvRefCount();
	DWORD GetSendRefCount();

	LONG IncreaseRef(EIOType eIOType);
	LONG DecreaseRef(EIOType eIOType);

public:
	static CIocpManager* m_pIOCPManager;
	EConnectionType m_eConnectionType;
	DWORD m_dwConnectionIndex; //커넥션 번호
	SOCKET m_socket;
	EConnectionState m_eConnectionState;
	
	char m_szIP[IP_BUFF_SIZE];
	char m_AddrBuf[ADDR_BUFF_SIZE] = { 0, };
	DWORD m_dwRemotePort; 

	OverlappedEX* m_pRecvOverlapped;
	OverlappedEX* m_pSendOverlapped;

	SRRecvRingBuffer* m_pRecvBuff;
	SRSendRingBuffer* m_pSendBuff;

	SRWLOCK m_kIOLock;

	//IO가 걸려있는 만큼 끊어졌을 때에도 GQCS가 응답한다. 걸려있는 IO가 전부 처리되었을 때(레퍼런스 카운트가 모두 0) 끊을 수 있도록 한다.
	//CloseSocket 대신 DisconnectEX로 IO를 걸고 통보 받으면 AcceptEX를 다시 거는 구조.
	DWORD m_dwRecvRefCount; 
	DWORD m_dwSendRefCount;

	DWORD m_dwSendWait; //send 통보 완료 되기 전까지 보내지 않도록 대기. 여러 스레드에서 send와 통보를 받을 수 있으니 인터락으로 관리.

};
