package io.github.eekebin.extrapane;

import java.net.Socket;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicInteger;

public final class SocketCancellationCheck {
    static void check(boolean value) {
        if (!value) throw new AssertionError("Socket cancellation regression");
    }

    public static void main(String[] args) throws Exception {
        Thread caller = Thread.currentThread();
        CountDownLatch entered = new CountDownLatch(1),
                release = new CountDownLatch(1),
                finished = new CountDownLatch(1),
                otherClosed = new CountDownLatch(1);
        AtomicInteger count = new AtomicInteger();
        Socket blocked =
                new Socket() {
                    public void close() {
                        check(Thread.currentThread() != caller);
                        count.incrementAndGet();
                        entered.countDown();
                        try {
                            check(release.await(3, TimeUnit.SECONDS));
                        } catch (InterruptedException error) {
                            throw new AssertionError(error);
                        } finally {
                            finished.countDown();
                        }
                    }
                };
        Socket other =
                new Socket() {
                    public void close() {
                        check(Thread.currentThread() != caller);
                        otherClosed.countDown();
                    }
                };
        SocketCancellation cancellation = new SocketCancellation();
        cancellation.close(blocked);
        try {
            check(entered.await(2, TimeUnit.SECONDS));
            cancellation.close(blocked);
            cancellation.close(null);
            check(
                    count.get() == 1
                            && otherClosed.getCount() == 1); // One resource never closes another.
            new SocketCancellation().close(other);
            check(otherClosed.await(2, TimeUnit.SECONDS));
        } finally {
            release.countDown();
        }
        check(finished.await(2, TimeUnit.SECONDS));
        check(count.get() == 1);
        System.out.println(
                "Off-caller, idempotent and independently cancellable socket shutdown passed");
    }
}
