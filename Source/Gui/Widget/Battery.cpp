//
// AirPodsDesktop - AirPods Desktop User Experience Enhancement Program.
// Copyright (C) 2021-2022 SpriteOvO
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <https://www.gnu.org/licenses/>.
//

#include "Battery.h"

#include <QPainter>
#include <QPainterPath>

namespace Gui::Widget {

Battery::Battery(QWidget *parent) : QWidget{parent}
{
    // Add bold
    //
    auto currFont = font();
    currFont.setBold(true);
    setFont(currFont);

    setBatterySize(30, 13);
}

void Battery::paintEvent(QPaintEvent *event)
{
    QPainter painter{this};
    painter.setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing);

    if (_shape == Shape::Ring) {
        paintRing(painter);
    }
    else {
        paintBar(painter);
    }
}

void Battery::paintBar(QPainter &painter)
{
    QFontMetrics fontMetrics{this->fontMetrics()};

    qreal headWidth = getHeadWidth();

    _batteryRect =
        QRectF{_borderWidth, _borderWidth, _batterySize.width() - headWidth, _batterySize.height()};

    if (_isShowText) {
        _textRect = QRectF{
            _batteryRect.left(), _batteryRect.bottom() + _textPadding, (qreal)width(),
            (qreal)fontMetrics.height()};
    }

    _chargingRect = QRectF{
        _batteryRect.right() + headWidth + ChargingPadding, _batteryRect.top(),
        getChargingIconWidth(), _batterySize.height()};

    _headRect = QRectF{
        _batteryRect.right(), _batteryRect.bottom() / 3.0, headWidth, _batteryRect.bottom() / 3.0};

    drawBorder(painter);
    drawBackground(painter);
    drawHead(painter);
    drawChargingIcon(painter);
    drawText(painter);
}

void Battery::paintRing(QPainter &painter)
{
    // The ring fills `_batterySize` (square); the stroke scales with it so that the gauge keeps
    // its proportions at every size the callers pick.
    const qreal diameter = qMin(_batterySize.width(), _batterySize.height());
    const qreal penWidth = qMax(diameter * 0.12, 2.0);
    const QRectF ringRect{
        penWidth / 2.0 + (width() - diameter) / 2.0, penWidth / 2.0, diameter - penWidth,
        diameter - penWidth};

    drawRingTrack(painter, ringRect, penWidth);
    drawRingProgress(painter, ringRect, penWidth);
    drawRingText(painter, ringRect);
    drawRingChargingBadge(painter, ringRect, penWidth);
}

void Battery::drawRingTrack(QPainter &painter, const QRectF &rect, qreal penWidth)
{
    painter.save();
    {
        QColor track{_borderColor};
        track.setAlphaF(0.35);
        painter.setPen(QPen{track, penWidth, Qt::SolidLine, Qt::FlatCap});
        painter.setBrush(Qt::NoBrush);
        painter.drawEllipse(rect);
    }
    painter.restore();
}

void Battery::drawRingProgress(QPainter &painter, const QRectF &rect, qreal penWidth)
{
    if (_value <= _minValue || _maxValue <= _minValue) {
        return;
    }

    painter.save();
    {
        const qreal fraction =
            qBound(0.0, qreal(_value - _minValue) / qreal(_maxValue - _minValue), 1.0);
        // Qt angles are in 1/16 degree, counter-clockwise; start at 12 o'clock and go clockwise.
        constexpr int kStartAngle = 90 * 16;
        const int spanAngle = -qRound(fraction * 360.0 * 16.0);

        painter.setPen(QPen{getLevelColor(), penWidth, Qt::SolidLine, Qt::RoundCap});
        painter.setBrush(Qt::NoBrush);
        painter.drawArc(rect, kStartAngle, spanAngle);
    }
    painter.restore();
}

void Battery::drawRingText(QPainter &painter, const QRectF &rect)
{
    if (!_isShowText) {
        return;
    }

    painter.save();
    {
        // Shrink the label until "100%" fits inside the ring's hole.
        const QString text = QString{"%1%"}.arg(_value);
        const qreal inner = rect.width() * 0.72;
        QFont font = this->font();
        for (qreal size = qMax(rect.width() * 0.32, 6.0); size >= 5.0; size -= 0.5) {
            font.setPointSizeF(size);
            if (QFontMetricsF{font}.horizontalAdvance("100%") <= inner) {
                break;
            }
        }
        painter.setFont(font);
        painter.drawText(rect, Qt::AlignCenter, text);
    }
    painter.restore();
}

