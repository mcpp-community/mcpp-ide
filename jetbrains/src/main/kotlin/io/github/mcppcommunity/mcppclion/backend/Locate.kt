// Where the backend is: the configured path, the one the plugin carries for this system
// (`bin/<platform>/mcpp-ide[.exe]`), the one xlings installed, then PATH.
package io.github.mcppcommunity.mcppclion.backend

import java.io.File

object Locate {
    val windows: Boolean = System.getProperty("os.name").lowercase().startsWith("windows")

    /** "linux-x64", "darwin-arm64", "win32-x64": the same directory names the VS Code extension uses. */
    fun platformDir(os: String = System.getProperty("os.name"), arch: String = System.getProperty("os.arch")): String {
        val o = os.lowercase()
        val system = when {
            o.startsWith("windows") -> "win32"
            o.startsWith("mac") || o.contains("darwin") -> "darwin"
            else -> "linux"
        }
        val machine = when (arch.lowercase()) {
            "amd64", "x86_64" -> "x64"
            "aarch64", "arm64" -> "arm64"
            else -> arch.lowercase()
        }
        return "$system-$machine"
    }

    private fun exe(name: String) = if (windows) "$name.exe" else name

    fun onPath(name: String, path: String? = System.getenv("PATH")): File? =
        (path ?: "").split(File.pathSeparatorChar).filter { it.isNotEmpty() }
            .map { File(it, exe(name)) }.firstOrNull { it.isFile && it.canExecute() }

    data class Located(val program: File, val source: String)

    fun backend(configured: String, pluginRoot: File?): Located? {
        if (configured.isNotBlank()) return Located(File(configured.trim()), "setting")
        pluginRoot?.let { root ->
            val bundled = File(root, "bin/${platformDir()}/${exe("mcpp-ide")}")
            if (bundled.isFile) {
                if (!bundled.canExecute()) bundled.setExecutable(true)
                return Located(bundled, "bundled")
            }
        }
        val home = System.getProperty("user.home")
        listOfNotNull(System.getenv("XLINGS_HOME")?.let { File(it, "subos/current/bin/${exe("mcpp-ide")}") },
            File(home, ".xlings/subos/current/bin/${exe("mcpp-ide")}"),
            File(home, ".xlings/subos/default/bin/${exe("mcpp-ide")}"))
            .firstOrNull { it.isFile }?.let { return Located(it, "xlings") }
        return onPath("mcpp-ide")?.let { Located(it, "PATH") }
    }
}
