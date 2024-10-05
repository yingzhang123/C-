#include "CSession.h"
#include "CServer.h"
#include <iostream>
#include <sstream>
#include <json/json.h>
#include <json/value.h>
#include <json/reader.h>
#include "LogicSystem.h"

// CSession ���캯������ʼ��TCP socket��������ָ�롢�Ự��ʶ�ͽ�����Ϣͷ
CSession::CSession(boost::asio::io_context& io_context, CServer* server)
	: _socket(io_context), _server(server), _b_close(false), _b_head_parse(false), _user_uid(0) {
    // ����Ψһ�ĻỰID
	boost::uuids::uuid a_uuid = boost::uuids::random_generator()();
	_session_id = boost::uuids::to_string(a_uuid);
	_recv_head_node = make_shared<MsgNode>(HEAD_TOTAL_LEN);    // HEAD_TOTAL_LEN = 4
}

CSession::~CSession() {
	std::cout << "~CSession destruct" << endl;
}

tcp::socket& CSession::GetSocket() {
	return _socket;
}

std::string& CSession::GetSessionId() {
	return _session_id;
}

void CSession::SetUserId(int uid)
{
	_user_uid = uid;
}

int CSession::GetUserId()
{
	return _user_uid;
}

// ��ʼ�Ự�����ȶ�ȡ��Ϣͷ��
void CSession::Start() {
	AsyncReadHead(HEAD_TOTAL_LEN);        // HEAD_TOTAL_LEN = 4 ����ϢID(2) + ��Ϣ���ȣ�2����
}

void CSession::Send(std::string msg, short msgid) {
    // ʹ��std::lock_guard�Զ�����������ȷ���̰߳�ȫ
    std::lock_guard<std::mutex> lock(_send_lock);

    // ��ȡ��ǰ���Ͷ��еĴ�С
    int send_que_size = _send_que.size();

    // ��鷢�Ͷ����Ƿ�����
    if (send_que_size > MAX_SENDQUE) {
        // ���������������ӡ������Ϣ������
        std::cout << "session: " << _session_id << " send que fulled, size is " << MAX_SENDQUE << endl;
        return;
    }

    // ����һ���µ�SendNode���󣬲��������뷢�Ͷ���
    _send_que.push(make_shared<SendNode>(msg.c_str(), msg.length(), msgid));

    // ������Ͷ������Ѿ���������Ϣ�ڵȴ����ͣ���ֱ�ӷ���
    if (send_que_size > 0) {
        return;
    }

    // ��ȡ���Ͷ����еĵ�һ����Ϣ�ڵ�
    auto& msgnode = _send_que.front();

    // �첽д�����ݵ�socket
    boost::asio::async_write(
        _socket,  // Ŀ��socket
        boost::asio::buffer(msgnode->_data, msgnode->_total_len),  // Ҫ���͵����ݻ�����
        std::bind(&CSession::HandleWrite, this, std::placeholders::_1, SharedSelf())  // ��ɴ���ص�
    );
}

void CSession::Send(char* msg, short max_length, short msgid) {
	std::lock_guard<std::mutex> lock(_send_lock);
	int send_que_size = _send_que.size();
	if (send_que_size > MAX_SENDQUE) {
		std::cout << "session: " << _session_id << " send que fulled, size is " << MAX_SENDQUE << endl;
		return;
	}

	_send_que.push(make_shared<SendNode>(msg, max_length, msgid));
	if (send_que_size>0) {
		return;
	}
	auto& msgnode = _send_que.front();
	boost::asio::async_write(_socket, boost::asio::buffer(msgnode->_data, msgnode->_total_len), 
		std::bind(&CSession::HandleWrite, this, std::placeholders::_1, SharedSelf()));
}

void CSession::Close() {
	_socket.close();
	_b_close = true;
}

std::shared_ptr<CSession>CSession::SharedSelf() {
	return shared_from_this();
}

