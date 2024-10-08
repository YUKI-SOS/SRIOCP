#include "SRSendRingBuffer.h"
#include <stdio.h>

SRSendRingBuffer::SRSendRingBuffer()
{
	m_pBuffer = nullptr;
	m_pReadPos = nullptr;
	m_pWritePos = nullptr;
	m_dwTotalBytes = 0;
	m_dwUsageBytes = 0;
	m_dwReserveBytes = 0;
}

SRSendRingBuffer::~SRSendRingBuffer()
{
	delete[] m_pBuffer;
	m_pBuffer = nullptr;
	m_pReadPos = nullptr;
	m_pWritePos = nullptr;
}

bool SRSendRingBuffer::Initialize(DWORD dwSize)
{
	m_dwTotalBytes = dwSize;	

	m_pBuffer = new char[m_dwTotalBytes];
	if (m_pBuffer == nullptr)
	{
		printf("%s %d Alloc Fail\n", __FUNCTION__, __LINE__);
		return false;
	}
	memset(m_pBuffer, 0, dwSize);

	m_pReadPos = m_pBuffer;
	m_pWritePos = m_pBuffer;

	m_dwUsageBytes = 0;
	m_dwReserveBytes = dwSize;

	InitializeSRWLock(&m_sendLock);

	return true;
}

void SRSendRingBuffer::Recycle()
{
	memset(m_pBuffer, 0, m_dwTotalBytes);

	m_pReadPos = m_pBuffer;
	m_pWritePos = m_pBuffer;

	m_dwUsageBytes = 0;
	m_dwReserveBytes = m_dwTotalBytes;
}

bool SRSendRingBuffer::Push(char* pMsg, DWORD dwLength)
{
	//링버퍼 전체 사이즈 보다 큰 메세지가 들어오려고 하는지 체크
	if (m_dwTotalBytes < dwLength) 
	{
		printf("%s:%d m_dwBufferSize = %d dwLength = %d \n", __FUNCTION__, __LINE__, m_dwTotalBytes, dwLength);
		return false;
	}

	//버퍼가 순환 되어야 하는지 체크
	if (m_dwReserveBytes < dwLength) //여유가 없어서 순환되어야 하면.
	{
		//처리 못한 메세지가 남아있으면 
		if (m_dwUsageBytes > 0)
			memcpy(m_pBuffer, m_pReadPos, m_dwUsageBytes); //처리 안된 부분의 시작을 버퍼의 시작으로 카피한다.
		
		m_pReadPos = m_pBuffer; //리드의 위치를 버퍼의 시작으로 설정.
		m_pWritePos = m_pBuffer + m_dwUsageBytes; //라이트의 위치를 버퍼의 시작 + 처리 안된 메세지 길이 이후로 설정.

		m_dwReserveBytes = m_dwTotalBytes - m_dwUsageBytes; //여유를 전체에서 사용량을 빼서 갱신한다.
	} 
	
	//순환 했음에도 길이가 부족하다면 실패 처리 
	if (m_dwReserveBytes < dwLength) 
	{
		printf("%s:%d m_dwReserveBytes = %d dwLength = %d \n", __FUNCTION__, __LINE__, m_dwReserveBytes, dwLength);
		__debugbreak();
		return false;
	}

	printf("Push Prev m_pWritePos = %p dwLength = %d \n", m_pWritePos, dwLength);

	memcpy(m_pWritePos, pMsg, dwLength);
	m_pWritePos += dwLength; //메세지 만큼 라이트 위치 증가
	m_dwReserveBytes -= dwLength; //예약된 크기 메세지만큼 감소
	m_dwUsageBytes += dwLength; //처리 안된 크기 메세지만큼 증가
	
	printf("Push After m_pWritePos = %p \n", m_pWritePos);

	return true;
}

void SRSendRingBuffer::PostSend(DWORD dwLength)
{
	printf("PostSend Prev m_pReadPos = %p dwLength = %d \n", m_pReadPos, dwLength);

	m_pReadPos += dwLength;
	m_dwUsageBytes -= dwLength;

	printf("PostSend After m_pReadPos = %p \n", m_pReadPos);
}

char* SRSendRingBuffer::GetReadPtr()
{
	return m_pReadPos;
}

DWORD SRSendRingBuffer::GetReservedBytes()
{
	return m_dwReserveBytes;
}

DWORD SRSendRingBuffer::GetUsageBytes()
{
	return m_dwUsageBytes;
}

void SRSendRingBuffer::Lock()
{
	AcquireSRWLockExclusive(&m_sendLock);
}

void SRSendRingBuffer::UnLock()
{
	ReleaseSRWLockExclusive(&m_sendLock);
}
