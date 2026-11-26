#pragma once

#include <QList>
#include <QRect>
#include <QString>

// Another process's top-level window, by native handle (HWND on Windows, an X11 window id
// elsewhere). Third-party runners open their own game window; the launcher finds it by process,
// asks it to close, raises it, and (only where a runner is known to tolerate it) reparents it.
namespace NativeWindow {

// The largest visible, unowned top-level window that belongs to one of the processes; 0 when none.
quintptr findTopLevel(const QList<qint64>& pids);
bool exists(quintptr window);
// Screen rectangle of the window's frame; empty when unknown.
QRect geometry(quintptr window);
// Ask the window to close: WM_CLOSE, or WM_SYSCOMMAND SC_CLOSE when sysCommand (X11: WM_DELETE_WINDOW).
void requestClose(quintptr window, bool sysCommand);
// Bring the window to the front and give it the keyboard.
void activate(quintptr window);
// Whether the window, or the given one of ours, has the keyboard now; true where that can't be told (X11).
bool isForeground(quintptr window, quintptr ours = 0);
// Make window a borderless child of host filling rect (host coordinates). False when it failed.
bool reparent(quintptr window, quintptr host, const QRect& rect);
void moveChild(quintptr window, const QRect& rect);

}
