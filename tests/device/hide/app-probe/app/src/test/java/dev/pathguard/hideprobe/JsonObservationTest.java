package dev.pathguard.hideprobe;

import static org.junit.Assert.assertEquals;

import org.junit.Test;

public final class JsonObservationTest {
    @Test
    public void rendersStableContract() {
        assertEquals(
                "{\"schema\":1,\"kind\":\"observation\",\"test\":\"open\","
                        + "\"surface\":\"java_nio\",\"path\":\"A\\\\\\\"B\","
                        + "\"return_value\":-1,\"errno\":2,\"side_effect\":false,"
                        + "\"status\":\"observed\"}",
                JsonObservation.render(
                        "open", "java_nio", "A\\\"B", -1, 2, false, "observed"));
    }
}
