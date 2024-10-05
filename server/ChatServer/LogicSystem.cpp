#include "LogicSystem.h"
#include "StatusGrpcClient.h"
#include "MysqlMgr.h"
#include "const.h"
#include "RedisMgr.h"
#include "UserMgr.h"
#include "ChatGrpcClient.h"

using namespace std;

// ���캯��
LogicSystem::LogicSystem() : _b_stop(false) { // ��ʼ��ֹͣ��־Ϊ false
    RegisterCallBacks(); // ע����Ϣ����Ļص�����
    // ���������̣߳����ڴ�����Ϣ
    _worker_thread = std::thread(&LogicSystem::DealMsg, this); 
}

// ��������
LogicSystem::~LogicSystem() {
    _b_stop = true; // ����ֹͣ��־Ϊ true����ʾ�����߳�Ӧֹͣ
    _consume.notify_one(); // ֪ͨ�������������µ�״̬�仯
    _worker_thread.join(); // �ȴ������߳����
}

// ����Ϣ������Ͷ����Ϣ
void LogicSystem::PostMsgToQue(shared_ptr<LogicNode> msg) {
    std::unique_lock<std::mutex> unique_lk(_mutex); // ��ȡ������
    _msg_que.push(msg); // ����Ϣ������Ϣ����
    // ������дӿձ�Ϊ�ǿգ�֪ͨ�����߳�
    if (_msg_que.size() == 1) {
        unique_lk.unlock(); // �ͷŻ�����
        _consume.notify_one(); // ֪ͨ��������
    }
}
/*
DealMsg �����������Ϣ�����л�ȡ��Ϣ����������Ϣ ID ������Ӧ�Ļص��������д�����ͨ�����������ͻ�����ʵ�����̰߳�ȫ����Ϣ������ƣ�ͬʱ֧�����ŵعرչ����̣߳�ȷ����ϵͳ�ر�ʱ���������д�������Ϣ��
*/
void LogicSystem::DealMsg() {
	for (;;) {
		std::unique_lock<std::mutex> unique_lk(_mutex);
		//�ж϶���Ϊ�������������������ȴ������ͷ���
		while (_msg_que.empty() && !_b_stop) {
			_consume.wait(unique_lk);
		}

		//�ж��Ƿ�Ϊ�ر�״̬���������߼�ִ��������˳�ѭ��
		if (_b_stop ) {
			while (!_msg_que.empty()) {
				auto msg_node = _msg_que.front();
				cout << "recv_msg id  is " << msg_node->_recvnode->_msg_id << endl;
				auto call_back_iter = _fun_callbacks.find(msg_node->_recvnode->_msg_id);
				if (call_back_iter == _fun_callbacks.end()) {
					_msg_que.pop();
					continue;
				}
				call_back_iter->second(msg_node->_session, msg_node->_recvnode->_msg_id,
					std::string(msg_node->_recvnode->_data, msg_node->_recvnode->_cur_len));
				_msg_que.pop();
			}
			break;
		}

		//���û��ͣ������˵��������������
		auto msg_node = _msg_que.front();
		cout << "recv_msg id  is " << msg_node->_recvnode->_msg_id << endl;
		auto call_back_iter = _fun_callbacks.find(msg_node->_recvnode->_msg_id);
		if (call_back_iter == _fun_callbacks.end()) {
			_msg_que.pop();
			std::cout << "msg id [" << msg_node->_recvnode->_msg_id << "] handler not found" << std::endl;
			continue;
		}
		call_back_iter->second(msg_node->_session, msg_node->_recvnode->_msg_id, std::string(msg_node->_recvnode->_data, msg_node->_recvnode->_cur_len));
		_msg_que.pop();
	}
}

void LogicSystem::RegisterCallBacks() {
	_fun_callbacks[MSG_CHAT_LOGIN] = std::bind(&LogicSystem::LoginHandler, this,
		placeholders::_1, placeholders::_2, placeholders::_3);

	_fun_callbacks[ID_SEARCH_USER_REQ] = std::bind(&LogicSystem::SearchInfo, this,
		placeholders::_1, placeholders::_2, placeholders::_3);

	_fun_callbacks[ID_ADD_FRIEND_REQ] = std::bind(&LogicSystem::AddFriendApply, this,
		placeholders::_1, placeholders::_2, placeholders::_3);

	_fun_callbacks[ID_AUTH_FRIEND_REQ] = std::bind(&LogicSystem::AuthFriendApply, this,
		placeholders::_1, placeholders::_2, placeholders::_3);

	_fun_callbacks[ID_TEXT_CHAT_MSG_REQ] = std::bind(&LogicSystem::DealChatTextMsg, this,
		placeholders::_1, placeholders::_2, placeholders::_3);
	
}

