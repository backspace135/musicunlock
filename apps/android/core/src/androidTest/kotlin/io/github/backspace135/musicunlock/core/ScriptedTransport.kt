package io.github.backspace135.musicunlock.core

import java.util.concurrent.CopyOnWriteArrayList

/** Canned platform responses by URL fragment, like the core tests' scripted transports. Records every request. */
class ScriptedTransport(private vararg val routes: Pair<String, () -> HttpResponse>) : HttpTransport {
    val requests = CopyOnWriteArrayList<HttpRequest>()

    override fun execute(request: HttpRequest): HttpResponse {
        requests += request
        val route = routes.firstOrNull { (fragment, _) -> fragment in request.url }
            ?: throw LyricException("网络连接失败，请检查网络或代理设置后重试。")
        return route.second()
    }

    companion object {
        fun json(body: String, vararg cookies: String) = HttpResponse(200, body.toByteArray(), cookies.toList())

        /** A NetEase search with two songs, and lyrics with a translation for every song. */
        fun netease() = ScriptedTransport(
            "/api/search/get/web" to {
                json("""{"code":200,"result":{"songCount":2,"songs":[
                    {"id":186016,"name":"晴天","artists":[{"name":"周杰伦"}],"album":{"name":"叶惠美"},"duration":269000},
                    {"id":2,"name":"晴天（翻唱）","artists":[{"name":"歌手"}],"album":{"name":"专辑"},"duration":1000}]}}""")
            },
            "/api/song/lyric/v1" to { json("""{"code":200}""") },
            "/api/song/lyric" to {
                json("""{"code":200,"lrc":{"lyric":"[00:01.00]故事的小黄花\n[00:03.00]从出生那年就飘着"},
                    "tlyric":{"lyric":"[00:01.00]Little yellow flowers"}}""")
            },
        )
    }
}
