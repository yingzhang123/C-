#include "ChatGrpcClient.h"
#include "RedisMgr.h"
#include "ConfigMgr.h"
#include "UserMgr.h"

#include "CSession.h"
#include "MysqlMgr.h"

ChatGrpcClient::ChatGrpcClient()
{
	auto& cfg = ConfigMgr::Inst();
	auto server_list = cfg["PeerServer"]["Servers"];

	std::vector<std::string> words;

	std::stringstream ss(server_list);
	std::string word;

	while (std::getline(ss, word, ',')) {
		words.push_back(word);
	}

	for (auto& word : words) {
		if (cfg[word]["Name"].empty()) {
			continue;
		}
		_pools[cfg[word]["Name"]] = std::make_unique<ChatConPool>(5, cfg[word]["Host"], cfg[word]["Port"]);
	}
}

// �������ܣ���ָ���ķ��������� "��Ӻ���" �� gRPC ���󣬲�������Ӧ�����
// ���������
//   - server_ip: Ŀ��������� IP ��ַ��
//   - req: ��Ӻ��ѵ�������Ϣ�����������û� ID ��Ŀ���û� ID ����Ϣ��
// ��������� AddFriendRsp ���󣬱�ʾ����������Ӧ�����
AddFriendRsp ChatGrpcClient::NotifyAddFriend(std::string server_ip, const AddFriendReq& req)
{
    // ��ʼ����Ӧ���� rsp����ʹ�� Defer ��ȷ���ں�������ʱ����Ĭ�ϵĳɹ�״̬��������û� ID ��Ϣ��
    AddFriendRsp rsp;
    Defer defer([&rsp, &req]() {
        rsp.set_error(ErrorCodes::Success);   // ����Ĭ�ϳɹ�״̬
        rsp.set_applyuid(req.applyuid());     // ���������û� ID
        rsp.set_touid(req.touid());           // ����Ŀ���û� ID
    });

    // ���ҷ��������ӳأ��ҵ���Ӧ�����ӳض���
    auto find_iter = _pools.find(server_ip);
    if (find_iter == _pools.end()) {
        // ����Ҳ�����Ӧ�ķ����� IP����ֱ�ӷ��ص�ǰ rsp������Ĭ�ϵĳɹ�״̬��
        return rsp;
    }

    // ��ȡ�ҵ��ķ��������ӳض���
    auto &pool = find_iter->second;

    // ���� gRPC �����Ķ�������ά������״̬��
    ClientContext context;

    // �����ӳ��л�ȡ gRPC �ͻ��˵����Ӷ���
    auto stub = pool->getConnection();

    // ���� "��Ӻ���" �� gRPC ���󣬴��������ġ�������� req ����Ӧ���� rsp��
    Status status = stub->NotifyAddFriend(&context, req, &rsp);

    // ʹ�� Defer ȷ����������ʱ�����ӷ��ص����ӳ��С�
    Defer defercon([&stub, this, &pool]() {
        pool->returnConnection(std::move(stub));  // �����ӹ黹�����ӳ�
    });

    // ��� gRPC �����״̬
    if (!status.ok()) {
        // �������ʧ�ܣ��򽫴���������Ϊ RPC ʧ��
        rsp.set_error(ErrorCodes::RPCFailed);
        return rsp;
    }

    // ����ɹ���������Ӧ���
    return rsp;
}



