/****************************************************************************
**
** Copyright (C) 2019 The Qt Company Ltd.
** Contact: https://www.qt.io/licensing/
**
** This file is part of the QtHttpServer module of the Qt Toolkit.
**
** $QT_BEGIN_LICENSE:GPL$
** Commercial License Usage
** Licensees holding valid commercial Qt licenses may use this file in
** accordance with the commercial license agreement provided with the
** Software or, alternatively, in accordance with the terms contained in
** a written agreement between you and The Qt Company. For licensing terms
** and conditions see https://www.qt.io/terms-conditions. For further
** information use the contact form at https://www.qt.io/contact-us.
**
** GNU General Public License Usage
** Alternatively, this file may be used under the terms of the GNU
** General Public License version 3 or (at your option) any later version
** approved by the KDE Free Qt Foundation. The licenses are as published by
** the Free Software Foundation and appearing in the file LICENSE.GPL3
** included in the packaging of this file. Please review the following
** information to ensure the GNU General Public License requirements will
** be met: https://www.gnu.org/licenses/gpl-3.0.html.
**
** $QT_END_LICENSE$
**
****************************************************************************/

#include <qhttpserverresponder.h>
#include <qhttpserverrequest.h>
#include <qhttpserverresponder_p.h>
#include <qhttpserverrequest_p.h>
#include <QtCore/qjsondocument.h>
#include <QtCore/qloggingcategory.h>
#include <QtCore/qtimer.h>
#include <QtNetwork/qtcpsocket.h>
#include <map>
#include <memory>

#include <http_parser.h>

// https://www.w3.org/Protocols/rfc2616/rfc2616-sec10.html
static const std::map<QHttpServerResponder::StatusCode, QByteArray> statusString {
#define XX(num, name, string) { static_cast<QHttpServerResponder::StatusCode>(num), QByteArrayLiteral(#string) },
  HTTP_STATUS_MAP(XX)
#undef XX
};

static const QByteArray contentTypeString(QByteArrayLiteral("Content-Type"));
static const QByteArray contentLengthString(QByteArrayLiteral("Content-Length"));


template <qint64 BUFFERSIZE = 1 * 1024 * 1024>
struct IOChunkedTransfer
{
    // TODO This is not the fastest implementation, as it does read & write
    // in a sequential fashion, but these operation could potentially overlap.
    // TODO Can we implement it without the buffer? Direct write to the target buffer
    // would be great.

    const qint64 bufferSize = BUFFERSIZE;
    char *buffer = nullptr;
    qint64 beginIndex = -1;
    qint64 endIndex = -1;
    QPointer<QIODevice> source;
    const QPointer<QIODevice> sink;
    const QMetaObject::Connection bytesWrittenConnection;
    bool writeReady = true;
    IOChunkedTransfer(QIODevice *input, QIODevice *output) :
        source(input),
        sink(output),
        bytesWrittenConnection(QObject::connect(sink.data(), &QIODevice::bytesWritten, [this] () {
              writeReady = true;
              writeToOutput();
        }))
    {
        Q_ASSERT(!source->atEnd());  // TODO error out
        buffer = static_cast<char *>(malloc(std::size_t(bufferSize)));
        QObject::connect(sink.data(), &QObject::destroyed, source.data(), &QObject::deleteLater);
        QObject::connect(source.data(), &QObject::destroyed, [this] () {
            delete this;
        });
        readFromInput();
    }

    ~IOChunkedTransfer()
    {
        free(buffer);
        QObject::disconnect(bytesWrittenConnection);
    }

    inline bool isBufferEmpty()
    {
        Q_ASSERT(beginIndex <= endIndex);
        return beginIndex == endIndex;
    }

    void readFromInput()
    {
        if (!isBufferEmpty()) // We haven't consumed all the data yet.
            return;
        beginIndex = 0;
        endIndex = source->read(buffer, bufferSize);
        if (endIndex < 0) {
            endIndex = beginIndex; // Mark the buffer as empty
            qWarning("Error reading chunk: %s", qPrintable(source->errorString()));
        } else if (endIndex) {
            memset(buffer + endIndex, 0, std::size_t(bufferSize) - std::size_t(endIndex));
            writeToOutput();
        }
    }

    void writeToOutput()
    {
        if (isBufferEmpty() || !writeReady)
            return;

        const auto writtenBytes = sink->write(buffer + beginIndex, endIndex);
        writeReady = false;

        if (writtenBytes < 0) {
            qWarning("Error writing chunk: %s", qPrintable(sink->errorString()));
            return;
        }
        beginIndex += writtenBytes;
        if (isBufferEmpty()) {
            if (source->bytesAvailable())
                QTimer::singleShot(0, source.data(), [this]() { readFromInput(); });
            else if (source->atEnd()) // Finishing
                source->deleteLater();
        }
    }
};