void LogicSystem::LoginHandler(shared_ptr<CSession> session, const short &msg_id, const string &msg_data) {
    // ���� JSON �������� JSON ֵ����
    Json::Reader reader;
    Json::Value root;
    // �����������Ϣ����
    reader.parse(msg_data, root);

    // ��ȡ�û� ID �� Token
    auto uid = root["uid"].asInt(); // �� JSON ����ȡ�û� ID
    auto token = root["token"].asString(); // �� JSON ����ȡ Token
    std::cout << "user login uid is  " << uid << " user token  is " << token << endl;

    Json::Value  rtvalue; // ���ڴ洢���ؽ��
    // ʹ�� Defer �ṹ��ȷ������ͽ�����ͻ���
    Defer defer([this, &rtvalue, session]() {
        std::string return_str = rtvalue.toStyledString(); // �� JSON ����תΪ�ַ���
        session->Send(return_str, MSG_CHAT_LOGIN_RSP); // ���͵�¼��Ӧ
    });

    // �� Redis ��ȡ�û��� Token �Ƿ���ȷ
    std::string uid_str = std::to_string(uid); // ���û� ID תΪ�ַ���
    std::string token_key = USERTOKENPREFIX + uid_str; // ���� Redis �� Token �ļ�
    std::string token_value = ""; // ���ڴ洢�� Redis ��ȡ�� Token
    bool success = RedisMgr::GetInstance()->Get(token_key, token_value); // ��ȡ Token
    if (!success) {
        rtvalue["error"] = ErrorCodes::UidInvalid; // �����ȡʧ�ܣ����� UID ��Ч
        return;
    }

    // ��� Token �Ƿ��� Redis �е�ֵƥ��
    if (token_value != token) {
        rtvalue["error"] = ErrorCodes::TokenInvalid; // �����ƥ�䣬���� Token ��Ч
        return;
    }

    rtvalue["error"] = ErrorCodes::Success; // ��¼�ɹ������÷���״̬Ϊ�ɹ�

    // �����ݿ��ȡ�û�������Ϣ
    std::string base_key = USER_BASE_INFO + uid_str; // �����û�������Ϣ�ļ�
    auto user_info = std::make_shared<UserInfo>(); // �����û���Ϣ����
    bool b_base = GetBaseInfo(base_key, uid, user_info); // ��ȡ�û�������Ϣ
    if (!b_base) {
        rtvalue["error"] = ErrorCodes::UidInvalid; // �����ȡʧ�ܣ����� UID ��Ч
        return;
    }

    // ���û�������Ϣ��ӵ�����ֵ��
    rtvalue["uid"] = uid; // �û� ID
    rtvalue["pwd"] = user_info->pwd; // �û�����
    rtvalue["name"] = user_info->name; // �û���
    rtvalue["email"] = user_info->email; // �û�����
    rtvalue["nick"] = user_info->nick; // �û��ǳ�
    rtvalue["desc"] = user_info->desc; // �û�����
    rtvalue["sex"] = user_info->sex; // �û��Ա�
    rtvalue["icon"] = user_info->icon; // �û�ͷ��

  // �����ݿ��ȡ���������б�
    std::vector<std::shared_ptr<ApplyInfo>> apply_list; // �洢������Ϣ���б�
    auto b_apply = GetFriendApplyInfo(uid, apply_list); // ��ȡ����������Ϣ
    if (b_apply) {
        for (auto &apply : apply_list) {
            Json::Value obj; // ���� JSON ����洢������Ϣ
            obj["name"] = apply->_name; // ����������
            obj["uid"] = apply->_uid; // �������û� ID
            obj["icon"] = apply->_icon; // ������ͷ��
            obj["nick"] = apply->_nick; // �������ǳ�
            obj["sex"] = apply->_sex; // �������Ա�
            obj["desc"] = apply->_desc; // ����������
            obj["status"] = apply->_status; // ����״̬
            rtvalue["apply_list"].append(obj); // ��������Ϣ��ӵ�����ֵ
        }
    }

    // ��ȡ�����б�
    std::vector<std::shared_ptr<UserInfo>> friend_list; // �洢������Ϣ���б�
    bool b_friend_list = GetFriendList(uid, friend_list); // ��ȡ�����б�
    for (auto &friend_ele : friend_list) {
        Json::Value obj; // ���� JSON ����洢������Ϣ
        obj["name"] = friend_ele->name; // ��������
        obj["uid"] = friend_ele->uid; // �����û� ID
        obj["icon"] = friend_ele->icon; // ����ͷ��
        obj["nick"] = friend_ele->nick; // �����ǳ�
        obj["sex"] = friend_ele->sex; // �����Ա�
        obj["desc"] = friend_ele->desc; // ��������
        obj["back"] = friend_ele->back; // ���ѱ�����Ϣ
        rtvalue["friend_list"].append(obj); // ��������Ϣ��ӵ�����ֵ
    }

    // ��ȡ��ǰ����������
    auto server_name = ConfigMgr::Inst().GetValue("SelfServer", "Name");

    // ���µ�¼����
    auto rd_res = RedisMgr::GetInstance()->HGet(LOGIN_COUNT, server_name); // �� Redis ��ȡ��¼����
    int count = 0;
    if (!rd_res.empty()) {
        count = std::stoi(rd_res); // ����¼����ת��Ϊ����
    }

    count++; // ��¼��������
    auto count_str = std::to_string(count); // ת��Ϊ�ַ���
    RedisMgr::GetInstance()->HSet(LOGIN_COUNT, server_name, count_str); // ���� Redis �еĵ�¼����

    // ���û� ID �󶨵���ǰ�Ự��
    session->SetUserId(uid);
	
    // Ϊ�û����õ�¼ IP �ͷ���������
    std::string ipkey = USERIPPREFIX + uid_str; // �����û� IP �� Redis ��
    RedisMgr::GetInstance()->Set(ipkey, server_name); // �� Redis �������û� IP
	//uid��session�󶨹���,�����Ժ����˲���
    // ���û� ID �ͻỰ���Ա㽫�������ߵ��û�
    UserMgr::GetInstance()->SetUserSession(uid, session);

	return; // ��ɵ�¼����
}

