#pragma once
#include <Windows.h>

//recv�� ���. WSARecv �ɾ������ �װ� gqcs���� �Ϸ��뺸�� ���� �� �ִ� ������� �ϳ� ���̴�.
//�Ϸ� �뺸�� �޾Ƽ� �ٽ� WSARecv�� �ɾ���´�.
//������ ���� �ʿ� ����.

//1. ���ú� ������ �� ���ڶ�� ��ȯ�ؼ� ������ �������� ���� ������ �ű�� �ٽ� ����. 
//2. ���ڶ�� ���� ������ŭ�� ���� �ɰ� Ư�� ������ ���� ���ݸ� ������ ��ȯ�ؼ� �ٽ� ����. 
//2�� ������� ��ȯ�ϱ�� ����. 

#define RESET_RESERVE_SIZE 100

class SRRecvRingBuffer 
{
public:
	SRRecvRingBuffer();
	~SRRecvRingBuffer();

	int Initialize(DWORD dwSize);
	void RecvProcess(DWORD dwRecvBytes, char** ppMsg, DWORD* pdwMsgBytes, DWORD* pdwMsgNum);
	void CheckReset();

	char* GetReadPtr();
	DWORD GetReservedBytes();

private:
	char* m_pBuffer; //������ ���� ������
	char* m_pReadPos; //�Ϸ� �뺸 �� ��Ŷ���� �ϼ��� �ȵǰ� �����ִ� �������� ���� ��ġ
	char* m_pWritePos; //���ú� ���� �� ��ġ
	DWORD m_dwReservedBytes; //�������� ���� �뷮
	DWORD m_dwUsageBytes; //�����ۿ� ���� ������. �޾Ƽ� �����ۿ� ���̸� �ø���, ��Ŷ���� �ν��ؼ� �����Ϸ��ϸ� ���δ�.
	DWORD m_dwBufferTotalSize; //������ �� ������
};