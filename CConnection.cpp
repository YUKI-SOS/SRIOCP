#include "CConnection.h"
#include "SRIocp.h"

CIocpManager* CConnection::m_pIOCPManager = nullptr;

CConnection::CConnection()
{	
	m_dwConnectionIndex = -1;
	m_socket = INVALID_SOCKET;
	m_eConnectionState = EConnectionState::FREE;

	memset(m_szIP, 0 , sizeof(m_szIP));
	memset(m_AddrBuf, 0, sizeof(m_AddrBuf));
	m_dwRemotePort = -1;

	m_pRecvOverlapped = nullptr;
	m_pSendOverlapped = nullptr;

	m_pRecvBuff = nullptr;
	m_pSendBuff = nullptr;

	m_dwSendWait = 0;
}

CConnection::~CConnection()
{
	m_dwConnectionIndex = -1;
	m_socket = INVALID_SOCKET;
	m_eConnectionState = EConnectionState::FREE;

	memset(m_szIP, 0, sizeof(m_szIP));
	memset(m_AddrBuf, 0, sizeof(m_AddrBuf));
	m_dwRemotePort = -1;

	delete m_pRecvOverlapped;
	m_pRecvOverlapped = nullptr;
	delete m_pSendOverlapped;
	m_pSendOverlapped = nullptr;

	delete m_pRecvBuff;
	m_pRecvBuff = nullptr;
	delete m_pSendBuff;
	m_pSendBuff = nullptr;

	m_dwSendWait = 0;

}

bool CConnection::Initialize(DWORD dwConnectionIndex, SOCKET socket, DWORD dwRecvRingBuffSize, DWORD dwSendRingBuffSize)
{
	m_dwConnectionIndex = dwConnectionIndex;
	m_socket = socket;

	m_pRecvBuff = new SRRecvRingBuffer();
	m_pRecvBuff->Initialize(dwRecvRingBuffSize);
	
	m_pSendBuff = new SRSendRingBuffer();
	m_pSendBuff->Initialize(dwSendRingBuffSize);

	m_pRecvOverlapped = new OverlappedEX;
	ZeroMemory(m_pRecvOverlapped, sizeof(OverlappedEX));
	m_pRecvOverlapped->dwIndex = dwConnectionIndex;

	m_pSendOverlapped = new OverlappedEX;
	ZeroMemory(m_pSendOverlapped, sizeof(OverlappedEX));
	m_pSendOverlapped->dwIndex = dwConnectionIndex;

	InitializeSRWLock(&m_kIOLock);

	m_dwRecvRefCount = 0;
	m_dwSendRefCount = 0;

	m_dwSendWait = 0;

	return true;
}

void CConnection::Recycle()
{
	//closesocket()하지 않고 disconnectEX 소켓을 재활용 한다. 재활용 할 때 링버퍼 등을 초기화 해준다.
	m_pRecvBuff->Recycle();
	m_pSendBuff->Recycle();
}

int CConnection::SetAcceptContextOpt()
{
	int iRet = setsockopt(m_socket, SOL_SOCKET, SO_UPDATE_ACCEPT_CONTEXT, (char*)&(m_pIOCPManager->m_hListenSocket), sizeof(SOCKET));
	
	if(iRet == SOCKET_ERROR)
	{
		printf("AcceptEX SocketOption Fail WSAGetLastError = %d\n", WSAGetLastError());
	};

	return iRet;
}

int CConnection::SetConnectContextOpt()
{
	int iResult = setsockopt(m_socket, SOL_SOCKET, SO_UPDATE_CONNECT_CONTEXT, NULL, 0);

	if (iResult != 0)
	{
		printf("ConnectEX SocketOption Fail WSAGetLastError = %d\n", WSAGetLastError());
		return iResult;
	};

	return 0;
}