void LogicSystem::SearchInfo(std::shared_ptr<CSession> session, const short& msg_id, const string& msg_data)
{
    Json::Reader reader; // ���� JSON ������
    Json::Value root; // ���� JSON ֵ����
    reader.parse(msg_data, root); // ��������� JSON �ַ�������
    auto uid_str = root["uid"].asString(); // �� JSON �л�ȡ "uid" �ֶε�ֵ
    std::cout << "user SearchInfo uid is  " << uid_str << endl; // ��ӡ�������û� ID

    Json::Value rtvalue; // �������ڴ洢���ؽ���� JSON ֵ����

    // ʹ�� Defer ������ȷ���ڷ�������ʱ�Զ����ͽ��
    Defer defer([this, &rtvalue, session]() {
        std::string return_str = rtvalue.toStyledString(); // �����ת��Ϊ�ַ���
        session->Send(return_str, ID_SEARCH_USER_RSP); // ���ͽ�����ͻ���
    });

    // ��� uid_str �Ƿ��Ǵ�����
    bool b_digit = isPureDigit(uid_str); // �ж� uid_str �Ƿ�Ϊ����
    if (b_digit) { // ��������֣���ͨ�� UID ��ѯ�û���Ϣ
        GetUserByUid(uid_str, rtvalue); // ���� UID ��ȡ�û���Ϣ����䵽 rtvalue ��
    }
    else { // ����������֣���ͨ���û�����ѯ�û���Ϣ
        GetUserByName(uid_str, rtvalue); // �����û�����ȡ�û���Ϣ����䵽 rtvalue ��
    }
    return; // ����������������� Defer ���Զ�����
}

