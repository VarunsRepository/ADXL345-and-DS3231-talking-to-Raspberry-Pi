/*
 * mainwindow.h
 *
 * Live dashboard: subscribes to an MQTT topic and plots the pitch value
 * out of the accelerometer JSON published by the Pi.
 *
 * No broker credentials here - they are typed into the window, so nothing
 * sensitive ends up in the source or in git.
 *
 * Copyright (c) 2026 Varun
 * SPDX-License-Identifier: MIT
 */

#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QString>

#include "MQTTClient.h"

class QLineEdit;
class QLabel;
class QPlainTextEdit;
class QPushButton;
class PlotWidget;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

signals:
    // Both are emitted from the Paho callback thread. The connections are
    // queued ones, so the data crosses onto the GUI thread before any
    // widget is touched.
    void payloadArrived(QString payload);
    void connectionLost(QString reason);

private slots:
    void connectToBroker();
    void disconnectFromBroker();
    void handlePayload(QString payload);
    void handleConnectionLost(QString reason);

private:
    void buildUi();
    void log(const QString& line);
    void setConnectedState(bool isConnected);
    void teardownClient();

    // Paho wants plain function pointers, and it hands our own pointer
    // back through `context`. Using that instead of a global means no
    // `MainWindow* handle` and no friend declarations.
    static void onConnectionLost(void* context, char* cause);
    static int  onMessageArrived(void* context, char* topicName, int topicLen,
                                 MQTTClient_message* message);
    static void onDelivered(void* context, MQTTClient_deliveryToken token);

    // --- widgets (owned by Qt's parent/child tree) ---
    QLineEdit* addressEdit = nullptr;
    QLineEdit* clientIdEdit = nullptr;
    QLineEdit* userEdit = nullptr;
    QLineEdit* passwordEdit = nullptr;
    QLineEdit* topicEdit = nullptr;
    QPushButton* connectButton = nullptr;
    QPushButton* disconnectButton = nullptr;
    QLabel* statusLabel = nullptr;
    PlotWidget* plot = nullptr;
    QPlainTextEdit* logView = nullptr;

    MQTTClient client = nullptr;
    bool connected = false;
    QString subscribedTopic;
};

#endif // MAINWINDOW_H
