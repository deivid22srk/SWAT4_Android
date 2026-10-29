# ci/gen_stub

Empty function tables used by the **app-only** CI build. This is not
game code: it lets the APK compile, install and validate the game
folder. The full build replaces this directory with `gen/` generated
from the user's own copy (see `tools/push_gen.sh`).