// ʵ�����첽��ȡ��Ϣ��Ĺ��ܣ����ڶ�ȡ��ɺ���ϢͶ�ݵ��߼�ϵͳ�н��д�����������Ҳ�ǻ��ڻص����������첽��������
void CSession::AsyncReadBody(int total_len)
{
	 // ���ֵ�ǰ�Ự�Ĺ���ָ�룬�Ա������첽����ʱ��������
	auto self = shared_from_this();
	// �첽��ȡ��Ϣ�壬��ȡ���ֽ���Ϊ total_len
	asyncReadFull(total_len, [self, this, total_len](const boost::system::error_code& ec, std::size_t bytes_transfered) {
		try {
			if (ec) {
				std::cout << "handle read failed, error is " << ec.what() << endl;
				Close();
				_server->ClearSession(_session_id);
				return;
			}

            // ����ȡ���ֽ����Ƿ���Ԥ�ڵ���Ϣ�峤��һ��
            if (bytes_transfered < total_len) {
                std::cout << "read length not match, read [" << bytes_transfered << "] , total ["
                    << total_len << "]" << endl;
                // �����ȡ�ֽ���С��Ԥ�ڣ��رջỰ������
                Close();
                _server->ClearSession(_session_id);
                return;
            }

            // ����ȡ������Ϣ�����ݴ���ʱ������ _data ������ _recv_msg_node �� _data ��
            memcpy(_recv_msg_node->_data, _data, bytes_transfered);
            // ���� _recv_msg_node �ĵ�ǰ���ȣ����ӱ��ζ�ȡ���ֽ���
            _recv_msg_node->_cur_len += bytes_transfered;
            // ȷ�����ݵ����һλ�ǽ����� '\0'����֤��Ϣ�ַ����ĺϷ���
            _recv_msg_node->_data[_recv_msg_node->_total_len] = '\0';
            // ��ӡ���յ�����Ϣ����
            cout << "receive data is " << _recv_msg_node->_data << endl;
			
			//�˴�����ϢͶ�ݵ��߼�������
			LogicSystem::GetInstance()->PostMsgToQue(make_shared<LogicNode>(shared_from_this(), _recv_msg_node));
			//��������ͷ�������¼�
			AsyncReadHead(HEAD_TOTAL_LEN);
		}
		catch (std::exception& e) {
			std::cout << "Exception code is " << e.what() << endl;
		}
		});
}

void CSession::AsyncReadHead(int total_len)
{
	// ���ֵ�ǰ�Ự�Ĺ���ָ�룬�Ա������첽����ʱ�Ự��������
	auto self = shared_from_this();
	// �첽��ȡ��������Ϣͷ��HEAD_TOTAL_LEN �ֽڣ�����ͨ���ص����������ȡ���      asyncReadFullע��ص�
	asyncReadFull(HEAD_TOTAL_LEN, [self, this](const boost::system::error_code& ec, std::size_t bytes_transfered) {
		try {
			 // �����ȡ�����Ĵ���
			if (ec) {
				std::cout << "handle read failed, error is " << ec.what() << endl;
				// �رյ�ǰ�Ự������
				Close();
				_server->ClearSession(_session_id);
				return;
			}

			// ����ȡ�����ֽ����Ƿ���Ԥ�ڵ���Ϣͷ����һ��
			if (bytes_transfered < HEAD_TOTAL_LEN) {
				std::cout << "read length not match, read [" << bytes_transfered << "] , total ["
					<< HEAD_TOTAL_LEN << "]" << endl;
				// �����ȡ���ֽ���С��Ԥ�ڣ��رջỰ��������Դ
				Close();
				_server->ClearSession(_session_id);
				return;
			}

			// ��ս��ջ������еľ�����
			_recv_head_node->Clear();

			// ����ȡ�����ֽ����ݴ� _data����ʱ�����������Ƶ� _recv_head_node->_data �У�
			// ����ר�����ڴ洢��Ϣͷ�����ݵĻ�����
			memcpy(_recv_head_node->_data, _data, bytes_transfered);


		// ������Ϣͷ�е� MSGID����Ϣ��ʶ����
			short msg_id = 0;
			// �ӽ��ջ������и�����Ϣ ID ����
			memcpy(&msg_id, _recv_head_node->_data, HEAD_ID_LEN);

			// �������ֽ���� msg_id ת��Ϊ�����ֽ��򣨲�ͬ��ϵͳ���ܲ��ò�ͬ���ֽ����ʾ����
			msg_id = boost::asio::detail::socket_ops::network_to_host_short(msg_id);
			std::cout << "msg_id is " << msg_id << endl;

			// �����Ϣ ID �Ƿ���Ч����� ID �����������ֵ������Ϊ�ǷǷ� ID
			if (msg_id > MAX_LENGTH) {
				std::cout << "invalid msg_id is " << msg_id << endl;
				// ����Ự���ر�����
				_server->ClearSession(_session_id);
				return;
			}

		// ������Ϣͷ�е���Ϣ���ȣ�msg_len��
			short msg_len = 0;
			// �ӽ��ջ������и�����Ϣ��������
			memcpy(&msg_len, _recv_head_node->_data + HEAD_ID_LEN, HEAD_DATA_LEN);

			// �������ֽ���� msg_len ת��Ϊ�����ֽ���
			msg_len = boost::asio::detail::socket_ops::network_to_host_short(msg_len);
			std::cout << "msg_len is " << msg_len << endl;

			// �����Ϣ�����Ƿ���Ч��������ȳ����������󳤶ȣ�����Ϊ�ǷǷ�����
			if (msg_len > MAX_LENGTH) {
				std::cout << "invalid data length is " << msg_len << endl;
				// ����Ự���ر�����
				_server->ClearSession(_session_id);
				return;
			}

			// ����һ��������Ϣ�ڵ㣬���ڴ洢���յ���Ϣ���ݣ�������Ϣ�岿�֣�
			_recv_msg_node = make_shared<RecvNode>(msg_len, msg_id);
			// ���� AsyncReadBody ��������ʼ�첽��ȡ��Ϣ�岿��
			AsyncReadBody(msg_len);
		}
		catch (std::exception& e) {
			std::cout << "Exception code is " << e.what() << endl;
		}
		});
}

