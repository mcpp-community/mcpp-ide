// Links in the console: `path:line:column:` as a compiler or mcpp prints it opens that place, a
// relative path read against the project.
package io.github.mcppcommunity.mcppclion.run

import com.intellij.execution.filters.ConsoleFilterProvider
import com.intellij.execution.filters.Filter
import com.intellij.execution.filters.OpenFileHyperlinkInfo
import com.intellij.openapi.project.Project
import com.intellij.openapi.vfs.LocalFileSystem
import java.io.File

class McppConsoleFilterProvider : ConsoleFilterProvider {
    override fun getDefaultFilters(project: Project): Array<Filter> = arrayOf(McppConsoleFilter(project))
}

class McppConsoleFilter(private val project: Project) : Filter {
    private val location = Regex("""((?:[A-Za-z]:)?[^\s:'"()]+\.(?:cppm|cpp|cc|cxx|ixx|mpp|c|h|hpp|hxx|toml|mcpp)):(\d+)(?::(\d+))?""")

    override fun applyFilter(line: String, entireLength: Int): Filter.Result? {
        val start = entireLength - line.length
        val items = location.findAll(line).mapNotNull { m ->
            val path = m.groupValues[1]
            val file = File(path).let { if (it.isAbsolute) it else File(project.basePath ?: return@mapNotNull null, path) }
            val vf = LocalFileSystem.getInstance().findFileByIoFile(file) ?: return@mapNotNull null
            val lineNo = (m.groupValues[2].toIntOrNull() ?: 1) - 1
            val column = (m.groupValues[3].toIntOrNull() ?: 1) - 1
            Filter.ResultItem(start + m.range.first, start + m.range.last + 1, OpenFileHyperlinkInfo(project, vf, lineNo, column))
        }.toList()
        return if (items.isEmpty()) null else Filter.Result(items)
    }
}
