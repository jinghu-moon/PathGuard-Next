package dev.pathguard.hideprobe

import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Test

class SelectorProbeTest {
    @Test fun mapsStableRequestLabels() {
        assertEquals("photo_picker", SelectorProbe.labelForRequest(SelectorProbe.REQUEST_PHOTO_PICKER)); assertEquals("saf_dcim_tree", SelectorProbe.labelForRequest(SelectorProbe.REQUEST_DCIM_TREE))
    }
    @Test fun mapsExpectedDirectoryChildren() {
        assertEquals("Nagram", SelectorProbe.expectedChildForRequest(SelectorProbe.REQUEST_PICTURES_TREE)); assertEquals("Screenshots", SelectorProbe.expectedChildForRequest(SelectorProbe.REQUEST_DCIM_TREE)); assertNull(SelectorProbe.expectedChildForRequest(SelectorProbe.REQUEST_PHOTO_PICKER))
    }
}
