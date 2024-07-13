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

	//recv ������ �Լ�
	void RecvProcess(DWORD dwRecvBytes, char** ppMsg, DWORD* pdwMsgBytes, DWORD* pdwMsgNum);
	void CheckReset();

	//send ������ �Լ�
	bool Send(char* pMsg, DWORD dwBytes); //��Ʈ��ũ �ʿ��� ȣ��
	bool PushSend(char* pMsg, DWORD dwBytes); //������ ������ �޼��� �ֱ�
	bool SendBuff(); //���� WSASend�� ȣ��Ǿ� send �� ���

	bool CloseSocket();

	bool GetPeerName(char* pAddress, DWORD* pPort);

public:
	CIocp* GetNetwork();
	void SetNetwork(CIocp* pNetwork);
	SOCKET GetSocket();
	bool GetConnectionStaus();
	void SetConnectionStatus(bool status);

	void SetRemoteIP(char* szIP, DWORD dwLength);

	char* GetAddrBuff();

	DWORD GetRemotePort();
	void SetRemotePort(DWORD dwPort);

	OverlappedEX* GetRecvOverlapped();
	OverlappedEX* GetSendOverlapped();

	SRRecvRingBuffer* GetRecvRingBuff();
	SRSendRingBuffer* GetSendRingBuff();

private:
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

};
