/*
 * plotwidget.h
 *
 * A rolling strip-chart of one value against time, drawn with QPainter.
 *
 * This replaces QCustomPlot. The dashboard needs one line on one pair of
 * axes, which is a fraction of what that library does - and dropping it
 * removes 1.5 MB of vendored GPL-3.0 code, so this project can stay MIT.
 *
 * Copyright (c) 2026 Varun
 * SPDX-License-Identifier: MIT
 */

#ifndef PLOTWIDGET_H
#define PLOTWIDGET_H

#include <QElapsedTimer>
#include <QString>
#include <QVector>
#include <QWidget>

class PlotWidget : public QWidget {
    Q_OBJECT

public:
    explicit PlotWidget(QWidget* parent = nullptr);

    // Appends a sample, timestamped now, and drops anything that has
    // scrolled off the left edge - so memory stays bounded however long
    // the dashboard runs.
    void addSample(double value);

    void setYRange(double minimum, double maximum);
    void setWindowSeconds(double seconds);
    void setYLabel(const QString& label);
    void clear();

    bool hasData() const { return !samples.isEmpty(); }
    double latest() const { return samples.isEmpty() ? 0.0 : samples.last().value; }

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    struct Sample {
        double seconds = 0.0;   // since the widget was constructed
        double value = 0.0;
    };

    // Maps a sample to a pixel inside the plotting area.
    QPointF toPixel(const Sample& sample, const QRectF& area, double tNow) const;

    QVector<Sample> samples;

    // QElapsedTimer is monotonic. QTime::currentTime() was the previous
    // timebase, and it jumps backwards at midnight.
    QElapsedTimer clock;

    double yMin = -90.0;
    double yMax = 90.0;
    double windowSeconds = 60.0;
    QString yLabel = QStringLiteral("value");
};

#endif // PLOTWIDGET_H