void LogicSystem::AddFriendApply(std::shared_ptr<CSession> session, const short& msg_id, const string& msg_data)
{
    // ����Json�������͸��ڵ�
    Json::Reader reader;
    Json::Value root;

    // �����������Ϣ����
    if (!reader.parse(msg_data, root)) {
        // �������ʧ�ܣ����������ﴦ��������緵�ش�����Ӧ���ͻ���
        // �����м��������ܹ��ɹ�����
        return;
    }

 	// ��JSON����ȡ������UID���������ơ���ע�����Լ�������UID
    auto uid = root["uid"].asInt();         // �����ߵ�UID
    auto applyname = root["applyname"].asString();  // ����ʱʹ�õ�����
    auto bakname = root["bakname"].asString();      // ��ע����
    auto touid = root["touid"].asInt();             // �����ߵ�UID

    // ��ӡ������Ϣ
    std::cout << "user login uid is  " << uid << " applyname  is " << applyname << " bakname is " << bakname << " touid is " << touid << endl;

    // ������ӦJSON����
    Json::Value rtvalue;
    rtvalue["error"] = ErrorCodes::Success;  // ���ô�����Ϊ�ɹ�
	
    // ʹ��Deferģʽȷ���ں�������ǰ��ͻ��˷�����Ӧ
    Defer defer([this, &rtvalue, session]() {
        std::string return_str = rtvalue.toStyledString();  // ��JSON����ת���ɸ�ʽ�����ַ���
        session->Send(return_str, ID_ADD_FRIEND_RSP);       // ��ͻ��˷�����Ӧ
    });

    // �������ݿ⣬��¼��������
    MysqlMgr::GetInstance()->AddFriendApply(uid, touid);

    // ��ѯRedis�Բ��ҽ����߶�Ӧ�ķ�����IP
    auto to_str = std::to_string(touid);
    auto to_ip_key = USERIPPREFIX + to_str;  // ��ϳɼ�
    std::string to_ip_value = "";            // �洢��ѯ���
    bool b_ip = RedisMgr::GetInstance()->Get(to_ip_key, to_ip_value);  // ִ�в�ѯ
    if (!b_ip) {  // ���û���ҵ���Ӧ��IP��ַ
        // ������������Ӵ������߼����������ô����벢����
        return;
    }

    // ��ȡ��ǰ����������
    auto& cfg = ConfigMgr::Inst();
    auto self_name = cfg["SelfServer"]["Name"];  // ��ǰ����������


   // ��Redis�л�ȡ�����ߵ��û�������Ϣ
    std::string base_key = USER_BASE_INFO + std::to_string(uid);
    auto apply_info = std::make_shared<UserInfo>();  // ����UserInfoָ��
    bool b_info = GetBaseInfo(base_key, uid, apply_info);  // ��ȡ�û�������Ϣ

    // ���������Ƿ���ͬһ��������
    if (to_ip_value == self_name) {
        // ��ͬһ��������ֱ�ӻ�ȡ�Ự��������Ϣ
        auto target_session = UserMgr::GetInstance()->GetSession(touid);
        if (target_session) {
            // ����֪ͨJSON����
            Json::Value notify;
            notify["error"] = ErrorCodes::Success;
            notify["applyuid"] = uid;  // �����ߵ�UID
            notify["name"] = applyname;  // ����ʱʹ�õ�����
            notify["desc"] = "";  // ��ע������Ϊ�գ�

            // �����ȡ���û�������Ϣ���������Ӧ�ֶ�
            if (b_info) {
                notify["icon"] = apply_info->icon;  // �û�ͷ��
                notify["sex"] = apply_info->sex;    // �û��Ա�
                notify["nick"] = apply_info->nick;  // �û��ǳ�
            }

            // ת�����ַ���������֪ͨ
            std::string return_str = notify.toStyledString();
            target_session->Send(return_str, ID_NOTIFY_ADD_FRIEND_REQ);
        }
        return;  // ֱ�ӷ��أ���Ϊ�Ѿ��ڱ��ش������
    }

	
    // ����gRPC�������
    AddFriendReq add_req;
    add_req.set_applyuid(uid);  // ���������ߵ�UID
    add_req.set_touid(touid);   // ���ý����ߵ�UID
    add_req.set_name(applyname);  // ��������ʱʹ�õ�����
    add_req.set_desc("");        // ���ñ�ע������Ϊ�գ�

    // �����ȡ���û�������Ϣ���������Ӧ�ֶ�   
    if (b_info) {
        add_req.set_icon(apply_info->icon);  // �û�ͷ��
        add_req.set_sex(apply_info->sex);    // �û��Ա�
        add_req.set_nick(apply_info->nick);  // �û��ǳ�
    }

    // ͨ��gRPC�ͻ��˷��Ϳ��������֪ͨ
    ChatGrpcClient::GetInstance()->NotifyAddFriend(to_ip_value, add_req);

}

