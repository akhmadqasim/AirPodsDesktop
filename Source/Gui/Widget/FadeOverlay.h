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

#include <QPixmap>
#include <QVariantAnimation>
#include <QWidget>

namespace Gui::Widget {

// Cross-fades a region of a window between two arbitrary layouts: snapshot the region, change
// the widgets underneath, then let the snapshot dissolve on top of the new content. Layout jumps
// (rings merging, moving) read as one smooth transition instead of a cut.
//
class FadeOverlay : public QWidget
{
    Q_OBJECT

public:
    explicit FadeOverlay(QWidget *parent = nullptr);

    void SetDuration(int milliseconds);
    // `snapshot` is what the region looked like before; the overlay covers `region` (parent
    // coordinates) and fades out.
    void Dissolve(QPixmap snapshot, const QRect &region);
    bool IsRunning() const;

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    QPixmap _snapshot;
    QVariantAnimation _fade{this};
};

} // namespace Gui::Widget