void Battery::drawRingChargingBadge(QPainter &painter, const QRectF &rect, qreal penWidth)
{
    if (!_isCharging) {
        return;
    }

    painter.save();
    {
        // A small bolt sits on the ring at 6 o'clock, cut out from the track so it stays legible.
        const qreal badge = qMax(penWidth * 2.2, 8.0);
        const QRectF badgeRect{
            rect.center().x() - badge / 2.0, rect.bottom() - badge / 2.0, badge, badge};

        painter.setPen(Qt::NoPen);
        painter.setBrush(palette().color(QPalette::Window));
        painter.drawEllipse(badgeRect);

        const qreal inset = badge * 0.22;
        drawChargingGlyph(painter, badgeRect.adjusted(inset, inset * 0.6, -inset, -inset * 0.6));
    }
    painter.restore();
}

void Battery::drawChargingGlyph(QPainter &painter, const QRectF &rect)
{
    // Same silhouette as `drawChargingIcon`, expressed in unit coordinates so it can be
    // placed anywhere.
    QPainterPath path;
    path.moveTo(0.62, 0.0);
    path.lineTo(0.0, 0.58);
    path.lineTo(0.42, 0.58);
    path.lineTo(0.30, 1.0);
    path.lineTo(1.0, 0.40);
    path.lineTo(0.56, 0.40);
    path.closeSubpath();

    QTransform transform;
    transform.translate(rect.left(), rect.top());
    transform.scale(rect.width(), rect.height());

    painter.setPen(Qt::NoPen);
    painter.fillPath(transform.map(path), QBrush{_chargingIconColor});
}

QColor Battery::getLevelColor() const
{
    return _value > _alarmValue ? _normalColor : _alarmColor;
}

void Battery::drawBorder(QPainter &painter)
{
    painter.save();
    {
        painter.setPen(QPen{_borderColor, _borderWidth});
        painter.setBrush(Qt::NoBrush);
        painter.drawRoundedRect(_batteryRect, _borderRadius, _borderRadius);
    }
    painter.restore();
}

void Battery::drawBackground(QPainter &painter)
{
    if (_value <= _minValue) {
        return;
    }

    painter.save();
    {
        qreal margin = /*qMin(width(), height()) / 20.0*/ 1.0;
        qreal unit = (_batteryRect.width() - (margin * 2.0)) / 100.0;
        qreal width = _value * unit;

        QRectF rect{
            _batteryRect.left() + margin, _batteryRect.top() + margin, width,
            _batteryRect.height() - margin * 2};

        painter.setPen(Qt::NoPen);
        painter.setBrush(QBrush{_value /*>=*/ > _alarmValue ? _normalColor : _alarmColor});
        painter.drawRoundedRect(rect, _backgroundRadius, _backgroundRadius);
    }
    painter.restore();
}

void Battery::drawHead(QPainter &painter)
{
    painter.save();
    {
        painter.setPen(Qt::NoPen);
        painter.setBrush(QBrush{_borderColor});
        painter.drawRoundedRect(_headRect, _headRadius, _headRadius);
    }
    painter.restore();
}

void Battery::drawChargingIcon(QPainter &painter)
{
    if (!_isCharging) {
        return;
    }

    painter.save();
    {
        constexpr qreal innerPadding = 2.0;

        QPainterPath path;

        QPointF pointStart{_chargingRect.right() - innerPadding, _chargingRect.top()};
        QPointF pointL1{
            _chargingRect.left(),
            _chargingRect.bottom() - _chargingRect.height() / 2.0 + innerPadding / 2.0};
        QPointF pointR1{
            _chargingRect.left() + _chargingRect.width() / 2.0 + innerPadding / 2.0,
            _chargingRect.bottom() - _chargingRect.height() / 2.0 - innerPadding / 2.0};
        QPointF pointL2{
            _chargingRect.left() + _chargingRect.width() / 2.0 - innerPadding / 2.0,
            _chargingRect.bottom() - _chargingRect.height() / 2.0 + innerPadding / 2.0};
        QPointF pointR2{
            _chargingRect.right(),
            _chargingRect.bottom() - _chargingRect.height() / 2.0 - innerPadding / 2.0};
        QPointF pointEnd{_chargingRect.left() + innerPadding, _chargingRect.bottom()};

        path.moveTo(pointStart);
        path.lineTo(pointL1);
        path.lineTo(pointL2);
        path.lineTo(pointEnd);
        path.lineTo(pointR2);
        path.lineTo(pointR1);
        path.lineTo(pointStart);

        painter.setPen(Qt::NoPen);
        painter.fillPath(path, QBrush{_chargingIconColor});
    }
    painter.restore();
}

void Battery::drawText(QPainter &painter)
{
    if (!_isShowText) {
        return;
    }

    painter.save();
    {
        QTextOption textOption;
        textOption.setWrapMode(QTextOption::NoWrap);

        painter.drawText(_textRect, QString{"%1%"}.arg(_value), textOption);
    }
    painter.restore();
}

auto Battery::getShape() const -> Shape
{
    return _shape;
}

void Battery::setShape(Shape shape)
{
    if (_shape == shape) {
        return;
    }
    _shape = shape;
    updateFixedSize();
}

