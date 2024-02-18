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
#include <deque>
#include <ws2tcpip.h>

const unsigned int BUFFSIZE = 512;

class CIocp;

enum class CSType
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
	OVERLAPPED overLapped;
	WSABUF dataBuff;
	CHAR Buff[BUFFSIZE];
	SOCKET connectSocket;
	IOType ioType;
}IODATA;

typedef struct
{
	CIocp* connection;
	CHAR Buff[BUFFSIZE];
}PacketInfo;


//CIocp를 상속받는 메인소켓과 똑같이 CIocp를 상속받는 차일드소켓
//차일드소켓들은 메인소켓을 멤버로 가지고 있음. 차일드 소켓들이 각각 연결된 커넥션 정도의 의미.
class CIocp
{
public:
	CSType m_csType;
	UINT bindPort; //accept용도의 bind할 포트
	bool isConnected; //차일드 소켓의 경우 accept/connect 체크용. 메인 소켓의 경우 루프 중 delete 타이밍을 맞추기 위해 이용.
	SOCKET m_listenSocket; //리슨소켓
	SOCKET m_socket; //차일드 소켓들이 가질 소켓. send recv용
	HANDLE completionPort; //completionport 핸들
	SYSTEM_INFO sysInfo; //cpu개수 체크용
	IODATA* m_ioData; //오버랩 확장. 버퍼 및 리시브 센드 타입
	std::vector<CIocp*> pConnectionList; //차일드 소켓 풀.
	CIocp* m_pMainConnection;
	std::string remoteIP; //외부에서 연결시도한 커넥션의 ip
	UINT remotePort; //외부에서 연결시도한 커넥션의 포트
	bool bIsWorkerThread;
	CRITICAL_SECTION recvCS;
	LPFN_ACCEPTEX lpfnAcceptEx;
	LPFN_DISCONNECTEX lpfnDisconnectEx;
	LPFN_CONNECTEX lpfnConnectEx;
	std::vector<PacketInfo>* readBuff; //메인소켓에서 패킷들을 계속 처리할 버퍼
	UINT readBuffPos; //패킷 프로세스 스레드에서 읽고 있는 리드 버퍼 위치.
	std::vector<PacketInfo>* writeBuff;//차일들 소켓들이 리시브 버퍼의 내용을 계속 써넣을 메인소켓의 버퍼
	ULONG ilWriteBuffPos; //인터락으로 관리할 라이트 버퍼의 위치.
	std::vector<PacketInfo>* sendBuff;
	UINT sendBuffPos;
	char* recvBuff; //완전하지 않은 패킷을 임시 보관하는 버퍼
	UINT recvBuffPos;
	UINT ioBuffPos;

	DWORD dwLockNum; //워커스레드 갯수이자 RW버퍼 스왑할 때 걸 락의 갯수.
	SRWLOCK* m_BufferSwapLock; //버퍼 스왑용 SRWLock 배열
	DWORD* m_ThreadIdArr; //워커스레드 아이디 배열

	UINT m_ChildSockNum;

public:
	CIocp();
	virtual ~CIocp();

	//차일드 소켓에서 OnRecv()등의 다형성을 위해ciocp를 상속받아 만든 소켓 클래스의 타입을 알아야 돼서 템플릿으로..
	template<typename T>
	T* SetChildSockType(T* type, int count)
	{
		for (int i = 0; i < count; i++)
		{
			CIocp* child = new T();
			child->recvBuff = new char[BUFFSIZE];
			ZeroMemory(child->recvBuff, BUFFSIZE);
			child->m_pMainConnection = this;
			child->recvCS = this->recvCS;
			pConnectionList.push_back(child);
		}

		m_ChildSockNum = count;
		return type;
	};


	bool InitSocket(CSType csType, UINT port); // 클라이언트의 경우 포트는 NULL 넣어서 이용.
	void InitSocketOption(SOCKET socket);
	void SetReuseSocketOpt(SOCKET socket);
	void SetLingerOpt(SOCKET socket);
	void SetNagleOffOpt(SOCKET socket);
	bool CreateWorkerThread();
	static unsigned __stdcall WorkerThread(LPVOID CompletionPortObj);
	void PacketProcess();
	void SendPacketProcess();
	void SwapRWBuffer();
	void PushWriteBuffer(PacketInfo* packetInfo, DWORD dwLockIndex);
	bool InitAcceptPool(UINT num);
	bool InitConnectPool(UINT num);
	bool ReAcceptSocket(SOCKET socket);
	void CloseSocket(SOCKET socket);
	SOCKET Connect(LPCTSTR lpszHostAddress, UINT port);
	bool RecvSet(CIocp* pClient);
	bool RecvSet(CIocp* pClient, IOType ioType); //리시브 연결하되 타입은 recv가 아니도록 받기 위해서. ConnectEx가 0바이트를 보내는데 연결 종료와 구분하기 위해 이용.
	CIocp* GetEmptyConnection();
	CIocp* GetConnection(SOCKET socket);
	CIocp* GetNoneConnectConnection(); //소켓 재활용해서 소켓핸들은 이미 있지만 연결은 안되어 있는 차일드 커넥션 얻을때.
	bool GetPeerName(CString& peerAdress, UINT& peerPort);
	bool Send(void* lpBuff, int nBuffSize);
	void SendToBuff(void* lpBuff, int nBuffSize);

	PacketInfo GetPacket(); //read버퍼 위치를 가르키는 곳의 패킷을 반환.
	void StopThread();
	UINT GetThreadLockNum();
	UINT GetWriteContainerSize();
	UINT GetReadContainerSize();



public:
	virtual void OnAccept(SOCKET socket) {};
	virtual void OnConnect(SOCKET socket) {};
	virtual void OnReceive() {};
	virtual void OnClose() {};
};

