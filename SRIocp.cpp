#include "SRIocp.h"
#include "CConnection.h"

//전역 변수 초기화
AcceptFunc CIocp::g_pOnAcceptFunc = NULL;
ConnectFunc CIocp::g_pOnConnectFunc = NULL;
CloseFunc CIocp::g_pOnCloseFunc = NULL;
RecvFunc CIocp::g_pOnRecvFunc = NULL;


CIocp::CIocp()
{
	m_ListenSocket = INVALID_SOCKET;
	m_CompletionPort = NULL;
	m_bWorkerThreadLive = false;

	m_eCSType = ECSType::NONE;
	m_nBindPort = 0;
	m_nRemotePort = 0;

	m_dwConnectionMax = 0;
	m_dwConnectionSize = 0;

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

	lpfnAcceptEx = nullptr;
	lpfnConnectEx = nullptr;
	lpfnDisconnectEx = nullptr;
}


CIocp::~CIocp()
{
	if (m_pBufferSwapLock != nullptr)
		delete[] m_pBufferSwapLock;
	if (m_pThreadIdArr != nullptr)
		delete[] m_pThreadIdArr;

}

bool CIocp::InitConnectionList(DWORD dwCount)
{
	m_ServerConnectionList.resize(dwCount);

	for (DWORD i = 0; i < dwCount; i++)
	{
		CConnection* pConnection = new CConnection;
		SOCKET socket = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_IP, NULL, 0, WSA_FLAG_OVERLAPPED);
		pConnection->Initialize(i, socket, RECV_RING_BUFFER_MAX, SEND_RING_BUFFER_MAX);
		pConnection->SetNetwork(this);
		m_ServerConnectionList[i] = pConnection;
	}

	m_dwConnectionMax = dwCount;

	return false;
}

bool CIocp::GetIoExFuncPointer()
{
	//AcceptEx 함수 쓸 수 있도록 등록
	lpfnAcceptEx = NULL;
	GUID guidAcceptEx = WSAID_ACCEPTEX;
	DWORD dwBytes = 0;

	if (WSAIoctl(m_ListenSocket,
		SIO_GET_EXTENSION_FUNCTION_POINTER,
		&guidAcceptEx,
		sizeof(GUID),
		&lpfnAcceptEx,
		sizeof(LPFN_ACCEPTEX),
		&dwBytes,
		NULL,
		NULL) == SOCKET_ERROR)
	{
		printf("AcceptEx WsaIoctl Error. WSAGetLastError = %d \n", WSAGetLastError());
		return false;
	}

	//DisconnectEx 함수 쓸 수 있도록 등록
	lpfnDisconnectEx = NULL;
	GUID guidDiconnectEx = WSAID_DISCONNECTEX;
	dwBytes = 0;

	if (WSAIoctl(m_ListenSocket,
		SIO_GET_EXTENSION_FUNCTION_POINTER,
		&guidDiconnectEx,
		sizeof(GUID),
		&lpfnDisconnectEx,
		sizeof(LPFN_DISCONNECTEX),
		&dwBytes,
		NULL,
		NULL) == SOCKET_ERROR)
	{
		printf("DisConnectEx WsaIoctl Error. WSAGetLastError = %d \n", WSAGetLastError());
		return false;
	}

	//ConnectEx 함수 쓸 수 있도록 등록
	lpfnConnectEx = NULL;
	GUID guidConnectEx = WSAID_CONNECTEX;
	dwBytes = 0;

	if (WSAIoctl(m_ListenSocket,
		SIO_GET_EXTENSION_FUNCTION_POINTER,
		&guidConnectEx,
		sizeof(GUID),
		&lpfnConnectEx,
		sizeof(LPFN_CONNECTEX),
		&dwBytes,
		NULL,
		NULL) == SOCKET_ERROR)
	{
		printf("ConnectEx WsaIoctl Error. WSAGetLastError = %d \n", WSAGetLastError());
		return false;
	}

	return true;
}

