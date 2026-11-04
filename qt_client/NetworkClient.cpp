#include "NetworkClient.h"
#include <QTcpSocket>
#include <QDataStream>
#include <QAbstractSocket>

static constexpr quint32 kMagic   = 0x4D495F53;  // "MI_S"
static constexpr quint32 kVersion = 0x00000001;
static constexpr int     kHeaderSize = 16;

NetworkClient::NetworkClient(QObject* parent)
    : QObject(parent), socket_(new QTcpSocket(this)) {
    connect(socket_, &QTcpSocket::connected, this, &NetworkClient::onConnected);
    connect(socket_, &QTcpSocket::readyRead, this, &NetworkClient::onReadyRead);
    connect(socket_, &QTcpSocket::disconnected, this, &NetworkClient::disconnected);
    connect(socket_, &QAbstractSocket::errorOccurred,
            this, &NetworkClient::onSocketError);
}

void NetworkClient::connectToServer(const QString& host, quint16 port) {
    socket_->abort();
    inBuffer_.clear();
    socket_->connectToHost(host, port);
}

void NetworkClient::disconnect() {
    socket_->disconnectFromHost();
}

bool NetworkClient::isConnected() const {
    return socket_->state() == QAbstractSocket::ConnectedState;
}

void NetworkClient::onConnected() {
    emit connected();
}

void NetworkClient::onSocketError() {
    // 连接建立后的中途断开也会触发，这里只把错误信息上报给 UI 提示
    emit connectionFailed(socket_->errorString());
}

void NetworkClient::sendPacket(quint32 code, const std::string& body) {
    sendPacket(code, QByteArray::fromStdString(body));
}

void NetworkClient::sendPacket(quint32 code, const QByteArray& body) {
    QByteArray packet;
    QDataStream out(&packet, QIODevice::WriteOnly);
    out.setByteOrder(QDataStream::BigEndian);
    out << kMagic << kVersion << (quint32)code << (quint32)body.size();
    packet.append(body);
    socket_->write(packet);
    socket_->flush();
}

void NetworkClient::onReadyRead() {
    inBuffer_.append(socket_->readAll());
    tryParse();
}

// 循环解析：缓冲区里可能同时有 0 个、1 个或多个完整报文（粘包）
void NetworkClient::tryParse() {
    while (inBuffer_.size() >= kHeaderSize) {
        QDataStream head(inBuffer_.left(kHeaderSize));
        head.setByteOrder(QDataStream::BigEndian);
        quint32 magic, version, code, length;
        head >> magic >> version >> code >> length;

        if (magic != kMagic) {  // 协议不同步，丢弃缓冲区
            inBuffer_.clear();
            return;
        }
        if (inBuffer_.size() < kHeaderSize + (int)length) {
            break;  // 报文体尚未到齐（半包），等待更多数据
        }
        QByteArray body = inBuffer_.mid(kHeaderSize, length);
        inBuffer_.remove(0, kHeaderSize + length);
        emit packetReceived(code, body);
    }
}
