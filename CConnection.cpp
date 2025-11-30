#include "CConnection.h"
#include "SRIocp.h"

CIocpManager* CConnection::m_pIOCPManager = nullptr;

CConnection::CConnection()
{	
	m_dwConnectionIndex = -1;
	m_socket = INVALID_SOCKET;
	m_bConnected = false;

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
	m_bConnected = false;

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
	SetConnected(false);

	return true;
}

bool CConnection::PrepareRecv()
{
	DWORD dwFlags = 0;
	DWORD dwBytes = 0;

	memset(m_pRecvOverlapped, 0, sizeof(OVERLAPPED));
	m_pRecvOverlapped->wsabuff.len = m_pRecvBuff->GetReservedBytes();
	m_pRecvOverlapped->wsabuff.buf = m_pRecvBuff->GetWritePtr();
	m_pRecvOverlapped->eIoType = IOType::RECV;
	m_pRecvOverlapped->dwIndex = m_dwConnectionIndex;

	IncreaseRecvRef();

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
			DecreaseRecvRef();
			return false;
		}
	}

	return true;
}

void CConnection::RecvProcess(DWORD dwRecvBytes, char** ppMsg, DWORD* pdwMsgBytes, DWORD* pdwMsgNum)
{
	m_pRecvBuff->RecvProcess(dwRecvBytes, ppMsg, pdwMsgBytes, pdwMsgNum);
}

void CConnection::CheckReset()
{
	m_pRecvBuff->CheckReset();
}

bool CConnection::Send(char* pMsg, DWORD dwBytes)
{
	m_pSendBuff->Lock();
	
	if(PushSend(pMsg, dwBytes) == false)
	{
		m_pSendBuff->UnLock();
		return false;
	}
	
	//send에 대한 통보가 오지 않아 send할 수 없는 상태면 버퍼에 푸쉬만 하고  보내지 않는다. 
	if (m_dwSendWait == TRUE)
	{
		m_pSendBuff->UnLock();
		return true;
	}

	if(SendBuff() == false)
	{
		m_pSendBuff->UnLock();
		return false;
	}

	//send 완료 통보가 오기 전 까지 send할 수 없도록 락
	InterlockedExchange(&m_dwSendWait, TRUE);

	m_pSendBuff->UnLock();

	return true;
}

bool CConnection::PushSend(char* pMsg, DWORD dwBytes)
{
	return m_pSendBuff->Push(pMsg, dwBytes);
}

bool CConnection::SendBuff()
{
	DWORD dwBytes = 0;
	memset(m_pSendOverlapped, 0, sizeof(OVERLAPPED));
	m_pSendOverlapped->wsabuff.len = m_pSendBuff->GetUsageBytes();
	m_pSendOverlapped->wsabuff.buf = m_pSendBuff->GetReadPtr();
	m_pSendOverlapped->eIoType = IOType::SEND;
	m_pSendOverlapped->dwIndex = m_dwConnectionIndex;

	IncreaseSendRef();

	if (WSASend(m_socket,
		&m_pSendOverlapped->wsabuff,
		1,
		&dwBytes,
		0,
		(LPOVERLAPPED)&m_pSendOverlapped->overlapped,
		NULL) == SOCKET_ERROR)
	{
		if (WSAGetLastError() != ERROR_IO_PENDING)
		{
			printf("WSASend Fail. WSAGetLastError = %d \n", WSAGetLastError());
			DecreaseSendRef();
			return false;
		}
	}

#ifdef __DEV_LOG__
	printf("SendBuff readptr = %p UsageBytes = %d  dwBytes = %d \n", m_pSendBuff->GetReadPtr(), m_pSendBuff->GetUsageBytes(), dwBytes);
#endif

	return true;
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

bool CConnection::GetConnected()
{
	return m_bConnected;
}

void CConnection::SetConnected(bool status)
{
	m_bConnected = status;
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

DWORD CConnection::GetRecvRefCount()
{
	return m_dwRecvRefCount;
}

DWORD CConnection::GetSendRefCount()
{
	return m_dwSendRefCount;
}


void CConnection::IncreaseRecvRef()
{
	InterlockedIncrement(&m_dwRecvRefCount);
}

void CConnection::DecreaseRecvRef()
{
	InterlockedDecrement(&m_dwRecvRefCount);
}

void CConnection::IncreaseSendRef()
{
	InterlockedIncrement(&m_dwSendRefCount);
}

void CConnection::DecreaseSendRef()
{
	InterlockedDecrement(&m_dwSendRefCount);
}




