// Reparenting a window from another process into a Qt host, on real Windows: the original helper
// program opens its own top-level window, the test finds it by process, makes it a child of the host,
// checks GetParent/IsChild, resizes the host (whose resize handler moves the child like an embedding
// host does) and checks the child fills it, then closes it and expects a clean exit.
#include "NativeWindow.h"

#include <QApplication>
#include <QProcess>
#include <QResizeEvent>
#include <QTest>
#include <QWidget>

#include <windows.h>

namespace {

class Host : public QWidget {
public:
    quintptr child = 0;

protected:
    void resizeEvent(QResizeEvent* e) override
    {
        QWidget::resizeEvent(e);
        if (child)
            NativeWindow::moveChild(child, QRect(QPoint(0, 0), e->size() * devicePixelRatioF()));
    }
};

QRect childRectInHost(HWND child, HWND host)
{
    RECT r;
    GetWindowRect(child, &r);
    MapWindowPoints(HWND_DESKTOP, host, reinterpret_cast<POINT*>(&r), 2);
    return QRect(QPoint(r.left, r.top), QPoint(r.right - 1, r.bottom - 1));
}

QRect hostClient(HWND host)
{
    RECT r;
    GetClientRect(host, &r);
    return QRect(QPoint(r.left, r.top), QPoint(r.right - 1, r.bottom - 1));
}

}

class TestReparent : public QObject {
    Q_OBJECT
private slots:
    void helperWindowFollowsTheHost();
};

void TestReparent::helperWindowFollowsTheHost()
{
    Host host;
    host.resize(800, 450);
    host.show();
    QVERIFY(QTest::qWaitForWindowExposed(&host));
    const HWND hostWindow = reinterpret_cast<HWND>(host.winId());

    QProcess helper;
    helper.start(QStringLiteral(XSEMU_REPARENT_HELPER), {});
    QVERIFY2(helper.waitForStarted(), qPrintable(helper.errorString()));
    quintptr child = 0;
    QTRY_VERIFY_WITH_TIMEOUT((child = NativeWindow::findTopLevel({helper.processId()})) != 0, 10000);

    const QRect fill(QPoint(0, 0), host.size() * host.devicePixelRatioF());
    QVERIFY(NativeWindow::reparent(child, quintptr(hostWindow), fill));
    host.child = child;
    QCOMPARE(GetParent(reinterpret_cast<HWND>(child)), hostWindow);
    QVERIFY(IsChild(hostWindow, reinterpret_cast<HWND>(child)));
    QCOMPARE(childRectInHost(reinterpret_cast<HWND>(child), hostWindow), hostClient(hostWindow));

    host.resize(1024, 600);
    QTRY_COMPARE_WITH_TIMEOUT(childRectInHost(reinterpret_cast<HWND>(child), hostWindow), hostClient(hostWindow), 5000);
    QCOMPARE(hostClient(hostWindow).size(), QSize(1024, 600) * host.devicePixelRatioF());

    NativeWindow::requestClose(child, false);
    QVERIFY(helper.waitForFinished(10000));
    QCOMPARE(helper.exitStatus(), QProcess::NormalExit);
    QCOMPARE(helper.exitCode(), 0);
}

int main(int argc, char** argv)
{
    qputenv("QT_QPA_PLATFORM", "windows"); // a real HWND for the host, whatever the environment asks for
    QApplication app(argc, argv);
    TestReparent test;
    return QTest::qExec(&test, argc, argv);
}
#include "tst_reparent.moc"