bool CIocp::InitNetwork(ECSType csType, UINT port)
{
	int retVal;
	WSADATA wsaData;

	m_eCSType = csType;

	//큐 준비
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

	//이벤트 초기화(수동 리셋)
	m_QueueSwapWaitEvent = CreateEvent(NULL, TRUE, FALSE, NULL);

	//윈속 초기화
	if ((retVal = WSAStartup(MAKEWORD(2, 2), &wsaData)) != 0)
	{
		printf("WSAStartup Fail\n");
		return false;
	}

	//iocp객체 생성
	//CreateIoCompletionPort마지막 인자가 0이면 cpu 코어 개수만큼 스레드 이용
	if ((m_CompletionPort = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 0)) == NULL)
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
	if ((m_ListenSocket = WSASocket(AF_INET,
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

	if (GetIoExFuncPointer() == false)
	{
		printf("GetIoExFuncPointer Fail\n");
		return false;
	}


	//아래로는 서버 초기화
	if (csType == ECSType::CLIENT)
		return true;

	//listen 소켓 iocp 연결
	if (CreateIoCompletionPort((HANDLE)m_ListenSocket,
		m_CompletionPort,
		(ULONG_PTR)this,
		0) == NULL)
	{
		printf("CreateIoCompletionPort Fail\n");
		return false;
	}

	//bind
	SOCKADDR_IN serverAddr;
	ZeroMemory(&serverAddr, sizeof(serverAddr));
	serverAddr.sin_family = AF_INET;
	serverAddr.sin_addr.s_addr = htonl(INADDR_ANY);
	serverAddr.sin_port = htons(port);

	//TCP홀펀칭 이미 사용중인 포트에 다른 소켓 강제 바인딩 
	//SetReuseSocketOption(m_ListenSocket);

	if (bind(m_ListenSocket,
		(PSOCKADDR)&serverAddr,
		sizeof(serverAddr)) == SOCKET_ERROR)
	{
		printf("Bind Fail. WSAGetLastError = %d\n", WSAGetLastError());
		return false;
	}

	//listen
	if (listen(m_ListenSocket, 5) == SOCKET_ERROR)
	{
		printf("Listen Fail\n");
		return false;
	}

	//포트를 0으로 바인드 했을 경우 할당해준 포트를 알아낸다. 
	SOCKADDR_IN sin;
	socklen_t len = sizeof(sin);
	if (getsockname(m_ListenSocket, (SOCKADDR*)&sin, &len) != -1)
	{
		m_nBindPort = ntohs(sin.sin_port);
	}

	printf("Listen Start: m_ListenSocket = %d Port = %d \n", m_ListenSocket, m_nBindPort);
	return true;

}

void CIocp::InitSocketOption(SOCKET socket)
{
	SetLingerSocketOption(socket);
	SetNagleOffSocketOption(socket);
}

void CIocp::SetReuseSocketOption(SOCKET socket)
{
	//socket Reuse Option. SO_REUSEADDR은 서버 소켓에서만 time_wait를 재활용 할 수 있는 것 같다.
	int option = 1;
	if (setsockopt(socket, SOL_SOCKET, SO_REUSEADDR, (char*)&option, sizeof(int)) == SOCKET_ERROR)
	{
		std::cout << "Set Socket option ReuseAddr error :" << WSAGetLastError() << std::endl;
	};
}

void CIocp::SetLingerSocketOption(SOCKET socket)
{
	//onoff 0 - default 소켓버퍼에 남은 데이터를 전부 보내고 종료하는 정상종료
	//onoff 1 linger 0 - close 즉시 리턴하고 소켓버퍼에 남은 데이터를 버리는 비정상종료.
	//onoff 1 linger 1 - 지정시간동안 대기한 뒤 소켓버퍼에 남은 데이터를 전부 보내보고 다 보내면 정상종료 하며 리턴 못 보내면 비정상종료 에러와 함께 리턴.
	LINGER linger = { 0,0 };
	linger.l_onoff = 1;
	linger.l_linger = 0;

	setsockopt(socket, SOL_SOCKET, SO_LINGER, (CHAR*)&linger, sizeof(linger));
}

void CIocp::SetNagleOffSocketOption(SOCKET socket)
{
	int nagleOpt = 1; //1 비활성화 0 활성화
	setsockopt(socket, IPPROTO_TCP, TCP_NODELAY, (const char*)&nagleOpt, sizeof(nagleOpt));
}

bool CIocp::CreateWorkerThread()
{
	HANDLE threadHandle; //워커스레드 핸들

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
		if ((threadHandle = (HANDLE)_beginthreadex(NULL,
			0,
			&WorkerThread,
			this,
			0,
			(unsigned int*)&m_pThreadIdArr[i])) == NULL)
		{
			printf("CreateWorkerThread Fail\n");
			return false;
		}

		CloseHandle(threadHandle);
	}

	printf("WorkerThread Num = %d \n", m_dwLockNum);

	return true;
}

//Overlapped IO 작업 완료 통보를 받아 처리하는 워커 스레드
unsigned __stdcall CIocp::WorkerThread(LPVOID CompletionPortObj)
{
	CIocp* arg = (CIocp*)CompletionPortObj;
	HANDLE completionport = arg->m_CompletionPort;
	ULONG_PTR key = NULL;
	LPOVERLAPPED lpOverlapped = NULL;
	DWORD dwTransferredBytes = 0;

	DWORD dwLockIndex = 0;
	DWORD dwCurrentThreadID = GetCurrentThreadId();

	char* pMsg = nullptr;
	DWORD dwMsgBytes = 0;
	DWORD dwMsgNum = 0;

	//스레드 아이디를 비교해서 스레드가 가질 락 인덱스를 가진다.
	for (DWORD i = 0; i < arg->m_dwLockNum; i++)
	{
		if (arg->m_pThreadIdArr[i] == dwCurrentThreadID)
			dwLockIndex = i;
	}

	while (arg->m_bWorkerThreadLive)
	{
		BOOL bRet = GetQueuedCompletionStatus(completionport, //CompletionPort 핸들
			&dwTransferredBytes,				//비동기 입출력 작업으로 전송된 바이트
			(PULONG_PTR)&key,			//CreateIoCompletionPort함수 호출시 전달한 세번째 인자가 여기 저장
			&lpOverlapped,			//비동기 입출력 함수 호출 시 전달한 오버랩 구조체 주소값.
			INFINITE);

		if (lpOverlapped == nullptr)
		{
			printf("lpOverlapped = nullptr \n");
			continue;
		}

		/*if (key == NULL)
		{
			continue;
		}*/

		CIocp* pIocp = (CIocp*)key;
		OverlappedEX* pOverlapped = (OverlappedEX*)lpOverlapped;
		DWORD dwIndex = pOverlapped->dwIndex;
		CConnection* pConnection = arg->GetConnection(dwIndex);
		SOCKET socket = pConnection->GetSocket();

#ifdef __DEV_LOG__
		printf("GQCS CurrentThreadID = %d dwIndex = %d transferredBytes = %d eIoType = %d\n", dwCurrentThreadID, dwIndex, dwTransferredBytes, (int)pOverlapped->eIoType);
#endif 

		if (bRet == FALSE)
		{
			//걸려있던 IO 타입이 있으면 레퍼런스 카운트를 감소시켜 준다.
			switch (pOverlapped->eIoType)
			{
			case IOType::ACCEPT:
				pConnection->DecreaseAcceptRef();
				break;
			case IOType::CONNECT:
				pConnection->DecreaseConnectRef();
				break;
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
			arg->CloseConnection(dwIndex);
			continue;
		}

		//GetQueuedCompletionStatus 해서 가져오는데 성공했는데 전달받은 패킷이 0이면 접속이 끊긴 것으로 판단.
		if (dwTransferredBytes == 0
			&& pOverlapped->eIoType != IOType::ACCEPT
			&& pOverlapped->eIoType != IOType::CONNECT
			&& pOverlapped->eIoType != IOType::DISCONNECT)
		{
			if (pOverlapped->eIoType == IOType::RECV)
				pConnection->DecreaseRecvRef();
			else if (pOverlapped->eIoType == IOType::SEND)
				pConnection->DecreaseSendRef();

			printf("GQCS TRUE dwTransferredBytes = 0 \n");
			arg->CloseConnection(dwIndex);
			continue;
		}


		if (pOverlapped->eIoType == IOType::DISCONNECT)
		{
			//서버는 ReAccecptEx하면서 클라이언트는 소켓을 다시 할당하면서 InitConnectPool에서 isConnected를 false처리하기 때문에 여기서 하지 않는다.
			//클라는 isConnected인 소켓이 없으면 다시 소켓을 커넥션 수 만큼 만들기 때문에 판단하기 위해서 false로 만들지 않는다.
			printf("IOType is Disconnect. Socket = %d \n", socket);
			OnClose(dwIndex);

			if (arg->m_eCSType == ECSType::SERVER)
				arg->ReuseSocket(dwIndex);

			continue;
		}

		//비동기 입출력에서 오버랩구조체를 인자로 전달할 때 오버랩구조체를 멤버로 가진 구조체를 오버랩으로 캐스팅해서 보내고
		//GetQueuedCompletionStatus에서 받은 오버랩 구조체를 다시 원래 구조체로 캐스팅하면 다른 멤버도 받아올 수 있다.
		//그런 방법으로 IOType Enum을 끼어넣어서 받아와서 구분짓는다.
		if (pOverlapped->eIoType == IOType::ACCEPT)
		{
			arg->PostAccept(dwIndex);
		}
		else if (pOverlapped->eIoType == IOType::CONNECT)
		{
			arg->PostConnect(dwIndex);
		}
		else if (pOverlapped->eIoType == IOType::RECV)
		{
			arg->PostRecv(dwIndex, dwTransferredBytes, &pMsg, &dwMsgBytes, &dwMsgNum, dwLockIndex);
		}
		else if (pOverlapped->eIoType == IOType::SEND)
		{
#ifdef __DEV_LOG__
			printf("Post Send transferredBytes %d\n", dwTransferredBytes);
#endif
			arg->PostSend(dwIndex, dwTransferredBytes);
		}

	}

	char szLog[32];
	memset(szLog, 0, sizeof(szLog));
	sprintf_s(szLog, 32, "%d Tread END\n", dwCurrentThreadID);
	OutputDebugStringA(szLog);
	_endthreadex(0);

	return 0;
}

void CIocp::PacketProcess()
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

		if (pConnection->GetConnectionStaus() == false)
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

void CIocp::SwapRecvQueue()
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

void CIocp::PushWriteQueue(DWORD dwIndex, char* pMsg, DWORD dwMsgNum, DWORD dwMsgBytes, DWORD dwLockIndex)
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

bool CIocp::InitAcceptPool(DWORD dwNum)
{
	DWORD dwFlags;
	DWORD dwBytes;

	for (DWORD i = 0; i < dwNum; i++)
	{
		//오버랩IO를 위해 구조체 세팅
		CConnection* pConnection = GetConnection(i);
		OverlappedEX* pOverlapped = pConnection->GetRecvOverlapped();
		SOCKET socket = pConnection->GetSocket();

		if (pOverlapped == nullptr)
			return false;

		ZeroMemory(&pOverlapped->overlapped, sizeof(OVERLAPPED));

		pOverlapped->wsabuff.len = 0;
		pOverlapped->wsabuff.buf = pConnection->GetAddrBuff();
		pOverlapped->eIoType = IOType::ACCEPT;
		dwFlags = 0;
		dwBytes = 0;

		pConnection->SetConnectionStatus(false);

		InitSocketOption(socket);

		pConnection->IncreaseAcceptRef();

		if (AcceptEx(m_ListenSocket,
			socket,
			pOverlapped->wsabuff.buf,
			0, //Accept 하면서 데이터를 바로 수신하지 않고 연결만 수락하기 위해 0바이트 설정.
			sizeof(SOCKADDR_IN) + 16,
			sizeof(SOCKADDR_IN) + 16,
			&dwBytes,
			(LPOVERLAPPED)pOverlapped) == FALSE)
		{
			if (WSAGetLastError() != ERROR_IO_PENDING)
			{
				pConnection->DecreaseAcceptRef();
				printf("AcceptEx Fail. WSAGetLastError = %d \n", WSAGetLastError());
				return false;
			}

		}

		//소켓과 iocp 연결
		if ((CreateIoCompletionPort((HANDLE)socket,
			m_CompletionPort,
			(ULONG_PTR)this,
			0)) == NULL)
		{
			printf("CreateIoCompletionPort bind error \n");
			return false;
		}

	}

	return true;
}

/*
bool CIocp::InitConnectPool(UINT num)
{
	//bind
	SOCKADDR_IN addr;
	ZeroMemory(&addr, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = htonl(INADDR_ANY);

	DWORD dwFlags;
	DWORD dwBytes;

	for (int i = 0; i < num; i++)
	{
		//오버랩IO를 위해 구조체 세팅
		IODATA* pioData = new IODATA;

		if (pioData == NULL)
			return false;

		ZeroMemory(pioData, sizeof(IODATA));
		pioData->socket = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_IP,
			NULL, 0, WSA_FLAG_OVERLAPPED);
		pioData->WSABuff.len = 0;
		pioData->WSABuff.buf = NULL;
		pioData->ioType = IOType::CONNECT;
		dwFlags = 0;
		dwBytes = 0;

		CIocp* pConnection = m_ServerConnectionList[i];
		pConnection->m_socket = pioData->socket;
		pConnection->m_pIoData = pioData;
		pConnection->m_isConnected = false;
		pConnection->m_pMainConnection = this;

		pioData->dwIndex = pConnection->m_uConnectionIndex;

		InitSocketOption(pConnection->m_socket);

		//TCP홀펀칭 이미 사용중인 포트에 다른 소켓 강제 바인딩
		//SetReuseSocketOption(pConnection->m_socket);

		//ConnectEx용 bind
		if (bind(pConnection->m_socket, (PSOCKADDR)&addr,
			sizeof(addr)) == SOCKET_ERROR)
		{
			printf("ConnectEx bind Fail \n");
			return false;
		}

		//소켓과 iocp 연결
		if ((CreateIoCompletionPort((HANDLE)pioData->socket,
			m_CompletionPort,
			(ULONG_PTR)pConnection,
			0)) == NULL)
		{
			printf("CreateIoCompletionPort Bind Error \n");
			return false;
		}

	}
	return true;
}
*/

bool CIocp::ReuseSocket(DWORD dwIndex)
{
	//DWORD flags;
	DWORD dwBytes = 0;

	CConnection* pConnection = GetConnection(dwIndex);
	OverlappedEX* pOverlapped = pConnection->GetRecvOverlapped();
	SOCKET socket = pConnection->GetSocket();

	if (pOverlapped == nullptr)
		return false;

	ZeroMemory(&pOverlapped->overlapped, sizeof(OVERLAPPED));

	pOverlapped->wsabuff.len = 0;
	pOverlapped->wsabuff.buf = pConnection->GetAddrBuff();
	pOverlapped->eIoType = IOType::ACCEPT;

	pConnection->SetConnectionStatus(false);

	pConnection->IncreaseAcceptRef();

	if (AcceptEx(m_ListenSocket,
		socket,
		pOverlapped->wsabuff.buf,
		0,
		sizeof(SOCKADDR_IN) + 16,
		sizeof(SOCKADDR_IN) + 16,
		&dwBytes,
		(LPOVERLAPPED)pOverlapped) == FALSE)
	{
		if (WSAGetLastError() != ERROR_IO_PENDING)
		{
			printf("ReAcceptEx Fail. WSAGetLastError = %d \n", WSAGetLastError());
			return false;
		}
	}

	printf("dwIndex = %d socket = %d ReAccept \n", dwIndex, socket);
	return true;
}

bool CIocp::CloseConnection(DWORD dwIndex)
{
	//TF_REUSE_SOCKET 하여 소켓 재활용 한다.
	//비정상 적인 상황이나 종료 시에 CConnection이 가진 CloseSocket 으로 진짜 소켓 해제.	
	CConnection* pConnection = GetConnection(dwIndex);
	OverlappedEX* pOverlapped = pConnection->GetRecvOverlapped();
	SOCKET socket = pConnection->GetSocket();

	if (pOverlapped == nullptr)
		return false;

	DWORD dwAcceptRef = pConnection->GetAcceptRefCount();
	DWORD dwRecvRef = pConnection->GetRecvRefCount();
	DWORD dwSendRef = pConnection->GetSendRefCount();

	if (dwAcceptRef != 0  || dwRecvRef != 0 || dwSendRef != 0)
	{
		printf("dwIndex = %d Remain IO. dwAcceptRef = %d dwRecvRef = %d  dwSendRef = %d \n", dwIndex, dwAcceptRef, dwRecvRef, dwSendRef);
		return false;
	}

	ZeroMemory(&pOverlapped->overlapped, sizeof(OVERLAPPED));

	pOverlapped->wsabuff.len = 0;
	pOverlapped->wsabuff.buf = pConnection->GetRecvRingBuff()->GetReadPtr();
	pOverlapped->eIoType = IOType::DISCONNECT;
	pConnection->SetConnectionStatus(false);

	//TF_DISCONNECT넣으면 10022 WSAEINVAL 오류 바인딩 실패. 
	//lpfnDisconnectEx(socket, NULL, TF_DISCONNECT | TF_REUSE_SOCKET, NULL);

	if (lpfnDisconnectEx(socket,
		(LPOVERLAPPED)pOverlapped,
		TF_REUSE_SOCKET,
		NULL) == FALSE)
	{
		if (WSAGetLastError() != ERROR_IO_PENDING)
		{
			printf("Socket = %d DissconnectEx Error. WSAGetLastError = %d \n", socket, WSAGetLastError());
			return false;
		}
	};

	printf("socket = %d DisconnectEX \n", socket);

	//TransmitFile(socket, NULL, 0, 0, (LPOVERLAPPED)pClient->m_ioData, NULL, TF_DISCONNECT | TF_REUSE_SOCKET);
	//shutdown(socket, SD_BOTH);
	//closesocket(socket);

	return true;
}

SOCKET CIocp::Connect(char* pAddress, u_short port)
{
	//gethostbyname이 deprecated. getaddrinfo를 대신 써서 domain으로 부터 ip를 얻는다. 
	/*
	char host[20];
	gethostname(host, 20);
	hostent* hent = gethostbyname(host);
	in_addr addr;
	addr.s_addr = *(ULONG*)*hent->h_addr_list;
	char* address = inet_ntoa(addr);
	*/

	ADDRINFO* pAddrInfo = nullptr;
	ADDRINFO stAddrInfo = { 0, };
	stAddrInfo.ai_family = AF_INET;
	stAddrInfo.ai_socktype = SOCK_STREAM;
	stAddrInfo.ai_protocol = IPPROTO_TCP;

	getaddrinfo(pAddress, NULL, &stAddrInfo, &pAddrInfo);

	if (pAddrInfo == nullptr)
	{
		std::cout << "domain convert address fail" << std::endl;
		return INVALID_SOCKET;
	}

	sockaddr_in* pSockAddr_in = (sockaddr_in*)pAddrInfo->ai_addr;
	char* pAddr = inet_ntoa(pSockAddr_in->sin_addr);

	SOCKADDR_IN sockAddr;
	ZeroMemory(&sockAddr, sizeof(sockAddr));
	sockAddr.sin_family = AF_INET;
	//inet_addr이 deprecated되어 inet_pton(AF_INET, char_str, &(sockAddr.sin_addr.s_addr));
	sockAddr.sin_addr.s_addr = inet_addr(pAddr);
	sockAddr.sin_port = htons(port);

	CConnection* pConnection = GetFreeConnection();

	if (pConnection == nullptr)
		return INVALID_SOCKET;

	OverlappedEX* pOverlapped = pConnection->GetRecvOverlapped();
	SOCKET socket = pConnection->GetSocket();
	/*
	if (pConnection == NULL)
	{
		//다시 커넥트 소켓풀 초기화
		if (!InitConnectPool(m_dwConnectionMax))
		{
			std::cout << "re_InitConnectPool fail" << std::endl;
		};
		OutputDebugString(L"커넥션풀 초기화");
		pConnection = GetFreeConnection();
	}
	*/

	//어차피 해당 타입이 다른 프로세스로 넘어가진 않는다. 
	//IOType::CONNECT으로 해서 커넥트 시에 발생하는 0바이트 패킷을 연결이 끊어주는 패킷과 구분해주도록 한다.
	pOverlapped->eIoType = IOType::CONNECT;

	pConnection->IncreaseConnectRef();

	if (lpfnConnectEx(socket,
		(SOCKADDR*)&sockAddr,
		sizeof(sockAddr),
		NULL,
		0,
		NULL,
		(LPOVERLAPPED)pOverlapped) == FALSE)
	{
		if (WSAGetLastError() != ERROR_IO_PENDING)
		{
			pConnection->DecreaseConnectRef();
			printf("ConnectEx Fail. WSAGetLastError = %d \n", WSAGetLastError());
			return NULL;
		}
	};

	return socket;
}

CConnection* CIocp::GetConnection(DWORD dwIndex)
{
	return m_ServerConnectionList[dwIndex];
}

CConnection* CIocp::GetFreeConnection()
{
	for (DWORD i = 0; i < m_dwConnectionMax; i++)
	{
		if (m_ServerConnectionList[i]->GetConnectionStaus() == false)
			return m_ServerConnectionList[i];
	}

	return nullptr;
}

bool CIocp::Send(DWORD dwIndex, char* pMsg, DWORD dwBytes)
{
	DWORD dwCurrentThreadId = GetCurrentThreadId();

	CConnection* pConnection = GetConnection(dwIndex);
	bool ret = pConnection->Send(pMsg, dwBytes);

	return ret;
}

void CIocp::PostAccept(DWORD dwIndex)
{
	//https://learn.microsoft.com/ko-kr/windows/win32/api/mswsock/nf-mswsock-acceptex
	//Windows XP 이상에서는 AcceptEx 함수가 완료되고 허용된 소켓에 SO_UPDATE_ACCEPT_CONTEXT 옵션이 설정되면
	//getsockname 함수를 사용하여 수락된 소켓과 연결된 로컬 주소를 검색할 수도 있습니다.
	//마찬가지로 허용된 소켓과 연결된 원격 주소는 getpeername 함수를 사용하여 검색할 수 있습니다.
	/*
	SetAcceptContextOpt();
	*/

	CConnection* pConnection = GetConnection(dwIndex);
	SOCKET socket = pConnection->GetSocket();

	pConnection->DecreaseAcceptRef();

	SOCKADDR_IN* sockAddr = NULL;
	int addrlen = sizeof(SOCKADDR);
	SOCKADDR_IN* remoteAddr = NULL;
	int remoteaddrlen = sizeof(SOCKADDR_IN);
	GetAcceptExSockaddrs(pConnection->GetAddrBuff(), //커넥션의 m_AddrBuf. AcceptEx의 lpOutputBuffer와 동일한 매개 변수
		0,
		sizeof(SOCKADDR_IN) + 16,
		sizeof(SOCKADDR_IN) + 16,
		(SOCKADDR**)&sockAddr,
		&addrlen,
		(SOCKADDR**)&remoteAddr,
		&remoteaddrlen);

	char* szRemoteAddr = inet_ntoa(remoteAddr->sin_addr);
	DWORD dwRemotePort = ntohs(remoteAddr->sin_port);

	static int iAcceptCnt = 0;
	iAcceptCnt++;
	printf("Accept Cnt = %d\n", iAcceptCnt);
	printf("Accept Socket = %d ip = %s port = %d \n", socket, inet_ntoa(remoteAddr->sin_addr), ntohs(remoteAddr->sin_port));

	CIocp::OnAccept(dwIndex);

	pConnection->SetConnectionStatus(true);
	pConnection->SetRemoteIP(szRemoteAddr, ADDR_BUFF_SIZE);
	pConnection->SetRemotePort(dwRemotePort);

	pConnection->PrepareRecv();
}

void CIocp::PostConnect(DWORD dwIndex)
{
	CConnection* pConnection = GetConnection(dwIndex);
	SOCKET socket = pConnection->GetSocket();

	pConnection->DecreaseConnectRef();

	pConnection->SetConnectContextOpt();

	printf("Socket = %d Connected \n", socket);

	pConnection->SetConnectionStatus(true);
	OnConnect(dwIndex);

	pConnection->PrepareRecv();
}

void CIocp::PostRecv(DWORD dwIndex, DWORD dwRecvBytes, char** ppMsg, DWORD* pdwMsgBytes, DWORD* pdwMsgNum, DWORD dwLockIndex)
{
	CConnection* pConnection = GetConnection(dwIndex);

	pConnection->DecreaseRecvRef(); //recv IO 카운트 감소
	pConnection->RecvProcess(dwRecvBytes, ppMsg, pdwMsgBytes, pdwMsgNum); //받은 데이터로 패킷 만들기
	pConnection->CheckReset(); //링버퍼 리셋 체크
	PushWriteQueue(dwIndex, *ppMsg, *pdwMsgNum, *pdwMsgBytes, dwLockIndex); //라이트 큐에 삽입			
	pConnection->PrepareRecv(); //RECV IO 다시 걸기
}

void CIocp::PostSend(DWORD dwIndex, DWORD dwBytes)
{
	CConnection* pConnection = GetConnection(dwIndex);

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

void CIocp::StopWorkerThread()
{
	m_bWorkerThreadLive = false;
	for (DWORD i = 0; i < m_dwLockNum; i++)
	{
		if (PostQueuedCompletionStatus(m_CompletionPort, 0, (ULONG_PTR)0, 0) == 0)
		{
			printf("PostQueuedCompletionStatus Fail \n");
		};
	}
}

UINT CIocp::GetThreadLockNum()
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

