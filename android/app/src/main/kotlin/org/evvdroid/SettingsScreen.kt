package org.evvdroid

import android.content.Context
import android.view.inputmethod.InputMethodManager
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.ExperimentalFoundationApi
import androidx.compose.foundation.clickable
import androidx.compose.foundation.combinedClickable
import androidx.compose.foundation.focusable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.selection.selectable
import androidx.compose.foundation.selection.selectableGroup
import androidx.compose.foundation.text.KeyboardActions
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Button
import androidx.compose.material3.DropdownMenu
import androidx.compose.material3.DropdownMenuItem
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedTextField
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
import androidx.compose.ui.focus.focusProperties
import androidx.compose.ui.input.key.Key
import androidx.compose.ui.input.key.KeyEventType
import androidx.compose.ui.input.key.key
import androidx.compose.ui.input.key.onKeyEvent
import androidx.compose.ui.input.key.type
import androidx.compose.ui.platform.LocalFocusManager
import androidx.compose.ui.platform.LocalSoftwareKeyboardController
import androidx.compose.ui.platform.LocalView
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.CustomAccessibilityAction
import androidx.compose.ui.semantics.ProgressBarRangeInfo
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.clearAndSetSemantics
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.customActions
import androidx.compose.ui.semantics.progressBarRangeInfo
import androidx.compose.ui.semantics.role
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.semantics.setProgress
import androidx.compose.ui.semantics.stateDescription
import androidx.compose.ui.text.input.ImeAction
import androidx.compose.ui.text.style.TextDecoration
import androidx.compose.ui.unit.dp
import kotlin.math.roundToInt

