#!/bin/sh
# Local compile check (no Android SDK needed): the app's Java tree against
# the REAL android framework jar (Robolectric android-all, java/javax
# stripped) with the Eclipse batch compiler. Mirrors what gradle does in CI.
#   1) curl -sL -o /tmp/ecj.jar https://repo1.maven.org/maven2/org/eclipse/jdt/ecj/3.33.0/ecj-3.33.0.jar
#   2) curl -sL -o /tmp/a33.jar https://repo1.maven.org/maven2/org/robolectric/android-all/13-robolectric-9030017/android-all-13-robolectric-9030017.jar
#   3) python3 - <<'PY'  (strip java/javax — libcore classes clash with JDK modules)
#      import zipfile; src=zipfile.ZipFile('/tmp/a33.jar'); dst=zipfile.ZipFile('/tmp/android-pure33.jar','w',zipfile.ZIP_DEFLATED)
#      for n in src.namelist():
#          if n.endswith('/') or n.startswith(('java/','javax/','META-INF/')): continue
#          dst.writestr(n, src.read(n))
#      dst.close()
#      PY
#   4) sh tools/java_check.sh
set -e
cd "$(dirname "$0")/../android/app/src/main/java"
exec java -jar /tmp/ecj.jar -source 17 -target 17 -nowarn \
    -cp /tmp/android-pure33.jar -d /tmp/jout \
    $(find io org -name "*.java")
