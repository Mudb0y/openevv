"""Where the tree is, and how one tool reaches another.

Every tool needs the repository root, and six of them need to load another
tool as a module. Both used to be written out in each file: the root as
`dirname(dirname(__file__))', which says "I sit exactly one directory below
the root", and a sibling as a `sys.path' insert of the tool's own directory
followed by an ordinary import, which says "the tool I want is beside me".
Both statements were true and both stopped being true the moment the tools
were grouped, in thirty-three files and six files respectively.

So they are said once, here. The root is found by walking up until the
Makefile is in sight, which is true at any depth. A sibling is named the way
the tree names it -- `rules/patterns', `module/lexicon' -- and loaded by
path, because a name with a hyphen in it is not one `import' can spell and
because a tool is a script rather than a package.
"""

import importlib.util
import os
import sys

TOOLS = os.path.dirname(os.path.abspath(__file__))


def _find_root(start):
    at = start
    while True:
        if os.path.exists(os.path.join(at, 'Makefile')):
            return at
        up = os.path.dirname(at)
        if up == at:
            raise SystemExit('evv: no Makefile above %s, so this is not a '
                             'checkout of the tree' % start)
        at = up


ROOT = _find_root(TOOLS)


def sibling(name):
    """Another tool as a module, named as the tree names it."""
    path = os.path.join(TOOLS, name + '.py')
    if not os.path.exists(path):
        raise SystemExit('evv: no tool at %s' % path)
    key = 'evvtool_' + name.replace('/', '_').replace('-', '_')
    if key in sys.modules:
        return sys.modules[key]
    spec = importlib.util.spec_from_file_location(key, path)
    mod = importlib.util.module_from_spec(spec)
    sys.modules[key] = mod
    spec.loader.exec_module(mod)
    return mod
