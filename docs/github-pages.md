# Chocolate Stunts on GitHub Pages

The landing page lives in `site/`. It presents the game's features, release
notes, grouped package downloads, platform guides, and the browser game in a
same-origin iframe. Its relative URLs work under a repository path such as
`/restunts-bb11/`.

The linked `site/quick-guide.html` shares the landing page styling and gives
returning players a short guide to added hotkeys and launch parameters. Keep
it in the builder's `WEBSITE_FILES` list so it is included in the published site.

The landing page also links to `site/hypervision-timing.html`, an interactive
comparison of input sampling, physics updates, and video presentation in
HyperVision and the original 20 FPS renderer. The timeline illustrates the
normal 20 Hz physics schedule and HyperVision interpolation; it is a timing
model, not a measurement of the visitor's machine. Its HTML, CSS, and JavaScript
files are included in `WEBSITE_FILES`.

## Publish

1. Add the website and workflow changes to the repository through the usual
   review process.
2. Open **Settings > Pages** in GitHub and choose **GitHub Actions** as the build
   and deployment source.
3. Open **Actions > GitHub Pages > Run workflow**, select the reviewed branch,
   and run it. Allow that branch in the `github-pages` environment if needed.
4. The deployment job reports the published URL.

Publication is manual. Pushes, pull requests, and game releases do not publish
the site automatically. GitHub must have the workflow on the default branch
before its manual run button is available.

The workflow uses the same pinned Emscripten toolchain and browser packaging
script as release builds. It validates the archive's target, source commit,
checksum, file inventory, licenses, and relink materials before assembling the
site. It does not run the full game replay validation suite; publish a reviewed
commit whose normal project checks have passed.

No original Stunts game resources are copied to the website. Visitors choose
their own Broderbund Stunts 1.1 folder locally. The browser's own folder picker
and save controls work inside the same-origin frame; the page also provides a
link to open the game separately and allows fullscreen.

## Build and preview locally

Use the complete `restunts-browser.zip` and its matching
`restunts-browser.zip.sha256` from the package workflow. The assembly command
checks that its source commit matches the current repository HEAD:

```sh
python3 tools/scripts/build-pages.py \
    --browser-package /path/to/restunts-browser.zip \
    --output out/github-pages
python3 -m http.server 8000 --directory out/github-pages
```

Open `http://localhost:8000/`. For another known package revision, pass its full
source SHA with `--commit`; this is an explicit choice to preview website text
and a game from different revisions. Keep the checksum sidecar beside the ZIP.

The output directory must be new or empty. For another preview, select a fresh
`--output` directory or remove only the previous generated output yourself.
The builder copies only the named landing files and permitted files beneath
`site/assets/`; it rejects asset symlinks and unexpected formats.

To build a fresh package on a clean checkout in the Debian 12 build environment,
install the base tools listed in `.github/workflows/github-pages.yml`, then run:

```sh
bash tools/scripts/install-package-dependencies.sh browser
GITHUB_SHA=$(git rev-parse HEAD) bash tools/scripts/build-package.sh browser
python3 tools/scripts/build-pages.py \
    --browser-package dist/packages/restunts-browser.zip \
    --output out/github-pages
```

Installing the base Debian build tools requires root access. The browser
toolchain installer downloads its verified tools separately. See
[the browser build guide](wasm.md) for a direct local Emscripten build;
the Pages builder requires the complete release ZIP, not a standalone HTML file.

The hosted game is `game/restunts.html`. Its matching, unmodified browser ZIP is
available as `game/browser-runtime.zip`, with a checksum sidecar. That archive
includes the dependency notices, licenses, Nuked source, and complete relink kit.
Keep the archive and notice links with the hosted game. Release package entries
are maintained independently of this required runtime-materials link.

## Update release download links