void LogicSystem::AuthFriendApply(std::shared_ptr<CSession> session, const short& msg_id, const string& msg_data) {
	
    // ����Json�������͸��ڵ�
    Json::Reader reader;
    Json::Value root;

    // �����������Ϣ����
    if (!reader.parse(msg_data, root)) {
        // �������ʧ�ܣ����������ﴦ��������緵�ش�����Ӧ���ͻ���
        // �����м��������ܹ��ɹ�����
        return;
    }
    // ��JSON����ȡ������UID��������UID�Լ���ע����
    auto uid = root["fromuid"].asInt();         // �����ߵ�UID
    auto touid = root["touid"].asInt();         // �����ߵ�UID
    auto back_name = root["back"].asString();   // ��ע����

    // ��ӡ������Ϣ
    std::cout << "from " << uid << " auth friend to " << touid << std::endl;

    // ������ӦJSON����
    Json::Value rtvalue;
    rtvalue["error"] = ErrorCodes::Success;  // ���ô�����Ϊ�ɹ�

    // ����UserInfoָ���Դ洢�û���Ϣ
    auto user_info = std::make_shared<UserInfo>();

    // ��Redis�л�ȡ�����ߵ��û�������Ϣ
    std::string base_key = USER_BASE_INFO + std::to_string(touid);
    bool b_info = GetBaseInfo(base_key, touid, user_info);  // ��ȡ�û�������Ϣ
    if (b_info) {
        // �����ȡ���û�������Ϣ���������Ӧ�ֶ�
        rtvalue["name"] = user_info->name;  // �û���
        rtvalue["nick"] = user_info->nick;  // �û��ǳ�
        rtvalue["icon"] = user_info->icon;  // �û�ͷ��
        rtvalue["sex"] = user_info->sex;    // �û��Ա�
        rtvalue["uid"] = touid;             // �û�UID
    } else {
        // ���û���ҵ��û���Ϣ�����ô�����
        rtvalue["error"] = ErrorCodes::UidInvalid;
    }


    // ʹ��Deferģʽȷ���ں�������ǰ��ͻ��˷�����Ӧ
    Defer defer([this, &rtvalue, session]() {
        std::string return_str = rtvalue.toStyledString();  // ��JSON����ת���ɸ�ʽ�����ַ���
        session->Send(return_str, ID_AUTH_FRIEND_RSP);       // ��ͻ��˷�����Ӧ
    });

    // �������ݿ⣬��¼������֤ͨ��
    MysqlMgr::GetInstance()->AuthFriendApply(uid, touid);

    // �������ݿ⣬��Ӻ��ѹ�ϵ
    MysqlMgr::GetInstance()->AddFriend(uid, touid, back_name);

    // ��ѯRedis�Բ��ҽ����߶�Ӧ�ķ�����IP
    auto to_str = std::to_string(touid);
    auto to_ip_key = USERIPPREFIX + to_str;  // ��ϳɼ�
    std::string to_ip_value = "";            // �洢��ѯ���
    bool b_ip = RedisMgr::GetInstance()->Get(to_ip_key, to_ip_value);  // ִ�в�ѯ
    if (!b_ip) {  // ���û���ҵ���Ӧ��IP��ַ
        // ������������Ӵ������߼����������ô����벢����
        return;
    }

    // ��ȡ��ǰ����������
    auto& cfg = ConfigMgr::Inst();
    auto self_name = cfg["SelfServer"]["Name"];  // ��ǰ����������

    // ���������Ƿ���ͬһ��������
    if (to_ip_value == self_name) {
        // ��ͬһ��������ֱ�ӻ�ȡ�Ự��������Ϣ
        auto target_session = UserMgr::GetInstance()->GetSession(touid);
        if (target_session) {
            // ����֪ͨJSON����
            Json::Value notify;
            notify["error"] = ErrorCodes::Success;
            notify["fromuid"] = uid;  // �����ߵ�UID
            notify["touid"] = touid;   // �����ߵ�UID

            // ��Redis�л�ȡ�����ߵ��û�������Ϣ
            std::string base_key = USER_BASE_INFO + std::to_string(uid);
            auto user_info = std::make_shared<UserInfo>();
            bool b_info = GetBaseInfo(base_key, uid, user_info);  // ��ȡ�û�������Ϣ
            if (b_info) {
                notify["name"] = user_info->name;  // �û���
                notify["nick"] = user_info->nick;  // �û��ǳ�
                notify["icon"] = user_info->icon;  // �û�ͷ��
                notify["sex"] = user_info->sex;    // �û��Ա�
            } else {
                notify["error"] = ErrorCodes::UidInvalid;
            }

            // ת�����ַ���������֪ͨ
            std::string return_str = notify.toStyledString();
            target_session->Send(return_str, ID_NOTIFY_AUTH_FRIEND_REQ);
        }
        return;  // ֱ�ӷ��أ���Ϊ�Ѿ��ڱ��ش������
    }


    // ����gRPC�������
    AuthFriendReq auth_req;
    auth_req.set_fromuid(uid);  // ���÷����ߵ�UID
    auth_req.set_touid(touid);  // ���ý����ߵ�UID

    // ͨ��gRPC�ͻ��˷��Ϳ��������֪ͨ
    ChatGrpcClient::GetInstance()->NotifyAuthFriend(to_ip_value, auth_req);
}

