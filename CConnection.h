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
	
	int SetAcceptContextOpt(); //getpeername �� getsockname ���� �۵��ϱ� ���ؼ� �ʿ�
	int SetConnectContextOpt();

	bool CloseSocket();

	//recv
	bool PrepareRecv();
	void RecvProcess(DWORD dwRecvBytes, char** ppMsg, DWORD* pdwMsgBytes, DWORD* pdwMsgNum);
	void CheckReset();

	//send
	bool Send(char* pMsg, DWORD dwBytes); //��Ʈ��ũ �ʿ��� ȣ��
	bool PushSend(char* pMsg, DWORD dwBytes); //������ ������ �޼��� �ֱ�
	bool SendBuff(); //���� WSASend�� ȣ��Ǿ� send �� ���

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
	DWORD m_dwConnectionIndex; //Ŀ�ؼ� ��ȣ
	SOCKET m_socket;
	bool m_ConnectionStatus;
	
	char m_szIP[IP_BUFF_SIZE];
	char m_AddrBuf[ADDR_BUFF_SIZE] = { 0, };
	DWORD m_dwRemotePort; 

	OverlappedEX* m_pRecvOverlapped;
	OverlappedEX* m_pSendOverlapped;

	SRRecvRingBuffer* m_pRecvBuff;
	SRSendRingBuffer* m_pSendBuff;

	//IO�� �ɷ��ִ� ��ŭ �������� ������ GQCS�� �����Ѵ�. �ɷ��ִ� IO�� ���� ó���Ǿ��� ��(���۷��� ī��Ʈ�� ��� 0) ���� �� �ֵ��� �Ѵ�.
	//CloseSocket ��� DisconnectEX�� IO�� �ɰ� �뺸 ������ AcceptEX�� �ٽ� �Ŵ� ����.
	DWORD m_dwAcceptRefCount;
	DWORD m_dwConnectRefCount;
	DWORD m_dwRecvRefCount; 
	DWORD m_dwSendRefCount;

	DWORD m_dwSendWait; //send �뺸 �Ϸ� �Ǳ� ������ ������ �ʵ��� ���. ���� �����忡�� send�� �뺸�� ���� �� ������ ���Ͷ����� ����.

};
