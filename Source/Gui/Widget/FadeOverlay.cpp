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
    // Sequential: the snapshot fades out over the window colour, then the new content fades in.
    _fade.setDuration(1000);
    _fade.setEasingCurve(QEasingCurve::Linear);
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
    if (!IsRunning()) {
        return;
    }
    const qreal t = _fade.currentValue().toReal();
    const QEasingCurve ease{QEasingCurve::InOutSine};
    const QColor cover = parentWidget() ? parentWidget()->palette().color(QPalette::Window)
                                        : palette().color(QPalette::Window);

    QPainter painter{this};
    if (t < 0.5) {
        painter.fillRect(rect(), cover);
        painter.setOpacity(1.0 - ease.valueForProgress(t * 2.0));
        painter.drawPixmap(rect(), _snapshot);
    }
    else {
        painter.setOpacity(1.0 - ease.valueForProgress(t * 2.0 - 1.0));
        painter.fillRect(rect(), cover);
    }
}

} // namespace Gui::Widget
