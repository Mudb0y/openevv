package org.evvdroid

import android.util.Log
import java.io.File

/**
 * Intelligent Emoji reading and dictionary for Eloquence TTS.
 *
 * Eloquence is a legacy engine with an 8-bit character set that cannot natively
 * pronounce Unicode emojis. This module:
 * 1. Contains a comprehensive built-in dictionary of hundreds of standard Unicode emojis
 *    (faces, emotions, gestures, hearts, animals, food, objects, symbols).
 * 2. Supports user-defined emoji mappings and overrides loaded from an editable
 *    emoji dictionary file (`volume-3-emoji.dic`).
 * 3. Handles variation selectors (\uFE0F, \uFE0E) and ensures clean spacing around
 *    spoken emoji descriptions.
 */
object Emoji {

	private const val TAG = "evvdroid"

	@Volatile
	private var userEmojiMap: Map<String, String> = emptyMap()

	/**
	 * Loads user-custom emoji mappings from [file] (UTF-8 encoded).
	 */
	fun loadUserEmoji(file: File) {
		if (!file.exists() || file.length() == 0L) {
			userEmojiMap = emptyMap()
			return
		}
		val map = mutableMapOf<String, String>()
		try {
			file.forEachLine(Charsets.UTF_8) { rawLine ->
				val line = rawLine.trim()
				if (line.isEmpty() || line.startsWith("#") || line.startsWith("//")) return@forEachLine
				val split = line.indexOf('\t')
				if (split > 0) {
					val key = line.substring(0, split).trim()
					val rest = line.substring(split + 1).trim()
					val tab2 = rest.indexOf('\t')
					val say = (if (tab2 >= 0) rest.substring(0, tab2) else rest).trim()
					if (key.isNotEmpty() && say.isNotEmpty()) {
						map[key] = say
						// Also store without variation selector \uFE0F if present
						val cleanKey = key.replace("\uFE0F", "").replace("\uFE0E", "")
						if (cleanKey != key && cleanKey.isNotEmpty()) {
							map[cleanKey] = say
						}
					}
				}
			}
			userEmojiMap = map
			Log.i(TAG, "Loaded ${map.size} custom emoji mappings from ${file.name}")
		} catch (e: Exception) {
			Log.e(TAG, "Failed to load user emoji dictionary", e)
		}
	}

	/**
	 * Scans [text] for emojis and replaces them with their spoken descriptions.
	 * User overrides take precedence over the built-in dictionary.
	 */
	fun apply(text: String): String {
		if (text.isEmpty()) return text
		val userMap = userEmojiMap
		var out = text

		// 1. Apply user custom emoji replacements first (sorted by key length descending)
		if (userMap.isNotEmpty()) {
			val sortedUserKeys = userMap.keys.sortedByDescending { it.length }
			for (key in sortedUserKeys) {
				if (out.contains(key)) {
					val say = userMap[key] ?: continue
					out = out.replace(key, " $say ")
				}
			}
		}

		// 2. Fast check: return early if no surrogate pairs or high-code-point emoji ranges
		if (!hasEmojiCandidate(out)) {
			return collapseSpaces(out)
		}

		// 3. Apply built-in emoji replacements
		for (entry in BUILT_IN_EMOJIS_SORTED) {
			if (out.contains(entry.key)) {
				out = out.replace(entry.key, " ${entry.value} ")
			}
		}

		return collapseSpaces(out)
	}

	private fun hasEmojiCandidate(text: String): Boolean {
		for (i in 0 until text.length) {
			val c = text[i]
			// High surrogate or symbol block
			if (c.isHighSurrogate() || c.code in 0x2600..0x27BF || c.code in 0x2300..0x23FF) {
				return true
			}
		}
		return false
	}

	private fun collapseSpaces(text: String): String =
		text.replace(Regex("""[ \t]+"""), " ")

	private val BUILT_IN_EMOJIS_SORTED: List<Map.Entry<String, String>> by lazy {
		BUILT_IN_EMOJIS.entries.sortedByDescending { it.key.length }
	}

