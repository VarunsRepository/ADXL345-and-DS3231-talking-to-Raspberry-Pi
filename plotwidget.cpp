/*
 * plotwidget.cpp
 *
 * Copyright (c) 2026 Varun
 * SPDX-License-Identifier: MIT
 */

#include "plotwidget.h"

#include <QFontMetrics>
#include <QPainter>
#include <QPaintEvent>
#include <QPen>
#include <QPolygonF>

namespace {

constexpr int kMarginLeft = 54;
constexpr int kMarginRight = 10;
constexpr int kMarginTop = 10;
constexpr int kMarginBottom = 24;
constexpr int kHorizontalGridLines = 6;

} // namespace

PlotWidget::PlotWidget(QWidget* parent)
    : QWidget(parent)
{
    clock.start();
    setMinimumHeight(180);
    setAutoFillBackground(true);
}

void PlotWidget::addSample(double value)
{
    Sample sample;
    sample.seconds = static_cast<double>(clock.elapsed()) / 1000.0;
    sample.value = value;
    samples.append(sample);

    // Discard everything older than the visible window.
    const double cutoff = sample.seconds - windowSeconds;
    int stale = 0;
    while (stale < samples.size() && samples.at(stale).seconds < cutoff) {
        ++stale;
    }
    if (stale > 0) {
        samples.remove(0, stale);
    }

    update();   // QWidget::update(): schedules a repaint
}

void PlotWidget::setYRange(double minimum, double maximum)
{
    if (maximum > minimum) {
        yMin = minimum;
        yMax = maximum;
        update();
    }
}

void PlotWidget::setWindowSeconds(double seconds)
{
    if (seconds > 0.0) {
        windowSeconds = seconds;
        update();
    }
}

void PlotWidget::setYLabel(const QString& label)
{
    yLabel = label;
    update();
}

void PlotWidget::clear()
{
    samples.clear();
    update();
}

QPointF PlotWidget::toPixel(const Sample& sample, const QRectF& area, double tNow) const
{
    // x: now at the right edge, (now - windowSeconds) at the left
    const double age = tNow - sample.seconds;
    const double x = area.right() - (age / windowSeconds) * area.width();

    // y: yMax at the top
    const double clamped = qBound(yMin, sample.value, yMax);
    const double fraction = (clamped - yMin) / (yMax - yMin);
    const double y = area.bottom() - fraction * area.height();

    return QPointF(x, y);
}

void PlotWidget::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    const QRectF area(kMarginLeft,
                      kMarginTop,
                      qMax(1, width() - kMarginLeft - kMarginRight),
                      qMax(1, height() - kMarginTop - kMarginBottom));

    painter.fillRect(rect(), palette().base());

    // ---- grid and y labels -------------------------------------------
    const QFontMetrics metrics(painter.font());
    painter.setPen(QPen(palette().mid().color(), 1, Qt::DotLine));

    for (int i = 0; i <= kHorizontalGridLines; ++i) {
        const double fraction = static_cast<double>(i) / kHorizontalGridLines;
        const double y = area.bottom() - fraction * area.height();
        const double value = yMin + fraction * (yMax - yMin);

        painter.drawLine(QPointF(area.left(), y), QPointF(area.right(), y));

        const QString label = QString::number(value, 'f', 0);
        painter.save();
        painter.setPen(palette().text().color());
        painter.drawText(QPointF(area.left() - metrics.horizontalAdvance(label) - 6,
                                 y + metrics.ascent() / 2.0),
                         label);
        painter.restore();
    }

    // ---- frame --------------------------------------------------------
    painter.setPen(QPen(palette().shadow().color(), 1));
    painter.drawRect(area);

    // ---- axis captions ------------------------------------------------
    painter.setPen(palette().text().color());
    painter.drawText(QPointF(area.left(), area.bottom() + metrics.height() + 4),
                     QString("-%1 s").arg(windowSeconds, 0, 'f', 0));
    painter.drawText(QPointF(area.right() - metrics.horizontalAdvance("now"),
                             area.bottom() + metrics.height() + 4),
                     QStringLiteral("now"));
    painter.drawText(QPointF(area.left() + 4, area.top() + metrics.ascent()), yLabel);

    if (samples.size() < 2) {
        return;                       // nothing to join up yet
    }

    // ---- the trace ----------------------------------------------------
    const double tNow = static_cast<double>(clock.elapsed()) / 1000.0;

    QPolygonF trace;
    trace.reserve(samples.size());
    for (const Sample& sample : samples) {
        const QPointF point = toPixel(sample, area, tNow);
        if (point.x() >= area.left()) {
            trace.append(point);
        }
    }

    if (trace.size() >= 2) {
        painter.setPen(QPen(palette().highlight().color(), 2));
        painter.setClipRect(area);
        painter.drawPolyline(trace);
        painter.setClipping(false);
    }
}
