#include "SRIocp.h"
#include "CConnection.h"

//전역 변수 초기화
AcceptFunc CIocpManager::g_pOnAcceptFunc = NULL;
ConnectFunc CIocpManager::g_pOnConnectFunc = NULL;
CloseFunc CIocpManager::g_pOnCloseFunc = NULL;
RecvFunc CIocpManager::g_pOnRecvFunc = NULL;


CIocpManager::CIocpManager()
{
	m_hListenSocket = INVALID_SOCKET;
	m_hCompletionPort = NULL;
	m_eCSType = ECSType::NONE;

	m_bAcceptThreadLive = false;
	m_bWorkerThreadLive = false;

	m_dwConnectionMax = 0;
	m_dwConnectionCount = 0;
	m_dwConnectConnectionMax = 0;

	m_pReadQueue = nullptr;
	m_pWriteQueue = nullptr;
	m_pThreadIdArr = nullptr;

	m_dwReadQueuePos = 0;
	m_dwReadQueueSize = 0;
	m_dwWriteQueuePos = -1;
	m_dwWriteQueueSize = 0;

	m_dwSendQueuePos = 0;

	m_dwLockNum = 0;
	m_pBufferSwapLock = nullptr;

	m_QueueSwapWaitEvent = NULL;
}


CIocpManager::~CIocpManager()
{
	if (m_pBufferSwapLock != nullptr)
		delete[] m_pBufferSwapLock;
	if (m_pThreadIdArr != nullptr)
		delete[] m_pThreadIdArr;

}

bool CIocpManager::Initialize(ECSType eCSType, u_short uPort, DWORD dwAcceptNum, DWORD dwConnectNum)
{
	if (eCSType == ECSType::NONE)
	{
		printf("CIocpManager::Initialize Fail. eCSType == ECSType::NONE \n");
		return false;
	}

	if (InitNetwork(eCSType, uPort) == false)
		return false;
	if (InitAcceptConnection(dwAcceptNum) == false)
		return false;
	if (InitConnectConnection(dwConnectNum) == false)
		return false;

	if (eCSType == ECSType::SERVER)
	{
		if (CreateAcceptThread() == false)
			return false;
	}

	return true;
}

bool CIocpManager::InitNetwork(ECSType eCSType, u_short uPort)
{
	WSADATA wsaData;

	m_eCSType = eCSType;

	m_dwReadQueuePos = 0;
	m_dwReadQueueSize = 0;

	m_dwWriteQueuePos = -1;
	m_dwWriteQueueSize = 0;

	m_RecvQueue1.resize(RECV_QUEUE_MAX);
	m_RecvQueue2.resize(RECV_QUEUE_MAX);

	m_pReadQueue = &m_RecvQueue1;
	m_pWriteQueue = &m_RecvQueue2;

	m_pSendQueue.resize(SEND_QUEUE_MAX);
	m_dwSendQueuePos = 0;

	CConnection::m_pIOCPManager = this;

	//이벤트 초기화(수동 리셋)
	m_QueueSwapWaitEvent = CreateEvent(NULL, TRUE, FALSE, NULL);

	//윈속 초기화
	if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
	{
		printf("WSAStartup Fail\n");
		return false;
	}

	//iocp객체 생성
	//CreateIoCompletionPort마지막 인자가 0이면 cpu 코어 개수만큼 스레드 이용
	if ((m_hCompletionPort = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 0)) == NULL)
	{
		printf("CreateIoCompletionPort Fail\n");
		return false;
	}

	//워커스레드 생성
	if (CreateWorkerThread() == false)
	{
		printf("CreateWorkerThread Fail\n");
		return false;
	}

	//클라이언트도 ConnectEx 등의 함수의 포인터를 얻기 위해 임의의 소켓이 필요함.
	if ((m_hListenSocket = WSASocket(AF_INET,
		SOCK_STREAM,
		IPPROTO_TCP,
		NULL,
		0,
		WSA_FLAG_OVERLAPPED
	)) == INVALID_SOCKET)
	{
		printf("WSASocket Fail\n");
		return false;
	}

	//아래로는 서버 초기화
	if (eCSType == ECSType::CLIENT)
		return true;	

	//bind
	SOCKADDR_IN serverAddr;
	ZeroMemory(&serverAddr, sizeof(serverAddr));
	serverAddr.sin_family = AF_INET;
	serverAddr.sin_addr.s_addr = htonl(INADDR_ANY);
	serverAddr.sin_port = htons(uPort);

	//TCP홀펀칭 이미 사용중인 포트에 다른 소켓 강제 바인딩 
	//SetReuseSocketOption(m_ListenSocket);

	if (bind(m_hListenSocket,
		(PSOCKADDR)&serverAddr,
		sizeof(serverAddr)) == SOCKET_ERROR)
	{
		printf("Bind Fail. WSAGetLastError = %d\n", WSAGetLastError());
		return false;
	}

	//listen
	if (listen(m_hListenSocket, 5) == SOCKET_ERROR)
	{
		printf("Listen Fail\n");
		return false;
	}	
	
	return true;

}