	private val BUILT_IN_EMOJIS: Map<String, String> = mapOf(
		// --- Smileys & Emotions ---
		"😀" to "grinning face",
		"😃" to "grinning face with big eyes",
		"😄" to "grinning face with smiling eyes",
		"😁" to "beaming face",
		"😆" to "grinning squinting face",
		"😅" to "sweat smile",
		"🤣" to "rolling on the floor laughing",
		"😂" to "face with tears of joy",
		"🙂" to "slightly smiling face",
		"🙃" to "upside-down face",
		"🫠" to "melting face",
		"😉" to "winking face",
		"😊" to "smiling face with smiling eyes",
		"😇" to "smiling face with halo",
		"🥰" to "smiling face with hearts",
		"😍" to "heart eyes",
		"🤩" to "star-struck",
		"😘" to "face blowing a kiss",
		"😗" to "kissing face",
		"😚" to "kissing face with closed eyes",
		"😙" to "kissing face with smiling eyes",
		"🥲" to "smiling face with tear",
		"😋" to "face savoring food",
		"😛" to "face with tongue",
		"😜" to "winking face with tongue",
		"🤪" to "zany face",
		"😝" to "squinting face with tongue",
		"🤑" to "money-mouth face",
		"🤗" to "smiling face with open hands",
		"🤭" to "face with hand over mouth",
		"🫢" to "face with open eyes and hand over mouth",
		"🫣" to "face with peeking eye",
		"🤫" to "shushing face",
		"🤔" to "thinking face",
		"🫡" to "saluting face",
		"🤐" to "zipper-mouth face",
		"🤨" to "face with raised eyebrow",
		"😐" to "neutral face",
		"😑" to "expressionless face",
		"😶" to "face without mouth",
		"🫥" to "dotted line face",
		"😶‍🌫️" to "face in clouds",
		"😏" to "smirking face",
		"😒" to "unamused face",
		"🙄" to "face with rolling eyes",
		"😬" to "grimacing face",
		"😮‍💨" to "face exhaling",
		"🤥" to "lying face",
		"🫨" to "shaking face",
		"😌" to "relieved face",
		"😔" to "pensive face",
		"😪" to "sleepy face",
		"🤤" to "drooling face",
		"😴" to "sleeping face",
		"😷" to "face with medical mask",
		"🤒" to "face with thermometer",
		"🤕" to "face with head-bandage",
		"🤢" to "nauseated face",
		"🤮" to "face vomiting",
		"🤧" to "sneezing face",
		"🥵" to "hot face",
		"🥶" to "cold face",
		"🥴" to "woozy face",
		"😵" to "dizzy face",
		"😵‍💫" to "face with spiral eyes",
		"🤯" to "exploding head",
		"🤠" to "cowboy hat face",
		"🥳" to "partying face",
		"🥸" to "disguised face",
		"😎" to "smiling face with sunglasses",
		"🤓" to "nerd face",
		"🧐" to "face with monocle",
		"😕" to "confused face",
		"🫤" to "face with diagonal mouth",
		"😟" to "worried face",
		"🙁" to "slightly frowning face",
		"☹️" to "frowning face",
		"☹" to "frowning face",
		"😮" to "face with open mouth",
		"😯" to "hushed face",
		"😲" to "astonished face",
		"😳" to "flushed face",
		"🥺" to "pleading face",
		"🥹" to "face holding back tears",
		"😦" to "frowning face with open mouth",
		"😧" to "anguished face",
		"😨" to "fearful face",
		"😰" to "anxious face with sweat",
		"😥" to "sad but relieved face",
		"😢" to "crying face",
		"😭" to "loudly crying face",
		"😱" to "face screaming in fear",
		"😖" to "confounded face",
		"😣" to "persevering face",
		"😞" to "disappointed face",
		"😓" to "downcast face with sweat",
		"😩" to "weary face",
		"😫" to "tired face",
		"🥱" to "yawning face",
		"😤" to "face with steam from nose",
		"😡" to "pouting face",
		"😠" to "angry face",
		"🤬" to "face with symbols on mouth",
		"😈" to "smiling face with horns",
		"👿" to "angry face with horns",
		"💀" to "skull",
		"☠️" to "skull and crossbones",
		"☠" to "skull and crossbones",
		"💩" to "pile of poo",
		"🤡" to "clown face",
		"👹" to "ogre",
		"👺" to "goblin",
		"👻" to "ghost",
		"👽" to "alien",
		"👾" to "alien monster",
		"🤖" to "robot",
		"😺" to "grinning cat",
		"😸" to "grinning cat with smiling eyes",
		"😹" to "cat with tears of joy",
		"😻" to "smiling cat with heart-eyes",
		"😼" to "cat with wry smile",
		"😽" to "kissing cat",
		"🙀" to "weary cat",
		"😿" to "crying cat",
		"😾" to "pouting cat",

		// --- Hand Gestures ---
		"👋" to "waving hand",
		"🤚" to "raised back of hand",
		"🖐️" to "hand with fingers splayed",
		"🖐" to "hand with fingers splayed",
		"✋" to "raised hand",
		"🖖" to "vulcan salute",
		"🫱" to "rightwards hand",
		"🫲" to "leftwards hand",
		"🫳" to "palm down hand",
		"🫴" to "palm up hand",
		"🫷" to "leftwards pushing hand",
		"🫸" to "rightwards pushing hand",
		"👌" to "OK hand",
		"🤌" to "pinched fingers",
		"🤏" to "pinching hand",
		"✌️" to "victory hand",
		"✌" to "victory hand",
		"🤞" to "crossed fingers",
		"🫰" to "hand with index finger and thumb crossed",
		"🤟" to "love-you gesture",
		"🤘" to "sign of the horns",
		"🤙" to "call me hand",
		"👈" to "backhand index pointing left",
		"👉" to "backhand index pointing right",
		"👆" to "backhand index pointing up",
		"🖕" to "middle finger",
		"👇" to "backhand index pointing down",
		"☝️" to "index pointing up",
		"☝" to "index pointing up",
		"🫵" to "index pointing at the viewer",
		"👍" to "thumbs up",
		"👎" to "thumbs down",
		"✊" to "raised fist",
		"👊" to "oncoming fist",
		"🤛" to "left-facing fist",
		"🤜" to "right-facing fist",
		"👏" to "clapping hands",
		"🙌" to "raising hands",
		"🫶" to "heart hands",
		"👐" to "open hands",
		"🤲" to "palms up together",
		"🤝" to "handshake",
		"🙏" to "folded hands",
		"✍️" to "writing hand",
		"✍" to "writing hand",
		"💅" to "nail polish",
		"🤳" to "selfie",
		"💪" to "flexed biceps",
		"🦾" to "mechanical arm",
		"🦿" to "mechanical leg",
		"🦵" to "leg",
		"🦶" to "foot",
		"👂" to "ear",
		"🦻" to "ear with hearing aid",
		"👃" to "nose",
		"🧠" to "brain",
		"🫀" to "anatomical heart",
		"🫁" to "lungs",
		"🦷" to "tooth",
		"🦴" to "bone",
		"👀" to "eyes",
		"👁️" to "eye",
		"👁" to "eye",
		"👅" to "tongue",
		"👄" to "mouth",
		"🫦" to "biting lip",

		// --- Hearts & Emotion Symbols ---
		"❤️" to "red heart",
		"❤" to "red heart",
		"🩷" to "pink heart",
		"🧡" to "orange heart",
		"💛" to "yellow heart",
		"💚" to "green heart",
		"💙" to "blue heart",
		"🩵" to "light blue heart",
		"💜" to "purple heart",
		"🤎" to "brown heart",
		"🖤" to "black heart",
		"🩶" to "grey heart",
		"🤍" to "white heart",
		"💔" to "broken heart",
		"❤️‍🔥" to "heart on fire",
		"❤️‍🩹" to "mending heart",
		"❣️" to "heart exclamation",
		"💕" to "two hearts",
		"💞" to "revolving hearts",
		"💓" to "beating heart",
		"💗" to "growing heart",
		"💖" to "sparkling heart",
		"💘" to "heart with arrow",
		"💝" to "heart with ribbon",
		"💟" to "heart decoration",
		"💋" to "kiss mark",
		"💯" to "hundred points",
		"💢" to "anger symbol",
		"💥" to "collision",
		"💫" to "dizzy",
		"💦" to "sweat droplets",
		"💨" to "dashing away",
		"🕳️" to "hole",
		"💬" to "speech balloon",
		"🗨️" to "left speech bubble",
		"🗯️" to "right anger bubble",
		"💭" to "thought balloon",
		"💤" to "ZZZ",

		// --- Objects & Tools ---
		"🔥" to "fire",
		"✨" to "sparkles",
		"🌟" to "glowing star",
		"⭐" to "star",
		"🎉" to "party popper",
		"🎊" to "confetti ball",
		"🎈" to "balloon",
		"🎁" to "wrapped gift",
		"🏆" to "trophy",
		"🥇" to "1st place medal",
		"🥈" to "2nd place medal",
		"🥉" to "3rd place medal",
		"⚽" to "soccer ball",
		"🏀" to "basketball",
		"🏈" to "american football",
		"⚾" to "baseball",
		"🎾" to "tennis",
		"🏐" to "volleyball",
		"🏉" to "rugby football",
		"🎱" to "pool 8 ball",
		"🏓" to "ping pong",
		"🏸" to "badminton",
		"🥊" to "boxing glove",
		"🎮" to "video game",
		"🎲" to "game die",
		"🎯" to "bullseye",
		"📱" to "mobile phone",
		"💻" to "laptop",
		"🖥️" to "desktop computer",
		"⌨️" to "keyboard",
		"🖱️" to "computer mouse",
		"💾" to "floppy disk",
		"📷" to "camera",
		"📸" to "camera with flash",
		"📹" to "video camera",
		"📺" to "television",
		"📻" to "radio",
		"🔊" to "speaker high volume",
		"🔉" to "speaker medium volume",
		"🔈" to "speaker low volume",
		"🔇" to "muted speaker",
		"🔔" to "bell",
		"🔕" to "bell with slash",
		"📢" to "loudspeaker",
		"📣" to "megaphone",
		"🎵" to "musical note",
		"🎶" to "musical notes",
		"🎙️" to "studio microphone",
		"🎤" to "microphone",
		"🎧" to "headphone",
		"🔋" to "battery",
		"🪫" to "low battery",
		"🔌" to "electric plug",
		"💡" to "light bulb",
		"🔦" to "flashlight",
		"🕯️" to "candle",
		"📖" to "open book",
		"📚" to "books",
		"📜" to "scroll",
		"📄" to "page facing up",
		"📝" to "memo",
		"💼" to "briefcase",
		"📁" to "file folder",
		"📂" to "open file folder",
		"📅" to "calendar",
		"📆" to "tear-off calendar",
		"📈" to "chart increasing",
		"📉" to "chart decreasing",
		"📊" to "bar chart",
		"📋" to "clipboard",
		"📌" to "pushpin",
		"📍" to "round pushpin",
		"📎" to "paperclip",
		"📏" to "straight ruler",
		"📐" to "triangular ruler",
		"✂️" to "scissors",
		"🔒" to "locked",
		"🔓" to "unlocked",
		"🔑" to "key",
		"🗝️" to "old key",
		"🔨" to "hammer",
		"🪓" to "axe",
		"🔧" to "wrench",
		"🪛" to "screwdriver",
		"🔩" to "nut and bolt",
		"⚙️" to "gear",
		"🧲" to "magnet",
		"🔫" to "water pistol",
		"💣" to "bomb",
		"🛡️" to "shield",
		"🛒" to "shopping cart",
		"💰" to "money bag",
		"💵" to "dollar banknote",
		"💳" to "credit card",
		"🪙" to "coin",
		"✉️" to "envelope",
		"📧" to "e-mail",
		"📦" to "package",
		"🚗" to "car",
		"🚕" to "taxi",
		"🚙" to "SUV",
		"🚌" to "bus",
		"🏎️" to "racing car",
		"🚓" to "police car",
		"🚑" to "ambulance",
		"🚒" to "fire engine",
		"🚲" to "bicycle",
		"🛵" to "motor scooter",
		"🏍️" to "motorcycle",
		"✈️" to "airplane",
		"🚀" to "rocket",
		"🛸" to "flying saucer",
		"🚁" to "helicopter",
		"🚂" to "locomotive",
		"🚆" to "train",
		"🚇" to "metro",
		"⛵" to "sailboat",
		"🚢" to "ship",
		"⚓" to "anchor",

		// --- Nature & Food ---
		"🐶" to "dog face",
		"🐱" to "cat face",
		"🐭" to "mouse face",
		"🐹" to "hamster",
		"🐰" to "rabbit face",
		"🦊" to "fox",
		"🐻" to "bear",
		"🐼" to "panda",
		"🐨" to "koala",
		"🐯" to "tiger face",
		"🦁" to "lion",
		"🐮" to "cow face",
		"🐷" to "pig face",
		"🐸" to "frog",
		"🐵" to "monkey face",
		"🐔" to "chicken",
		"🐧" to "penguin",
		"🐦" to "bird",
		"🦆" to "duck",
		"🦅" to "eagle",
		"🦉" to "owl",
		"🐺" to "wolf",
		"🐴" to "horse face",
		"🦄" to "unicorn",
		"🐝" to "honeybee",
		"🐛" to "bug",
		"🦋" to "butterfly",
		"🐌" to "snail",
		"🐞" to "lady beetle",
		"🐜" to "ant",
		"🦟" to "mosquito",
		"🐢" to "turtle",
		"🐍" to "snake",
		"🐙" to "octopus",
		"🐠" to "tropical fish",
		"🐟" to "fish",
		"🐬" to "dolphin",
		"🐳" to "spouting whale",
		"🦈" to "shark",
		"🐊" to "crocodile",
		"🐘" to "elephant",
		"🌸" to "cherry blossom",
		"🌹" to "rose",
		"🌺" to "hibiscus",
		"🌻" to "sunflower",
		"🌼" to "blossom",
		"🌷" to "tulip",
		"🌱" to "seedling",
		"🌲" to "evergreen tree",
		"🌳" to "deciduous tree",
		"🌴" to "palm tree",
		"🌵" to "cactus",
		"🍀" to "four leaf clover",
		"🍁" to "maple leaf",
		"🍂" to "fallen leaf",
		"🍄" to "mushroom",
		"🌍" to "globe showing Europe-Africa",
		"🌎" to "globe showing Americas",
		"🌏" to "globe showing Asia-Australia",
		"🌕" to "full moon",
		"🌙" to "crescent moon",
		"☀️" to "sun",
		"⛅" to "sun behind cloud",
		"☁️" to "cloud",
		"🌧️" to "cloud with rain",
		"🌩️" to "cloud with lightning",
		"❄️" to "snowflake",
		"⚡" to "high voltage",
		"🌈" to "rainbow",
		"🌊" to "water wave",
		"🍏" to "green apple",
		"🍎" to "red apple",
		"🍐" to "pear",
		"🍊" to "tangerine",
		"🍋" to "lemon",
		"🍌" to "banana",
		"🍉" to "watermelon",
		"🍇" to "grapes",
		"🍓" to "strawberry",
		"🫐" to "blueberries",
		"🍒" to "cherries",
		"🍑" to "peach",
		"🥭" to "mango",
		"🍍" to "pineapple",
		"🥥" to "coconut",
		"🥝" to "kiwi fruit",
		"🍅" to "tomato",
		"🥑" to "avocado",
		"🥦" to "broccoli",
		"🌽" to "ear of corn",
		"🥕" to "carrot",
		"🥔" to "potato",
		"🥐" to "croissant",
		"🍞" to "bread",
		"🥖" to "baguette bread",
		"🥨" to "pretzel",
		"🧀" to "cheese wedge",
		"🍳" to "cooking",
		"🥞" to "pancakes",
		"🧇" to "waffle",
		"🥓" to "bacon",
		"🥩" to "cut of meat",
		"🍗" to "poultry leg",
		"🍖" to "meat on bone",
		"🌭" to "hot dog",
		"🍔" to "hamburger",
		"🍟" to "french fries",
		"🍕" to "pizza",
		"🥪" to "sandwich",
		"🌮" to "taco",
		"🌯" to "burrito",
		"🍜" to "steaming bowl",
		"🍝" to "spaghetti",
		"🍣" to "sushi",
		"🍦" to "soft ice cream",
		"🍧" to "shaved ice",
		"🍨" to "ice cream",
		"🍩" to "doughnut",
		"🍪" to "cookie",
		"🎂" to "birthday cake",
		"🍰" to "shortcake",
		"🧁" to "cupcake",
		"🍫" to "chocolate bar",
		"🍬" to "candy",
		"🍭" to "lollipop",
		"🍿" to "popcorn",
		"☕" to "hot beverage",
		"🍵" to "teacup without handle",
		"🧃" to "beverage box",
		"🥤" to "cup with straw",
		"🧋" to "bubble tea",
		"🍺" to "beer mug",
		"🍻" to "clinking beer mugs",
		"🍷" to "wine glass",
		"🍸" to "cocktail glass",
		"🍹" to "tropical drink",
		"🍾" to "bottle with popping cork",

		// --- Common Symbols & Flags ---
		"⚠️" to "warning",
		"⛔" to "no entry",
		"🚫" to "prohibited",
		"❌" to "cross mark",
		"⭕" to "hollow red circle",
		"✅" to "check mark button",
		"✔️" to "check mark",
		"✔" to "check mark",
		"ℹ️" to "information",
		"❓" to "red question mark",
		"❔" to "white question mark",
		"❕" to "white exclamation mark",
		"❗" to "red exclamation mark",
		"♿" to "wheelchair symbol",
		"🚹" to "men's room",
		"🚺" to "women's room",
		"🚻" to "restroom",
		"🚼" to "baby symbol",
		"🚭" to "no smoking",
		"🚩" to "triangular flag",
		"🏁" to "chequered flag",
		"🏳️" to "white flag",
		"🏴" to "black flag"
	)
}
