package dev.pathguard.hideprobe;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNull;

import org.junit.Test;

public final class SelectorProbeTest {
    @Test
    public void mapsStableRequestLabels() {
        assertEquals(
                "photo_picker",
                SelectorProbe.labelForRequest(SelectorProbe.REQUEST_PHOTO_PICKER));
        assertEquals(
                "saf_dcim_tree",
                SelectorProbe.labelForRequest(SelectorProbe.REQUEST_DCIM_TREE));
    }

    @Test
    public void mapsExpectedDirectoryChildren() {
        assertEquals(
                "Nagram",
                SelectorProbe.expectedChildForRequest(
                        SelectorProbe.REQUEST_PICTURES_TREE));
        assertEquals(
                "Screenshots",
                SelectorProbe.expectedChildForRequest(SelectorProbe.REQUEST_DCIM_TREE));
        assertNull(SelectorProbe.expectedChildForRequest(
                SelectorProbe.REQUEST_PHOTO_PICKER));
    }
}
