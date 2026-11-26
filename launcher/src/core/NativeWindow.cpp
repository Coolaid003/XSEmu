#include "NativeWindow.h"

#ifdef Q_OS_WIN
#include <windows.h>
#elif defined(XSEMU_XCB)
#include <xcb/xcb.h>
#include <cstdlib>
#include <cstring>
#endif

namespace NativeWindow {

#ifdef Q_OS_WIN

namespace {

struct Search {
    const QList<qint64>* pids;
    HWND best = nullptr;
    qint64 bestArea = -1;
};

BOOL CALLBACK visit(HWND w, LPARAM lp)
{
    auto* s = reinterpret_cast<Search*>(lp);
    DWORD pid = 0;
    GetWindowThreadProcessId(w, &pid);
    if (!s->pids->contains(qint64(pid)) || !IsWindowVisible(w) || GetWindow(w, GW_OWNER))
        return TRUE;
    RECT r{};
    GetWindowRect(w, &r);
    const qint64 area = qint64(r.right - r.left) * (r.bottom - r.top);
    if (area > s->bestArea) {
        s->best = w;
        s->bestArea = area;
    }
    return TRUE;
}

HWND hwnd(quintptr w) { return reinterpret_cast<HWND>(w); }

}

quintptr findTopLevel(const QList<qint64>& pids)
{
    if (pids.isEmpty())
        return 0;
    Search s{&pids};
    EnumWindows(visit, reinterpret_cast<LPARAM>(&s));
    return reinterpret_cast<quintptr>(s.best);
}

bool exists(quintptr window) { return window && IsWindow(hwnd(window)); }

QRect geometry(quintptr window)
{
    RECT r{};
    if (!window || !GetWindowRect(hwnd(window), &r))
        return {};
    return QRect(r.left, r.top, r.right - r.left, r.bottom - r.top);
}

void requestClose(quintptr window, bool sysCommand)
{
    if (!window)
        return;
    if (sysCommand)
        PostMessageW(hwnd(window), WM_SYSCOMMAND, SC_CLOSE, 0);
    else
        PostMessageW(hwnd(window), WM_CLOSE, 0, 0);
}

void activate(quintptr window)
{
    if (!window)
        return;
    HWND w = hwnd(window);
    if (IsIconic(w))
        ShowWindow(w, SW_RESTORE);
    // SetForegroundWindow is refused unless our thread shares input with the current foreground one
    const DWORD fg = GetWindowThreadProcessId(GetForegroundWindow(), nullptr);
    const DWORD self = GetCurrentThreadId();
    const bool attached = fg != self && AttachThreadInput(self, fg, TRUE);
    SetForegroundWindow(w);
    BringWindowToTop(w);
    if (attached)
        AttachThreadInput(self, fg, FALSE);
}

bool isForeground(quintptr window, quintptr ours)
{
    const HWND fg = GetForegroundWindow();
    return fg && (fg == hwnd(window) || (ours && fg == hwnd(ours)));
}

bool reparent(quintptr window, quintptr host, const QRect& rect)
{
    HWND w = hwnd(window);
    if (!IsWindow(w) || !IsWindow(hwnd(host)))
        return false;
    LONG_PTR style = GetWindowLongPtrW(w, GWL_STYLE);
    style &= ~(WS_POPUP | WS_CAPTION | WS_THICKFRAME | WS_SYSMENU | WS_MINIMIZEBOX | WS_MAXIMIZEBOX);
    style |= WS_CHILD | WS_CLIPSIBLINGS;
    SetWindowLongPtrW(w, GWL_STYLE, style);
    // SetParent returns the previous parent, which may be null for a top-level window: only a null
    // result with an error set is a failure
    SetLastError(0);
    if (!SetParent(w, hwnd(host)) && GetLastError() != 0)
        return false;
    SetWindowPos(w, HWND_TOP, rect.x(), rect.y(), rect.width(), rect.height(), SWP_FRAMECHANGED | SWP_SHOWWINDOW | SWP_NOACTIVATE);
    return GetParent(w) == hwnd(host);
}

void moveChild(quintptr window, const QRect& rect)
{
    if (window)
        SetWindowPos(hwnd(window), nullptr, rect.x(), rect.y(), rect.width(), rect.height(), SWP_NOZORDER | SWP_NOACTIVATE);
}

#elif defined(XSEMU_XCB)

namespace {

xcb_connection_t* conn()
{
    static xcb_connection_t* c = xcb_connect(nullptr, nullptr);
    return c && !xcb_connection_has_error(c) ? c : nullptr;
}

xcb_atom_t atom(const char* name)
{
    xcb_intern_atom_reply_t* r = xcb_intern_atom_reply(conn(), xcb_intern_atom(conn(), 0, uint16_t(strlen(name)), name), nullptr);
    const xcb_atom_t a = r ? r->atom : xcb_atom_t(XCB_ATOM_NONE);
    free(r);
    return a;
}

xcb_window_t root()
{
    return xcb_setup_roots_iterator(xcb_get_setup(conn())).data->root;
}

qint64 windowPid(xcb_window_t w)
{
    static const xcb_atom_t pidAtom = atom("_NET_WM_PID");
    xcb_get_property_reply_t* r = xcb_get_property_reply(conn(), xcb_get_property(conn(), 0, w, pidAtom, XCB_ATOM_CARDINAL, 0, 1), nullptr);
    qint64 pid = -1;
    if (r && xcb_get_property_value_length(r) >= 4)
        pid = *static_cast<uint32_t*>(xcb_get_property_value(r));
    free(r);
    return pid;
}

void sendClientMessage(xcb_window_t to, xcb_window_t about, xcb_atom_t type, uint32_t d0, uint32_t d1, uint32_t mask)
{
    xcb_client_message_event_t ev{};
    ev.response_type = XCB_CLIENT_MESSAGE;
    ev.format = 32;
    ev.window = about;
    ev.type = type;
    ev.data.data32[0] = d0;
    ev.data.data32[1] = d1;
    xcb_send_event(conn(), 0, to, mask, reinterpret_cast<const char*>(&ev));
    xcb_flush(conn());
}

}

quintptr findTopLevel(const QList<qint64>& pids)
{
    if (!conn() || pids.isEmpty())
        return 0;
    xcb_query_tree_reply_t* tree = xcb_query_tree_reply(conn(), xcb_query_tree(conn(), root()), nullptr);
    if (!tree)
        return 0;
    xcb_window_t best = 0;
    qint64 bestArea = -1;
    const xcb_window_t* kids = xcb_query_tree_children(tree);
    for (int i = 0; i < xcb_query_tree_children_length(tree); ++i) {
        if (!pids.contains(windowPid(kids[i])))
            continue;
        xcb_get_window_attributes_reply_t* a = xcb_get_window_attributes_reply(conn(), xcb_get_window_attributes(conn(), kids[i]), nullptr);
        const bool viewable = a && a->map_state == XCB_MAP_STATE_VIEWABLE && !a->override_redirect;
        free(a);
        if (!viewable)
            continue;
        const QRect g = geometry(kids[i]);
        if (qint64(g.width()) * g.height() > bestArea) {
            best = kids[i];
            bestArea = qint64(g.width()) * g.height();
        }
    }
    free(tree);
    return best;
}

bool exists(quintptr window)
{
    if (!conn() || !window)
        return false;
    xcb_get_window_attributes_reply_t* a = xcb_get_window_attributes_reply(conn(), xcb_get_window_attributes(conn(), xcb_window_t(window)), nullptr);
    const bool ok = a != nullptr;
    free(a);
    return ok;
}

QRect geometry(quintptr window)
{
    if (!conn() || !window)
        return {};
    xcb_get_geometry_reply_t* g = xcb_get_geometry_reply(conn(), xcb_get_geometry(conn(), xcb_window_t(window)), nullptr);
    if (!g)
        return {};
    xcb_translate_coordinates_reply_t* t = xcb_translate_coordinates_reply(conn(), xcb_translate_coordinates(conn(), xcb_window_t(window), root(), 0, 0), nullptr);
    const QRect r(t ? t->dst_x : g->x, t ? t->dst_y : g->y, g->width, g->height);
    free(t);
    free(g);
    return r;
}

void requestClose(quintptr window, bool)
{
    if (!conn() || !window)
        return;
    sendClientMessage(xcb_window_t(window), xcb_window_t(window), atom("WM_PROTOCOLS"), atom("WM_DELETE_WINDOW"), XCB_CURRENT_TIME, XCB_EVENT_MASK_NO_EVENT);
}

void activate(quintptr window)
{
    if (!conn() || !window)
        return;
    const uint32_t above = XCB_STACK_MODE_ABOVE;
    xcb_configure_window(conn(), xcb_window_t(window), XCB_CONFIG_WINDOW_STACK_MODE, &above);
    sendClientMessage(root(), xcb_window_t(window), atom("_NET_ACTIVE_WINDOW"), 2, XCB_CURRENT_TIME,
                      XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT | XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY);
    xcb_set_input_focus(conn(), XCB_INPUT_FOCUS_PARENT, xcb_window_t(window), XCB_CURRENT_TIME);
    xcb_flush(conn());
}

bool isForeground(quintptr, quintptr) { return true; }

bool reparent(quintptr window, quintptr host, const QRect& rect)
{
    if (!conn() || !window || !host)
        return false;
    xcb_reparent_window(conn(), xcb_window_t(window), xcb_window_t(host), int16_t(rect.x()), int16_t(rect.y()));
    moveChild(window, rect);
    xcb_map_window(conn(), xcb_window_t(window));
    xcb_flush(conn());
    xcb_query_tree_reply_t* t = xcb_query_tree_reply(conn(), xcb_query_tree(conn(), xcb_window_t(window)), nullptr);
    const bool ok = t && t->parent == xcb_window_t(host);
    free(t);
    return ok;
}

void moveChild(quintptr window, const QRect& rect)
{
    if (!conn() || !window)
        return;
    const uint32_t v[] = {uint32_t(rect.x()), uint32_t(rect.y()), uint32_t(rect.width()), uint32_t(rect.height())};
    xcb_configure_window(conn(), xcb_window_t(window), XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y | XCB_CONFIG_WINDOW_WIDTH | XCB_CONFIG_WINDOW_HEIGHT, v);
    xcb_flush(conn());
}

#else

quintptr findTopLevel(const QList<qint64>&) { return 0; }
bool exists(quintptr) { return false; }
QRect geometry(quintptr) { return {}; }
void requestClose(quintptr, bool) {}
void activate(quintptr) {}
bool isForeground(quintptr, quintptr) { return true; }
bool reparent(quintptr, quintptr, const QRect&) { return false; }
void moveChild(quintptr, const QRect&) {}

#endif

}