void LogicSystem::DealChatTextMsg(std::shared_ptr<CSession> session, const short& msg_id, const string& msg_data) {
    // ����Json�������͸��ڵ�
    Json::Reader reader;
    Json::Value root;

    // �����������Ϣ����
    if (!reader.parse(msg_data, root)) {
        // �������ʧ�ܣ����������ﴦ��������緵�ش�����Ӧ���ͻ���
        // �����м��������ܹ��ɹ�����
        return;
    }

    // ��JSON����ȡ������UID��������UID�Լ���Ϣ����
    auto uid = root["fromuid"].asInt();
    auto touid = root["touid"].asInt();
    const Json::Value arrays = root["text_array"];
	
    // ������ӦJSON����
    Json::Value rtvalue;
    rtvalue["error"] = ErrorCodes::Success;  // ���ô�����Ϊ�ɹ�
    rtvalue["text_array"] = arrays;          // ��ԭʼ�ı����鸴�Ƶ���Ӧ��
    rtvalue["fromuid"] = uid;                // �����ߵ�UID
    rtvalue["touid"] = touid;                // �����ߵ�UID


    // ʹ��Deferģʽȷ���ں�������ǰ��ͻ��˷�����Ӧ
    Defer defer([this, &rtvalue, session]() {
        std::string return_str = rtvalue.toStyledString();  // ��JSON����ת���ɸ�ʽ�����ַ���
        session->Send(return_str, ID_TEXT_CHAT_MSG_RSP);    // ��ͻ��˷�����Ӧ
    });

    // ��ѯRedis�Բ��ҽ����߶�Ӧ�ķ�����IP
    auto to_str = std::to_string(touid);
    auto to_ip_key = USERIPPREFIX + to_str;  // ��ϳɼ�
    std::string to_ip_value = "";            // �洢��ѯ���
    bool b_ip = RedisMgr::GetInstance()->Get(to_ip_key, to_ip_value);  // ִ�в�ѯ
    if (!b_ip) {  // ���û���ҵ���Ӧ��IP��ַ
        // ������������Ӵ������߼����������ô����벢����
        return;
    }

    // ��ȡ��ǰ����������
    auto& cfg = ConfigMgr::Inst();
    auto self_name = cfg["SelfServer"]["Name"];  // ��ǰ����������

    // ���������Ƿ���ͬһ��������
    if (to_ip_value == self_name) {
        // ��ͬһ��������ֱ�ӻ�ȡ�Ự��������Ϣ
        auto target_session = UserMgr::GetInstance()->GetSession(touid);
        if (target_session) {
            std::string return_str = rtvalue.toStyledString();  // ת�����ַ���
            target_session->Send(return_str, ID_NOTIFY_TEXT_CHAT_MSG_REQ);  // ����֪ͨ
        }
        return;  // ֱ�ӷ��أ���Ϊ�Ѿ��ڱ��ش������
    }


    // ����gRPC�������
    TextChatMsgReq text_msg_req;
    text_msg_req.set_fromuid(uid);  // ���÷�����UID
    text_msg_req.set_touid(touid);  // ���ý�����UID
    // ������Ϣ���ݵ�gRPC�������
    for (const auto& txt_obj : arrays) {
        auto content = txt_obj["content"].asString();  // ��Ϣ����
        auto msgid = txt_obj["msgid"].asString();      // ��ϢID
        std::cout << "content is " << content << std::endl;
        std::cout << "msgid is " << msgid << std::endl;

        // ��ӵ�����Ϣ��gRPC�������
        auto *text_msg = text_msg_req.add_textmsgs();
        text_msg->set_msgid(msgid);
        text_msg->set_msgcontent(content);
    }



	//����֪ͨ todo...
	// ͨ��gRPC�ͻ��˷��Ϳ��������֪ͨ
	ChatGrpcClient::GetInstance()->NotifyTextChatMsg(to_ip_value, text_msg_req, rtvalue);
}



