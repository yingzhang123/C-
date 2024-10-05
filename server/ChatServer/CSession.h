#pragma once
#include <boost/asio.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <boost/uuid/uuid_generators.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast.hpp>
#include <boost/asio.hpp>
#include <queue>
#include <mutex>
#include <memory>
#include "const.h"
#include "MsgNode.h"
using namespace std;


namespace beast = boost::beast;         // from <boost/beast.hpp>
namespace http = beast::http;           // from <boost/beast/http.hpp>
namespace net = boost::asio;            // from <boost/asio.hpp>
using tcp = boost::asio::ip::tcp;       // from <boost/asio/ip/tcp.hpp>


class CServer;  // Ԥ����CServer��
class LogicSystem;  // Ԥ����LogicSystem��

// CSession�ࣺ����ÿ���ͻ��˻Ự
class CSession: public std::enable_shared_from_this<CSession>
{
public:
	CSession(boost::asio::io_context& io_context, CServer* server);
	~CSession();
	tcp::socket& GetSocket();
	std::string& GetSessionId();
	void SetUserId(int uid);
	int GetUserId();
	void Start();
	void Send(char* msg,  short max_length, short msgid);
	void Send(std::string msg, short msgid);
	void Close();
	// ���ع�����������
	std::shared_ptr<CSession> SharedSelf();
	// �첽��ȡ��Ϣ��
	void AsyncReadBody(int length);
	// �첽��ȡ��Ϣͷ
	void AsyncReadHead(int total_len);
private:
	// �첽��ȡ������Ϣ
	void asyncReadFull(std::size_t maxLength, std::function<void(const boost::system::error_code& , std::size_t)> handler);
	// �첽��ȡָ�����ȵ�����
	void asyncReadLen(std::size_t  read_len, std::size_t total_len,
		std::function<void(const boost::system::error_code&, std::size_t)> handler);
	
	// ����д�������ʱ�Ļص�
	void HandleWrite(const boost::system::error_code& error, std::shared_ptr<CSession> shared_self);
	// TCP���ӵ�socket����
	tcp::socket _socket;
	// �Ự��Ψһ��ʶ�� (UUID)
	std::string _session_id;

	// �洢�������ݵĻ�����
	char _data[MAX_LENGTH];

	// ָ������������ָ�룬���������������
	CServer* _server;
	// �Ự�رձ�־
	bool _b_close;
	// ��Ϣ���Ͷ���
	std::queue<shared_ptr<SendNode> > _send_que;
	// ���Ͷ��еĻ�������ȷ���̰߳�ȫ
	std::mutex _send_lock;

	// ���յ���Ϣ�ṹ��ָ��
	std::shared_ptr<RecvNode> _recv_msg_node;
	// ��־�Ƿ����ڽ�����Ϣͷ
	bool _b_head_parse;

	// ���յ�����Ϣͷ���ṹ��
	std::shared_ptr<MsgNode> _recv_head_node;
	// �û���ΨһID
	int _user_uid;
};


// LogicNode�ࣺ���ڹ����߼������еĻỰ�ͽ��սڵ�
class LogicNode {
	friend class LogicSystem;  // LogicSystem����Է���LogicNode��˽�г�Ա
public:
	// ���캯������ʼ���Ự�ͽ��սڵ�
	LogicNode(shared_ptr<CSession>, shared_ptr<RecvNode>);
private:
	// �洢�Ự����
	shared_ptr<CSession> _session;

	// �洢������Ϣ�ڵ����
	shared_ptr<RecvNode> _recvnode;
};