package dev.pathguard.hideprobe

import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Test

class StoragePathTest {
    @Test fun convertsPrimaryStorageDirectoryForMediaStore() {
        assertEquals("Pictures/Nagram/", StoragePath.relativeDirectory("/storage/emulated/0/Pictures/Nagram/"))
    }
    @Test fun rejectsRootsAndAliasesThatNeedExplicitResolution() {
        assertNull(StoragePath.relativeDirectory("/storage/emulated/0/"))
        assertNull(StoragePath.relativeDirectory("/sdcard/Pictures/Nagram"))
    }
    @Test fun expandsCanonicalPathToFrozenAliasMatrix() {
        val aliases = StoragePath.expandVfsAliases(arrayOf("/storage/emulated/0/Pictures/Nagram"))
        assertEquals(8, aliases.size); assertEquals("/storage/emulated/0/Pictures/Nagram", aliases[0]); assertEquals("/data/media/0/Pictures/Nagram", aliases[7]); assertEquals(aliases.size, aliases.distinct().size)
    }
}
