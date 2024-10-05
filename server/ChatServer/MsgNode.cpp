#include "MsgNode.h"

// RecvNode ���캯��
// ��ʼ��������Ϣ�ڵ㣬�̳��� MsgNode��������󳤶ȣ�����¼��Ϣ ID
RecvNode::RecvNode(short max_len, short msg_id)
    : MsgNode(max_len),    // ���û��� MsgNode �Ĺ��캯������ʼ����Ϣ����
    _msg_id(msg_id) {      // ��ʼ����Ϣ ID
}

// SendNode ���캯��
// ��ʼ��������Ϣ�ڵ㣬�̳��� MsgNode��������󳤶Ⱥ���Ϣ ID
SendNode::SendNode(const char* msg, short max_len, short msg_id)
    : MsgNode(max_len + HEAD_TOTAL_LEN),  // ������Ϣ�ܳ��ȣ���Ϣ�������Ϣͷ����
    _msg_id(msg_id) {                     // ��ʼ����Ϣ ID

    // �Ƚ���Ϣ ID ת��Ϊ�����ֽ��򣨴�˸�ʽ���������Ƶ����ݻ�������
    short msg_id_host = boost::asio::detail::socket_ops::host_to_network_short(msg_id);
    memcpy(_data, &msg_id_host, HEAD_ID_LEN);  // ����Ϣ ID ���Ƶ���Ϣ���ݵĿ�ͷ

    // ����Ϣ���ȣ�max_len��ת��Ϊ�����ֽ��򣬲����Ƶ����ݻ�������
    short max_len_host = boost::asio::detail::socket_ops::host_to_network_short(max_len);
    memcpy(_data + HEAD_ID_LEN, &max_len_host, HEAD_DATA_LEN);  // ������Ϣͷ��֮��

    // ��ʵ�ʵ���Ϣ���ݸ��Ƶ����ݻ������У���Ϣͷ��֮��
    memcpy(_data + HEAD_ID_LEN + HEAD_DATA_LEN, msg, max_len);
}
