package dev.pathguard.hideprobe

import java.util.concurrent.atomic.AtomicLong

/** Prevents a non-interruptible native probe from publishing stale output. */
internal class ProbeRunGate {
    private val current = AtomicLong(0)

    fun begin(): Long = current.incrementAndGet()

    fun isCurrent(runToken: Long): Boolean = current.get() == runToken

    fun invalidate() {
        current.incrementAndGet()
    }
}
