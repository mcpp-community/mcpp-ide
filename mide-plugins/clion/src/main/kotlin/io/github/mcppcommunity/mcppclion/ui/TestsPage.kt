// What the last runs of the project's tests say (design 2026-11-10) -- the run, its failures and since
// when, each metric against its bound, the slowest and the flaky cases, the schedule with its critical
// path -- as the page webui/tests.html draws the backend's `test/insights` in JCEF, the store mcpp keeps
// read by the backend. It is drawn again when a test job ends; a click on a failure opens its place.
package io.github.mcppcommunity.mcppclion.ui

import com.google.gson.JsonObject
import com.google.gson.JsonParser
import com.intellij.openapi.project.Project
import com.intellij.ui.JBColor
import io.github.mcppcommunity.mcppclion.McppSession

class TestsPage(project: Project) :
    JcefPage(project, "tests.html", McppSession.get(project).t("This IDE runs without JCEF, which draws the test insights.")) {
    override val id = "tests"
    private val unsubscribe = session.on("job/finished") { p ->
        if (p.isJsonObject && p.asJsonObject["kind"]?.asString == "test") refresh()
    }

    private fun labels() = JsonObject().apply {
        addProperty("title", session.t("Test Insights"))
        addProperty("noRun", session.t("No run is kept yet: mcpp test keeps each run in {0} (mcpp 2026.11.9.1 and later)."))
        addProperty("window", session.t("{0} runs kept · the last began {1}"))
        addProperty("failures", session.t("Failures"))
        addProperty("fresh", session.t("new"))
        addProperty("since", session.t("since {0}"))
        addProperty("expected", session.t("expected"))
        addProperty("actual", session.t("actual"))
        addProperty("metrics", session.t("Metrics"))
        addProperty("slowest", session.t("Slowest"))
        addProperty("median", session.t("median {0}"))
        addProperty("flaky", session.t("Flaky"))
        addProperty("flakyCounts", session.t("{0} passed · {1} failed · {2} changes"))
        addProperty("schedule", session.t("Schedule"))
        addProperty("worker", session.t("worker {0}"))
        addProperty("criticalPath", session.t("Critical path"))
        addProperty("noMetrics", session.t("Metrics appear when the project's mcpp states them (test-metrics, mcpp#917)."))
    }

    override fun refresh() {
        val root = session.root ?: return
        session.request("test/insights", JsonObject().apply { addProperty("root", root) }).whenComplete { insights, _ ->
            if (insights == null || !insights.isJsonObject) return@whenComplete
            show(JsonObject().apply {
                add("insights", insights)
                add("labels", labels())
                addProperty("dark", !JBColor.isBright())
            })
        }
    }

    /** `{"open": {"path": ..., "line": ...}}`: a failure's place in an editor. */
    override fun received(text: String) {
        val at = runCatching { JsonParser.parseString(text).asJsonObject["open"]?.asJsonObject }.getOrNull() ?: return
        openSource(at["path"]?.asString ?: return, at["line"]?.asInt ?: 0)
    }

    override fun dispose() {
        unsubscribe()
    }
}
