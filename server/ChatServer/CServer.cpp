#include "CServer.h"
#include <iostream>
#include "AsioIOServicePool.h"
#include "UserMgr.h"

// ���캯������ʼ��I/O�����ġ������˿��Լ�TCP���������������������ӵĹ���
CServer::CServer(boost::asio::io_context& io_context, short port)
    : _io_context(io_context), _port(port),
      _acceptor(io_context, tcp::endpoint(tcp::v4(), port))  // ��ʼ��TCP���������󶨶˿�
{
    // ��ӡ�����������ɹ�����Ϣ
    cout << "Server start success, listen on port : " << _port << endl;

    // ��ʼ����������
    StartAccept();
}

// ������������ӡ����������ʱ����Ϣ
CServer::~CServer() {
    cout << "Server destruct listen on port : " << _port << endl;
}

// �����½��ܵ���������
void CServer::HandleAccept(shared_ptr<CSession> new_session, const boost::system::error_code& error) {
    if (!error) {  // ���û�д���
        new_session->Start();  // �����»Ự

        // ʹ�û����������Ự�б�ķ��ʣ�ȷ���̰߳�ȫ
        lock_guard<mutex> lock(_mutex);

        // ���»Ự��ӵ���Ծ�Ựӳ����У���Ϊ�ỰID
        _sessions.insert(make_pair(new_session->GetSessionId(), new_session));
    } else {
        // �������������ʱ���ִ��󣬴�ӡ������Ϣ
        cout << "Session accept failed, error is: " << error.message() << endl;
    }

    // �����Ƿ�ɹ�����ǰ���ӣ������������������µ�����
    StartAccept();
}


// ��ʼ���������������ӵĺ���
void CServer::StartAccept() {
    // ��ȡһ��I/O�����ķ�����е�I/O����������ڴ����첽I/O����
    auto &io_context = AsioIOServicePool::GetInstance()->GetIOService();

    // ����һ���µĻỰ����
    shared_ptr<CSession> new_session = make_shared<CSession>(io_context, this);

    // �첽���������ӣ��ɹ������HandleAccept���������
    _acceptor.async_accept(new_session->GetSocket(), 
        std::bind(&CServer::HandleAccept, this, new_session, placeholders::_1));
}


// ���ָ���Ự�����ݻỰID�Ƴ��Ự
void CServer::ClearSession(std::string uuid) {
    // ���Ự�Ƿ����
    if (_sessions.find(uuid) != _sessions.end()) {
        // �����û��������Ƴ��û��ͻỰ֮��Ĺ���
        UserMgr::GetInstance()->RmvUserSession(_sessions[uuid]->GetUserId());
    }

    // ʹ�û����������Ựӳ���ķ��ʣ�ȷ���̰߳�ȫ
    {
        lock_guard<mutex> lock(_mutex);
        // �ӻỰӳ������Ƴ�ָ���Ự
        _sessions.erase(uuid);
    }
}