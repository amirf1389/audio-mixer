// WHERE THE CONSOLE WINDOW OPENS: centred on the work area of the screen it opens on (the taskbar left out), or where it was last
// closed when that place is still on a connected screen. A saved place on a monitor that is gone (unplugged, a laptop off its dock)
// or hanging over an edge is pulled back onto a visible screen. All sizes are Qt's device-independent pixels, so the Windows scale
// of each monitor (125 %, 150 %, ...) is already in them.
#pragma once
#include <QList>
#include <QRect>
#include <QSize>
#include <algorithm>

namespace winplace {

// the client size fitted into the work area (the frame and title bar added round it), never under the minimum
inline QSize fit(QSize want, QSize minimum, const QRect& avail, const QMargins& frame) {
  const int w = std::max(minimum.width(), std::min(want.width(), avail.width() - frame.left() - frame.right()));
  const int h = std::max(minimum.height(), std::min(want.height(), avail.height() - frame.top() - frame.bottom()));
  return {w, h};
}

// the client rectangle of a window of this size centred on the work area (the frame inside it too)
inline QRect centred(QSize size, const QRect& avail, const QMargins& frame) {
  const int fw = size.width() + frame.left() + frame.right(), fh = size.height() + frame.top() + frame.bottom();
  const int x = avail.x() + std::max(0, (avail.width() - fw) / 2) + frame.left();
  const int y = avail.y() + std::max(0, (avail.height() - fh) / 2) + frame.top();
  return {QPoint(x, y), size};
}

// a client rectangle moved (not resized, unless it is larger than the work area) fully into the work area, title bar included
inline QRect clamp(QRect r, const QRect& avail, const QMargins& frame, QSize minimum) {
  r.setSize(fit(r.size(), minimum, avail, frame));
  const int left = avail.left() + frame.left(), top = avail.top() + frame.top();
  const int right = avail.right() - frame.right(), bottom = avail.bottom() - frame.bottom();
  int x = std::min(r.x(), right - r.width() + 1), y = std::min(r.y(), bottom - r.height() + 1);
  x = std::max(x, left); y = std::max(y, top);
  r.moveTopLeft(QPoint(x, y)); return r;
}

// the place to open at. saved: the client rectangle the window had when it was closed (empty: none). screens: the work areas of
// the connected screens. home: the work area of the screen to open on when there is no usable saved place (the one under the
// mouse, else the primary one).
inline QRect choose(QSize want, QSize minimum, const QRect& saved, const QList<QRect>& screens, const QRect& home, const QMargins& frame) {
  if (saved.isValid() && saved.width() >= minimum.width() && saved.height() >= minimum.height()) {
    // the screen showing the most of the saved window; a saved place that shows less than a usable part (the title bar can not be
    // grabbed) counts as lost and the window opens centred instead
    QRect best; long long most = 0;
    for (const QRect& s : screens) { const QRect i = s.intersected(saved.adjusted(-frame.left(), -frame.top(), frame.right(), frame.bottom()));
      const long long a = static_cast<long long>(i.width()) * i.height(); if (i.isValid() && a > most) { most = a; best = s; } }
    const long long need = static_cast<long long>(std::min(saved.width(), 200)) * std::min(saved.height(), 120);
    if (best.isValid() && most >= need) return clamp(saved, best, frame, minimum);
  }
  return centred(fit(want, minimum, home, frame), home, frame);
}

}  // namespace winplace
