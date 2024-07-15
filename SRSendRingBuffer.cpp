#include "SRSendRingBuffer.h"
#include <stdio.h>

SRSendRingBuffer::SRSendRingBuffer()
{
	m_pBuffer = nullptr;
	m_pReadPos = nullptr;
	m_pWritePos = nullptr;
	m_dwBufferSize = 0;
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
	m_dwBufferSize = dwSize;
	m_dwUsageBytes = 0;
	m_dwReserveBytes = dwSize;

	m_pBuffer = new char[m_dwBufferSize];
	if (m_pBuffer == nullptr)
	{
		printf("%s %d Alloc Fail\n", __FUNCTION__, __LINE__);
		return false;
	}
	memset(m_pBuffer, 0, dwSize);

	m_pReadPos = m_pBuffer;
	m_pWritePos = m_pBuffer;

	InitializeSRWLock(&m_sendLock);

	return true;
}

bool SRSendRingBuffer::Push(char* pMsg, DWORD dwLength)
{
	//������ ��ü ������ ���� ū �޼����� �������� �ϴ��� üũ
	if (m_dwBufferSize < dwLength) 
	{
		printf("%s:%d m_dwBufferSize = %d dwLength = %d \n", __FUNCTION__, __LINE__, m_dwBufferSize, dwLength);
		return false;
	}

	//���۰� ��ȯ �Ǿ�� �ϴ��� üũ
	if (m_dwReserveBytes < dwLength) //������ ��� ��ȯ�Ǿ�� �ϸ�.
	{
		//ó�� ���� �޼����� ���������� 
		if (m_dwUsageBytes > 0)
			memcpy(m_pBuffer, m_pReadPos, m_dwReserveBytes); //ó�� �ȵ� �κ��� ������ ������ �������� ī���Ѵ�.
		
		m_pReadPos = m_pBuffer; //������ ��ġ�� ������ �������� ����.
		m_pWritePos = m_pBuffer + m_dwUsageBytes; //����Ʈ�� ��ġ�� ������ ���� + ó�� �ȵ� �޼��� ���� ���ķ� ����.

		m_dwReserveBytes = m_dwBufferSize - m_dwUsageBytes; //������ ��ü���� ��뷮�� ���� �����Ѵ�.
	} 
	
	//��ȯ �������� ���̰� �����ϴٸ� ���� ó�� 
	if (m_dwReserveBytes < dwLength) 
	{
		printf("%s:%d m_dwReserveBytes = %d dwLength = %d \n", __FUNCTION__, __LINE__, m_dwReserveBytes, dwLength);
		__debugbreak();
		return false;
	}

	memcpy(m_pWritePos, pMsg, dwLength);
	m_dwReserveBytes -= dwLength; //����� ũ�� �޼�����ŭ ����
	m_dwUsageBytes += dwLength; //ó�� �ȵ� ũ�� �޼�����ŭ ����
	
	return true;
}

bool SRSendRingBuffer::PostSend(DWORD dwLength)
{
	m_pReadPos += dwLength;
	m_dwUsageBytes -= dwLength;

	return true;
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