bool CIocpManager::InitAcceptConnection(DWORD dwNum)
{
	m_ConnectionList.resize(dwNum);

	for (DWORD i = 0; i < dwNum; i++)
	{
		CConnection* pConnection = new CConnection;
		//SOCKET socket = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, NULL, 0, WSA_FLAG_OVERLAPPED);
		pConnection->Initialize(i, INVALID_SOCKET, RECV_RING_BUFFER_MAX, SEND_RING_BUFFER_MAX);
		m_ConnectionList[i] = pConnection;
	}

	m_dwConnectionMax = dwNum;

	return true;
}

bool CIocpManager::InitConnectConnection(DWORD dwNum)
{
	m_ConnectConnectionList.resize(dwNum);

	for (DWORD i = 0; i < dwNum; i++)
	{
		CConnection* pConnection = new CConnection;
		SOCKET socket = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, NULL, 0, WSA_FLAG_OVERLAPPED);
		pConnection->Initialize(i, socket, RECV_RING_BUFFER_MAX, SEND_RING_BUFFER_MAX);
		m_ConnectConnectionList[i] = pConnection;
	}

	m_dwConnectConnectionMax = dwNum;

	return true;
}

bool CIocpManager::AcceptIOCompletionPort(CConnection* pConnection)
{	
	SOCKET socket = pConnection->GetSocket();

	//소켓과 iocp 연결
	if ((CreateIoCompletionPort((HANDLE)socket,
		m_hCompletionPort,
		(ULONG_PTR)pConnection,
		0)) == NULL)
	{
		printf("CreateIoCompletionPort bind error \n");
		return false;
	}

	return true;
}

bool CIocpManager::ConnectIOCompletionPort(CConnection* pConnection)
{
	SOCKET socket = pConnection->GetSocket();
	
	//소켓과 iocp 연결
	if ((CreateIoCompletionPort((HANDLE)socket,
		m_hCompletionPort,
		(ULONG_PTR)pConnection,
		0)) == NULL)
	{
		printf("CreateIoCompletionPort bind error \n");
		return false;
	}

	return true;
}

void CIocpManager::InitSocketOption(SOCKET socket)
{
	SetLingerSocketOption(socket);
	SetNagleOffSocketOption(socket);
}

void CIocpManager::SetReuseSocketOption(SOCKET socket)
{
	//socket Reuse Option. SO_REUSEADDR은 서버 소켓에서만 time_wait를 재활용 할 수 있는 것 같다.
	int option = 1;
	if (setsockopt(socket, SOL_SOCKET, SO_REUSEADDR, (char*)&option, sizeof(int)) == SOCKET_ERROR)
	{
		std::cout << "Set Socket option ReuseAddr error :" << WSAGetLastError() << std::endl;
	};
}

void CIocpManager::SetLingerSocketOption(SOCKET socket)
{
	//onoff 0 - default 소켓버퍼에 남은 데이터를 전부 보내고 종료하는 정상종료
	//onoff 1 linger 0 - close 즉시 리턴하고 소켓버퍼에 남은 데이터를 버리는 비정상종료.
	//onoff 1 linger 1 - 지정시간동안 대기한 뒤 소켓버퍼에 남은 데이터를 전부 보내보고 다 보내면 정상종료 하며 리턴 못 보내면 비정상종료 에러와 함께 리턴.
	LINGER linger = { 0,0 };
	linger.l_onoff = 1;
	linger.l_linger = 0;

	setsockopt(socket, SOL_SOCKET, SO_LINGER, (CHAR*)&linger, sizeof(linger));
}

