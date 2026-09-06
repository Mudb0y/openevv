package org.evvdroid

import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.selection.selectable
import androidx.compose.foundation.selection.selectableGroup
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.RadioButton
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Slider
import androidx.compose.material3.Switch
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.material3.TopAppBar
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.ProgressBarRangeInfo
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.clearAndSetSemantics
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.progressBarRangeInfo
import androidx.compose.ui.semantics.role
import androidx.compose.ui.semantics.setProgress
import androidx.compose.ui.semantics.stateDescription
import androidx.compose.ui.unit.dp
import kotlin.math.roundToInt

/**
 * Every setting the engine has, and a button to hear them.
 *
 * Two things about it are deliberate and easy to undo by accident.
 *
 * A control is one thing to stop at. A label, a bar and a number are three
 * stops for a screen reader, so each row merges into a single node that says
 * what it is and what it is set to, and adjusts in place.
 *
 * And the numbers are percentages. The engine's own are on three scales --
 * gender is a choice of two, speed runs to 250, the rest to a hundred -- and a
 * bare number leaves it to the listener to remember which scale they are on.
 */
@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun SettingsScreen(state: SettingsModel) {
	var wanted by remember { mutableStateOf(Eci.DICT_MAIN) }
	val pick = rememberLauncherForActivityResult(ActivityResultContracts.OpenDocument()) { uri ->
		uri?.let { state.chooseDictionary(wanted, it) }
	}
	Scaffold(
		topBar = { TopAppBar(title = { Text(stringResource(R.string.app_name)) }) }
	) { inset ->
		Column(
			modifier = Modifier
				.fillMaxSize()
				.padding(inset)
				.verticalScroll(rememberScrollState())
				.padding(bottom = 24.dp)
		) {
			ChoiceRow(
				label = stringResource(R.string.voice_label),
				options = Eci.PRESET_NAMES.toList(),
				chosen = state.voice,
				onChoose = state::chooseVoice
			)
			Gap()
			ChoiceRow(
				label = stringResource(R.string.gender_label),
				options = listOf(
					stringResource(R.string.gender_male),
					stringResource(R.string.gender_female)
				),
				chosen = state.gender,
				onChoose = { state.setShape(Eci.VOICE_GENDER, it) }
			)

			Gap()
			for (param in Settings.SLIDERS) {
				SliderRow(
					label = stringResource(labelOf(param)),
					percent = state.percentOf(param),
					onPercent = { state.setPercent(param, it) }
				)
			}
			Gap()
			ActionRow(stringResource(R.string.speak_label), state::say)
			ActionRow(stringResource(R.string.reset_label), state::resetVoice)

			Gap()
			ChoiceRow(
				label = stringResource(R.string.pauses_label),
				options = listOf(
					stringResource(R.string.pauses_keep),
					stringResource(R.string.pauses_end),
					stringResource(R.string.pauses_all)
				),
				chosen = state.pauses,
				onChoose = state::choosePauses
			)
			SwitchRow(
				label = stringResource(R.string.phrase_prediction_label),
				checked = state.phrasePrediction,
				onChange = state::choosePhrasePrediction
			)
			SwitchRow(
				label = stringResource(R.string.abbreviations_label),
				checked = state.abbreviations,
				onChange = state::chooseAbbreviations
			)
			Gap()
			for (volume in Eci.DICT_VOLUMES) {
				ValueRow(
					label = stringResource(dictionaryLabelOf(volume)),
					value = state.dictionaryName(volume),
					onClick = { wanted = volume; pick.launch(arrayOf("*/*")) }
				)
			}
			ActionRow(stringResource(R.string.dictionary_remove), state::removeDictionaries)

			Gap()
			ChoiceRow(
				label = stringResource(R.string.sample_rate_label),
				options = SettingsModel.RATES.map { stringResource(R.string.hertz, it) },
				chosen = SettingsModel.RATES.indexOf(state.sampleRateHz).coerceAtLeast(0),
				onChoose = { state.chooseSampleRate(SettingsModel.RATES[it]) }
			)
		}
	}
}

private fun dictionaryLabelOf(volume: Int): Int = when (volume) {
	Eci.DICT_MAIN -> R.string.dictionary_main
	Eci.DICT_ROOT -> R.string.dictionary_root
	else -> R.string.dictionary_abbreviation
}

private fun labelOf(param: Int): Int = when (param) {
	Eci.VOICE_SPEED -> R.string.speed_label
	Eci.VOICE_PITCH_BASELINE -> R.string.pitch_label
	Eci.VOICE_PITCH_FLUCTUATION -> R.string.inflection_label
	Eci.VOICE_HEAD_SIZE -> R.string.head_size_label
	Eci.VOICE_ROUGHNESS -> R.string.roughness_label
	Eci.VOICE_BREATHINESS -> R.string.breathiness_label
	else -> R.string.volume_label
}

