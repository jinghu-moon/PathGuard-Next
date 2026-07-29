package dev.pathguard.hideprobe;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNull;

import java.util.Arrays;

import org.junit.Test;

public final class StoragePathTest {
    @Test
    public void convertsPrimaryStorageDirectoryForMediaStore() {
        assertEquals(
                "Pictures/Nagram/",
                StoragePath.relativeDirectory("/storage/emulated/0/Pictures/Nagram/"));
    }

    @Test
    public void rejectsRootsAndAliasesThatNeedExplicitResolution() {
        assertNull(StoragePath.relativeDirectory("/storage/emulated/0/"));
        assertNull(StoragePath.relativeDirectory("/sdcard/Pictures/Nagram"));
    }

    @Test
    public void expandsCanonicalPathToFrozenAliasMatrix() {
        String[] aliases = StoragePath.expandVfsAliases(new String[]{
                "/storage/emulated/0/Pictures/Nagram",
        });
        assertEquals(8, aliases.length);
        assertEquals("/storage/emulated/0/Pictures/Nagram", aliases[0]);
        assertEquals("/sdcard/Pictures/Nagram", aliases[1]);
        assertEquals("/data/media/0/Pictures/Nagram", aliases[7]);
        assertEquals(aliases.length, Arrays.stream(aliases).distinct().count());
    }
}