void CIocpManager::SetNagleOffSocketOption(SOCKET socket)
{
	int nagleOpt = 1; //1 비활성화 0 활성화
	setsockopt(socket, IPPROTO_TCP, TCP_NODELAY, (const char*)&nagleOpt, sizeof(nagleOpt));
}

bool CIocpManager::CreateAcceptThread()
{
	HANDLE hThread;
	m_bAcceptThreadLive = true;

	if ((hThread = (HANDLE)_beginthreadex(NULL,
		0,
		AcceptThread,
		(LPVOID)this,
		0,
		NULL)) == NULL)
	{
		printf("AcceptThread Create Fail\n");
		return false;
	};

	return true;
}

unsigned __stdcall CIocpManager::AcceptThread(LPVOID lpCompletionPortObj)
{
	CIocpManager* pIOCPManager = (CIocpManager*)lpCompletionPortObj;

	while (pIOCPManager->m_bAcceptThreadLive)
	{
		sockaddr_in kSockAddr;
		int iAddrLen = sizeof(kSockAddr);
		SOCKET socket = accept(pIOCPManager->m_hListenSocket, (sockaddr*)&kSockAddr, &iAddrLen);

		if (socket == INVALID_SOCKET)
		{
			printf("Accept Fail. WSAGetLastError = %d\n", WSAGetLastError());
			return false;
		}

		CConnection* pConnection = pIOCPManager->GetFreeConnection();

		if (pConnection == nullptr)
		{
			printf("No Free Connection Available\n");
			closesocket(socket);
			continue;
		}

		char* szAddr = inet_ntoa(kSockAddr.sin_addr);
		u_short uPort = ntohs(kSockAddr.sin_port);
		pConnection->SetRemoteIP(szAddr, (DWORD)strlen(szAddr));
		pConnection->SetRemotePort(uPort);

		pConnection->SetSocket(socket);
		pIOCPManager->InitSocketOption(socket);
		pIOCPManager->AcceptIOCompletionPort(pConnection);
		pConnection->SetConnected(true);
		pIOCPManager->PostAccept(pConnection->m_dwConnectionIndex);
	}

	_endthreadex(0);
	return 0;
}

bool CIocpManager::CreateWorkerThread()
{
	HANDLE hThread; //워커스레드 핸들

	m_bWorkerThreadLive = true;

	//cpu 개수 확인
	SYSTEM_INFO sysInfo;
	GetSystemInfo(&sysInfo);

	m_dwLockNum = sysInfo.dwNumberOfProcessors * 2;
	//m_dwLockNum = 2; //테스트

	//SRWLock 생성 및 초기화.
	m_pBufferSwapLock = new SRWLOCK[m_dwLockNum];
	for (DWORD i = 0; i < m_dwLockNum; i++)
	{
		InitializeSRWLock(m_pBufferSwapLock + i);
	}
	//스레드 아이디 배열에 저장
	m_pThreadIdArr = new DWORD[m_dwLockNum];

	//(CPU 개수 * 2)개의 워커 스레드 생성
	for (DWORD i = 0; i < m_dwLockNum; i++)
	{
		if ((hThread = (HANDLE)_beginthreadex(NULL,
			0,
			&WorkerThread,
			this,
			0,
			(unsigned int*)&m_pThreadIdArr[i])) == NULL)
		{
			printf("CreateWorkerThread Fail\n");
			return false;
		}

		CloseHandle(hThread);
	}

	printf("WorkerThread Num = %d \n", m_dwLockNum);

	return true;
}