/** Air between one group of settings and the next. There were headings here,
 *  and a heading is a stop of its own for a screen reader that says nothing
 *  the labels under it do not. A spacer carries no semantics at all. */
@Composable
private fun Gap() {
	Spacer(modifier = Modifier.height(16.dp))
}

/**
 * A label, the value it is at, and a bar, as one thing.
 *
 * clearAndSetSemantics throws away what the label and the bar would each have
 * said and puts one control there instead: the name, the value, the range it
 * moves in, and how to move it. Without it a reader finds the text and the bar
 * separately and neither says what the other is.
 */
@Composable
private fun SliderRow(label: String, percent: Int, onPercent: (Int) -> Unit) {
	val spoken = stringResource(R.string.percent_spoken, percent)
	Column(
		modifier = Modifier
			.fillMaxWidth()
			.padding(horizontal = 16.dp, vertical = 8.dp)
			.clearAndSetSemantics {
				contentDescription = label
				stateDescription = spoken
				progressBarRangeInfo = ProgressBarRangeInfo(percent.toFloat(), 0f..100f, 99)
				setProgress { want ->
					onPercent(want.roundToInt())
					true
				}
			}
	) {
		Text(
			text = stringResource(
				R.string.labelled_value,
				label,
				stringResource(R.string.percent, percent)
			),
			style = MaterialTheme.typography.labelLarge
		)
		Slider(
			value = percent.toFloat(),
			onValueChange = { onPercent(it.roundToInt()) },
			valueRange = 0f..100f,
			steps = 0
		)
	}
}

@Composable
private fun ChoiceRow(
	label: String,
	options: List<String>,
	chosen: Int,
	onChoose: (Int) -> Unit
) {
	var open by remember { mutableStateOf(false) }
	val now = options.getOrElse(chosen) { "" }
	Text(
		text = stringResource(R.string.labelled_value, label, now),
		style = MaterialTheme.typography.bodyLarge,
		modifier = Modifier
			.fillMaxWidth()
			.clickable { open = true }
			.padding(horizontal = 16.dp, vertical = 14.dp)
			.clearAndSetSemantics {
				contentDescription = label
				stateDescription = now
				role = Role.Button
			}
	)
	if (open) {
		AlertDialog(
			onDismissRequest = { open = false },
			title = { Text(label) },
			text = {
				Column(modifier = Modifier.selectableGroup()) {
					options.forEachIndexed { at, option ->
						Row(
							modifier = Modifier
								.fillMaxWidth()
								.selectable(
									selected = at == chosen,
									onClick = {
										onChoose(at)
										open = false
									},
									role = Role.RadioButton
								)
								.padding(vertical = 12.dp),
							verticalAlignment = Alignment.CenterVertically
						) {
							RadioButton(selected = at == chosen, onClick = null)
							Text(
								text = option,
								modifier = Modifier.padding(start = 12.dp),
								style = MaterialTheme.typography.bodyLarge
							)
						}
					}
				}
			},
			confirmButton = {
				TextButton(onClick = { open = false }) {
					Text(stringResource(R.string.cancel))
				}
			}
		)
	}
}

@Composable
private fun SwitchRow(label: String, checked: Boolean, onChange: (Boolean) -> Unit) {
	val on = stringResource(if (checked) R.string.switch_on else R.string.switch_off)
	Row(
		modifier = Modifier
			.fillMaxWidth()
			.clickable { onChange(!checked) }
			.padding(horizontal = 16.dp, vertical = 14.dp)
			.clearAndSetSemantics {
				contentDescription = label
				stateDescription = on
				role = Role.Switch
			},
		verticalAlignment = Alignment.CenterVertically
	) {
		Text(
			text = label,
			style = MaterialTheme.typography.bodyLarge,
			modifier = Modifier.weight(1f)
		)
		Switch(checked = checked, onCheckedChange = null)
	}
}

/** A label, what it is set to, and something to press to change it. */
@Composable
private fun ValueRow(label: String, value: String, onClick: () -> Unit) {
	Text(
		text = stringResource(R.string.labelled_value, label, value),
		style = MaterialTheme.typography.bodyLarge,
		modifier = Modifier
			.fillMaxWidth()
			.clickable(onClick = onClick)
			.padding(horizontal = 16.dp, vertical = 14.dp)
			.clearAndSetSemantics {
				contentDescription = label
				stateDescription = value
				role = Role.Button
			}
	)
}

@Composable
private fun ActionRow(label: String, onClick: () -> Unit) {
	Text(
		text = label,
		style = MaterialTheme.typography.bodyLarge,
		modifier = Modifier
			.fillMaxWidth()
			.clickable(onClick = onClick)
			.padding(horizontal = 16.dp, vertical = 14.dp)
			.clearAndSetSemantics {
				contentDescription = label
				role = Role.Button
			}
	)
}
