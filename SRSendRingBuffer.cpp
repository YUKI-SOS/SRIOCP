#include "SRSendRingBuffer.h"
#include <stdio.h>

SRSendRingBuffer::SRSendRingBuffer()
{
	m_pBuffer = nullptr;
	m_pReadPos = nullptr;
	m_pWritePos = nullptr;
	m_dwTotalBytes = 0;
	m_dwUsageBytes = 0;
	m_dwTailReserveBytes = 0;
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
	m_dwTailReserveBytes = dwSize;

	InitializeSRWLock(&m_sendLock);

	return true;
}

void SRSendRingBuffer::Recycle()
{
	memset(m_pBuffer, 0, m_dwTotalBytes);

	m_pReadPos = m_pBuffer;
	m_pWritePos = m_pBuffer;

	m_dwUsageBytes = 0;
	m_dwTailReserveBytes = m_dwTotalBytes;
}

bool SRSendRingBuffer::Push(char* pMsg, DWORD dwLength)
{
	//메세지 길이가 0이하이면 실패 처리
	if (dwLength <= 0)
		return false;

	if(pMsg == nullptr)
		return false;

	//링버퍼 전체에서 남은 용량 체크
	DWORD dwTotalReserveBytes = m_dwTotalBytes - m_dwUsageBytes;

	if (dwTotalReserveBytes < dwLength)
	{
		printf("%s:%d m_dwBufferSize = %d dwLength = %d \n", __FUNCTION__, __LINE__, m_dwTotalBytes, dwLength);
		return false;
	}

	bool bBuffOver = false;

	//버퍼 뒤에 공간이 남는지 체크.
	if (m_dwTailReserveBytes < dwLength) 
		bBuffOver = true;	
	
	//카피할 길이 결정.
	DWORD dwTailCopyBytes = dwLength;  //WritePos에서부터 카피할 길이. 순환 시에는 버퍼의 끝까지만.

	if (bBuffOver == true)
	{
		dwTailCopyBytes = m_dwTailReserveBytes;
	}
	
	DWORD dwRemainCopyBytes = dwLength - dwTailCopyBytes; ////순환하게 되면 남은 메세지 크기

	//보낼 메세지 버퍼에 카피
	memcpy(m_pWritePos, pMsg, dwTailCopyBytes);

	//순환 시에는 테일에 카피하고 남은 메세지 크기만큼 버퍼의 시작으로 카피
	if (dwRemainCopyBytes > 0)
		memcpy(m_pBuffer, pMsg + dwTailCopyBytes, dwRemainCopyBytes);
	
	//카피 후 pos 이동.
	m_pWritePos += dwTailCopyBytes;

	if (m_pWritePos >= m_pBuffer + m_dwTotalBytes) //순환 시에는 버퍼의 시작으로 이동
		m_pWritePos = m_pBuffer;

	if (bBuffOver == true)
		m_pWritePos += dwRemainCopyBytes;
	
	m_dwUsageBytes += dwLength; //사용량 증가
	
	//m_dwTailReserveBytes(m_pWritePos부터 비어있는 버퍼 사이즈) 구하기
	if (m_pWritePos > m_pReadPos) //리드가 앞에 라이트가 뒤에 있는 일반적인 상태. 라이트 부터 끝까지.
	{
		m_dwTailReserveBytes = (DWORD)((m_pBuffer + m_dwTotalBytes) - m_pWritePos);
	}
	else if(m_pWritePos < m_pReadPos) //순환해서 리드가 뒤에 라이트가 앞에 있는 상태.
	{
		m_dwTailReserveBytes = (DWORD)((m_pReadPos) - m_pWritePos);
	}
	else //리드와 라이트가 같은 위치. 비거나 가득 찬 상태. 
	{
		if (m_dwUsageBytes == 0) //비어있으면 라이트부터 마지막 까지 사용 가능. 
			m_dwTailReserveBytes = (DWORD)((m_pBuffer + m_dwTotalBytes) - m_pWritePos);
		else //가득 차 있으면 못씀.
			m_dwTailReserveBytes = 0;
	}

	return true;
}

void SRSendRingBuffer::PostSend(DWORD dwLength)
{
	printf("PostSend Prev m_pReadPos = %p dwLength = %d \n", m_pReadPos, dwLength);

	m_pReadPos += dwLength;
	m_dwUsageBytes -= dwLength;

	printf("PostSend After m_pReadPos = %p \n", m_pReadPos);

	if (m_pReadPos == m_pBuffer + m_dwTotalBytes) // 리드가 버퍼 끝에 도달했다면 리드 위치를 순환.
		m_pReadPos = m_pBuffer; 

	//m_dwTailReserveBytes(m_pWritePos부터 비어있는 버퍼 사이즈) 구하기
	if (m_pWritePos > m_pReadPos) //리드가 앞에 라이트가 뒤에 있는 일반적인 상태. 라이트 부터 끝까지.
	{
		m_dwTailReserveBytes = (DWORD)((m_pBuffer + m_dwTotalBytes) - m_pWritePos);
	}
	else if (m_pWritePos < m_pReadPos) //순환해서 리드가 뒤에 라이트가 앞에 있는 상태.
	{
		m_dwTailReserveBytes = (DWORD)((m_pReadPos)-m_pWritePos);
	}
	else //리드와 라이트가 같은 위치. 비거나 가득 찬 상태. 
	{
		if (m_dwUsageBytes == 0) //비어있으면 라이트부터 마지막 까지 사용 가능. 
			m_dwTailReserveBytes = (DWORD)((m_pBuffer + m_dwTotalBytes) - m_pWritePos);
		else //가득 차 있으면 못씀.
			m_dwTailReserveBytes = 0;
	}
}

char* SRSendRingBuffer::GetReadPtr()
{
	return m_pReadPos;
}

DWORD SRSendRingBuffer::GetReservedBytes()
{
	return m_dwTailReserveBytes;
}

DWORD SRSendRingBuffer::GetUsageBytes()
{
	return m_dwUsageBytes;
}

DWORD SRSendRingBuffer::GetSendBytes()
{
	if (m_dwUsageBytes == 0) // 송신할 데이터가 없으면
		return 0;
	
	if (m_pReadPos < m_pWritePos) //리드 - 라이트 순서. 순환하지 않았음. 리트부터 라이트까지. 
	{		
		return (DWORD)(m_pWritePos - m_pReadPos);
	}
	else //리드가 라이트보다 뒤에 있거나 같아서 가득찬 경우. 리드부터 버퍼 마지막까지. 
	{
		return (DWORD)((m_pBuffer + m_dwTotalBytes) - m_pReadPos);
	}

}

void SRSendRingBuffer::Lock()
{
	AcquireSRWLockExclusive(&m_sendLock);
}

void SRSendRingBuffer::UnLock()
{
	ReleaseSRWLockExclusive(&m_sendLock);
}
