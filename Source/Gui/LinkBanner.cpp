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

#include "LinkBanner.h"

#include <algorithm>

#include <QCursor>
#include <QEasingCurve>
#include <QGuiApplication>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QScreen>

#include "MainWindowPresentation.h"
#include "Theme.h"
#include "Widget/DeviceImage.h"

using namespace std::chrono_literals;

namespace Gui {

namespace {

constexpr int kButtonDiameter = 36;
constexpr int kGlyphBox = 30;
constexpr int kSidePadding = 14;

} // namespace

LinkBanner::LinkBanner(QWidget *parent) : QWidget{parent}
{
    setWindowFlags(Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_ShowWithoutActivating);
    setProperty(Theme::Manager::kSkipDwmProperty, true);
    setFixedSize(_bannerSize);
    setMouseTracking(true);

    _dismissTimer->setObjectName("bannerDismissTimer");
    _dismissTimer->setSingleShot(true);
    connect(_dismissTimer, &QTimer::timeout, this, &LinkBanner::Dismiss);
    _armTimer->setObjectName("bannerArmTimer");
    _armTimer->setSingleShot(true);

    _slide.setDuration(350);
    connect(&_slide, &QPropertyAnimation::finished, this, [this] {
        if (!_shown) {
            hide();
        }
    });
}

void LinkBanner::UpdateState(const Core::AirPods::State &state)
{
    if (_glyphModel != state.model) {
        LoadGlyph(state.model);
    }
    if (!IsArmed() || state.isConnected || !state.isLinked.has_value()) {
        return;
    }
    if (state.caseBox.isBothPodsInCase) {
        _armTimer->stop(); // just put away, not moved
        return;
    }
    if (*state.isLinked) {
        _armTimer->stop();
        ShowMovedAway();
    }
}

void LinkBanner::ArmMovedAway()
{
    _armTimer->start(8s);
}

bool LinkBanner::IsArmed() const
{
    return _armTimer->isActive();
}

void LinkBanner::LoadGlyph(Core::AirPods::Model model)
{
    _glyphModel = model;
    _podGlyph = {};

    const QImage render{GetAnimationPresentation(model).FallbackResource()};
    if (render.isNull()) {
        return;
    }
    // One bud is enough of a hint; the render lists the pods before the case.
    const auto source = render.convertToFormat(QImage::Format_ARGB32);
    const int minGap = (std::max)(source.width() / 50, 4);
    const auto slices = Widget::Detail::SliceByTransparentGaps(source, minGap).slices;
    if (slices.size() >= 3) {
        const auto &pod = slices[0];
        _podGlyph = source.copy(pod.left, 0, pod.right - pod.left, source.height());
    }
    else {
        _podGlyph = source;
    }
    // Trim the transparent rows so the bud fills its box.
    int top = 0, bottom = _podGlyph.height();
    const auto rowOpaque = [&](int y) {
        const auto *line = reinterpret_cast<const QRgb *>(_podGlyph.constScanLine(y));
        for (int x = 0; x < _podGlyph.width(); ++x) {
            if (qAlpha(line[x]) > 16) {
                return true;
            }
        }
        return false;
    };
    while (top < bottom && !rowOpaque(top)) {
        ++top;
    }
    while (bottom > top && !rowOpaque(bottom - 1)) {
        --bottom;
    }
    if (bottom > top) {
        _podGlyph = _podGlyph.copy(0, top, _podGlyph.width(), bottom - top);
    }
    update();
}

QPoint LinkBanner::RestingPosition(QScreen *screen) const
{
    const auto area = screen->availableGeometry();
    return {area.center().x() - width() / 2, area.top() + _topMargin};
}

void LinkBanner::Present(
    const QString &text, bool withButton, std::optional<std::chrono::milliseconds> stay)
{
    _text = text;
    _buttonShown = withButton;
    _buttonPressed = false;
    update();

    _dismissTimer->stop();
    if (stay.has_value()) {
        _dismissTimer->start(*stay);
    }

    if (_shown && isVisible()) {
        return;
    }
    _shown = true;

    auto *screen = QGuiApplication::screenAt(QCursor::pos());
    if (screen == nullptr) {
        screen = QGuiApplication::primaryScreen();
    }
    const auto target = RestingPosition(screen);
    const QPoint start{target.x(), screen->geometry().top() - height()};

    _slide.stop();
    move(start);
    show();
    _slide.setEasingCurve(QEasingCurve::OutCubic);
    _slide.setStartValue(start);
    _slide.setEndValue(target);
    _slide.start();
}

void LinkBanner::ShowMovedAway()
{
    Present(QObject::tr("Moved to another device"), true, 8s);
}

void LinkBanner::ShowConnecting()
{
    Present(QObject::tr("Connecting…"), false, 20s);
}

void LinkBanner::ShowFailed()
{
    Present(QObject::tr("Could not connect"), true, 4s);
}

void LinkBanner::Dismiss()
{
    _dismissTimer->stop();
    if (!_shown) {
        return;
    }
    _shown = false;
    if (!isVisible()) {
        return;
    }
    _slide.stop();
    _slide.setEasingCurve(QEasingCurve::InCubic);
    _slide.setStartValue(pos());
    _slide.setEndValue(QPoint{x(), screen()->geometry().top() - height()});
    _slide.start();
}

QString LinkBanner::Text() const
{
    return _text;
}

bool LinkBanner::IsButtonShown() const
{
    return _buttonShown;
}

QRect LinkBanner::ButtonRect() const
{
    return {
        width() - kSidePadding - kButtonDiameter, (height() - kButtonDiameter) / 2, kButtonDiameter,
        kButtonDiameter};
}

void LinkBanner::paintEvent(QPaintEvent *)
{
    QPainter painter{this};
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);