bool ChatGrpcClient::GetBaseInfo(std::string base_key, int uid, std::shared_ptr<UserInfo>& userinfo)
{
	//���Ȳ�redis�в�ѯ�û���Ϣ
	std::string info_str = "";
	bool b_base = RedisMgr::GetInstance()->Get(base_key, info_str);
	if (b_base) {
		Json::Reader reader;
		Json::Value root;
		reader.parse(info_str, root);
		userinfo->uid = root["uid"].asInt();
		userinfo->name = root["name"].asString();
		userinfo->pwd = root["pwd"].asString();
		userinfo->email = root["email"].asString();
		userinfo->nick = root["nick"].asString();
		userinfo->desc = root["desc"].asString();
		userinfo->sex = root["sex"].asInt();
		userinfo->icon = root["icon"].asString();
		std::cout << "user login uid is  " << userinfo->uid << " name  is "
			<< userinfo->name << " pwd is " << userinfo->pwd << " email is " << userinfo->email << endl;
	}
	else {
		//redis��û�����ѯmysql
		//��ѯ���ݿ�
		std::shared_ptr<UserInfo> user_info = nullptr;
		user_info = MysqlMgr::GetInstance()->GetUser(uid);
		if (user_info == nullptr) {
			return false;
		}

		userinfo = user_info;

		//�����ݿ�����д��redis����
		Json::Value redis_root;
		redis_root["uid"] = uid;
		redis_root["pwd"] = userinfo->pwd;
		redis_root["name"] = userinfo->name;
		redis_root["email"] = userinfo->email;
		redis_root["nick"] = userinfo->nick;
		redis_root["desc"] = userinfo->desc;
		redis_root["sex"] = userinfo->sex;
		redis_root["icon"] = userinfo->icon;
		RedisMgr::GetInstance()->Set(base_key, redis_root.toStyledString());
	}

}

AuthFriendRsp ChatGrpcClient::NotifyAuthFriend(std::string server_ip, const AuthFriendReq& req) {
	AuthFriendRsp rsp;
	rsp.set_error(ErrorCodes::Success);

	Defer defer([&rsp, &req]() {
		rsp.set_fromuid(req.fromuid());
		rsp.set_touid(req.touid());
		});

	auto find_iter = _pools.find(server_ip);
	if (find_iter == _pools.end()) {
		return rsp;
	}

	auto& pool = find_iter->second;
	ClientContext context;
	auto stub = pool->getConnection();
	Status status = stub->NotifyAuthFriend(&context, req, &rsp);
	Defer defercon([&stub, this, &pool]() {
		pool->returnConnection(std::move(stub));
		});

	if (!status.ok()) {
		rsp.set_error(ErrorCodes::RPCFailed);
		return rsp;
	}

	return rsp;
}

TextChatMsgRsp ChatGrpcClient::NotifyTextChatMsg(std::string server_ip, const TextChatMsgReq& req, const Json::Value& rtvalue) {
    // ������Ӧ�������ó�ʼ������Ϊ�ɹ�
    TextChatMsgRsp rsp;
    rsp.set_error(ErrorCodes::Success);

    // ʹ�� Defer ģʽȷ���ں�������ǰ�����Ӧ����
    Defer defer([&rsp, &req]() {
        rsp.set_fromuid(req.fromuid()); // ���÷����� UID
        rsp.set_touid(req.touid());     // ���ý����� UID
        
        // ���������е���Ϣ��������ӵ���Ӧ��
        for (const auto& text_data : req.textmsgs()) {
            TextChatData* new_msg = rsp.add_textmsgs(); // �������Ϣ
            new_msg->set_msgid(text_data.msgid());      // ������Ϣ ID
            new_msg->set_msgcontent(text_data.msgcontent()); // ������Ϣ����
        }
    });

    // �����ӳ��в���ָ���ķ����� IP
    auto find_iter = _pools.find(server_ip);
    if (find_iter == _pools.end()) {
        // ���û���ҵ���Ӧ�����ӳأ����ص�ǰ��Ӧ
        return rsp;
    }

    // ��ȡ���ӳ��е�����
    auto& pool = find_iter->second;
    ClientContext context; // ���� gRPC �ͻ���������
    auto stub = pool->getConnection(); // ��ȡ����

    // ���� gRPC ���󲢻�ȡ��Ӧ״̬
    Status status = stub->NotifyTextChatMsg(&context, req, &rsp);

    // ʹ�� Defer ģʽȷ���ں�������ǰ�黹����
    Defer defercon([&stub, this, &pool]() {
        pool->returnConnection(std::move(stub)); // �黹���ӵ����ӳ�
    });

    // ��� gRPC �����Ƿ�ɹ�
    if (!status.ok()) {
        rsp.set_error(ErrorCodes::RPCFailed); // ���ô�����Ϊ RPC ʧ��
        return rsp; // ������Ӧ
    }

    return rsp; // ���سɹ�����Ӧ
}
// �ú�����ʵ��ּ��ͨ�� gRPC ��ָ���ķ����������ı�������Ϣ����ȷ���ڴ������������ȷ������Ӧ��