/*
 * mainwindow.cpp
 *
 * Copyright (c) 2026 Varun
 * SPDX-License-Identifier: MIT
 */

#include "mainwindow.h"

#include <QByteArray>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QVBoxLayout>
#include <QWidget>

#include "plotwidget.h"

namespace {

constexpr int kQoS = 1;
constexpr int kDisconnectTimeoutMs = 10000;

} // namespace

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
{
    setWindowTitle(QStringLiteral("EEN1071 - Pitch Dashboard"));
    buildUi();

    // Compile-checked connect syntax: a typo is a build error rather than
    // a runtime warning that is easy to miss.
    connect(connectButton, &QPushButton::clicked, this, &MainWindow::connectToBroker);
    connect(disconnectButton, &QPushButton::clicked, this, &MainWindow::disconnectFromBroker);

    // Default (auto) connection type: because these signals are emitted on
    // Paho's thread and this object lives on the GUI thread, Qt queues them.
    connect(this, &MainWindow::payloadArrived, this, &MainWindow::handlePayload);
    connect(this, &MainWindow::connectionLost, this, &MainWindow::handleConnectionLost);

    setConnectedState(false);
    resize(720, 560);
}

MainWindow::~MainWindow()
{
    // The previous version only deleted the UI, so the MQTT client was
    // never destroyed.
    teardownClient();
}

void MainWindow::buildUi()
{
    QWidget* central = new QWidget(this);

    addressEdit = new QLineEdit(QStringLiteral("tcp://localhost:1883"), central);
    clientIdEdit = new QLineEdit(QStringLiteral("qt-dashboard"), central);
    userEdit = new QLineEdit(central);
    passwordEdit = new QLineEdit(central);
    passwordEdit->setEchoMode(QLineEdit::Password);
    topicEdit = new QLineEdit(QStringLiteral("Sensors/ADXL345"), central);

    QFormLayout* form = new QFormLayout;
    form->addRow(QStringLiteral("Broker"), addressEdit);
    form->addRow(QStringLiteral("Client ID"), clientIdEdit);
    form->addRow(QStringLiteral("Username"), userEdit);
    form->addRow(QStringLiteral("Password"), passwordEdit);
    form->addRow(QStringLiteral("Topic"), topicEdit);

    QGroupBox* brokerBox = new QGroupBox(QStringLiteral("Broker"), central);
    brokerBox->setLayout(form);

    connectButton = new QPushButton(QStringLiteral("Connect"), central);
    disconnectButton = new QPushButton(QStringLiteral("Disconnect"), central);
    statusLabel = new QLabel(central);

    QHBoxLayout* buttons = new QHBoxLayout;
    buttons->addWidget(connectButton);
    buttons->addWidget(disconnectButton);
    buttons->addWidget(statusLabel, 1);

    plot = new PlotWidget(central);
    plot->setYRange(-90.0, 90.0);          // pitch cannot exceed this
    plot->setWindowSeconds(60.0);
    plot->setYLabel(QStringLiteral("pitch (deg)"));

    logView = new QPlainTextEdit(central);
    logView->setReadOnly(true);
    logView->setMaximumBlockCount(500);    // bounded: no unlimited growth
    logView->setCenterOnScroll(true);

    // Layouts rather than fixed geometry, so the window actually resizes
    // and the plot takes the slack.
    QVBoxLayout* root = new QVBoxLayout(central);
    root->addWidget(brokerBox);
    root->addLayout(buttons);
    root->addWidget(plot, 3);
    root->addWidget(logView, 1);

    setCentralWidget(central);
}

void MainWindow::log(const QString& line)
{
    logView->appendPlainText(line);
}

void MainWindow::setConnectedState(bool isConnected)
{
    connected = isConnected;
    connectButton->setEnabled(!isConnected);
    disconnectButton->setEnabled(isConnected);
    statusLabel->setText(isConnected ? QStringLiteral("connected")
                                     : QStringLiteral("not connected"));
}

void MainWindow::teardownClient()
{
    if (!client) {
        return;
    }
    if (connected) {
        MQTTClient_disconnect(client, kDisconnectTimeoutMs);
        connected = false;
    }
    MQTTClient_destroy(&client);
    client = nullptr;              // so a second call does nothing
}

