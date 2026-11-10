package io.github.mcppcommunity.mcppclion

import com.google.gson.JsonObject
import com.google.gson.JsonParser
import io.github.mcppcommunity.mcppclion.run.TestTree
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

// The test tree of a test job (design 2026-11-10): a program of several files is a suite with a suite
// per file, each case a test inside its file's suite; a case that begins and then ends is reported
// once; a failure with both sides is a comparison failure; the suites close at the end, innermost first.
class TestTreeTest {
    private fun json(text: String): JsonObject = JsonParser.parseString(text).asJsonObject
    private fun tree() = TestTree(mapOf("unit" to "/p/tests/unit/a.cpp", "unit/b" to "/p/tests/unit/b.cpp"), { it }) { _, _, case, file -> "file://$file:$case" }

    @Test
    fun aProgramOfSeveralFilesNestsItsCasesInTheirFiles() {
        val tree = tree()
        val text = tree.messages("test/result", json("""{"root":"/p","test":"unit/b","program":"unit","part":"unit/b","status":"failed",
            "cases":[{"case":"beta","status":"failed","durationMs":2,"message":"b.cpp:5: sum == 4",
                      "failures":[{"path":"tests/unit/b.cpp","line":4,"message":"sum == 4","expected":"4","actual":"5"}],
                      "location":{"path":"tests/unit/b.cpp","line":4}}]}"""))
        assertTrue(text, text.contains("testSuiteStarted nodeId='p:unit' parentNodeId='0' name='unit' locationHint='file:///p/tests/unit/a.cpp'"))
        assertTrue(text, text.contains("testSuiteStarted nodeId='f:unit/b' parentNodeId='p:unit' name='b'"))
        assertTrue(text, text.contains("testStarted nodeId='c:unit\u001fbeta' parentNodeId='f:unit/b' name='beta'"))
        assertTrue(text, text.contains("type='comparisonFailure' expected='4' actual='5'"))
        assertTrue(text, text.contains("details='tests/unit/b.cpp:5"))
        assertTrue(text, text.contains("testFinished nodeId='c:unit\u001fbeta' duration='2'"))
        assertEquals("##teamcity[testSuiteFinished nodeId='f:unit/b']\n##teamcity[testSuiteFinished nodeId='p:unit']\n", tree.close())
    }

    @Test
    fun aLiveCaseIsReportedOnceWithItsMetrics() {
        val tree = tree()
        val begin = tree.messages("test/caseEvent", json("""{"root":"/p","program":"unit","part":"unit/a","case":"alpha","event":"begin"}"""))
        assertTrue(begin, begin.contains("testStarted nodeId='c:unit\u001falpha'") && !begin.contains("testFinished"))
        val end = tree.messages("test/caseEvent", json("""{"root":"/p","program":"unit","part":"unit/a","case":"alpha","event":"end","status":"passed",
            "durationMs":1,"metrics":[{"name":"ready","value":4.2,"unit":"s"}],"bounds":[{"metric":"ready","max":15}]}"""))
        assertTrue(end, !end.contains("testStarted") && end.contains("ready 4.2 s ≤ 15 s") && end.contains("testFinished nodeId='c:unit\u001falpha' duration='1'"))
        // The part's record comes after: its cases were already reported.
        val record = tree.messages("test/result", json("""{"root":"/p","test":"unit/a","program":"unit","part":"unit/a","status":"passed",
            "cases":[{"case":"alpha","status":"passed","durationMs":1}]}"""))
        assertEquals("", record)
    }

    @Test
    fun aProgramWithoutCasesIsOneTest() {
        val tree = tree()
        val text = tree.messages("test/result", json("""{"root":"/p","test":"smoke","status":"timedOut","timedOut":true,"durationMs":9,"output":"x","reason":""}"""))
        assertTrue(text, text.contains("testStarted nodeId='t:smoke' parentNodeId='0' name='smoke'"))
        assertTrue(text, text.contains("testFailed nodeId='t:smoke' message='The test timed out'"))
        assertEquals("", tree.close())
    }
}