bool LogicSystem::isPureDigit(const std::string& str)
{
	for (char c : str) {
		if (!std::isdigit(c)) {
			return false;
		}
	}
	return true;
}

void LogicSystem::GetUserByUid(std::string uid_str, Json::Value& rtvalue)
{
	rtvalue["error"] = ErrorCodes::Success;

	std::string base_key = USER_BASE_INFO + uid_str;

	//���Ȳ�redis�в�ѯ�û���Ϣ
	std::string info_str = "";
	bool b_base = RedisMgr::GetInstance()->Get(base_key, info_str);
	if (b_base) {
		Json::Reader reader;
		Json::Value root;
		reader.parse(info_str, root);
		auto uid = root["uid"].asInt();
		auto name = root["name"].asString();
		auto pwd = root["pwd"].asString();
		auto email = root["email"].asString();
		auto nick = root["nick"].asString();
		auto desc = root["desc"].asString();
		auto sex = root["sex"].asInt();
		auto icon = root["icon"].asString();
		std::cout << "user  uid is  " << uid << " name  is "
			<< name << " pwd is " << pwd << " email is " << email <<" icon is " << icon << endl;

		rtvalue["uid"] = uid;
		rtvalue["pwd"] = pwd;
		rtvalue["name"] = name;
		rtvalue["email"] = email;
		rtvalue["nick"] = nick;
		rtvalue["desc"] = desc;
		rtvalue["sex"] = sex;
		rtvalue["icon"] = icon;
		return;
	}

	auto uid = std::stoi(uid_str);
	//redis��û�����ѯmysql
	//��ѯ���ݿ�
	std::shared_ptr<UserInfo> user_info = nullptr;
	user_info = MysqlMgr::GetInstance()->GetUser(uid);
	if (user_info == nullptr) {
		rtvalue["error"] = ErrorCodes::UidInvalid;
		return;
	}

	//�����ݿ�����д��redis����
	Json::Value redis_root;
	redis_root["uid"] = user_info->uid;
	redis_root["pwd"] = user_info->pwd;
	redis_root["name"] = user_info->name;
	redis_root["email"] = user_info->email;
	redis_root["nick"] = user_info->nick;
	redis_root["desc"] = user_info->desc;
	redis_root["sex"] = user_info->sex;
	redis_root["icon"] = user_info->icon;

	RedisMgr::GetInstance()->Set(base_key, redis_root.toStyledString());

	//��������
	rtvalue["uid"] = user_info->uid;
	rtvalue["pwd"] = user_info->pwd;
	rtvalue["name"] = user_info->name;
	rtvalue["email"] = user_info->email;
	rtvalue["nick"] = user_info->nick;
	rtvalue["desc"] = user_info->desc;
	rtvalue["sex"] = user_info->sex;
	rtvalue["icon"] = user_info->icon;
}

void LogicSystem::GetUserByName(std::string name, Json::Value& rtvalue)
{
	rtvalue["error"] = ErrorCodes::Success;

	std::string base_key = NAME_INFO + name;

	//���Ȳ�redis�в�ѯ�û���Ϣ
	std::string info_str = "";
	bool b_base = RedisMgr::GetInstance()->Get(base_key, info_str);
	if (b_base) {
		Json::Reader reader;
		Json::Value root;
		reader.parse(info_str, root);
		auto uid = root["uid"].asInt();
		auto name = root["name"].asString();
		auto pwd = root["pwd"].asString();
		auto email = root["email"].asString();
		auto nick = root["nick"].asString();
		auto desc = root["desc"].asString();
		auto sex = root["sex"].asInt();
		std::cout << "user  uid is  " << uid << " name  is "
			<< name << " pwd is " << pwd << " email is " << email << endl;

		rtvalue["uid"] = uid;
		rtvalue["pwd"] = pwd;
		rtvalue["name"] = name;
		rtvalue["email"] = email;
		rtvalue["nick"] = nick;
		rtvalue["desc"] = desc;
		rtvalue["sex"] = sex;
		return;
	}

	//redis��û�����ѯmysql
	//��ѯ���ݿ�
	std::shared_ptr<UserInfo> user_info = nullptr;
	user_info = MysqlMgr::GetInstance()->GetUser(name);
	if (user_info == nullptr) {
		rtvalue["error"] = ErrorCodes::UidInvalid;
		return;
	}

	//�����ݿ�����д��redis����
	Json::Value redis_root;
	redis_root["uid"] = user_info->uid;
	redis_root["pwd"] = user_info->pwd;
	redis_root["name"] = user_info->name;
	redis_root["email"] = user_info->email;
	redis_root["nick"] = user_info->nick;
	redis_root["desc"] = user_info->desc;
	redis_root["sex"] = user_info->sex;

	RedisMgr::GetInstance()->Set(base_key, redis_root.toStyledString());
	
	//��������
	rtvalue["uid"] = user_info->uid;
	rtvalue["pwd"] = user_info->pwd;
	rtvalue["name"] = user_info->name;
	rtvalue["email"] = user_info->email;
	rtvalue["nick"] = user_info->nick;
	rtvalue["desc"] = user_info->desc;
	rtvalue["sex"] = user_info->sex;
}