void MainWindow::connectToBroker()
{
    if (connected) {
        return;                    // guarded: clicking twice used to leak a client
    }

    const QString address = addressEdit->text().trimmed();
    const QString clientId = clientIdEdit->text().trimmed();
    const QString topic = topicEdit->text().trimmed();

    if (address.isEmpty() || clientId.isEmpty() || topic.isEmpty()) {
        log(QStringLiteral("Broker, client ID and topic are all required"));
        return;
    }

    teardownClient();              // clears any client left from a failed attempt

    const QByteArray addressUtf8 = address.toUtf8();
    const QByteArray clientIdUtf8 = clientId.toUtf8();

    int rc = MQTTClient_create(&client, addressUtf8.constData(), clientIdUtf8.constData(),
                               MQTTCLIENT_PERSISTENCE_NONE, nullptr);
    if (rc != MQTTCLIENT_SUCCESS) {
        log(QString("Create failed: %1 (%2)").arg(rc).arg(MQTTClient_strerror(rc)));
        client = nullptr;
        return;
    }

    rc = MQTTClient_setCallbacks(client, this,
                                &MainWindow::onConnectionLost,
                                &MainWindow::onMessageArrived,
                                &MainWindow::onDelivered);
    if (rc != MQTTCLIENT_SUCCESS) {
        log(QString("Could not set callbacks: %1 (%2)").arg(rc).arg(MQTTClient_strerror(rc)));
        teardownClient();
        return;
    }

    // These QByteArrays must outlive the connect call, because the options
    // struct holds raw pointers into them.
    const QByteArray userUtf8 = userEdit->text().toUtf8();
    const QByteArray passwordUtf8 = passwordEdit->text().toUtf8();

    MQTTClient_connectOptions opts = MQTTClient_connectOptions_initializer;
    opts.keepAliveInterval = 20;
    opts.cleansession = 1;
    if (!userUtf8.isEmpty()) {
        opts.username = userUtf8.constData();
        opts.password = passwordUtf8.constData();
    }

    if ((rc = MQTTClient_connect(client, &opts)) != MQTTCLIENT_SUCCESS) {
        log(QString("Connect failed: %1 (%2)").arg(rc).arg(MQTTClient_strerror(rc)));
        teardownClient();
        return;
    }

    const QByteArray topicUtf8 = topic.toUtf8();
    if ((rc = MQTTClient_subscribe(client, topicUtf8.constData(), kQoS)) != MQTTCLIENT_SUCCESS) {
        log(QString("Subscribe failed: %1 (%2)").arg(rc).arg(MQTTClient_strerror(rc)));
        teardownClient();
        return;
    }

    subscribedTopic = topic;
    plot->clear();
    setConnectedState(true);
    log(QString("Connected to %1, subscribed to %2 at QoS %3")
            .arg(address, topic).arg(kQoS));
}

void MainWindow::disconnectFromBroker()
{
    if (!connected) {
        return;
    }

    if (!subscribedTopic.isEmpty()) {
        MQTTClient_unsubscribe(client, subscribedTopic.toUtf8().constData());
    }

    teardownClient();
    setConnectedState(false);
    log(QStringLiteral("Disconnected"));
}

void MainWindow::handlePayload(QString payload)
{
    log(payload);

    // Qt's own JSON parser: one less third-party dependency than nlohmann.
    QJsonParseError error{};
    const QJsonDocument document = QJsonDocument::fromJson(payload.toUtf8(), &error);
    if (document.isNull() || !document.isObject()) {
        log(QString("  (not JSON: %1)").arg(error.errorString()));
        return;
    }

    // Expected shape: { "d": { "pitch": <number>, ... } }
    const QJsonValue pitch = document.object().value(QStringLiteral("d"))
                                     .toObject().value(QStringLiteral("pitch"));
    if (!pitch.isDouble()) {
        log(QStringLiteral("  (no numeric d.pitch in this message)"));
        return;
    }

    plot->addSample(pitch.toDouble());
    statusLabel->setText(QString("connected - pitch %1 deg")
                             .arg(pitch.toDouble(), 0, 'f', 2));
}

void MainWindow::handleConnectionLost(QString reason)
{
    log(QString("** connection lost: %1 **").arg(reason));

    // Back to a usable state so Connect can be pressed again, rather than
    // leaving the buttons claiming we are still connected.
    teardownClient();
    setConnectedState(false);
}

// ---------------------------------------------------------------------
// Paho callbacks. These run on the client's own thread, so they must not
// touch widgets directly - they go through payloadArrived instead.
// ---------------------------------------------------------------------

int MainWindow::onMessageArrived(void* context, char* topicName, int topicLen,
                                 MQTTClient_message* message)
{
    Q_UNUSED(topicLen);

    MainWindow* window = static_cast<MainWindow*>(context);

    // payload is not NUL-terminated, so the length is used explicitly.
    if (window && message) {
        const QString payload = QString::fromUtf8(static_cast<const char*>(message->payload),
                                                  message->payloadlen);
        emit window->payloadArrived(payload);
    }

    MQTTClient_freeMessage(&message);
    MQTTClient_free(topicName);
    return 1;                      // 1 = consumed successfully
}

void MainWindow::onConnectionLost(void* context, char* cause)
{
    MainWindow* window = static_cast<MainWindow*>(context);
    if (!window) {
        return;
    }

    // cause may be null, so it is checked rather than dereferenced. The
    // previous version did `(void)*cause;`, which dereferences it.
    const QString reason = cause ? QString::fromUtf8(cause)
                                 : QStringLiteral("unknown");
    emit window->connectionLost(reason);
}

void MainWindow::onDelivered(void* context, MQTTClient_deliveryToken token)
{
    Q_UNUSED(context);
    Q_UNUSED(token);
    // Nothing to do: this dashboard only subscribes.
}
