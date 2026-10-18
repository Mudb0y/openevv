package org.evvdroid

import androidx.test.ext.junit.runners.AndroidJUnit4
import org.junit.Assert.*
import org.junit.Test
import org.junit.runner.RunWith
import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit
import java.util.concurrent.atomic.AtomicReference

@RunWith(AndroidJUnit4::class)
class EngineCancellationTest {
    @Test
    fun closeReleasesASpeakWaitingForUndrainedAudio() {
        val engine = requireNotNull(EvvEngine.open(0x00010000))
        val handleField = EvvEngine::class.java.getDeclaredField("handle").apply { isAccessible = true }
        val handle = handleField.getLong(engine)
        val finished = CountDownLatch(1)
        val failure = AtomicReference<Throwable?>()
        var next: Thread? = null
        var closer: Thread? = null
        try {
            assertTrue(engine.speak("The quick brown fox jumps over the lazy dog. ".repeat(100)))
            next = Thread {
                try { engine.speak("Next item.") } catch (t: Throwable) { failure.set(t) }
            }.apply { start() }
            val deadline = System.nanoTime() + TimeUnit.SECONDS.toNanos(5)
            while (!next.stackTrace.any { it.className.endsWith("EvvNative") && it.methodName == "speak" } &&
                System.nanoTime() < deadline) Thread.sleep(10)
            assertTrue("second speak never entered native wait",
                next.stackTrace.any { it.className.endsWith("EvvNative") && it.methodName == "speak" })
            closer = Thread {
                try { engine.close() } catch (t: Throwable) { failure.set(t) }
                finally { finished.countDown() }
            }.apply { start() }
            assertTrue("close blocked behind speak's engine lock", finished.await(3, TimeUnit.SECONDS))
            next.join(3000)
            assertFalse("speak never released", next.isAlive)
            failure.get()?.let { throw AssertionError("engine call failed", it) }
            assertEquals(-1, engine.read(ByteArray(4096)))
            assertFalse(engine.speak("Already closed."))
            engine.stop()
            engine.close()
        } finally {
            // Only use the raw handle if destruction has not begun; successful
            // close frees it. The old implementation waits holding guard and
            // still has closed=false, so it is safe to release that deadlock.
            val closedField = EvvEngine::class.java.getDeclaredField("closed").apply { isAccessible = true }
            if (!closedField.getBoolean(engine)) EvvNative.stop(handle)
            next?.join(3000)
            closer?.join(3000)
            engine.close()
        }
    }

    @Test
    fun stopReleasesASpeakWaitingForUndrainedAudio() {
        val engine = requireNotNull(EvvEngine.open(0x00010000))
        val failure = AtomicReference<Throwable?>()
        val entered = CountDownLatch(1)
        val stopped = CountDownLatch(1)
        var next: Thread? = null
        var stopper: Thread? = null
        // Emergency cleanup bypasses the Kotlin lock so the failing test
        // cannot leave the instrumentation process deadlocked.
        val handleField = EvvEngine::class.java.getDeclaredField("handle").apply { isAccessible = true }
        val handle = handleField.getLong(engine)
        try {
            assertTrue(engine.speak("The quick brown fox jumps over the lazy dog. ".repeat(100)))
            next = Thread {
                entered.countDown()
                try { engine.speak("Next item.") } catch (t: Throwable) { failure.set(t) }
            }.apply { start() }
            assertTrue(entered.await(1, TimeUnit.SECONDS))
            val deadline = System.nanoTime() + TimeUnit.SECONDS.toNanos(5)
            while (!next.stackTrace.any { it.className.endsWith("EvvNative") && it.methodName == "speak" } &&
                System.nanoTime() < deadline) Thread.sleep(10)
            assertTrue("second speak must be waiting inside native speak",
                next.stackTrace.any { it.className.endsWith("EvvNative") && it.methodName == "speak" })
            stopper = Thread { engine.stop(); stopped.countDown() }.apply { start() }
            assertTrue("stop blocked behind speak's engine lock", stopped.await(1, TimeUnit.SECONDS))
            next.join(3000)
            assertFalse("speak never released", next.isAlive)
            failure.get()?.let { throw AssertionError("speak failed", it) }
            engine.stop()
            assertTrue(engine.speak("Speech recovered."))
            val buffer = ByteArray(4096)
            var bytes = 0
            while (true) {
                val n = engine.read(buffer)
                if (n <= 0) break
                bytes += n
            }
            assertTrue("no audio after cancellation", bytes > 0)
        } finally {
            EvvNative.stop(handle)
            next?.join(3000)
            stopper?.join(3000)
            engine.close()
        }
    }
}