//Overlapped IO 작업 완료 통보를 받아 처리하는 워커 스레드
unsigned __stdcall CIocpManager::WorkerThread(LPVOID lpCompletionPortObj)
{
	CIocpManager* pIocpManager = (CIocpManager*)lpCompletionPortObj;
	HANDLE completionport = pIocpManager->m_hCompletionPort;
	ULONG_PTR key = NULL;
	LPOVERLAPPED lpOverlapped = NULL;
	DWORD dwTransferredBytes = 0;

	DWORD dwLockIndex = 0;
	DWORD dwCurrentThreadID = GetCurrentThreadId();

	char* pMsg = nullptr;
	DWORD dwMsgBytes = 0;
	DWORD dwMsgNum = 0;

	//스레드 아이디를 비교해서 스레드가 가질 락 인덱스를 가진다.
	for (DWORD i = 0; i < pIocpManager->m_dwLockNum; i++)
	{
		if (pIocpManager->m_pThreadIdArr[i] == dwCurrentThreadID)
			dwLockIndex = i;
	}

	while (pIocpManager->m_bWorkerThreadLive)
	{
		BOOL bRet = GetQueuedCompletionStatus(completionport,	//CompletionPort 핸들
			&dwTransferredBytes,								//비동기 입출력 작업으로 전송된 바이트
			(PULONG_PTR)&key,									//CreateIoCompletionPort함수 호출시 전달한 세번째 인자가 여기 저장
			&lpOverlapped,										//비동기 입출력 함수 호출 시 전달한 오버랩 구조체 주소값.
			INFINITE);

		if (lpOverlapped == nullptr)
		{
			printf("lpOverlapped = nullptr \n");
			continue;
		}

		CConnection* pConnection = (CConnection*)key;
		OverlappedEX* pOverlapped = (OverlappedEX*)lpOverlapped;
		DWORD dwIndex = (DWORD)pOverlapped->dwIndex;
		//CConnection* pConnection = pIocpManager->GetConnection(dwIndex);
		SOCKET socket = pConnection->GetSocket();

#ifdef __DEV_LOG__
		printf("GQCS CurrentThreadID = %d dwIndex = %d transferredBytes = %d eIoType = %d\n", dwCurrentThreadID, dwIndex, dwTransferredBytes, (int)pOverlapped->eIoType);
#endif 

		if (bRet == FALSE)
		{
			//걸려있던 IO 타입이 있으면 레퍼런스 카운트를 감소시켜 준다.
			switch (pOverlapped->eIoType)
			{					
			case IOType::RECV:
				pConnection->DecreaseRecvRef();
				break;
			case IOType::SEND:
				pConnection->DecreaseSendRef();
				break;
			default:
				break;
			}

			printf("Socket = %d GetQueuedCompletionStatus Fail WSAGetLastError = %d \n", socket, WSAGetLastError());
			pIocpManager->CloseConnection(pConnection);
			continue;
		}

		//IO가 통보되었는데 사이즈가 0이면 상대쪽에서 연결을 끊은 것으로 판단.
		if (dwTransferredBytes == 0)
		{
			if (pOverlapped->eIoType == IOType::RECV)
				pConnection->DecreaseRecvRef();
			else if (pOverlapped->eIoType == IOType::SEND)
				pConnection->DecreaseSendRef();

			printf("dwIndex = %d Socket = %d dwTransferredBytes = 0 \n", dwIndex, socket);			
			pIocpManager->CloseConnection(pConnection);
			continue;
		}

		//비동기 입출력에서 오버랩구조체를 인자로 전달할 때 오버랩구조체를 멤버로 가진 확장구조체를 오버랩으로 캐스팅해서 보내고
		//GetQueuedCompletionStatus에서 받은 오버랩 구조체를 다시 원래 구조체로 캐스팅하면 다른 멤버도 받아올 수 있다.
		//그런 방법으로 IOType Enum을 끼어넣어서 받아와서 구분.
		if (pOverlapped->eIoType == IOType::RECV)
		{
			pIocpManager->PostRecv(dwIndex, dwTransferredBytes, &pMsg, &dwMsgBytes, &dwMsgNum, dwLockIndex);
		}
		else if (pOverlapped->eIoType == IOType::SEND)
		{
#ifdef __DEV_LOG__
			printf("Post Send transferredBytes %d\n", dwTransferredBytes);
#endif
			pIocpManager->PostSend(pConnection, dwTransferredBytes);
		}

	}

	char szLog[32];
	memset(szLog, 0, sizeof(szLog));
	sprintf_s(szLog, 32, "%d Tread END\n", dwCurrentThreadID);
	OutputDebugStringA(szLog);
	_endthreadex(0);

	return 0;
}