/**
 * Every setting the engine has, and a button to hear them.
 *
 * Controls are designed with accessibility in mind:
 * A control is one thing to stop at. A label, a bar and a number are three
 * stops for a screen reader, so each row merges into a single node that says
 * what it is and what it is set to, and adjusts in place.
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
					onPercent = { state.setPercent(param, it) },
					onStep = { state.stepPercent(param, it) }
				)
			}
			Gap()
			if (state.languages.size > 1) {
				ChoiceRow(
					label = stringResource(R.string.preview_language_label),
					options = state.languageNames,
					chosen = state.language,
					onChoose = state::chooseLanguage
				)
			}
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
			SwitchRow(
				label = stringResource(R.string.read_emoji_label),
				checked = state.readEmoji,
				onChange = state::chooseReadEmoji
			)

			// ---- Punctuation Pronunciation Settings ----
			Gap()
			ChoiceRow(
				label = stringResource(R.string.punctuation_label),
				options = listOf(
					stringResource(R.string.punctuation_level_none),
					stringResource(R.string.punctuation_level_some),
					stringResource(R.string.punctuation_level_most),
					stringResource(R.string.punctuation_level_all)
				),
				chosen = state.punctuationLevel,
				onChoose = state::choosePunctuationLevel
			)

			// ---- Number Reading Settings ----
			Gap()
			SwitchRow(
				label = stringResource(R.string.number_processing_label),
				checked = state.processNumbers,
				onChange = state::chooseProcessNumbers
			)
			if (state.processNumbers) {
				ChoiceRow(
					label = stringResource(R.string.number_mode_label),
					options = listOf(
						stringResource(R.string.number_mode_default),
						stringResource(R.string.number_mode_digits),
						stringResource(R.string.number_mode_pairs),
						stringResource(R.string.number_mode_triplets),
						stringResource(R.string.number_mode_words)
					),
					chosen = state.numberMode,
					onChoose = state::chooseNumberMode
				)
			}
			SwitchRow(
				label = stringResource(R.string.number_time_label),
				checked = state.readTimeNaturally,
				onChange = state::chooseReadTimeNaturally
			)
			SwitchRow(
				label = stringResource(R.string.number_roman_label),
				checked = state.readRomanNumerals,
				onChange = state::chooseReadRomanNumerals
			)

			// ---- User Dictionaries ----
			Gap()
			for (volume in Eci.DICT_VOLUMES) {
				val volLabel = stringResource(dictionaryLabelOf(volume))
				ValueRow(
					label = volLabel,
					value = state.dictionaryName(volume),
					onClick = { state.openDictionaryManager(volume) }
				)
				ActionRow(
					label = stringResource(R.string.dictionary_import) + " ($volLabel)",
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

	// Dialog for managing words in a dictionary volume
	val managingVol = state.managingVolume
	if (managingVol != null) {
		DictionaryManagerDialog(state = state, volume = managingVol)
	}

	// Dialog for adding or editing a single word
	if (state.isAddEditDialogOpen) {
		AddEditWordDialog(
			state = state,
			volume = state.managingVolume ?: Eci.DICT_MAIN,
			entry = state.editingEntry
		)
	}
}

data class WordEntryActionItem(
	val label: String,
	val action: () -> Unit
)

fun getWordEntryActions(
	entry: DictEntry,
	editLabel: String,
	suspendLabel: String,
	resumeLabel: String,
	deleteLabel: String,
	onEdit: () -> Unit,
	onToggleSuspend: () -> Unit,
	onDelete: () -> Unit
): List<WordEntryActionItem> {
	val toggleLabel = if (entry.suspended) resumeLabel else suspendLabel
	return listOf(
		WordEntryActionItem(label = editLabel, action = onEdit),
		WordEntryActionItem(label = toggleLabel, action = onToggleSuspend),
		WordEntryActionItem(label = deleteLabel, action = onDelete)
	)
}

@Composable
private fun DictionaryManagerDialog(state: SettingsModel, volume: Int) {
	val volLabel = stringResource(dictionaryLabelOf(volume))
	val keyboardController = LocalSoftwareKeyboardController.current
	val focusManager = LocalFocusManager.current
	val view = LocalView.current

	fun hideKeyboard() {
		keyboardController?.hide()
		focusManager.clearFocus()
		(view.context.getSystemService(Context.INPUT_METHOD_SERVICE) as? InputMethodManager)
			?.hideSoftInputFromWindow(view.windowToken, 0)
	}

	fun closeManager() {
		hideKeyboard()
		state.closeDictionaryManager()
	}

	AlertDialog(
		onDismissRequest = ::closeManager,
		title = { Text(stringResource(R.string.dictionary_manage_words, volLabel)) },
		text = {
			Column(modifier = Modifier.fillMaxWidth()) {
				OutlinedTextField(
					value = state.dictSearchQuery,
					onValueChange = { state.dictSearchQuery = it },
					label = { Text(stringResource(R.string.dictionary_search_placeholder)) },
					singleLine = true,
					keyboardOptions = KeyboardOptions(imeAction = ImeAction.Search),
					keyboardActions = KeyboardActions(onSearch = { hideKeyboard() }),
					modifier = Modifier
						.fillMaxWidth()
						.padding(bottom = 8.dp)
				)

				Button(
					onClick = {
						hideKeyboard()
						state.openAddWordDialog(null)
					},
					modifier = Modifier
						.fillMaxWidth()
						.padding(vertical = 4.dp)
				) {
					Text(stringResource(R.string.dictionary_add_word))
				}

				HorizontalDivider(modifier = Modifier.padding(vertical = 4.dp))

				val query = state.dictSearchQuery.trim()
				val filtered = remember(state.activeDictWords, query) {
					if (query.isEmpty()) state.activeDictWords
					else state.activeDictWords.filter {
						it.key.contains(query, ignoreCase = true) ||
						it.say.contains(query, ignoreCase = true)
					}
				}

				if (filtered.isEmpty()) {
					Text(
						text = stringResource(R.string.dictionary_empty),
						style = MaterialTheme.typography.bodyMedium,
						modifier = Modifier.padding(vertical = 16.dp)
					)
				} else {
					Column(
						modifier = Modifier
							.fillMaxWidth()
							.heightIn(max = 350.dp)
							.verticalScroll(rememberScrollState())
					) {
						filtered.forEach { entry ->
							WordRow(
								entry = entry,
								onEdit = {
									hideKeyboard()
									state.openAddWordDialog(entry)
								},
								onToggleSuspend = {
									hideKeyboard()
									state.toggleSuspendWord(volume, entry)
								},
								onDelete = {
									hideKeyboard()
									state.deleteWord(volume, entry.key, entry.caseSensitive)
								}
							)
							HorizontalDivider()
						}
					}
				}
			}
		},
		confirmButton = {
			TextButton(onClick = ::closeManager) {
				Text(stringResource(R.string.dictionary_close))
			}
		}
	)
}

@OptIn(ExperimentalFoundationApi::class)
@Composable
private fun WordRow(
	entry: DictEntry,
	onEdit: () -> Unit,
	onToggleSuspend: () -> Unit,
	onDelete: () -> Unit
) {
	var showMenu by remember { mutableStateOf(false) }

	val editLabel = stringResource(R.string.dictionary_action_edit)
	val suspendLabel = stringResource(R.string.dictionary_action_suspend)
	val resumeLabel = stringResource(R.string.dictionary_action_resume)
	val deleteLabel = stringResource(R.string.dictionary_action_delete)
	val moreOptionsDesc = stringResource(R.string.dictionary_more_options, entry.key)

	val actions = remember(entry, editLabel, suspendLabel, resumeLabel, deleteLabel) {
		getWordEntryActions(
			entry = entry,
			editLabel = editLabel,
			suspendLabel = suspendLabel,
			resumeLabel = resumeLabel,
			deleteLabel = deleteLabel,
			onEdit = onEdit,
			onToggleSuspend = onToggleSuspend,
			onDelete = onDelete
		)
	}

	val desc = buildString {
		append(
			if (entry.caseSensitive) {
				stringResource(R.string.dictionary_word_item_cs_desc, entry.key, entry.say)
			} else {
				stringResource(R.string.dictionary_word_item_desc, entry.key, entry.say)
			}
		)
		if (entry.suspended) {
			append(stringResource(R.string.dictionary_word_suspended_suffix))
		}
	}

	Box(modifier = Modifier.fillMaxWidth()) {
		Row(
			modifier = Modifier
				.fillMaxWidth()
				.combinedClickable(
					onClick = onEdit,
					onLongClick = { showMenu = true }
				)
				.semantics(mergeDescendants = true) {
					contentDescription = desc
					customActions = actions.map { item ->
						CustomAccessibilityAction(label = item.label) {
							item.action()
							true
						}
					}
				}
				.padding(horizontal = 8.dp, vertical = 10.dp),
			horizontalArrangement = Arrangement.SpaceBetween,
			verticalAlignment = Alignment.CenterVertically
		) {
			Column(modifier = Modifier.weight(1f)) {
				Row(verticalAlignment = Alignment.CenterVertically) {
					Text(
						text = entry.key,
						style = MaterialTheme.typography.bodyLarge,
						textDecoration = if (entry.suspended) TextDecoration.LineThrough else null,
						color = if (entry.suspended) MaterialTheme.colorScheme.onSurfaceVariant else MaterialTheme.colorScheme.onSurface
					)
					if (entry.caseSensitive) {
						Text(
							text = " (CS)",
							style = MaterialTheme.typography.bodySmall,
							color = MaterialTheme.colorScheme.primary,
							modifier = Modifier.padding(start = 4.dp)
						)
					}
					if (entry.suspended) {
						Text(
							text = " (" + stringResource(R.string.dictionary_action_suspend) + ")",
							style = MaterialTheme.typography.bodySmall,
							color = MaterialTheme.colorScheme.error,
							modifier = Modifier.padding(start = 4.dp)
						)
					}
				}
				Text(
					text = "→ " + entry.say,
					style = MaterialTheme.typography.bodyMedium,
					color = MaterialTheme.colorScheme.onSurfaceVariant
				)
			}

			IconButton(
				onClick = { showMenu = true },
				modifier = Modifier.semantics {
					contentDescription = moreOptionsDesc
				}
			) {
				Text(
					text = "⋮",
					style = MaterialTheme.typography.titleLarge
				)
			}
		}

		DropdownMenu(
			expanded = showMenu,
			onDismissRequest = { showMenu = false }
		) {
			actions.forEach { item ->
				DropdownMenuItem(
					text = { Text(item.label) },
					onClick = {
						showMenu = false
						item.action()
					}
				)
			}
		}
	}
}

@Composable
private fun AddEditWordDialog(
	state: SettingsModel,
	volume: Int,
	entry: DictEntry?
) {
	var word by remember { mutableStateOf(entry?.key ?: "") }
	var say by remember { mutableStateOf(entry?.say ?: "") }
	var caseSensitive by remember { mutableStateOf(entry?.caseSensitive ?: false) }
	val keyboardController = LocalSoftwareKeyboardController.current
	val focusManager = LocalFocusManager.current
	val view = LocalView.current

	fun hideKeyboard() {
		keyboardController?.hide()
		focusManager.clearFocus()
		(view.context.getSystemService(Context.INPUT_METHOD_SERVICE) as? InputMethodManager)
			?.hideSoftInputFromWindow(view.windowToken, 0)
	}

	fun closeDialog() {
		hideKeyboard()
		state.closeAddWordDialog()
	}

	AlertDialog(
		onDismissRequest = ::closeDialog,
		title = {
			Text(
				if (entry == null) stringResource(R.string.dictionary_add_word)
				else stringResource(R.string.dictionary_edit_word)
			)
		},
		text = {
			Column(modifier = Modifier.fillMaxWidth()) {
				OutlinedTextField(
					value = word,
					onValueChange = { word = it },
					label = { Text(stringResource(R.string.dictionary_word_key)) },
					singleLine = true,
					keyboardOptions = KeyboardOptions(imeAction = ImeAction.Next),
					modifier = Modifier
						.fillMaxWidth()
						.padding(bottom = 8.dp)
				)

				OutlinedTextField(
					value = say,
					onValueChange = { say = it },
					label = { Text(stringResource(R.string.dictionary_word_say)) },
					singleLine = true,
					keyboardOptions = KeyboardOptions(imeAction = ImeAction.Done),
					keyboardActions = KeyboardActions(onDone = { hideKeyboard() }),
					modifier = Modifier
						.fillMaxWidth()
						.padding(bottom = 8.dp)
				)

				SwitchRow(
					label = stringResource(R.string.dictionary_case_sensitive),
					checked = caseSensitive,
					onChange = { caseSensitive = it }
				)
			}
		},
		confirmButton = {
			Button(
				onClick = {
					hideKeyboard()
					if (word.isNotBlank() && say.isNotBlank()) {
						if (entry != null && (entry.key != word || entry.caseSensitive != caseSensitive)) {
							state.deleteWord(volume, entry.key, entry.caseSensitive)
						}
						state.addOrUpdateWord(
							volume = volume,
							key = word.trim(),
							say = say.trim(),
							caseSensitive = caseSensitive,
							suspended = entry?.suspended ?: false
						)
					}
				},
				enabled = word.isNotBlank() && say.isNotBlank()
			) {
				Text(stringResource(R.string.dictionary_save))
			}
		},
		dismissButton = {
			TextButton(onClick = ::closeDialog) {
				Text(stringResource(R.string.cancel))
			}
		}
	)
}

private fun dictionaryLabelOf(volume: Int): Int = when (volume) {
	Eci.DICT_MAIN -> R.string.dictionary_main
	Eci.DICT_ROOT -> R.string.dictionary_root
	Eci.DICT_ABBREVIATION -> R.string.dictionary_abbreviation
	else -> R.string.dictionary_emoji
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

@Composable
private fun Gap() {
	Spacer(modifier = Modifier.height(16.dp))
}

@Composable
private fun SliderRow(
	label: String,
	percent: Int,
	onPercent: (Int) -> Unit,
	onStep: (Int) -> Unit
) {
	val spoken = stringResource(R.string.percent_spoken, percent)
	Column(
		modifier = Modifier
			.fillMaxWidth()
			.padding(horizontal = 16.dp, vertical = 8.dp)
			.onKeyEvent { press ->
				if (press.type != KeyEventType.KeyDown) return@onKeyEvent false
				when (press.key) {
					Key.DirectionLeft, Key.DirectionDown -> onStep(-1)
					Key.DirectionRight, Key.DirectionUp -> onStep(1)
					Key.MoveHome -> onPercent(0)
					Key.MoveEnd -> onPercent(Eci.PERCENT_MAX)
					else -> return@onKeyEvent false
				}
				true
			}
			.focusable()
			.clearAndSetSemantics {
				contentDescription = label
				stateDescription = spoken
				progressBarRangeInfo = ProgressBarRangeInfo(percent.toFloat(), 0f..100f, 99)
				setProgress { want ->
					val target = want.roundToInt()
					if (target != percent) onStep(if (target > percent) 1 else -1)
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
			modifier = Modifier.focusProperties { canFocus = false },
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
			.clickable(
				role = Role.Button,
				onClick = { open = true }
			)
			.semantics {
				contentDescription = label
				stateDescription = now
			}
			.padding(horizontal = 16.dp, vertical = 14.dp)
	)
	if (open) {
		AlertDialog(
			onDismissRequest = { open = false },
			title = { Text(label) },
			text = {
				Column(
					modifier = Modifier
						.selectableGroup()
						.verticalScroll(rememberScrollState())
				) {
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
			.clickable(
				role = Role.Switch,
				onClick = { onChange(!checked) }
			)
			.semantics(mergeDescendants = true) {
				contentDescription = label
				stateDescription = on
			}
			.padding(horizontal = 16.dp, vertical = 14.dp),
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

@Composable
private fun ValueRow(label: String, value: String, onClick: () -> Unit) {
	Text(
		text = stringResource(R.string.labelled_value, label, value),
		style = MaterialTheme.typography.bodyLarge,
		modifier = Modifier
			.fillMaxWidth()
			.clickable(
				role = Role.Button,
				onClick = onClick
			)
			.semantics {
				contentDescription = label
				stateDescription = value
			}
			.padding(horizontal = 16.dp, vertical = 14.dp)
	)
}

@Composable
private fun ActionRow(label: String, onClick: () -> Unit) {
	Text(
		text = label,
		style = MaterialTheme.typography.bodyLarge,
		modifier = Modifier
			.fillMaxWidth()
			.clickable(
				role = Role.Button,
				onClick = onClick
			)
			.semantics {
				contentDescription = label
			}
			.padding(horizontal = 16.dp, vertical = 14.dp)
	)
}