void CSession::HandleWrite(const boost::system::error_code& error, std::shared_ptr<CSession> shared_self) {
    // �����쳣����
    try {
        // ����Ƿ��д�����
        if (!error) {
            // ���û�д���ʹ��std::lock_guard�Զ�����������ȷ���̰߳�ȫ
            std::lock_guard<std::mutex> lock(_send_lock);

            // �ӷ��Ͷ������Ƴ��ѳɹ����͵���Ϣ�ڵ�
            _send_que.pop();

            // ��鷢�Ͷ����Ƿ���������Ϣ��Ҫ����
            if (!_send_que.empty()) {
                // ��ȡ���Ͷ����е���һ����Ϣ�ڵ�
                auto& msgnode = _send_que.front();

                // �첽д�����ݵ�socket
                boost::asio::async_write(
                    _socket,  // Ŀ��socket
                    boost::asio::buffer(msgnode->_data, msgnode->_total_len),  // Ҫ���͵����ݻ�����
                    std::bind(&CSession::HandleWrite, this, std::placeholders::_1, shared_self)  // ��ɴ���ص�
                );
            }
        } else {
            // ����д���������ӡ������Ϣ���رջỰ
            std::cout << "handle write failed, error is " << error.what() << endl;

            // �رյ�ǰ�Ự
            Close();

            // �ӷ������������ǰ�Ự
            _server->ClearSession(_session_id);
        }
    } catch (std::exception& e) {
        // ���񲢴����׼�쳣
        std::cerr << "Exception code : " << e.what() << endl;
    }
}

//��ȡ��������
void CSession::asyncReadFull(std::size_t maxLength, std::function<void(const boost::system::error_code&, std::size_t)> handler )
{
	// ��ջ�����
	::memset(_data, 0, MAX_LENGTH);
	// ��ʼ��ȡָ�����ȵ����� 4���ֽ�
	asyncReadLen(0, maxLength, handler);
}

//��ȡָ���ֽ���
void CSession::asyncReadLen(std::size_t read_len, std::size_t total_len, std::function<void(const boost::system::error_code&, std::size_t)> handler)
{
	auto self = shared_from_this();
	// �첽��ȡ���ݲ��洢�� _data ��������
	_socket.async_read_some(boost::asio::buffer(_data + read_len, total_len-read_len),
		[read_len, total_len, handler, self](const boost::system::error_code& ec, std::size_t  bytesTransfered) {
			if (ec) {
				// ���ִ��󣬵��ûص�����
				handler(ec, read_len + bytesTransfered);
				return;
			}
			// �����ȡ���ֽ����ﵽ��Ԥ�ڵ��ܳ��ȣ�����ûص�������������	
			if (read_len + bytesTransfered >= total_len) {
				//���ȹ��˾͵��ûص�����
				handler(ec, read_len + bytesTransfered);
				return;
			}

			// û�д����ҳ��Ȳ����������ȡ
			self->asyncReadLen(read_len + bytesTransfered, total_len, handler);
	});
}
/*
_data ��һ���ַ������������ڴ洢������ socket ���첽��ȡ���ֽ����ݡ���ÿ�ζ�ȡ����ʱ��

_data �ᱻ��գ�Ȼ��� socket �н��յ������ݻᱻ�洢������������С�
�����ݽ�����Ϻ�_data �е����ݻᱻ���Ƶ���Ϣ�ڵ㣨�� _recv_head_node �� _recv_msg_node���У������������ʹ���
*/

LogicNode::LogicNode(shared_ptr<CSession>  session, 
	shared_ptr<RecvNode> recvnode):_session(session),_recvnode(recvnode) {
	
}
