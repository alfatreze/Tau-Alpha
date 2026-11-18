"""tau-meter: the Tau Alpha meter SDK (build, verify, test, compose). MIT licence."""
import sys
from pathlib import Path
HERE = Path(__file__).resolve().parent
SDK_ROOT = HERE.parent                                   # the folder with include/, ld/, runner/, conformance/
VENDOR = HERE / "_vendor"                                # byte copies of the project's own tools (a test keeps them identical to tools/ in the Tau Alpha repository)
if str(VENDOR) not in sys.path: sys.path.insert(0, str(VENDOR))
__version__ = "1.0.0"
