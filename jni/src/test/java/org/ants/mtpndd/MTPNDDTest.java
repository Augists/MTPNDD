package org.ants.mtpndd;

import org.junit.jupiter.api.AfterEach;
import org.junit.jupiter.api.BeforeAll;
import org.junit.jupiter.api.BeforeEach;
import org.junit.jupiter.api.Test;

import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.util.Locale;

import static org.junit.jupiter.api.Assertions.*;

class MTPNDDTest {

    private static String resolveLibraryName() {
        String os = System.getProperty("os.name", "unknown").toLowerCase(Locale.ROOT);
        if (os.contains("win")) {
            return "mtpnddjni.dll";
        }
        if (os.contains("mac") || os.contains("darwin")) {
            return "libmtpnddjni.dylib";
        }
        return "libmtpnddjni.so";
    }

    @BeforeAll
    static void setupLibraryPath() {
        Path candidate = Paths.get("build", resolveLibraryName()).toAbsolutePath();
        if (!Files.exists(candidate)) {
            throw new IllegalStateException("JNI library not found at " + candidate
                    + ". Please build it with `cmake -B build && cmake --build build` inside the jni/ directory.");
        }
        System.setProperty("org.ants.mtpndd.library.path", candidate.toString());
        // Trigger class loading
        MTPNDDEngine.isInitialized();
    }

    @BeforeEach
    void initialise() {
        if (MTPNDDEngine.isInitialized()) {
            MTPNDDEngine.shutdown();
        }
        MTPNDDConfig config = MTPNDDConfig.builder()
                .workers(1)
                .laceDequeSize(1 << 14)
                .bddNodeTableSize(1 << 20)
                .mtpnddNodeTableSize(1 << 18)
                .operationCacheSize(1 << 18)
                .edgeBucketCount(64)
                .nodetableBucketCount(1 << 15)
                .gcBucketCount(1 << 15)
                .nodeSlabCapacity(1024)
                .edgeEntrySlabCapacity(2048)
                .nodetableEntrySlabCapacity(1024)
                .edgeMapSlabCapacity(1024)
                .build();
        MTPNDDEngine.init(config);
    }

    @AfterEach
    void shutdown() {
        if (MTPNDDEngine.isInitialized()) {
            MTPNDDEngine.shutdown();
        }
    }

    @Test
    void basicApiFlow() {
        int fieldId = MTPNDDEngine.declareField(1);
        assertEquals(1, fieldId);
        MTPNDDEngine.generateFields();

        MTPNDDFieldInfo info = MTPNDDEngine.getFieldInfo(fieldId);
        assertNotNull(info);
        assertEquals(1, info.bitWidth());

        MTPNDD var = MTPNDD.getVar(fieldId, 0);
        assertNotNull(var);
        MTPNDD notVar = MTPNDD.getNotVar(fieldId, 0);
        assertNotNull(notVar);
        assertNotEquals(var, notVar);

        MTPNDD andResult = var.and(var);
        assertEquals(var, andResult);
        MTPNDD orResult = var.or(notVar);
        assertFalse(orResult.isFalse());
        MTPNDD diffResult = var.diff(var);
        assertEquals(MTPNDD.terminalFalse(), diffResult);
        MTPNDD notResult = MTPNDD.terminalFalse().not();
        assertEquals(MTPNDD.terminalTrue(), notResult);

        MTPNDD existResult = var.exist(fieldId);
        assertFalse(existResult.isFalse());

        double satTrue = MTPNDD.terminalTrue().satCount();
        assertTrue(satTrue >= 1.0);
        double satFalse = MTPNDD.terminalFalse().satCount();
        assertEquals(0.0, satFalse);

        MTPNDDStats stats = MTPNDDEngine.stats();
        assertNotNull(stats);
        assertTrue(stats.nodeCount() >= 0);
        if (stats.recordingEnabled()) {
            assertTrue(stats.nodesCreatedTotal() >= 1);
        }
    }
}
