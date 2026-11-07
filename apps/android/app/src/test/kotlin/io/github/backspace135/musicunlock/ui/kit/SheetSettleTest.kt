package io.github.backspace135.musicunlock.ui.kit

import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class SheetSettleTest {
    private val height = 1000f
    private val projection = 0.15f

    @Test
    fun releasedNearTheTopStaysOpen() {
        assertFalse(sheetShouldDismiss(offset = 200f, velocity = 0f, height = height, projectionSeconds = projection))
    }

    @Test
    fun releasedPastHalfwayCloses() {
        assertTrue(sheetShouldDismiss(offset = 600f, velocity = 0f, height = height, projectionSeconds = projection))
    }

    @Test
    fun downwardFlingClosesFromNearTheTop() {
        assertTrue(sheetShouldDismiss(offset = 150f, velocity = 3000f, height = height, projectionSeconds = projection))
    }

    @Test
    fun upwardFlingKeepsItOpenFromLowDown() {
        assertFalse(sheetShouldDismiss(offset = 700f, velocity = -3000f, height = height, projectionSeconds = projection))
    }

    @Test
    fun unmeasuredSheetCloses() {
        assertTrue(sheetShouldDismiss(offset = 0f, velocity = 0f, height = 0f, projectionSeconds = projection))
    }
}
