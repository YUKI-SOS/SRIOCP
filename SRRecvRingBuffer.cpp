#include "SRRecvRingBuffer.h"
#include <stdio.h>

SRRecvRingBuffer::SRRecvRingBuffer()
{
	m_pBuffer = nullptr;
	m_pReadPos = nullptr;
	m_pWritePos = nullptr;
	m_dwUsageBytes = 0;
	m_dwReservedBytes = 0;
	m_dwBufferTotalSize = 0;
}

SRRecvRingBuffer::~SRRecvRingBuffer()
{

}

int SRRecvRingBuffer::Initialize(DWORD dwSize)
{
	m_dwBufferTotalSize = dwSize;

	m_pBuffer = new char[m_dwBufferTotalSize];
	if (m_pBuffer == nullptr)
	{
		printf("%s %d Alloc Fail\n", __FUNCTION__, __LINE__);
		return false;
	}
	memset(m_pBuffer, 0, dwSize);

	m_pReadPos = m_pBuffer;
	m_pWritePos = m_pBuffer;

	m_dwUsageBytes = 0;
	m_dwReservedBytes = dwSize;

	return 0;
}

void SRRecvRingBuffer::RecvProcess(DWORD dwRecvBytes, char** ppMsg, DWORD* pdwMsgBytes, DWORD* pdwMsgNum)
{
	//�����ۿ��� ���� �����ͷ� ��Ŷ�� ������ �Լ�.
	DWORD dwPaketLength = 0;
	*pdwMsgBytes = 0;
	*pdwMsgNum = 0;

	m_dwReservedBytes -= dwRecvBytes;
	m_dwUsageBytes += dwRecvBytes;

	//�����ϸ� ������ ��ŭ�� �ɰ� ���� ���� �����ؼ� �ٽ� �Ǵ�.
	//��ġ���� üũ�� �ǹ� ����. ���� �� �ִ� ��ŭ�� �ɾ�δϱ�.
	
	//��Ŷ ���� ��ġ �Ѱ��ְ� ���ú� ����Ʈ ť(�����)�� ī���� �� �̿�.
	(*ppMsg) = m_pReadPos;

	while (true)
	{
		//�ּ����� ��Ŷ�� �� �� ���� �����͸� ������ Ż��. 
		if (m_dwUsageBytes < 4)
			return;

		//����� ��´�.
		dwPaketLength = *(DWORD*)m_pReadPos;

		//��Ŷ�� �ȵǰ� �����ִ� �뷮�� ��Ŷ ���̺��� ������ ��Ŷ ������� ���������� Ż��
		if (m_dwUsageBytes < dwPaketLength)
			return;

		//���� ��Ŷ ������ ��ŭ ���� ����
		m_pReadPos += dwPaketLength;
		(*pdwMsgBytes) += dwPaketLength;
		(*pdwMsgNum)++;

		//�ϼ��� ��Ŷ ��ŭ ���� ������ ����
		m_dwUsageBytes -= dwPaketLength;
	}

}

void SRRecvRingBuffer::CheckReset()
{
	//���� �뷮�� �����ϱ�� ���ص� ������� ������ �� ������ �����.
	if (m_dwReservedBytes < RESET_RESERVE_SIZE) 
	{
		m_dwReservedBytes = m_dwBufferTotalSize - m_dwUsageBytes; //������ ���� �뷮 ����.
		memcpy(m_pBuffer, m_pReadPos, m_dwUsageBytes); //ó�� ���ϰ� �����ִ� ������ ������ �������� ī��
		m_pReadPos = m_pBuffer; //���带 ������ �������� ����
		m_pWritePos = m_pBuffer + m_dwUsageBytes; //����Ʈ�� ���� �����͸� ������ �������� ī���� ���ķ� ����
	}
}

char* SRRecvRingBuffer::GetReadPtr()
{
	return m_pReadPos;
}

DWORD SRRecvRingBuffer::GetReservedBytes()
{
	return m_dwReservedBytes;
}
