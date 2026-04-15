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

class MTPNDDFractionTest {

    private static String resolveLibraryName() {
        String os = System.getProperty("os.name", "unknown").toLowerCase(Locale.ROOT);
        if (os.contains("win")) return "mtpnddjni.dll";
        if (os.contains("mac") || os.contains("darwin")) return "libmtpnddjni.dylib";
        return "libmtpnddjni.so";
    }

    @BeforeAll
    static void setupLibraryPath() {
        Path candidate = Paths.get("build", resolveLibraryName()).toAbsolutePath();
        if (!Files.exists(candidate)) {
            throw new IllegalStateException("JNI library not found at " + candidate);
        }
        System.setProperty("org.ants.mtpndd.library.path", candidate.toString());
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
                .bddNodeTableSize(1 << 16)
                .mtpnddNodeTableSize(1 << 14)
                .operationCacheSize(1 << 12)
                .edgeBucketCount(16)
                .nodetableBucketCount(1 << 10)
                .nodeSlabCapacity(1024)
                .edgeEntrySlabCapacity(2048)
                .nodetableEntrySlabCapacity(1024)
                .edgeMapSlabCapacity(1024)
                .build();
        MTPNDDEngine.init(config);
        MTPNDDEngine.declareField(2);
        MTPNDDEngine.generateFields();
    }

    @AfterEach
    void shutdown() {
        if (MTPNDDEngine.isInitialized()) {
            MTPNDDEngine.shutdown();
        }
    }

    @Test
    void fractionCanonicalization() {
        MTPNDD a = MTPNDDEngine.makeFraction(2, 4);
        MTPNDD b = MTPNDDEngine.makeFraction(1, 2);
        assertEquals(a, b);
        assertEquals(1, MTPNDDEngine.getNumer(a));
        assertEquals(2, MTPNDDEngine.getDenom(a));
        assertTrue(MTPNDDEngine.isFractionLeaf(a));
        assertFalse(MTPNDDEngine.isDoubleLeaf(a));
        assertTrue(MTPNDDEngine.isLeaf(a));

        // 0/k → MTPNDD_FALSE
        MTPNDD zero = MTPNDDEngine.makeFraction(0, 5);
        assertEquals(MTPNDD.getFalse(), zero);
        // k/k → MTPNDD_TRUE
        MTPNDD one = MTPNDDEngine.makeFraction(3, 3);
        assertEquals(MTPNDD.getTrue(), one);
    }

    @Test
    void fractionArithmetic() {
        MTPNDD a = MTPNDDEngine.makeFraction(1, 3);
        MTPNDD b = MTPNDDEngine.makeFraction(1, 6);
        MTPNDD sum = MTPNDDEngine.plus(a, b);
        assertEquals(1, MTPNDDEngine.getNumer(sum));
        assertEquals(2, MTPNDDEngine.getDenom(sum));

        MTPNDD prod = MTPNDDEngine.times(a, b);
        assertEquals(1, MTPNDDEngine.getNumer(prod));
        assertEquals(18, MTPNDDEngine.getDenom(prod));

        MTPNDD quot = MTPNDDEngine.divide(a, b);
        assertEquals(2, MTPNDDEngine.getNumer(quot));
        assertEquals(1, MTPNDDEngine.getDenom(quot));

        // a - a = 0
        MTPNDD diff = MTPNDDEngine.minus(a, a);
        assertEquals(MTPNDD.getFalse(), diff);
    }

    @Test
    void doubleLeaves() {
        MTPNDD a = MTPNDDEngine.makeDouble(0.5);
        MTPNDD b = MTPNDDEngine.makeDouble(0.25);
        assertTrue(MTPNDDEngine.isDoubleLeaf(a));
        assertEquals(0.5, MTPNDDEngine.getDoubleLeaf(a));

        MTPNDD sum = MTPNDDEngine.plus(a, b);
        assertTrue(MTPNDDEngine.isDoubleLeaf(sum));
        assertEquals(0.75, MTPNDDEngine.getDoubleLeaf(sum));

        // Mixing types → exception
        MTPNDD frac = MTPNDDEngine.makeFraction(1, 2);
        assertThrows(MTPNDDException.class, () -> MTPNDDEngine.plus(a, frac));
    }

    @Test
    void leafCount() {
        MTPNDD a = MTPNDDEngine.makeFraction(1, 2);
        assertEquals(1L, MTPNDDEngine.leafCount(a));
        MTPNDD b = MTPNDDEngine.makeFraction(1, 3);
        MTPNDD sum = MTPNDDEngine.plus(a, b);  // leaf 5/6
        assertEquals(1L, MTPNDDEngine.leafCount(sum));
    }
}