The published package cards in `site/index.html` link directly to the
[v1.3.0 release archives](https://github.com/CommonLoon102/restunts-bb11/releases/tag/v1.3.0).
Available downloads use `a.package` with the matching published asset URL as
`href` and a **Download** label. Include only published packages and omit empty
architecture groups. FreeBSD and OpenBSD each show their sole 64-bit package
card directly under the operating system, without an architecture subgroup;
keep the card’s bitness badge. NetBSD retains its **64 bit** and **32 bit** groups.

For a new release, verify the published asset inventory before updating each
available card's `href`. Update the version, release link, and availability in
the download notice and corresponding What's new entry. Update the offline
browser-package link in the Play section to that release's `restunts-browser.zip`.
Keep this release download separate from the local `game/browser-runtime.zip`
link, which must continue to match the hosted game. Add package cards and their
architecture groups only when matching assets have been published, using
`a.package` with the published asset URL as `href` and a **Download** label.
Remove entries for unavailable packages and any architecture groups left empty.

The download hierarchy uses native `details` and `summary` elements, collapsed
by default. Keep the operating systems in this order: Windows, Linux, MacOS,
Android, DOS, BSD, Haiku, TempleOS, Amiga500+. BSD contains FreeBSD, OpenBSD,
and NetBSD in that order. Where both are available, architecture groups list
**64 bit** before **32 bit**; DOS lists **32 bit** before **16 bit**. Keep
operating-system icons in their summaries, and bitness badges on architecture
summaries and package cards.

Match each card's `data-target` to the archive names in
[the release package list](releases.md); the SSE2 and no-SSE2 variants remain
separate packages. Preserve CPU labels, bitness badges, and platform requirements
when enabling downloads. The browser game and its required runtime-materials
archive remain together in the Play section.

The website uses plain HTML, CSS, and JavaScript; no npm installation or separate
frontend compilation is needed. HTML and Markdown use CRLF working-tree line
endings, while the workflow and Python builder use LF.

## Refresh TempleOS downloads and screenshots

Rebuild the code-only RedSea package from the current sources with Python 3,
QEMU, dosfstools, mtools, and the official stock TempleOS ISO:

```sh
python3 src/restunts/platform/templeos/tools/PACKAGE.PY \
    --iso /path/to/TempleOS.ISO \
    --output /path/to/new/RESTUNTS_STOCK_TEMPLEOS
```

The output directory and its sibling ZIP must not already exist. Copy the new
`RESTUNTS.ISO`, `README.MD`, and sibling `RESTUNTS_STOCK_TEMPLEOS.ZIP` into
`site/downloads/templeos/`. The builder copies the installation guide from
`docs/templeos.md`. Regenerate the public download checksums from the repository
root; the package's internal checksum file covers a different inventory:

```sh
python3 - <<'PYTHON'
import hashlib
from pathlib import Path

downloads = Path("site/downloads/templeos")
names = ("RESTUNTS.ISO", "RESTUNTS_STOCK_TEMPLEOS.ZIP", "README.MD")
checksums = "".join(
    f"{hashlib.sha256((downloads / name).read_bytes()).hexdigest()}  {name}\r\n"
    for name in names
)
(downloads / "SHA256SUMS.TXT").write_bytes(checksums.encode("ascii"))
PYTHON
```

Use the [guest capture controls](../src/restunts/platform/templeos/tools/VM.TXT)
to take framebuffer screenshots from a disposable stock TempleOS VM.
Capture the same six scenes from the current build and replace
`main-menu.png`, `car-selection.png`, `default-cockpit.png`,
`bernies-ferrari-f2.png`, `helens-jaguar-f3.png`, and `joes-porsche-indy-f3.png`
in `site/assets/templeos/`. Keep the raw 640×480 captures. The game applies the
DOS artwork’s intended 4:3 display correction to its internal 640×400 rendering
and fills the screen; do not resize or recolor the captured images. Check the
Countach dashboard against the DOS EGA version with aspect correction enabled
to verify its steering-wheel proportions.
Update scene captions and alternative text if a scene changes, and update the
download size labels in `site/templeos/index.html` from the new ISO and ZIP sizes.
Run the Pages checks below before publishing all changed files together.

## Check the builder

```sh
python3 -m unittest discover -s tools/scripts/tests -p test_build_pages.py
```

These checks cover complete browser distribution, source revision and checksum
failures, forbidden game data and archive paths, asset symlinks, and protecting
an existing output directory. The workflow also runs the shared release-package
validation tests.
