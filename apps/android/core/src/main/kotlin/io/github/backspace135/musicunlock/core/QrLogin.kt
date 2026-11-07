package io.github.backspace135.musicunlock.core

import android.graphics.Bitmap
import android.graphics.Color
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.channels.awaitClose
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.callbackFlow
import kotlinx.coroutines.flow.flowOn
import kotlinx.coroutines.launch
import org.json.JSONObject

/** A QR login's state (qr_login.h `QrLoginState`). */
enum class QrLoginState {
    LOADING,
    WAITING,
    SCANNED,
    CONFIRMED,
    EXPIRED,
    FAILED;

    /** The core's default status line, e.g. "请使用网易云音乐扫码". */
    val defaultMessage: String get() = Lyrics.label(Lyrics.QR_MESSAGE, ordinal)

    companion object {
        /** 800 expired, 801 waiting, 802 scanned, 803 confirmed; any other code is failed. */
        fun fromCode(code: Int): QrLoginState = entries[Lyrics.qrState(code)]

        /** What to show when NetEase refuses a QR login (e.g. code 8821), pointing to the official login page. */
        fun refusalMessage(code: Long, platformMessage: String): String = Lyrics.qrRefusal(code, platformMessage)
    }
}

/** The module grid of a QR code with medium error correction; draw it white on black in every theme. */
class QrMatrix internal constructor(val size: Int, private val modules: BooleanArray) {
    fun dark(x: Int, y: Int): Boolean = modules[y * size + x]

    /** SVG path data of the dark modules, one unit per module, no quiet zone ("M0,0h7v1h-7z..."). */
    var pathData: String = ""
        internal set

    /**
     * A black-on-white bitmap with a [quietZone]-module white border, [moduleSize] pixels per module: for display
     * (`Image(bitmap.asImageBitmap(), …, filterQuality = FilterQuality.None)`) or for saving to the gallery.
     */
    fun toBitmap(moduleSize: Int = 8, quietZone: Int = 4): Bitmap {
        val side = (size + quietZone * 2) * moduleSize
        val pixels = IntArray(side * side) { Color.WHITE }
        for (y in 0 until size) for (x in 0 until size) {
            if (!dark(x, y)) continue
            for (dy in 0 until moduleSize) {
                val row = ((y + quietZone) * moduleSize + dy) * side
                val column = (x + quietZone) * moduleSize
                pixels.fill(Color.BLACK, row + column, row + column + moduleSize)
            }
        }
        return Bitmap.createBitmap(pixels, side, side, Bitmap.Config.ARGB_8888)
    }

    companion object {
        /** The QR code of [text] (qr_login.h `qr_matrix`). */
        fun of(text: String): QrMatrix {
            val json = JSONObject(Lyrics.qrCode(text))
            val modules = json.getString("modules")
            return QrMatrix(json.getInt("size"), BooleanArray(modules.length) { modules[it] == '1' }).apply {
                pathData = json.getString("path")
            }
        }
    }
}

/**
 * One state of a QR login, as [QrLogin.start] emits them. [message] is the core's text to show; [matrix] and [url] are
 * set from WAITING on (the code to draw, and its link for "在网易云音乐中打开"); [session] holds the accepted cookies
 * once CONFIRMED (pass them to [AccountStore.accept]).
 */
data class QrLoginUpdate(
    val state: QrLoginState,
    val message: String,
    val matrix: QrMatrix? = null,
    val url: String? = null,
    val session: List<Cookie> = emptyList(),
)

/** Receives poll_qr_login's reports from C++. Kept by consumer-rules.pro. */
internal fun interface QrPollListener {
    fun report(state: QrLoginState, message: String)
}

/**
 * The NetEase QR login of the core (create_qr_code + poll_qr_login, the PC client's eapi requests that #23 / #25
 * fixed). Do not build these requests yourself.
 *
 * ```
 * val job = scope.launch {
 *     QrLogin().start().collect { update ->
 *         show(update)                                   // state, message, matrix
 *         if (update.state == QrLoginState.CONFIRMED) {
 *             notice = AccountStore.accept(MusicProvider.NETEASE, update.session)
 *                 ?: "平台没有返回有效登录，请使用官方登录页重试。"
 *         }
 *     }
 * }
 * job.cancel()   // refresh, close or switch to the web login: stops polling at once
 * ```
 */
class QrLogin(private val api: MusicApi = MusicApi.shared) {
    /**
     * A cold flow: LOADING, then WAITING with the code, SCANNED when the phone scanned it, and finally CONFIRMED,
     * EXPIRED or FAILED, after which it completes. Polls every [intervalMillis] up to [attempts] times (the Windows
     * app: 2 s, 90 times). Cancelling the collector cancels the request in flight and the polling.
     */
    fun start(intervalMillis: Long = 2_000, attempts: Int = 90): Flow<QrLoginUpdate> = callbackFlow {
        send(QrLoginUpdate(QrLoginState.LOADING, QrLoginState.LOADING.defaultMessage))
        val cancel = CancelToken()
        val ticket = try {
            api.createQrCode(cancel)
        } catch (error: LyricException) {
            send(QrLoginUpdate(QrLoginState.FAILED, error.message))
            close()
            return@callbackFlow
        } catch (error: CoreException) {
            send(QrLoginUpdate(QrLoginState.FAILED, CONNECTION_FAILED))
            close()
            return@callbackFlow
        }
        val matrix = QrMatrix.of(ticket.url)
        send(QrLoginUpdate(QrLoginState.WAITING, QrLoginState.WAITING.defaultMessage, matrix, ticket.url))
        launch {
            val step = blockingCall(cancel) { token ->
                api.pollQrLogin(ticket, token, intervalMillis, attempts) { state, message ->
                    trySend(QrLoginUpdate(state, message, matrix, ticket.url))
                }
            }
            val state = QrLoginState.entries[step.getInt("state")]
            send(QrLoginUpdate(state, step.getString("message"), matrix, ticket.url,
                Cookie.list(step.getJSONArray("session"))))
            close()
        }
        awaitClose { cancel.cancel() }
    }.flowOn(Dispatchers.IO)

    private companion object {
        const val CONNECTION_FAILED = "无法连接网易云音乐，请检查网络后刷新二维码。"
    }
}
