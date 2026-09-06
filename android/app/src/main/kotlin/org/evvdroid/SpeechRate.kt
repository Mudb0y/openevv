package org.evvdroid

/**
 * Android's speech rate, turned into one of the engine's speed numbers.
 *
 * Android means a multiplier: 100 is the voice as it is, 200 is twice as fast,
 * and the system slider goes well above that. The engine's speed is 0 to 250
 * and is nothing like linear in how fast it actually speaks. Measured on one
 * sentence, with everything else about the voice held still:
 *
 *     speed   50   100   150    200    250
 *     times  1.0   2.7   7.0   16.6   21.2
 *
 * So multiplying the speed number by the percentage, which is what this used to
 * do, is wrong by a lot at the top: a rate of 400 asked for four times and got
 * speed 200, which is sixteen times and far too fast to follow. It is what made
 * the engine unusable on a watch, where the rate had been turned up.
 *
 * [TIMES] is that measurement. Reading it forwards says how many times the
 * default speed a given speed number is; reading it backwards turns a wanted
 * multiplier back into a speed number. Both interpolate between the points.
 *
 * The curve flattens near the top, so very large rates saturate rather than
 * running away. That is the engine's own ceiling and not a limit imposed here.
 */
object SpeechRate {

	/** Speed numbers, and how many times the speed-50 rate each one speaks at.
	 *  Measured with one sentence at 11,025 Hz; the numbers came out identical
	 *  for two voices with different defaults, so speed is the only thing that
	 *  decides duration and one table serves every voice. */
	private val SPEED = intArrayOf(0, 5, 10, 20, 30, 40, 50, 60, 70, 85, 100, 125, 150, 175, 200, 225, 250)

	private val TIMES = doubleArrayOf(
		0.400, 0.416, 0.450, 0.552, 0.675, 0.823, 1.000, 1.226, 1.504,
		1.976, 2.740, 4.383, 6.986, 11.801, 16.633, 20.427, 21.170
	)

	/** How many times the speed-50 rate [speed] speaks at. */
	fun timesFor(speed: Int): Double {
		val want = speed.coerceIn(SPEED.first(), SPEED.last())
		for (i in 1 until SPEED.size) {
			if (want > SPEED[i]) continue
			val span = (SPEED[i] - SPEED[i - 1]).toDouble()
			val along = (want - SPEED[i - 1]) / span
			return TIMES[i - 1] + along * (TIMES[i] - TIMES[i - 1])
		}
		return TIMES.last()
	}

	/** The speed number that speaks [times] the speed-50 rate. */
	fun speedFor(times: Double): Int {
		if (times <= TIMES.first()) return SPEED.first()
		for (i in 1 until TIMES.size) {
			if (times > TIMES[i]) continue
			val span = TIMES[i] - TIMES[i - 1]
			val along = if (span <= 0.0) 0.0 else (times - TIMES[i - 1]) / span
			return Math.round(SPEED[i - 1] + along * (SPEED[i] - SPEED[i - 1])).toInt()
		}
		return SPEED.last()
	}

	/**
	 * The speed number for [percent] of the rate [base] speaks at, where 100 is
	 * the voice left alone.
	 */
	fun speedForPercent(base: Int, percent: Int): Int {
		if (percent == 100) return base
		val want = timesFor(base) * percent.coerceAtLeast(0) / 100.0
		return speedFor(want).coerceIn(0, Eci.SPEED_MAX)
	}
}