/*!
    Constructs a QHttpServerResponder using the request \a request
    and the socket \a socket.
*/
QHttpServerResponder::QHttpServerResponder(const QHttpServerRequest &request,
                                           QTcpSocket *socket) :
    d_ptr(new QHttpServerResponderPrivate(request, socket))
{
    Q_ASSERT(socket);
}

/*!
    Move-constructs a QHttpServerResponder instance, making it point
    at the same object that \a other was pointing to.
*/
QHttpServerResponder::QHttpServerResponder(QHttpServerResponder &&other) :
    d_ptr(other.d_ptr.take())
{}

/*!
    Destroys a QHttpServerResponder.
*/
QHttpServerResponder::~QHttpServerResponder()
{}

/*!
    Answers a request with an HTTP status code \a status and a
    MIME type \a mimeType. The I/O device \a data provides the body
    of the response. If \a data is sequential, the body of the
    message is sent in chunks: otherwise, the function assumes all
    the content is available and sends it all at once but the read
    is done in chunks.

    \note This function takes the ownership of \a data.
*/
void QHttpServerResponder::write(QIODevice *data,
                                 const QByteArray &mimeType,
                                 StatusCode status)
{
    Q_D(QHttpServerResponder);
    Q_ASSERT(d->socket);
    QScopedPointer<QIODevice, QScopedPointerDeleteLater> input(data);

    input->setParent(nullptr);
    if (!input->isOpen()) {
        if (!input->open(QIODevice::ReadOnly)) {
            // TODO Add developer error handling
            qDebug("500: Could not open device %s", qPrintable(input->errorString()));
            write(StatusCode::InternalServerError);
            return;
        }
    } else if (!(input->openMode() & QIODevice::ReadOnly)) {
        // TODO Add developer error handling
        qDebug() << "500: Device is opened in a wrong mode" << input->openMode();
        write(StatusCode::InternalServerError);
        return;
    }

    if (!d->socket->isOpen()) {
        qWarning("Cannot write to socket. It's disconnected");
        return;
    }

    d->writeStatusLine(status);

    if (!input->isSequential()) // Non-sequential QIODevice should know its data size
        d->addHeader(contentLengthString, QByteArray::number(input->size()));

    d->addHeader(contentTypeString, mimeType);

    d->writeHeaders();
    d->socket->write("\r\n");

    if (input->atEnd()) {
        qDebug("No more data available.");
        return;
    }

    // input takes ownership of the IOChunkedTransfer pointer inside his constructor
    new IOChunkedTransfer<>(input.take(), d->socket);
}

/*!
    Answers a request with an HTTP status code \a status, a
    MIME type \a mimeType and a body \a data.
*/
void QHttpServerResponder::write(const QByteArray &data,
                                 const QByteArray &mimeType,
                                 StatusCode status)
{
    Q_D(QHttpServerResponder);
    d->writeStatusLine(status);
    addHeaders(contentTypeString, mimeType,
               contentLengthString, QByteArray::number(data.size()));
    d->writeHeaders();
    d->writeBody(data);
}

/*!
    Answers a request with an HTTP status code \a status, and JSON
    document \a document.
*/
void QHttpServerResponder::write(const QJsonDocument &document, StatusCode status)
{
    write(document.toJson(), QByteArrayLiteral("text/json"), status);
}

/*!
    Answers a request with an HTTP status code \a status.
*/
void QHttpServerResponder::write(StatusCode status)
{
    write(QByteArray(), QByteArrayLiteral("application/x-empty"), status);
}

/*!
    Returns the socket used.
*/
QTcpSocket *QHttpServerResponder::socket() const
{
    Q_D(const QHttpServerResponder);
    return d->socket;
}

bool QHttpServerResponder::addHeader(const QByteArray &key, const QByteArray &value)
{
    Q_D(QHttpServerResponder);
    return d->addHeader(key, value);
}

void QHttpServerResponderPrivate::writeStatusLine(StatusCode status,
                                                  const QPair<quint8, quint8> &version) const
{
    Q_ASSERT(socket->isOpen());
    socket->write("HTTP/");
    socket->write(QByteArray::number(version.first));
    socket->write(".");
    socket->write(QByteArray::number(version.second));
    socket->write(" ");
    socket->write(QByteArray::number(quint32(status)));
    socket->write(" ");
    socket->write(statusString.at(status));
    socket->write("\r\n");
}

void QHttpServerResponderPrivate::writeHeader(const QByteArray &header,
                                              const QByteArray &value) const
{
    socket->write(header);
    socket->write(": ");
    socket->write(value);
    socket->write("\r\n");
}

void QHttpServerResponderPrivate::writeHeaders() const
{
    for (const auto &pair : qAsConst(headers()))
        writeHeader(pair.first, pair.second);
}

void QHttpServerResponderPrivate::writeBody(const QByteArray &body) const
{
    Q_ASSERT(socket->isOpen());
    socket->write("\r\n");
    socket->write(body);
}

QT_END_NAMESPACE
