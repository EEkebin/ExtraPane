package io.github.eekebin.extrapane;

import java.io.*;
import java.util.*;
import java.util.concurrent.*;
import java.util.concurrent.atomic.*;
import java.util.function.*;

/**
 * Runs the production pool/worker/lifetime logic with fake wire transport, no Android/JNI/network.
 */
public final class AdmissionBrokerCheck {
    static void require(boolean value) {
        if (!value) throw new AssertionError("Admission lifecycle");
    }

    static void waitFor(BooleanSupplier condition) throws Exception {
        long until = System.nanoTime() + TimeUnit.SECONDS.toNanos(3);
        while (!condition.getAsBoolean()) {
            if (System.nanoTime() > until) throw new AssertionError("Admission deadline");
            Thread.sleep(5);
        }
    }

    static final class Fake implements AdmissionBroker.Transport {
        volatile boolean closed, cancelled, failNext;
        boolean failOpen, blockOpen;
        volatile Thread owner;
        final CountDownLatch opened = new CountDownLatch(1);
        final List<Integer> requested = new CopyOnWriteArrayList<>();

        public void open(String a, int p, String l, android.net.Network n, String code)
                throws Exception {
            owner = Thread.currentThread();
            opened.countDown();
            if (failOpen) throw new IOException("Wrong access code");
            if (blockOpen) new CountDownLatch(1).await();
        }

        public String exchange(int operation, int kind) throws Exception {
            require(Thread.currentThread() == owner);
            if (failNext) throw new IOException("Control lost");
            requested.add(kind);
            return "ticket-" + kind;
        }

        public void cancel() {
            cancelled = true;
        }

        public void close() {
            require(Thread.currentThread() == owner || owner == null);
            closed = true;
        }
    }

    static AdmissionBroker.Lease acquire(
            String address,
            String local,
            String code,
            int kind,
            Runnable lost,
            BooleanSupplier cancel)
            throws Exception {
        return AdmissionBroker.acquire(address, 43824, local, null, code, kind, lost, cancel);
    }

    public static void main(String[] args) throws Exception {
        List<Fake> made = new CopyOnWriteArrayList<>();
        AdmissionBroker.testTransport(
                () -> {
                    Fake f = new Fake();
                    made.add(f);
                    return f;
                });
        AtomicInteger lost = new AtomicInteger();
        AdmissionBroker.Lease first =
                acquire("192.0.2.1", "", "123456", 0, lost::incrementAndGet, () -> false);
        AdmissionBroker.Lease second =
                acquire("192.0.2.1", "", "", 1, lost::incrementAndGet, () -> false);
        require(
                made.size() == 1
                        && first.credential.equals("ticket-0")
                        && second.credential.equals("ticket-1"));
        first.close();
        Thread.sleep(30);
        require(!made.get(0).closed);
        AdmissionBroker.Lease other =
                acquire("192.0.2.2", "", "123456", 2, lost::incrementAndGet, () -> false);
        AdmissionBroker.Lease route =
                acquire("192.0.2.1", "192.0.2.10", "123456", 2, lost::incrementAndGet, () -> false);
        require(made.size() == 3);
        second.close();
        waitFor(() -> made.get(0).closed);
        require(!made.get(1).closed && !made.get(2).closed && lost.get() == 0);
        other.close();
        route.close();
        waitFor(() -> made.stream().allMatch(f -> f.closed));
        int count = made.size();
        try (AdmissionBroker.Lease automatic =
                acquire("192.0.2.3", "", "", 0, lost::incrementAndGet, () -> false)) {
            require(automatic.credential.isEmpty() && made.size() == count);
        }
        String direct = "EPCT1:1:" + "a".repeat(32) + ":" + "b".repeat(64);
        int beforeDirect = made.size();
        try (AdmissionBroker.Lease directLease =
                acquire("127.0.0.1", "127.0.0.1", direct, 1, () -> {}, () -> false)) {
            require(directLease.credential.equals(direct) && made.size() == beforeDirect);
        }
        try {
            acquire("127.0.0.1", "127.0.0.1", direct, 2, () -> {}, () -> false);
            throw new AssertionError("Wrong-kind ticket accepted");
        } catch (IOException expected) {
        }
        try {
            acquire("127.0.0.1", "127.0.0.1", direct + "\n", 1, () -> {}, () -> false);
            throw new AssertionError("Malformed ticket accepted");
        } catch (IOException expected) {
        }
        Fake wrong = new Fake();
        wrong.failOpen = true;
        AdmissionBroker.testTransport(() -> wrong);
        try {
            acquire("192.0.2.4", "", "123456", 0, lost::incrementAndGet, () -> false);
            throw new AssertionError("Accepted bad admission");
        } catch (IOException expected) {
            require(expected.getMessage().contains("Wrong access code"));
        }
        waitFor(() -> wrong.closed);
        require(lost.get() == 0);
        Fake failed = new Fake();
        AdmissionBroker.testTransport(() -> failed);
        AdmissionBroker.Lease active =
                acquire("192.0.2.5", "", "123456", 0, lost::incrementAndGet, () -> false);
        failed.failNext = true;
        try {
            acquire("192.0.2.5", "", "", 1, lost::incrementAndGet, () -> false);
            throw new AssertionError("Accepted failed control");
        } catch (IOException expected) {
        }
        waitFor(() -> failed.closed && lost.get() == 1);
        active.close();
        Fake pending = new Fake();
        pending.blockOpen = true;
        AdmissionBroker.testTransport(() -> pending);
        AtomicBoolean cancel = new AtomicBoolean();
        ExecutorService executor = Executors.newSingleThreadExecutor();
        Future<?> attempt =
                executor.submit(
                        () -> {
                            try {
                                acquire(
                                        "192.0.2.6",
                                        "",
                                        "123456",
                                        0,
                                        lost::incrementAndGet,
                                        cancel::get);
                                throw new AssertionError("Cancelled acquire returned");
                            } catch (InterruptedException expected) {
                            } catch (Exception error) {
                                throw new RuntimeException(error);
                            }
                        });
        require(pending.opened.await(2, TimeUnit.SECONDS));
        cancel.set(true);
        attempt.get(2, TimeUnit.SECONDS);
        waitFor(() -> pending.closed);
        require(pending.cancelled && lost.get() == 1);
        executor.shutdownNow();
        try {
            acquire("192.0.2.7", "", "", 5, () -> {}, () -> false);
            throw new AssertionError("Invalid kind");
        } catch (IOException expected) {
        }
        try (AdmissionBroker.Lease speaker =
                acquire("192.0.2.7", "", "", 4, () -> {}, () -> false)) {
            require(speaker.credential.isEmpty());
        }
        System.out.println("Android admission broker lifecycle checks passed");
    }
}