void CIocpManager::PacketProcess()
{
	//리드버퍼 다 처리했으면 탈출
	if (m_dwReadQueuePos >= m_dwReadQueueSize)
		goto SWAPCHECK;

#ifdef __DEV_LOG__
	printf("Read Queue Count = %d\n", m_dwReadQueueSize);
#endif

	for (DWORD i = 0; i < m_dwReadQueueSize; i++)
	{
#ifdef __DEV_LOG__
		printf("PacketProcess i = %d m_dwReadQueuePos = %d\n", i, m_dwReadQueuePos);
#endif
		PacketInfo* pPacketInfo = &(*m_pReadQueue)[m_dwReadQueuePos];
		DWORD dwIndex = pPacketInfo->dwIndex;
		CConnection* pConnection = GetConnection(dwIndex);

		if (pConnection->GetConnected() == false)
			continue;

		/*char szLog[32];
		memset(szLog, 0, sizeof(szLog));
		sprintf_s(szLog, "%d 패킷 처리\n", *(int*)(pPacketInfo->buff + 4));
		OutputDebugStringA(szLog);*/

		OnRecv(pPacketInfo->dwIndex, pPacketInfo->buff, pPacketInfo->dwLength);

		m_dwReadQueuePos++;
	}

SWAPCHECK:
	if (m_dwWriteQueueSize > 0)
	{
		SwapRecvQueue();
	}

}

void CIocpManager::SwapRecvQueue()
{
	//스왑 중에 라이트 큐에 넣지 못하도록 일괄 락.
	for (DWORD i = 0; i < m_dwLockNum; i++)
	{
		AcquireSRWLockExclusive(m_pBufferSwapLock + i);
	}

#ifdef __DEV_LOG__
	printf("Reset Event\n");
#endif 

	ResetEvent(m_QueueSwapWaitEvent);

#ifdef __DEV_LOG__
	printf("Prev ReadQueueSize = %d WriteQueueSize = %d \n", m_dwReadQueueSize, m_dwWriteQueueSize);
#endif

	//리드 큐 다 읽었을 때 스왑이 일어날 때 노드 마다 가지고 있는 버퍼를 비워줄 것인가? 
	//m_pReadQueue->clear();

	//스왑
	auto tempBuff = m_pWriteQueue;
	m_pWriteQueue = m_pReadQueue;
	m_pReadQueue = tempBuff;

	//라이트큐에 현재까지 쌓은 위치가 사이즈. 스왑 하면서 리드큐에 넣는다.
	m_dwReadQueueSize = m_dwWriteQueueSize;
	m_dwReadQueuePos = 0;
	InterlockedExchange(&m_dwWriteQueuePos, -1); //락으로 쌓여있는 상태니까 여긴 인터락 아니어도 될 거 같긴 한데.
	InterlockedExchange(&m_dwWriteQueueSize, 0);

#ifdef __DEV_LOG__
	printf("After ReadQueueSize = %d WriteQueueSize = %d \n", m_dwReadQueueSize, m_dwWriteQueueSize);
#endif

#ifdef __DEV_LOG__
	printf("Set Event\n");
#endif
	SetEvent(m_QueueSwapWaitEvent);

	for (DWORD i = 0; i < m_dwLockNum; i++)
	{
		ReleaseSRWLockExclusive(m_pBufferSwapLock + i);
	}

}

