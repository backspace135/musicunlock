package io.github.backspace135.musicunlock.ui.nav

import androidx.compose.runtime.saveable.SaverScope
import androidx.lifecycle.ViewModel
import androidx.lifecycle.ViewModelProvider
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class NavigatorTest {
    private fun navigator() = Navigator(listOf(PageEntry(0, Route.Root)), nextId = 1)

    @Test
    fun rootCannotBePopped() {
        val navigator = navigator()
        navigator.pop()
        assertEquals(listOf(Route.Root), navigator.routes)
        assertFalse(navigator.canGoBack)
    }

    @Test
    fun pushAndPopBeforeTheStackIsShownApplyImmediately() {
        val navigator = navigator()
        navigator.push(Route.Root)
        navigator.push(Route.Root)
        assertEquals(3, navigator.routes.size)
        navigator.pop()
        assertEquals(2, navigator.routes.size)
        navigator.popToRoot()
        assertEquals(listOf(Route.Root), navigator.routes)
    }

    @Test
    fun poppedPagesClearTheirViewModels() {
        val navigator = navigator()
        navigator.push(Route.Root)
        val page = navigator.entries.last()
        val model = ViewModelProvider.create(page, ViewModelProvider.NewInstanceFactory())[Probe::class.java]
        navigator.pop()
        assertTrue(model.cleared)
    }

    @Test
    fun savedStackRestoresRoutesAndIds() {
        val navigator = navigator()
        navigator.push(Route.Root)
        val saver = Navigator.saver()
        val saved = with(saver) { SaverScope { true }.save(navigator) }!!
        val restored = saver.restore(saved)!!
        assertEquals(navigator.entries.map { it.id }, restored.entries.map { it.id })
        restored.push(Route.Root)
        assertEquals(2L, restored.entries.last().id)
    }

    class Probe : ViewModel() {
        var cleared = false
        override fun onCleared() {
            cleared = true
        }
    }
}
