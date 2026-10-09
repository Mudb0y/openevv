# Where the openevv driver finds pronunciation dictionaries.
#
# Three places, read in this order for each language, and the later of two
# saying the same word wins:
#
# A managed set: the community dictionaries as another add-on carries them.
# Eloquence Dictionary Manager is the one there is, and what it publishes is a
# small written contract any synthesiser can read, so it is found by the
# contract rather than by its name: an installed add-on with
# dictionaries/contract.ini in it saying format eci-dictionary-sets, version
# one, and a folder per set under dictionaries/sets. The reader chooses one in
# the voice settings, and none is the default -- installing the manager is not
# the same as asking for the engine to sound different.
#
# Personal entries: what the reader has added in the manager's editor, which
# it keeps in the NVDA configuration and shares with every synthesiser that
# reads the contract. On unless the reader turns it off.
#
# A folder of the reader's own, openevv in the NVDA configuration, for anyone
# who has a dictionary file and no wish for another add-on. Not the add-on's
# directory, which NVDA replaces whole on every update.

import configparser
import os
import re
from collections import namedtuple

from logHandler import log

#: What a provider's marker has to say.
CONTRACT_FORMAT = "eci-dictionary-sets"
CONTRACT_VERSION = 1

#: What a set has to say about itself. Missing any one and the set is passed
#: over, and only that set.
SET_FIELDS = (
	"id",
	"name",
	"source_url",
	"source_version",
	"source_revision",
	"attribution",
	"license",
	"license_url",
)

#: What the setting holds when no managed set is chosen. Never a set's id,
#: since every one of those is qualified by where it came from.
NO_SET = "none"

_SET_ID = re.compile(r"^[a-z0-9]+(?:[.-][a-z0-9]+)+$")

ManagedSet = namedtuple("ManagedSet", "id name version folder")


def _configPath():
	import globalVars

	return globalVars.appArgs.configPath


def ownFolder():
	return os.path.join(_configPath(), "openevv")


def personalFolder():
	return os.path.join(_configPath(), "eciDictionaries", "personal")


def _read(path):
	"""An ini file, or None where it is missing or will not parse."""
	parser = configparser.ConfigParser(interpolation=None)
	try:
		with open(path, encoding="utf-8") as f:
			parser.read_file(f)
	except (OSError, UnicodeDecodeError, configparser.Error) as e:
		log.debugWarning("openevv: %s is not readable: %s" % (path, e))
		return None
	return parser


def _offers(root):
	"""Whether a provider's dictionaries folder carries a contract this
	driver understands. An unknown version is a provider written for a
	driver newer than this one, and is left alone rather than guessed at."""
	marker = os.path.join(root, "contract.ini")
	if not os.path.isfile(marker):
		return False
	contract = _read(marker)
	if contract is None or not contract.has_section("contract"):
		log.warning("openevv: %s has no contract section" % marker)
		return False
	if contract.get("contract", "format", fallback="") != CONTRACT_FORMAT:
		log.warning("openevv: %s is not a dictionary contract" % marker)
		return False
	try:
		version = contract.getint("contract", "version")
	except (ValueError, configparser.Error):
		version = None
	if version != CONTRACT_VERSION:
		log.warning("openevv: %s is contract version %r and this driver reads %d"
		            % (marker, version, CONTRACT_VERSION))
		return False
	return True


def _oneSet(folder, name):
	"""One managed set, or None. Its own file has to name it as its folder
	does: the id is what a reader's choice is kept under, and a set that
	disagrees about its own id cannot be told apart from another."""
	described = _read(os.path.join(folder, "set.ini"))
	if described is None or not described.has_section("set"):
		log.warning("openevv: the dictionary set in %s does not describe itself"
		            % folder)
		return None
	fields = dict(described.items("set"))
	missing = [f for f in SET_FIELDS if not fields.get(f, "").strip()]
	if missing:
		log.warning("openevv: the dictionary set in %s leaves out %s"
		            % (folder, ", ".join(missing)))
		return None
	if fields["id"] != name or not _SET_ID.match(name):
		log.warning("openevv: the dictionary set in %s calls itself %r"
		            % (folder, fields["id"]))
		return None
	return ManagedSet(name, fields["name"].strip(), fields["source_version"].strip(),
	                  folder)


def managedSets():
	"""Every managed set an installed add-on offers, by id.

	Disabled providers count, since what is read is files they left on disk
	and not code of theirs; one still being installed does not, because its
	files are not where they will be. Where two offer the same set the first
	found is kept.
	"""
	import addonHandler

	found = {}
	try:
		addons = list(addonHandler.getAvailableAddons())
	except Exception:  # noqa: BLE001
		log.error("openevv: NVDA would not list its add-ons", exc_info=True)
		return found
	for addon in addons:
		if getattr(addon, "isPendingInstall", False):
			continue
		root = os.path.join(addon.path, "dictionaries")
		if not _offers(root):
			continue
		sets = os.path.join(root, "sets")
		try:
			names = sorted(os.listdir(sets))
		except OSError:
			continue
		for name in names:
			folder = os.path.join(sets, name)
			if not os.path.isdir(folder) or name in found:
				continue
			one = _oneSet(folder, name)
			if one is not None:
				found[name] = one
	return found


def folders(chosen, personal):
	"""The folders a language's dictionary is made from, in the order they
	are read. A chosen set that is not installed is said once in the log and
	otherwise skipped: the choice is kept, and comes back into force if the
	set does."""
	out = []
	if chosen and chosen != NO_SET:
		one = managedSets().get(chosen)
		if one is None:
			log.warning("openevv: the dictionary set %s is not installed" % chosen)
		else:
			out.append(one.folder)
	if personal:
		out.append(personalFolder())
	out.append(ownFolder())
	return tuple(out)
