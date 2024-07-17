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
	
	bool PostRecv();

	//recv 오버랩 함수
	void RecvProcess(DWORD dwRecvBytes, char** ppMsg, DWORD* pdwMsgBytes, DWORD* pdwMsgNum);
	void CheckReset();

	//send 오버랩 함수
	bool Send(char* pMsg, DWORD dwBytes); //네트워크 쪽에서 호출
	bool PushSend(char* pMsg, DWORD dwBytes); //보내기 전까지 메세지 넣기
	bool SendBuff(); //실제 WSASend가 호출되어 send 를 대기
	bool PostSend(DWORD dwBytes);

	bool CloseSocket();

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

	void LockSend();
	void UnLockSend();

private:
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

	DWORD m_dwSendWait; //Send 통보 완료 되기 전까지 보내지 않도록 대기. 여러 스레드에서 send와 통보를 받을 수 있으니 인터락으로 관리.

};