bool CConnection::CloseSocket()
{		
	//소켓을 closesocket해도 이미 걸려있는 IO가 사라지는 것은 아님.
	//closesocket하면 걸려있던 IO에 대해서 GQCS에서 통보한다. 
	// 미리 closesocket을 호출해서 걸려있는 IO가 통보되도록 하고 모든 IO가 완료되었다면 커넥션을 초기화하고 상태를 FREE로 하여 재활용할 수 있도록 표시한다. 
	if (m_eConnectionState != EConnectionState::CLOSING)
	{
		return false;
	}

	if (m_socket == INVALID_SOCKET)
	{
		printf("m_socket is Already INVALID_SOCKET \n");
		return false;
	}	

	if (closesocket(m_socket) == SOCKET_ERROR)
	{
		printf("CloseSocket Fail. WSAGetLastError = %d \n", WSAGetLastError());
		return false;
	}

	m_socket = INVALID_SOCKET;	

	return true;
}

bool CConnection::PrepareRecv()
{
	DWORD dwFlags = 0;
	DWORD dwBytes = 0;
	bool bResult = false;

	IOLock();

	EConnectionState eState = GetConnectionState();
	
	if (eState != EConnectionState::CONNECTED)
	{
		bResult = false;
		goto lb_return;
	}

	memset(m_pRecvOverlapped, 0, sizeof(OVERLAPPED));
	m_pRecvOverlapped->wsabuff.len = m_pRecvBuff->GetReservedBytes();
	m_pRecvOverlapped->wsabuff.buf = m_pRecvBuff->GetWritePtr();
	m_pRecvOverlapped->eIoType = EIOType::RECV;
	m_pRecvOverlapped->dwIndex = m_dwConnectionIndex;

	IncreaseRef(EIOType::RECV);

	if (WSARecv(m_socket,
		&m_pRecvOverlapped->wsabuff,
		1,
		&dwBytes,
		&dwFlags,
		(LPOVERLAPPED)&m_pRecvOverlapped->overlapped,
		NULL) == SOCKET_ERROR)
	{
		if (WSAGetLastError() != ERROR_IO_PENDING)
		{
			printf("WSARecv Fail. WSAGetLastError = %d \n", WSAGetLastError());
			DecreaseRef(EIOType::RECV);
			bResult = false;
			goto lb_return;
		}
	}

	bResult = true;

lb_return:
	IOUnLock();
	return bResult;
}

bool CConnection::RecvProcess(DWORD dwRecvBytes, char** ppMsg, DWORD* pdwMsgBytes, DWORD* pdwMsgNum)
{
	return m_pRecvBuff->RecvProcess(dwRecvBytes, ppMsg, pdwMsgBytes, pdwMsgNum);
}

void CConnection::CheckReset()
{
	m_pRecvBuff->CheckReset();
}

bool CConnection::Send(char* pMsg, DWORD dwBytes)
{
	bool bResult = false;

	m_pSendBuff->Lock();
	
	if (PushSend(pMsg, dwBytes) == false)
	{
		bResult = false;
		goto lb_return;
	}
	
	//send에 대한 통보가 오지 않아 send할 수 없는 상태면 버퍼에 푸쉬만 하고  보내지 않는다. 
	if (m_dwSendWait == TRUE)
	{
		bResult = true;
		goto lb_return;
	}

	if(PrepareSend() == false)
	{
		bResult = false;
		goto lb_return;
	}

	//send 완료 통보가 오기 전 까지 send할 수 없도록 락
	InterlockedExchange(&m_dwSendWait, TRUE);
	bResult = true; 

lb_return:
	m_pSendBuff->UnLock();
	return bResult;
}

bool CConnection::PushSend(char* pMsg, DWORD dwBytes)
{
	return m_pSendBuff->Push(pMsg, dwBytes);
}

