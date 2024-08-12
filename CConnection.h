#pragma once
#include "NetworkDefine.h"
#include "SRRecvRingBuffer.h"
#include "SRSendRingBuffer.h"

class CIocp;

class CConnection
{
public:
	CConnection();
	virtual ~CConnection();

public:
	bool Initialize(DWORD dwConnectionIndex, SOCKET socket, DWORD dwRecvRingBuffSize, DWORD dwSendRingBuffSize);
	void Recycle();
	
	int SetAcceptContextOpt(); //getpeername 및 getsockname 정상 작동하기 위해서 필요
	int SetConnectContextOpt();

	bool CloseSocket();

	//recv
	bool PrepareRecv();
	void RecvProcess(DWORD dwRecvBytes, char** ppMsg, DWORD* pdwMsgBytes, DWORD* pdwMsgNum);
	void CheckReset();

	//send
	bool Send(char* pMsg, DWORD dwBytes); //네트워크 쪽에서 호출
	bool PushSend(char* pMsg, DWORD dwBytes); //보내기 전까지 메세지 넣기
	bool SendBuff(); //실제 WSASend가 호출되어 send 를 대기

	void LockSend();
	void UnLockSend();


public:
	CIocp* GetNetwork();
	void SetNetwork(CIocp* pNetwork);
	SOCKET GetSocket();
	bool GetConnectionStaus();
	void SetConnectionStatus(bool status);

	void SetRemoteIP(char* szIP, DWORD dwLength);
	bool GetPeerName(char* pAddress, DWORD* pPort);

	char* GetAddrBuff();

	DWORD GetRemotePort();
	void SetRemotePort(DWORD dwPort);

	OverlappedEX* GetRecvOverlapped();
	OverlappedEX* GetSendOverlapped();

	SRRecvRingBuffer* GetRecvRingBuff();
	SRSendRingBuffer* GetSendRingBuff();

	DWORD GetAcceptRefCount();
	DWORD GetConnectRefCount();
	DWORD GetRecvRefCount();
	DWORD GetSendRefCount();

	void IncreaseAcceptRef();
	void DecreaseAcceptRef();
	void IncreaseConnectRef();
	void DecreaseConnectRef();
	void IncreaseRecvRef();
	void DecreaseRecvRef();
	void IncreaseSendRef();
	void DecreaseSendRef();

public:
	CIocp* m_pNetwork;
	DWORD m_dwConnectionIndex; //커넥션 번호
	SOCKET m_socket;
	bool m_ConnectionStatus;
	
	char m_szIP[IP_BUFF_SIZE];
	char m_AddrBuf[ADDR_BUFF_SIZE] = { 0, };
	DWORD m_dwRemotePort; 

	OverlappedEX* m_pRecvOverlapped;
	OverlappedEX* m_pSendOverlapped;

	SRRecvRingBuffer* m_pRecvBuff;
	SRSendRingBuffer* m_pSendBuff;

	//IO가 걸려있는 만큼 끊어졌을 때에도 GQCS가 응답한다. 걸려있는 IO가 전부 처리되었을 때(레퍼런스 카운트가 모두 0) 끊을 수 있도록 한다.
	//CloseSocket 대신 DisconnectEX로 IO를 걸고 통보 받으면 AcceptEX를 다시 거는 구조.
	DWORD m_dwAcceptRefCount;
	DWORD m_dwConnectRefCount;
	DWORD m_dwRecvRefCount; 
	DWORD m_dwSendRefCount;

	DWORD m_dwSendWait; //send 통보 완료 되기 전까지 보내지 않도록 대기. 여러 스레드에서 send와 통보를 받을 수 있으니 인터락으로 관리.

};
