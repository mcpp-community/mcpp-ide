// A graph of the project -- its packages or its modules -- as the page webui/graph.html draws it in
// JCEF (mcpp design 2026-10-30 part 2 §9). The page receives the backend's `graph/packages` or
// `graph/modules` and the strings it shows, and asks the IDE to open a module's source; it reaches
// nothing else. Without JCEF the tab says so.
package io.github.mcppcommunity.mcppclion.ui

import com.google.gson.JsonArray
import com.google.gson.JsonObject
import com.google.gson.JsonParser
import com.intellij.openapi.project.Project
import com.intellij.ui.JBColor
import io.github.mcppcommunity.mcppclion.McppSession

class GraphPage(project: Project, private val kind: String) :
    JcefPage(project, "graph.html", McppSession.get(project).t("This IDE runs without JCEF, which draws the graph.")) {
    override val id = "graph:$kind"

    override fun refresh() {
        val root = session.root ?: return
        session.request("graph/$kind", JsonObject().apply { addProperty("root", root) }).whenComplete { graph, error ->
            show(JsonObject().apply {
                addProperty("kind", kind)
                add("graph", graph?.takeIf { it.isJsonObject } ?: JsonObject().apply { add("nodes", JsonArray()); add("edges", JsonArray()) })
                addProperty("title", if (kind == "modules") session.t("Module Graph") else session.t("Dependency Graph"))
                addProperty("empty", error?.let { session.t("mcpp: {0}", (it.cause ?: it).message ?: "") } ?: session.t("Nothing to draw yet: build the project first."))
                addProperty("truncated", session.t("Only the first {0} nodes are drawn."))
                addProperty("dark", !JBColor.isBright())
            })
        }
    }

    /** `{"open": "<source>"}`: the module's source in an editor. */
    override fun received(text: String) {
        val source = runCatching { JsonParser.parseString(text).asJsonObject["open"]?.asString }.getOrNull() ?: return
        openSource(source)
    }
}