bool LogicSystem::GetBaseInfo(std::string base_key, int uid, std::shared_ptr<UserInfo>& userinfo)
{
    // ���ȴ� Redis �в�ѯ�û���Ϣ
    std::string info_str = ""; // ���ڴ洢�� Redis ��ȡ���û���Ϣ�ַ���
    bool b_base = RedisMgr::GetInstance()->Get(base_key, info_str); // �� Redis ��ȡ�û���Ϣ
    if (b_base) { // ����ɹ���ȡ���û���Ϣ
        Json::Reader reader; // ���� JSON ������
        Json::Value root; // ���� JSON ֵ����
        reader.parse(info_str, root); // ������ȡ�� JSON �ַ���
        
        // �����������Ϣ��䵽 userinfo ������
        userinfo->uid = root["uid"].asInt(); // �û� ID
        userinfo->name = root["name"].asString(); // �û�����
        userinfo->pwd = root["pwd"].asString(); // �û�����
        userinfo->email = root["email"].asString(); // �û�����
        userinfo->nick = root["nick"].asString(); // �û��ǳ�
        userinfo->desc = root["desc"].asString(); // �û�����
        userinfo->sex = root["sex"].asInt(); // �û��Ա�
        userinfo->icon = root["icon"].asString(); // �û�ͷ��
        
        // ��ӡ�û���¼��Ϣ
        std::cout << "user login uid is  " << userinfo->uid << " name  is "
                  << userinfo->name << " pwd is " << userinfo->pwd << " email is " << userinfo->email << endl;
    }
    else {
        // ��� Redis ��û����Ϣ�����ѯ MySQL ���ݿ�
        std::shared_ptr<UserInfo> user_info = nullptr; // ����ָ���û���Ϣ������ָ��
        user_info = MysqlMgr::GetInstance()->GetUser(uid); // �����ݿ��л�ȡ�û���Ϣ
        if (user_info == nullptr) { // ��������ݿ��л�ȡʧ��
            return false; // ���� false����ʾδ�ҵ��û���Ϣ
        }

        userinfo = user_info; // �������ݿ��ȡ���û���Ϣ��ֵ�� userinfo

        // �����ݿ��е�����д�� Redis ����
        Json::Value redis_root; // �������ڴ洢 Redis �� JSON ֵ����
        redis_root["uid"] = uid; // �û� ID
        redis_root["pwd"] = userinfo->pwd; // �û�����
        redis_root["name"] = userinfo->name; // �û�����
        redis_root["email"] = userinfo->email; // �û�����
        redis_root["nick"] = userinfo->nick; // �û��ǳ�
        redis_root["desc"] = userinfo->desc; // �û�����
        redis_root["sex"] = userinfo->sex; // �û��Ա�
        redis_root["icon"] = userinfo->icon; // �û�ͷ��

        // ���û���Ϣд�� Redis
        RedisMgr::GetInstance()->Set(base_key, redis_root.toStyledString()); // �� JSON ����ת��Ϊ�ַ��������� Redis
    }

    return true; // ���� true����ʾ�ɹ���ȡ�û�������Ϣ
}


bool LogicSystem::GetFriendApplyInfo(int to_uid, std::vector<std::shared_ptr<ApplyInfo>> &list) {
	//��mysql��ȡ���������б�
	return MysqlMgr::GetInstance()->GetApplyList(to_uid, list, 0, 10);
}

bool LogicSystem::GetFriendList(int self_id, std::vector<std::shared_ptr<UserInfo>>& user_list) {
	//��mysql��ȡ�����б�
	return MysqlMgr::GetInstance()->GetFriendList(self_id, user_list);
}
