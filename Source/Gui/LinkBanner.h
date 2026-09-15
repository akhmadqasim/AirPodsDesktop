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

#pragma once

#include <optional>

#include <QImage>
#include <QPropertyAnimation>
#include <QTimer>
#include <QWidget>

#include "../Core/AirPods.h"

namespace Gui {

// The little black pill that drops in from the top of the screen when the pods leave for another
// device, like the Dynamic Island banner on iPhone: a pod glyph, "Moved to another device", and a
// blue button that brings them back here.
//
class LinkBanner : public QWidget
{
    Q_OBJECT

public:
    explicit LinkBanner(QWidget *parent = nullptr);

    void UpdateState(const Core::AirPods::State &state);
    // The link to this PC dropped: show "moved" once the next advertisement says the pods hold a
    // link to someone else and are not sitting in the case. Nothing within a few seconds (pods
    // switched off, out of range) means nothing to announce.
    void ArmMovedAway();
    bool IsArmed() const;
    void ShowMovedAway();
    void ShowConnecting();
    void ShowFailed();
    void Dismiss();

    QString Text() const;
    bool IsButtonShown() const;
    QRect ButtonRect() const;

Q_SIGNALS:
    void ReturnRequested();

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;

private:
    constexpr static QSize _bannerSize{340, 56};
    constexpr static int _topMargin = 10;

    QString _text;
    bool _buttonShown{true};
    bool _buttonPressed{false};
    bool _buttonHovered{false};
    QImage _podGlyph;
    std::optional<Core::AirPods::Model> _glyphModel;
    QPropertyAnimation _slide{this, "pos"};
    QTimer *_dismissTimer = new QTimer{this};
    QTimer *_armTimer = new QTimer{this};
    bool _shown{false};

    void Present(const QString &text, bool withButton, std::optional<std::chrono::milliseconds> stay);
    void LoadGlyph(Core::AirPods::Model model);
    QPoint RestingPosition(QScreen *screen) const;
};

} // namespace Gui