auto Battery::getMinValue() const -> ValueType
{
    return _minValue;
}

auto Battery::getMaxValue() const -> ValueType
{
    return _maxValue;
}

auto Battery::getAlarmValue() const -> ValueType
{
    return _alarmValue;
}

auto Battery::getValue() const -> ValueType
{
    return _value;
}

qreal Battery::getBorderWidth() const
{
    return _borderWidth;
}

qreal Battery::getBorderRadius() const
{
    return _borderRadius;
}

qreal Battery::getBackgroundRadius() const
{
    return _backgroundRadius;
}

qreal Battery::getHeadRadius() const
{
    return _headRadius;
}

QColor Battery::getBorderColor() const
{
    return _borderColor;
}

QColor Battery::getAlarmColor() const
{
    return _alarmColor;
}

QColor Battery::getNormalColor() const
{
    return _normalColor;
}

QColor Battery::getChargingIconColor() const
{
    return _chargingIconColor;
}

bool Battery::isCharging() const
{
    return _isCharging;
}

bool Battery::isShowText() const
{
    return _isShowText;
}

qreal Battery::getTextPadding() const
{
    return _textPadding;
}

// QSize Battery::sizeHint() const
//{
//    return QSize{30, 15};
//}

// QSize Battery::minimumSizeHint() const
//{
//    return QSize{30, 15};
//}

void Battery::setRange(ValueType minValue, ValueType maxValue)
{
    if (minValue >= maxValue) {
        return;
    }

    _minValue = minValue;
    _maxValue = maxValue;

    setValue(_value);
    update();
}

void Battery::setMinValue(ValueType value)
{
    setRange(value, _maxValue);
}

void Battery::setMaxValue(ValueType value)
{
    setRange(_minValue, value);
}

void Battery::setAlarmValue(ValueType value)
{
    if (_alarmValue == value) {
        return;
    }
    _alarmValue = value;
    update();
}

void Battery::setValue(ValueType value)
{
    value = std::clamp(value, _minValue, _maxValue);
    if (_value == value) {
        return;
    }

    _value = value;
    update();

    Q_EMIT valueChanged(_value);
}

void Battery::setBorderWidth(qreal value)
{
    if (_borderWidth == value) {
        return;
    }
    _borderWidth = value;
    update();
}

void Battery::setBorderRadius(qreal value)
{
    if (_borderRadius == value) {
        return;
    }
    _borderRadius = value;
    update();
}

void Battery::setBackgroundRadius(qreal value)
{
    if (_backgroundRadius == value) {
        return;
    }
    _backgroundRadius = value;
    update();
}

void Battery::setHeadRadius(qreal value)
{
    if (_headRadius == value) {
        return;
    }
    _headRadius = value;
    update();
}

void Battery::setBorderColor(const QColor &value)
{
    if (_borderColor == value) {
        return;
    }
    _borderColor = value;
    update();
}

void Battery::setAlarmColor(const QColor &value)
{
    if (_alarmColor == value) {
        return;
    }
    _alarmColor = value;
    update();
}

void Battery::setNormalColor(const QColor &value)
{
    if (_normalColor == value) {
        return;
    }
    _normalColor = value;
    update();
}

void Battery::setChargingIconColor(const QColor &value)
{
    if (_chargingIconColor == value) {
        return;
    }
    _chargingIconColor = value;
    update();
}

void Battery::setCharging(bool value)
{
    if (_isCharging == value) {
        return;
    }
    _isCharging = value;
    update();

    Q_EMIT chargingStateChanged(_isCharging);
}

void Battery::setShowText(bool value)
{
    if (_isShowText == value) {
        return;
    }
    _isShowText = value;
    update();
}

void Battery::setTextPadding(qreal value)
{
    if (_textPadding == value) {
        return;
    }
    _textPadding = value;
    update();
}

void Battery::setBatterySize(int width, int height)
{
    _batterySize = QSizeF{(qreal)width, (qreal)height};
    updateFixedSize();
}

void Battery::updateFixedSize()
{
    if (_shape == Shape::Ring) {
        const int diameter = qRound(qMin(_batterySize.width(), _batterySize.height()));
        setFixedSize(diameter, diameter);
    }
    else {
        QFontMetrics fontMetrics{this->fontMetrics()};
        setFixedSize(
            qRound(_batterySize.width() + getChargingIconWidth() + getHeadWidth() + ChargingPadding),
            qRound(_batterySize.height() + (_isShowText ? (fontMetrics.height() + _textPadding) : 0)));
    }
    update();
}

qreal Battery::getHeadWidth() const
{
    return qMax(_batterySize.width() / 15.0, 3.0);
}

qreal Battery::getChargingIconWidth() const
{
    return (_batterySize.height()) / 2.0;
}
} // namespace Gui::Widget
