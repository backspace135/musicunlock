package io.github.backspace135.musicunlock.core

import android.app.NotificationManager
import android.service.notification.StatusBarNotification
import androidx.core.app.NotificationCompat
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import kotlinx.coroutines.CoroutineStart
import kotlinx.coroutines.TimeoutCancellationException
import kotlinx.coroutines.async
import kotlinx.coroutines.flow.first
import kotlinx.coroutines.runBlocking
import kotlinx.coroutines.withTimeout
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith
import io.github.backspace135.musicunlock.core.ProcessingService.Progress
import io.github.backspace135.musicunlock.core.ProcessingService.Task

/** The foreground service shows each task's progress and relays its stop button. */
@RunWith(AndroidJUnit4::class)
class ProcessingServiceTest {
    private val context = Fixtures.context
    private val notifications = context.getSystemService(NotificationManager::class.java)

    @Before
    fun allowNotifications() {
        ProcessingService.notificationPermission?.let {
            InstrumentationRegistry.getInstrumentation().uiAutomation.grantRuntimePermission(context.packageName, it)
        }
    }

    private suspend fun waitFor(what: String, condition: (List<StatusBarNotification>) -> Boolean) {
        try {
            withTimeout(10_000) {
                while (!condition(notifications.activeNotifications.toList())) kotlinx.coroutines.delay(50)
            }
        } catch (error: TimeoutCancellationException) {
            throw AssertionError("$what: ${notifications.activeNotifications.map { it.id }}", error)
        }
    }

    @Test
    fun notificationsShowProgressAndRelayStop() = runBlocking {
        ProcessingService.start(context, Task.UNLOCK, Progress("正在转换 1 / 3", 0, 3))
        ProcessingService.start(context, Task.LYRICS, Progress("正在导出 1 / 2", 0, 2))
        ProcessingService.update(Task.UNLOCK, Progress("正在转换 2 / 3", 1, 3))
        waitFor("unlock progress") { list -> list.any { it.id == 1001 && it.notification.extras.getString(NotificationCompat.EXTRA_TITLE) == "正在转换 2 / 3" } }
        waitFor("lyrics shown") { list -> list.any { it.id == 1002 } }
        val unlock = notifications.activeNotifications.first { it.id == 1001 }.notification
        assertEquals(1, unlock.extras.getInt(NotificationCompat.EXTRA_PROGRESS))
        assertEquals(3, unlock.extras.getInt(NotificationCompat.EXTRA_PROGRESS_MAX))
        assertTrue(unlock.flags and android.app.Notification.FLAG_FOREGROUND_SERVICE != 0)
        assertEquals("停止", unlock.actions.single().title)

        val stopped = async(start = CoroutineStart.UNDISPATCHED) { ProcessingService.stopRequests.first() }
        unlock.actions.single().actionIntent.send()
        assertEquals(Task.UNLOCK, withTimeout(10_000) { stopped.await() })

        ProcessingService.stop(context, Task.UNLOCK)
        waitFor("unlock stopped") { list -> list.none { it.id == 1001 } && list.any { it.id == 1002 } }
        ProcessingService.stop(context, Task.LYRICS)
        waitFor("all stopped") { list -> list.none { it.id == 1001 || it.id == 1002 } }
    }
}
