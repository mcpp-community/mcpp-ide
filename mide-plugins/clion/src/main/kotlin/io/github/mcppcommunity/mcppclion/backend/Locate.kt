// Where the backend is, as the command that starts it (mcpp design 2026-10-28 part 2 §15), in the
// same order as the VS Code extension:
//
//   1. mcpp.ide.backendPath: a program, or an ok-image bundle (`*.okb`) run by okrun;
//   2. the bundle the plugin carries, `backend/mcpp-ide.okb`, run by the okrun it carries for this
//      system (`backend/okrun/<os>-<arch>/okrun[.exe]`), else by okrun on PATH;
//   3. the mcpp-ide xlings installed, then PATH.
package io.github.mcppcommunity.mcppclion.backend

import com.google.gson.JsonParser
import java.io.File

object Locate {
    val windows: Boolean = System.getProperty("os.name").lowercase().startsWith("windows")

    private fun system(os: String): String {
        val o = os.lowercase()
        return when {
            o.startsWith("windows") -> "windows"
            o.startsWith("mac") || o.contains("darwin") -> "macos"
            else -> "linux"
        }
    }

    private fun machine(arch: String): String = when (arch.lowercase()) {
        "amd64", "x86_64", "x64" -> "x86_64"
        "aarch64", "arm64" -> "aarch64"
        else -> arch.lowercase()
    }

    /** "linux-x86_64", "macos-aarch64", "windows-x86_64": mcpp's names, which the okrun directories use. */
    fun okrunDir(os: String = System.getProperty("os.name"), arch: String = System.getProperty("os.arch")): String =
        "${system(os)}-${machine(arch)}"

    private fun exe(name: String, onWindows: Boolean = windows) = if (onWindows) "$name.exe" else name

    fun onPath(name: String, path: String? = System.getenv("PATH")): File? =
        (path ?: "").split(File.pathSeparatorChar).filter { it.isNotEmpty() }
            .map { File(it, exe(name)) }.firstOrNull { it.isFile && it.canExecute() }

    data class Located(val command: List<String>, val source: String)

    private fun runnable(f: File): File {
        if (!windows && !f.canExecute()) f.setExecutable(true)
        return f
    }

    /**
     * A payload of mcppls (`payload.json` beside `bin/`), with the programs that start its server made
     * executable again (mcppls specification S6): every platform's runner of a version 5 payload, the
     * runner of a version 4 one, else a program server. A plugin's install may not keep the bit;
     * mcppls-clion puts it back when it starts its server, and the backend, which chooses this system's
     * entry, may run that program before, to read mcppls's version. Only a relative path with no `..`
     * part is followed.
     */
    fun runnablePayload(payload: File): File {
        val manifest = runCatching { JsonParser.parseString(File(payload, "payload.json").readText()).asJsonObject }.getOrNull()
        val server = manifest?.get("server")?.takeIf { it.isJsonObject }?.asJsonObject
        fun named(obj: com.google.gson.JsonObject?, key: String): String? =
            obj?.get(key)?.takeIf { it.isJsonPrimitive }?.asString
        val runners = manifest?.get("platforms")?.takeIf { it.isJsonObject }?.asJsonObject?.entrySet()
            ?.mapNotNull { (_, entry) -> entry.takeIf { it.isJsonObject }?.asJsonObject?.let { named(it, "runner") } }
            ?: listOfNotNull(named(server, "runner") ?: named(server, "path"))
        runners.filter { !File(it).isAbsolute && it.split('/', '\\').none { part -> part == ".." } }
            .map { File(payload, it) }.filter { it.isFile }.forEach { runnable(it) }
        return payload
    }

    fun backend(configured: String, pluginRoot: File?, path: String? = System.getenv("PATH"),
                os: String = System.getProperty("os.name"), arch: String = System.getProperty("os.arch"),
                home: String = System.getProperty("user.home")): Located? {
        val onWin = system(os) == "windows"
        val bundledOkrun = pluginRoot?.let { File(it, "backend/okrun/${okrunDir(os, arch)}/${exe("okrun", onWin)}") }
        fun okrun(): File? = bundledOkrun?.takeIf { it.isFile }?.let(::runnable) ?: onPath("okrun", path)

        val c = configured.trim()
        if (c.isNotEmpty()) {
            if (c.endsWith(".okb")) return okrun()?.let { Located(listOf(it.path, c), "setting") }
            return Located(listOf(c), "setting")
        }
        pluginRoot?.let { root ->
            val bundle = File(root, "backend/mcpp-ide.okb")
            if (bundle.isFile) okrun()?.let { return Located(listOf(it.path, bundle.path), "bundle") }
        }
        listOfNotNull(System.getenv("XLINGS_HOME")?.let { File(it, "subos/current/bin/${exe("mcpp-ide", onWin)}") },
            File(home, ".xlings/subos/current/bin/${exe("mcpp-ide", onWin)}"),
            File(home, ".xlings/subos/default/bin/${exe("mcpp-ide", onWin)}"))
            .firstOrNull { it.isFile }?.let { return Located(listOf(it.path), "xlings") }
        return onPath("mcpp-ide", path)?.let { Located(listOf(it.path), "PATH") }
    }
}
