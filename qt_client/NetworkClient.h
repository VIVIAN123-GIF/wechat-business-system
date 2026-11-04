// ============================================================
// NetworkClient.h - 网络层（基于 QTcpSocket 的异步收发）
// 负责：连接服务端、组包（16字节大端头 + JSON体）、
//       拆包（自动处理粘包/半包），收到完整包后发信号
// ============================================================
#pragma once
#include <QObject>
#include <QByteArray>
#include <string>

QT_BEGIN_NAMESPACE
class QTcpSocket;
QT_END_NAMESPACE

class NetworkClient : public QObject {
    Q_OBJECT
public:
    explicit NetworkClient(QObject* parent = nullptr);

    void connectToServer(const QString& host, quint16 port);
    bool isConnected() const;
    void disconnect();

    // 发送一个完整报文（code + JSON 体）
    void sendPacket(quint32 code, const std::string& body);
    void sendPacket(quint32 code, const QByteArray& body);

signals:
    void connected();
    void disconnected();
    void connectionFailed(const QString& reason);
    // 收到一个完整报文（已剥离头部）
    void packetReceived(quint32 code, const QByteArray& body);

private slots:
    void onReadyRead();
    void onConnected();
    void onSocketError();

private:
    QTcpSocket* socket_;
    QByteArray inBuffer_;
    void tryParse();
};
