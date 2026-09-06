package org.evvdroid

import androidx.lifecycle.Lifecycle
import androidx.test.core.app.ActivityScenario
import androidx.test.ext.junit.runners.AndroidJUnit4
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith

/** That the settings screen builds itself without falling over.
 *
 *  It reads the engine's own voices to fill itself in, and the eight settings
 *  are driven off one list of keys where seven are sliders and gender is a
 *  choice of two, so it is a screen that can fail before it is ever seen. It
 *  has done, twice: once casting the gender control to a slider and once
 *  writing a number into a control that stores text.
 *
 *  ActivityScenario.launch throws if anything in onCreate does, which is the
 *  whole of the check. What state it settles in afterwards is the device's
 *  business -- an activity does not resume behind a locked screen -- so this
 *  asks only that it got past being created. */
@RunWith(AndroidJUnit4::class)
class SettingsScreenTest {

	@Test
	fun itBuildsItself() {
		ActivityScenario.launch(SettingsActivity::class.java).use { scenario ->
			assertTrue(
				"the screen never got past being created, it is ${scenario.state}",
				scenario.state.isAtLeast(Lifecycle.State.CREATED)
			)
		}
	}
}