void CIocpManager::PushWriteQueue(DWORD dwIndex, char* pMsg, DWORD dwMsgNum, DWORD dwMsgBytes, DWORD dwLockIndex)
{
	//큐에 푸쉬하는 도중 큐 스왑이 일어나지 않기 위한 락
	AcquireSRWLockExclusive(m_pBufferSwapLock + dwLockIndex);

	int iTotalBytes = 0; //완성된 패킷의 헤더에 있는 사이즈를 더한 값

	for (DWORD i = 0; i < dwMsgNum; i++)
	{
		int iBytes = *(DWORD*)pMsg; //패킷 헤더에서 사이즈를 읽는다. 
		iTotalBytes += iBytes;
		DWORD uQueuePos = InterlockedIncrement(&m_dwWriteQueuePos);

		//패킷 하나가 가질 수 있는 사이즈 넘기는지 체크
		if (iBytes > PACKET_BUFF_MAX)
		{
			printf("%s %d", __FUNCTION__, __LINE__);
			__debugbreak();
		}

		//큐 사이즈가 넘치면 큐 스왑이 일어날 때 까지 대기한다.
		if (uQueuePos >= RECV_QUEUE_MAX)
		{
			printf("RecvQueue OVER. Wait Swap \n");
			//락이 걸려 있어 메인 스레드에서 스왑이 못일어나니까 일단 해제 후 대기
			ReleaseSRWLockExclusive(m_pBufferSwapLock + dwLockIndex);
			//스왑이 일어나기 까지 대기
			WaitForSingleObject(m_QueueSwapWaitEvent, INFINITE);
			//스왑 후 다시 락 획득. 획득 전에 다시 스왑 체크가 먼저 일어나도 라이트 큐 사이즈가 0으로 한 번 스왑돼서 더이상 스왑되지 않을 것.
			AcquireSRWLockExclusive(m_pBufferSwapLock + dwLockIndex);
			//스왑 후 큐 위치를 다시 구한다.
			uQueuePos = InterlockedIncrement(&m_dwWriteQueuePos);
		}

		PacketInfo* pPacketInfo = &(*m_pWriteQueue)[uQueuePos];
		pPacketInfo->dwIndex = dwIndex;
		pPacketInfo->dwLength = iBytes;
		memcpy(pPacketInfo->buff, pMsg, iBytes);

		pMsg += iBytes;
		InterlockedIncrement(&m_dwWriteQueueSize);
	}

	//패킷 헤더에서의 사이즈와 인자로 받은 사이즈가 다른지 체크
	if (iTotalBytes != dwMsgBytes)
	{
		printf("%s %d", __FUNCTION__, __LINE__);
		__debugbreak();
	}

	ReleaseSRWLockExclusive(m_pBufferSwapLock + dwLockIndex);
}

CConnection* CIocpManager::GetConnection(DWORD dwIndex)
{
	return m_ConnectionList[dwIndex];
}

CConnection* CIocpManager::GetConnectConnection(DWORD dwIndex)
{
	return m_ConnectConnectionList[dwIndex];
}

CConnection* CIocpManager::GetFreeConnection()
{
	for(DWORD i = 0; i < m_dwConnectionMax; i++)
	{
		if (m_ConnectionList[i]->GetConnected() == false)
			return m_ConnectionList[i];
	}

	return nullptr;
}

CConnection* CIocpManager::GetFreeConnectConnection()
{
	for (DWORD i = 0; i < m_dwConnectConnectionMax; i++)
	{
		if (m_ConnectConnectionList[i]->GetConnected() == false)
			return m_ConnectConnectionList[i];
	}

	return nullptr;
}

bool CIocpManager::CloseConnection(CConnection* pConnection)
{
	if(pConnection->GetConnected() == false)
		return false;

	if (pConnection->GetConnected() == false)
		return false;

	DWORD dwRecvRef = pConnection->GetRecvRefCount();
	DWORD dwSendRef = pConnection->GetSendRefCount();
	
	if (dwRecvRef != 0 || dwSendRef != 0)
	{
		printf("dwIndex = %d Remained IO. dwRecvRef = %d  dwSendRef = %d \n", pConnection, dwRecvRef, dwSendRef);
		return false;
	}
	
	OnClose(pConnection->m_dwConnectionIndex);
	pConnection->CloseSocket();

	printf("dwIndex = %d Connection Closed \n", pConnection->m_dwConnectionIndex);

	return true;
}

CConnection* CIocpManager::Connect(char* pAddress, u_short uPort)
{
	//도메인으로부터 ip주소 얻기
	ADDRINFO* pAddrInfo = nullptr;
	ADDRINFO stAddrInfo = { 0, };
	stAddrInfo.ai_family = AF_INET;
	stAddrInfo.ai_socktype = SOCK_STREAM;
	stAddrInfo.ai_protocol = IPPROTO_TCP;

	getaddrinfo(pAddress, NULL, &stAddrInfo, &pAddrInfo);

	if (pAddrInfo == nullptr)
	{
		std::cout << "domain convert address fail" << std::endl;
		return nullptr;
	}

	sockaddr_in* pSockAddr_in = (sockaddr_in*)pAddrInfo->ai_addr;
	char* pAddr = inet_ntoa(pSockAddr_in->sin_addr);

	SOCKADDR_IN sockAddr;
	ZeroMemory(&sockAddr, sizeof(sockAddr));
	sockAddr.sin_family = AF_INET;
	sockAddr.sin_addr.s_addr = inet_addr(pAddr);
	sockAddr.sin_port = htons(uPort);


	CConnection* pConnection = GetFreeConnectConnection();

	if(pConnection == nullptr)
	{
		printf("No Free Connect Connection Available\n");
		return nullptr;
	}

	if (connect(pConnection->m_socket, (SOCKADDR*)&sockAddr, sizeof(sockAddr)) == SOCKET_ERROR)
	{
		 WSAGetLastError();
		 return nullptr;
	}

	pConnection->SetRemoteIP(pAddr, IP_BUFF_SIZE);
	pConnection->SetRemotePort(uPort);
	ConnectIOCompletionPort(pConnection);
	PostConnect(pConnection->m_dwConnectionIndex);
	
	return pConnection;
}

