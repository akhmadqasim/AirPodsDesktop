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

#include "FadeOverlay.h"

#include <QEasingCurve>
#include <QPainter>

namespace Gui::Widget {

FadeOverlay::FadeOverlay(QWidget *parent) : QWidget{parent}
{
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setAttribute(Qt::WA_NoSystemBackground);
    _fade.setStartValue(1.0);
    _fade.setEndValue(0.0);
    _fade.setDuration(800);
    _fade.setEasingCurve(QEasingCurve::InOutSine);
    connect(&_fade, &QVariantAnimation::valueChanged, this, [this] { update(); });
    connect(&_fade, &QVariantAnimation::finished, this, [this] {
        _snapshot = {};
        hide();
    });
    hide();
}

void FadeOverlay::SetDuration(int milliseconds)
{
    _fade.setDuration(milliseconds);
}

void FadeOverlay::Dissolve(QPixmap snapshot, const QRect &region)
{
    if (snapshot.isNull()) {
        return;
    }
    _fade.stop();
    _snapshot = std::move(snapshot);
    setGeometry(region);
    raise();
    show();
    _fade.start();
    update();
}

bool FadeOverlay::IsRunning() const
{
    return _fade.state() == QAbstractAnimation::Running;
}

void FadeOverlay::paintEvent(QPaintEvent *)
{
    if (_snapshot.isNull()) {
        return;
    }
    QPainter painter{this};
    painter.setOpacity(IsRunning() ? _fade.currentValue().toReal() : 0.0);
    painter.drawPixmap(rect(), _snapshot);
}

} // namespace Gui::Widget