    // The pill.
    QPainterPath pill;
    pill.addRoundedRect(QRectF{rect()}, height() / 2.0, height() / 2.0);
    painter.fillPath(pill, QColor{0, 0, 0});

    // Pod glyph on the left.
    const QRect glyphBox{kSidePadding + 4, (height() - kGlyphBox) / 2, kGlyphBox, kGlyphBox};
    if (!_podGlyph.isNull()) {
        const QSize fitted = _podGlyph.size().scaled(glyphBox.size(), Qt::KeepAspectRatio);
        const QRect target{
            glyphBox.x() + (glyphBox.width() - fitted.width()) / 2,
            glyphBox.y() + (glyphBox.height() - fitted.height()) / 2, fitted.width(),
            fitted.height()};
        painter.drawImage(target, _podGlyph);
    }

    // Text.
    const int textLeft = glyphBox.right() + 12;
    const int textRight = _buttonShown ? ButtonRect().left() - 10 : width() - kSidePadding - 4;
    auto font = painter.font();
    font.setPointSize(10);
    painter.setFont(font);
    painter.setPen(Qt::white);
    const QRect textRect{textLeft, 0, textRight - textLeft, height()};
    painter.drawText(
        textRect, Qt::AlignLeft | Qt::AlignVCenter,
        painter.fontMetrics().elidedText(_text, Qt::ElideRight, textRect.width()));

    if (!_buttonShown) {
        return;
    }

    // The blue "bring them back" button with a return arrow.
    const auto button = ButtonRect();
    QColor blue{0x0A, 0x84, 0xFF};
    if (_buttonPressed) {
        blue = blue.darker(130);
    }
    else if (_buttonHovered) {
        blue = blue.lighter(110);
    }
    painter.setPen(Qt::NoPen);
    painter.setBrush(blue);
    painter.drawEllipse(button);

    QPen arrowPen{Qt::white, 2.2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin};
    painter.setPen(arrowPen);
    painter.setBrush(Qt::NoBrush);
    const QPointF c = QRectF{button}.center();
    // A shaft that curls down and back, with the head pointing left: "return".
    QPainterPath arrow;
    arrow.moveTo(c.x() - 6, c.y() + 1);
    arrow.lineTo(c.x() + 3, c.y() + 1);
    arrow.arcTo(QRectF{c.x() - 1, c.y() - 7, 8, 8}, -90, 180);
    painter.drawPath(arrow);
    painter.drawLine(QPointF{c.x() - 6, c.y() + 1}, QPointF{c.x() - 2.5, c.y() - 2.5});
    painter.drawLine(QPointF{c.x() - 6, c.y() + 1}, QPointF{c.x() - 2.5, c.y() + 4.5});
}

void LinkBanner::mousePressEvent(QMouseEvent *event)
{
    if (_buttonShown && ButtonRect().contains(event->pos())) {
        _buttonPressed = true;
        update();
    }
}

void LinkBanner::mouseReleaseEvent(QMouseEvent *event)
{
    const bool hit = _buttonPressed && ButtonRect().contains(event->pos());
    _buttonPressed = false;
    update();
    if (hit) {
        Q_EMIT ReturnRequested();
    }
}

void LinkBanner::mouseMoveEvent(QMouseEvent *event)
{
    const bool hovered = _buttonShown && ButtonRect().contains(event->pos());
    if (hovered != _buttonHovered) {
        _buttonHovered = hovered;
        update();
    }
}

} // namespace Gui