bool CIocpManager::Send(DWORD dwIndex, char* pMsg, DWORD dwBytes)
{
	DWORD dwCurrentThreadId = GetCurrentThreadId();
	CConnection* pConnection = GetConnection(dwIndex);
	
	if(pConnection->Send(pMsg, dwBytes) == false)
		return false;

	return true;
}

void CIocpManager::PostAccept(DWORD dwIndex)
{
	CConnection* pConnection = GetConnection(dwIndex);
	SOCKET socket = pConnection->GetSocket();
	pConnection->SetConnected(true);

	printf("Socket = %d Accepted \n", socket);

	CIocpManager::OnAccept(dwIndex);
	pConnection->PrepareRecv();
}

void CIocpManager::PostConnect(DWORD dwIndex)
{
	CConnection* pConnection = GetConnectConnection(dwIndex);
	SOCKET socket = pConnection->GetSocket();

	pConnection->SetConnectContextOpt();

	printf("Socket = %d Connected \n", socket);

	pConnection->SetConnected(true);
	OnConnect(dwIndex);

	pConnection->PrepareRecv();
}

void CIocpManager::PostRecv(DWORD dwIndex, DWORD dwRecvBytes, char** ppMsg, DWORD* pdwMsgBytes, DWORD* pdwMsgNum, DWORD dwLockIndex)
{
	CConnection* pConnection = GetConnection(dwIndex);

	pConnection->DecreaseRecvRef(); //recv IO 카운트 감소
	pConnection->RecvProcess(dwRecvBytes, ppMsg, pdwMsgBytes, pdwMsgNum); //받은 데이터로 패킷 만들기
	pConnection->CheckReset(); //링버퍼 리셋 체크
	PushWriteQueue(dwIndex, *ppMsg, *pdwMsgNum, *pdwMsgBytes, dwLockIndex); //라이트 큐에 삽입			
	pConnection->PrepareRecv(); //RECV IO 다시 걸기
}

void CIocpManager::PostSend(CConnection* pConnection, DWORD dwBytes)
{
	SRSendRingBuffer* pSendBuf = nullptr;
	pSendBuf = pConnection->GetSendRingBuff();

	pSendBuf->Lock();
	pConnection->DecreaseSendRef();
	InterlockedExchange(&pConnection->m_dwSendWait, FALSE);

	pSendBuf->PostSend(dwBytes);

	DWORD dwUsageBytes = pSendBuf->GetUsageBytes();

	if (dwUsageBytes > 0)
	{
		pConnection->SendBuff();
		InterlockedExchange(&pConnection->m_dwSendWait, TRUE);
	}

	pSendBuf->UnLock();
}

void CIocpManager::StopWorkerThread()
{
	m_bWorkerThreadLive = false;
	for (DWORD i = 0; i < m_dwLockNum; i++)
	{
		if (PostQueuedCompletionStatus(m_hCompletionPort, 0, (ULONG_PTR)0, 0) == 0)
		{
			printf("PostQueuedCompletionStatus Fail \n");
		};
	}
}

UINT CIocpManager::GetThreadLockNum()
{
	DWORD dwCurrentThreadID = GetCurrentThreadId();

	//스레드 아이디를 비교해서 스레드 순서에 따라 락 인덱스를 얻는다.
	for (DWORD i = 0; i < m_dwLockNum; i++)
	{
		if (m_pThreadIdArr[i] == dwCurrentThreadID)
		{
			return i;
		}
	}

	return -1;
}

