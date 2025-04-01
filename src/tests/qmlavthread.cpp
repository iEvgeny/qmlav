#include <gtest/gtest.h>

#include "./../qmlavthread.h"

template <typename T>
std::decay_t<T> generic_fn(T t)
{
    return t;
}

template <typename T1, typename T2>
decltype(auto) generic_fn2(T1 t1, T2 t2)
{
    return t1 + t2;
}

class Functor
{
public:
    int fn(int n) const { return n; }
    int operator () (int n) { return n; }
};

TEST(QmlAVThread, RunGenericFunction)
{
    QmlAVThreadLiveController<int> c = QmlAVThread::run(&generic_fn<int>, 42);

    EXPECT_EQ(c.result(), 42);
}

TEST(QmlAVThread, RunGenericFunction_RefArgTest)
{
    int n = 42;
    QmlAVThreadLiveController<int> c = QmlAVThread::run(&generic_fn<int &>, n);

    EXPECT_EQ(c.result(), 42);
}

TEST(QmlAVThread, RunGenericFunction_ConstRefArgTest)
{
    QmlAVThreadLiveController<int> c = QmlAVThread::run(&generic_fn<const int &>, 42);

    EXPECT_EQ(c.result(), 42);
}

TEST(QmlAVThread, RunGenericFunction_RefRefArgTest)
{
    QmlAVThreadLiveController<int> c = QmlAVThread::run(&generic_fn<int &&>, 42);

    EXPECT_EQ(c.result(), 42);
}

TEST(QmlAVThread, RunGenericFunction_RefRefArgTest2)
{
    int n = 42;
    QmlAVThreadLiveController<int> c = QmlAVThread::run(&generic_fn2<int &, int &&>, n, 42);
    QmlAVThreadLiveController<int> c2 = QmlAVThread::run(&generic_fn2<int &&, int &>, 42, n);

    EXPECT_EQ(c.result(), c2.result());
}

TEST(QmlAVThread, RunGenericFunction_WithoutArgsTest)
{
    int r = 0;

    QmlAVThreadLiveController<void> c = QmlAVThread::run([&]() { r = 42; });
    c.waitForFinished();

    EXPECT_EQ(r, 42);
}

TEST(QmlAVThread, RunGenericFunction_WithoutArgsTest2)
{
    QmlAVThreadLiveController<int> c = QmlAVThread::run([&]() { return 42; });
    c.waitForFinished(); // Important! Testing cyclic locking

    EXPECT_EQ(c.result(), 42);
}

TEST(QmlAVThread, RunClassMember)
{
    Functor f;
    QmlAVThreadLiveController<int> c = QmlAVThread::run(&Functor::fn, &f, 42);

    EXPECT_EQ(c.result(), 42);
}

TEST(QmlAVThread, RunFunctor)
{
    QmlAVThreadLiveController<int> c = QmlAVThread::run(Functor(), 42);

    EXPECT_EQ(c.result(), 42);
}

TEST(QmlAVThread, RunLambda)
{
    int r = 0;
    int n = 42;

    QmlAVThreadLiveController<void> c = QmlAVThread::run([&](int n) { r = n; }, n);
    c.waitForFinished();

    EXPECT_EQ(r, n);
}

TEST(QmlAVThread, LoopLambda)
{
    int n = 0;

    auto l = [&]() -> QmlAVLoopController {
        if (++n < 42) {
            return QmlAVLoopController::Continue;
        }

        return QmlAVLoopController::Break;
    };

    QmlAVThreadLiveController<QmlAVLoopController> c = QmlAVThread::loop(l);
    c.waitForFinished();

    EXPECT_EQ(n, 42);
}

TEST(QmlAVThread, QmlAVTask_GenericFunction)
{
    auto t = QmlAVThreadTask(&generic_fn<int>);
    t(42);

    QmlAVThreadLiveController<int> c = t.getLiveController();

    EXPECT_EQ(c.result(), 42);

    c.requestInterruption(true);
}

TEST(QmlAVThread, QmlAVTask_ClassMember)
{
    Functor f;
    
    QmlAVThreadTask<decltype(&Functor::fn)> t(&Functor::fn);
    t(&f, 42);

    QmlAVThreadLiveController<int> c = t.getLiveController();

    EXPECT_EQ(c.result(), 42);

    c.requestInterruption(true);
}

TEST(QmlAVThread, QmlAVTask_Functor)
{
    auto t = QmlAVThreadTask(Functor());
    t(42);

    QmlAVThreadLiveController<int> c = t.getLiveController();

    EXPECT_EQ(c.result(), 42);

    c.requestInterruption(true);
}

TEST(QmlAVThread, QmlAVTask_Retry)
{
    int n = 0;

    auto t = QmlAVThreadTask([&](std::string &s) -> QmlAVLoopController {
        if (!s.empty() && ++n < std::stoi(s)) {
            return QmlAVLoopController::Retry;
        }

        return QmlAVLoopController::Break;
    });
    t("42");

    QmlAVThreadLiveController<QmlAVLoopController> c = t.getLiveController();
    c.waitForFinished();

    EXPECT_EQ(n, 42);
}

TEST(QmlAVThread, QmlAVTask_DemuxerDecoderThreadModel)
{
    const int tasksCount = 1000;
    
    auto decoderTask = QmlAVThreadTask([](int n) {
        return n;
    });
    QmlAVThreadLiveController<int> decoderThread = decoderTask.getLiveController();

    QmlAVThreadLiveController<void> demuxerThread = QmlAVThread::run([=]() mutable {
        for (int i = 0; i < tasksCount; ++i) {
            // TODO: Test with empty args also
            decoderTask(42);
        }
    });

    demuxerThread.waitForFinished();

    for (int i = 0; i < tasksCount; ++i) {
        EXPECT_EQ(decoderThread.result(), 42);
    }

    decoderThread.requestInterruption(true);
}
