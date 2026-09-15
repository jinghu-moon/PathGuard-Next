package dev.pathguard.hideprobe

import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class ProbeRunGateTest {
    @Test fun staleRunCannotPublishAfterNewIntent() {
        val gate = ProbeRunGate()
        val first = gate.begin()
        val second = gate.begin()

        assertFalse(gate.isCurrent(first))
        assertTrue(gate.isCurrent(second))
    }

    @Test fun destroyInvalidatesCurrentRun() {
        val gate = ProbeRunGate()
        val run = gate.begin()
        gate.invalidate()

        assertFalse(gate.isCurrent(run))
    }
}