bool CConnection::PrepareSend()
{
	bool bResult = false;
	
	IOLock();

	EConnectionState eState = GetConnectionState();
	DWORD dwBytes = m_pSendBuff->GetSendBytes();

	if(eState != EConnectionState::CONNECTED)
	{
		bResult = false;
		goto lb_return;
	}

	if (dwBytes <= 0)
	{
		printf("PrepareSend Fail. dwBytes = %d \n", dwBytes);
		bResult = false;
		goto lb_return;
	}

	memset(m_pSendOverlapped, 0, sizeof(OVERLAPPED));
	m_pSendOverlapped->wsabuff.len = dwBytes;
	m_pSendOverlapped->wsabuff.buf = m_pSendBuff->GetReadPtr();
	m_pSendOverlapped->eIoType = EIOType::SEND;
	m_pSendOverlapped->dwIndex = m_dwConnectionIndex;

	IncreaseRef(EIOType::SEND);

	if (WSASend(m_socket,
		&m_pSendOverlapped->wsabuff,
		1,
		nullptr,
		0,
		(LPOVERLAPPED)&m_pSendOverlapped->overlapped,
		NULL) == SOCKET_ERROR)
	{
		if (WSAGetLastError() != ERROR_IO_PENDING)
		{
			printf("WSASend Fail. WSAGetLastError = %d \n", WSAGetLastError());
			DecreaseRef(EIOType::SEND);
			bResult = false;
			goto lb_return;
		}
	}

	bResult = true;

	printf("PrepareSend readptr = %p UsageBytes = %d  dwBytes = %d \n", m_pSendBuff->GetReadPtr(), m_pSendBuff->GetSendBytes(), dwBytes);

lb_return:
	IOUnLock();
	return bResult;
}

void CConnection::LockSend()
{
	m_pSendBuff->Lock();
}

void CConnection::UnLockSend()
{
	m_pSendBuff->UnLock();
}

CIocpManager* CConnection::GetNetworkManager()
{
	return m_pIOCPManager;
}

DWORD CConnection::GetConnectionIndex()
{
	return m_dwConnectionIndex;
}

SOCKET CConnection::GetSocket()
{
	return m_socket;
}

void CConnection::SetSocket(SOCKET socket)
{
	m_socket = socket;
}

EConnectionState CConnection::GetConnectionState()
{
	return m_eConnectionState;
}

void CConnection::SetConnectionState(EConnectionState eState)
{
	m_eConnectionState = eState;
}

void CConnection::SetRemoteIP(char* szIP, DWORD dwLength)
{
	memcpy(m_szIP, szIP, dwLength);
}

bool CConnection::GetPeerName(char* pAddress, DWORD* pPort)
{
	SOCKADDR_IN addr;
	int addrlen = sizeof(SOCKADDR_IN);
	getpeername(m_socket, (sockaddr*)&addr, &addrlen);

	char* p = inet_ntoa(addr.sin_addr);

	memcpy(pAddress, p, strlen(p));
	*pPort = ntohs(addr.sin_port);

	return true;
}

char* CConnection::GetAddrBuff()
{
	return m_AddrBuf;
}

DWORD CConnection::GetRemotePort()
{
	return m_dwRemotePort;
}

void CConnection::SetRemotePort(DWORD dwPort)
{
	m_dwRemotePort = dwPort;
}

OverlappedEX* CConnection::GetRecvOverlapped()
{
	return m_pRecvOverlapped;
}

OverlappedEX* CConnection::GetSendOverlapped()
{
	return m_pSendOverlapped;
}

SRRecvRingBuffer* CConnection::GetRecvRingBuff()
{
	return m_pRecvBuff;
}

SRSendRingBuffer* CConnection::GetSendRingBuff()
{
	return m_pSendBuff;
}

void CConnection::IOLock()
{
	AcquireSRWLockExclusive(&m_kIOLock);
}

void CConnection::IOUnLock()
{
	ReleaseSRWLockExclusive(&m_kIOLock);
}

DWORD CConnection::GetRecvRefCount()
{
	return m_dwRecvRefCount;
}

DWORD CConnection::GetSendRefCount()
{
	return m_dwSendRefCount;
}

LONG CConnection::IncreaseRef(EIOType eIOType)
{
	switch (eIOType)
	{
		case EIOType::RECV:
			return InterlockedIncrement(&m_dwRecvRefCount);
		case EIOType::SEND:
			return InterlockedIncrement(&m_dwSendRefCount);
		default:
			return 0;
	}
}

LONG CConnection::DecreaseRef(EIOType eIOType)
{
	switch (eIOType)
	{
	case EIOType::RECV:
		return InterlockedDecrement(&m_dwRecvRefCount);
	case EIOType::SEND:
		return InterlockedDecrement(&m_dwSendRefCount);
	default:
		return 0;
	}
}


