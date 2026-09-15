package dev.pathguard.hideprobe

import org.junit.Assert.assertEquals
import org.junit.Test

class JsonObservationTest {
    @Test fun rendersStableContract() {
        assertEquals("{\"schema\":1,\"kind\":\"observation\",\"test\":\"open\",\"surface\":\"java_nio\",\"path\":\"A\\\\\\\"B\",\"return_value\":-1,\"errno\":2,\"side_effect\":false,\"status\":\"observed\"}", JsonObservation.render("open", "java_nio", "A\\\"B", -1, 2, false, "observed"))
    }
    @Test fun rendersEachArrayValueAsJsonString() {
        assertEquals("[\"first\",\"A\\\\\\\"B\"]", JsonObservation.renderStringArray(arrayOf("first", "A\\\"B")))
    }
}
