# SRIOCP

IOCP를 이용한 네트워크 라이브러리 입니다. 주로 화면을 그릴 때 깜빡임을 방지하는 더블버퍼링의 개념을 응용하여 패킷을 Recv하는 것이 특징입니다. 
Read와 Write 두 개의 자료구조를 두고 워커스레드들이 GQCS에서 패킷을 받으면 Write쪽에 패킷을 넣고,
메인스레드 쪽에서는 Read에 있는 패킷들을 처리하고 Read에 있는 패킷을 다 처리했을 경우 두 자료구조 간에 서로 Swap하는 구조입니다.
해당 구조는 유영천님의 ‘실시간 게임서버 최적화 전략‘ 이라는 강의 자료를 보고 구현하였습니다.
https://www.slideshare.net/dgtman/ss-228096175

![Image](https://github.com/user-attachments/assets/62f8d7e7-1212-4cf5-bc37-a6ef75d72915)

![Image](https://github.com/user-attachments/assets/6904b7fa-3219-4116-866f-4d9edfa049f5)

![Image](https://github.com/user-attachments/assets/3bd741eb-fff0-49d4-8098-303c877ea126)

![Image](https://github.com/user-attachments/assets/439c4fe7-5921-4dc9-8e95-3c559edb9bfb)

![Image](https://github.com/user-attachments/assets/aa6eb289-9cd7-46c9-b864-ddbf68ac1c51)

![Image](https://github.com/user-attachments/assets/02078c52-04bf-44c9-8300-e0023266ecda)

![Image](https://github.com/user-attachments/assets/23fc9c5e-c3ab-4a21-a33b-4dbcf51f4ffd)

![Image](https://github.com/user-attachments/assets/7d7663d4-cc36-4b8a-b1e6-ba420d4ebe3c)

![Image](https://github.com/user-attachments/assets/72b564b6-cca5-4892-af70-7e4af70526bf)

![Image](https://github.com/user-attachments/assets/0068b1c0-c603-4fa0-a7ed-4772554a417a)
