/*
 * main.cpp
 *
 * Entry point for the EEN1071 pitch dashboard.
 *
 * Copyright (c) 2026 Varun
 * SPDX-License-Identifier: MIT
 */

#include <QApplication>

#include "mainwindow.h"

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);

    MainWindow window;
    window.show();

    return app.exec();
}
