#pragma once
#define _WINSOCK_DEPRECATED_NO_WARNINGS // inet_addr gethostbyname ���� deprecated �Լ� �̿�

#include <process.h> //beginthreadex ��
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

//���̺귯���� ����� �ܺο��� �ǳ��� �Լ�������
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
	//�ʱ�ȭ
	bool InitNetwork(ECSType csType, UINT port); // Ŭ���̾�Ʈ�� ��� ��Ʈ�� NULL �־ �̿�.
	bool InitConnectionList(DWORD dwCount);
	bool InitAcceptPool(DWORD dwNum);
	//bool InitConnectPool(UINT num);

	bool GetIoExFuncPointer();
	
	//��Ŀ������
	bool CreateWorkerThread();
	static unsigned __stdcall WorkerThread(LPVOID CompletionPortObj);
	void StopWorkerThread();
	UINT GetThreadLockNum();

	//�Լ�������
	static void SetOnAcceptFunc(AcceptFunc pFunc) { g_pOnAcceptFunc = pFunc; };
	static void OnAccept(DWORD dwIndex) { g_pOnAcceptFunc(dwIndex); };
	
	static void SetOnConnectFunc(ConnectFunc pFunc) { g_pOnConnectFunc = pFunc; };
	static void OnConnect(DWORD dwIndex) { g_pOnConnectFunc(dwIndex); };
	
	static void SetOnCloseFunc(CloseFunc pFunc) { g_pOnCloseFunc = pFunc; };
	static void OnClose(DWORD dwIndex) { g_pOnCloseFunc(dwIndex); };
	
	static void SetOnRecvFunc(RecvFunc pFunc) { g_pOnRecvFunc = pFunc; };
	static void OnRecv(DWORD dwIndex, char* pMsg, DWORD dwLength) { g_pOnRecvFunc(dwIndex, pMsg, dwLength); };

	//���� �ɼ�
	void InitSocketOption(SOCKET socket);
	void SetReuseSocketOption(SOCKET socket);
	void SetLingerSocketOption(SOCKET socket);
	void SetNagleOffSocketOption(SOCKET socket);

	//Ŀ�ؼ� ����
	CConnection* GetConnection(DWORD dwIndex);
	CConnection* GetFreeConnection();
	
	//IO�뺸 �� ó��
	void PostAccept(DWORD dwIndex);
	void PostConnect(DWORD dwIndex);
	void PostRecv(DWORD dwIndex, DWORD dwRecvBytes, char** ppMsg, DWORD* pdwMsgBytes, DWORD* pdwMsgNum, DWORD dwLockIndex);
	void PostSend(DWORD dwIndex, DWORD dwBytes);

	//��Ŷ ó��
	void PacketProcess();
	void SwapRecvQueue();
	void PushWriteQueue(DWORD dwIndex, char * pMsg, DWORD dwMsgNum, DWORD dwMsgBytes, DWORD dwLockIndex);

	//���� ����
	bool ReuseSocket(DWORD dwIndex);
	bool CloseConnection(DWORD dwIndex);
	
	SOCKET Connect(char* pAddress, u_short port);

	bool Send(DWORD dwIndex, char* pMsg, DWORD dwBytes);


public:
	ECSType m_eCSType; //���� Ŭ���̾�Ʈ ����.
	u_short m_nBindPort; //accept�뵵�� bind�� ��Ʈ
	SOCKET m_ListenSocket; //���� ����
	HANDLE m_CompletionPort; //completionport �ڵ�

	std::vector<CConnection*> m_ServerConnectionList; //���� �������� accept�� Ŀ�ؼ� ����Ʈ
	std::vector<CConnection*> m_ClientConnectionList; //Ŭ���̾�Ʈ �������� connect�� Ŀ�ؼ� ����Ʈ

	std::string m_szRemoteIP; //�ܺο��� ����õ��� Ŀ�ؼ��� ip
	u_short m_nRemotePort; //�ܺο��� ����õ��� Ŀ�ؼ��� ��Ʈ
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

	std::vector<PacketInfo> m_RecvQueue1;
	std::vector<PacketInfo> m_RecvQueue2;

	std::vector<PacketInfo>* m_pReadQueue; //��Ŷ�� ó���� ���� ť(���� ���۸� ����Ʈ ����)
	DWORD m_dwReadQueuePos; //���� ť ���� ��ġ
	DWORD m_dwReadQueueSize; //���� ť ��� ����
	std::vector<PacketInfo>* m_pWriteQueue;//���ú� ���� �����Ͱ� ��Ŷ�� �Ǹ� ����� ť(���� ���۸� �� ����)
	DWORD m_dwWriteQueuePos; //���Ͷ����� ������ ����Ʈ ť ���� ��ġ
	DWORD m_dwWriteQueueSize; //����Ʈ ť ��� ����

	HANDLE m_QueueSwapWaitEvent; //����Ʈ ť�� ��ġ�� ������ �� ���� ����ϱ� ���� �̺�Ʈ

	std::vector<PacketInfo> m_pSendQueue;
	DWORD m_dwSendQueuePos;

	DWORD m_dwLockNum; //��Ŀ������ �������� Read Write ���� ������ �� �� ���� ����.
	SRWLOCK* m_pBufferSwapLock; //���� ���ҿ� SRWLock �迭
	DWORD* m_pThreadIdArr; //��Ŀ������ ���̵� �迭

	DWORD m_dwConnectionMax; //Ŀ�ؼ� �ƽ� �ִ� ����
	DWORD m_dwConnectionSize; //Ŀ�ؼ� ���� ����
};

