# Testing the Windows and macOS plugin packages

These two packages have never been tried on a real Windows or Mac computer — they build without errors, but
that only proves the code compiles, not that OpenCPN can actually load them. This is what we need checked.

The one thing that actually matters: **does OpenCPN accept the plugin, and does it show the right version
number?** Everything past that (planning an actual route) is a nice bonus, not required.

## What you need first

1. **OpenCPN itself**, already installed. If it isn't:
   - **Windows**: download and run the installer from [opencpn.org](https://opencpn.org/OpenCPN/info/downloadopencpn.html).
   - **macOS**: download the `.dmg` from the same page, open it, drag OpenCPN into Applications. The first time
     you open it, macOS will say it's from an "unidentified developer" and refuse to run it — this is normal
     and OpenCPN's own site mentions it. Right-click the app, choose **Open**, then confirm **Open** again in
     the dialog that pops up. You only have to do this once.
2. **The plugin package** — a `.tar.gz` file. Get it from the
   [releases page](https://github.com/i-machine-things/open-autoroute/releases): pick the release you're testing, then
   download the asset matching your platform and that release's version (`vX.Y.Z`):
   - Windows: `openautoroute-opencpn-plugin-vX.Y.Z-windows-x86.tar.gz`
   - macOS: `openautoroute-opencpn-plugin-vX.Y.Z-macos-arm64.tar.gz`

   Don't unzip it — OpenCPN reads the `.tar.gz` file directly. Wherever this doc mentions a version number below, use
   the one from whichever release you actually downloaded.

## Import and check the version (the part that matters)

1. Open OpenCPN.
2. Go to **Options** (the wrench/gear icon) → **Plugins** tab.
3. Click **Import Plugin...** (sometimes a `+` button instead) and pick the `.tar.gz` file you downloaded.
4. **If OpenCPN says something like "Incompatible import plugin detected" — stop and report that exact
   message.** That's the main failure we're checking for, and exactly what we can't test ourselves.
5. If it imports, find **Auto-route** in the plugin list and make sure its checkbox is ticked (enabled).
6. Click on the **Auto-route** entry to see its details, or check the version shown in the plugin list.
   **It should match the release's version** (the `vX.Y.Z` from the download). If it says `0.0` or is blank, that's also worth reporting — it means a fix
   that was supposed to land didn't actually take effect on your platform.

That's the critical check. Screenshot whatever OpenCPN shows at this point (success or an error) and send it
back — that alone tells us whether this platform's build is usable at all.

## Optional: actually plan a route

Only worth doing if the import above worked cleanly and you want to go further.

1. You need some chart data loaded in OpenCPN first (**Options → Charts**, "Add Directory") — if you don't
   already have NOAA charts installed, download a small set for a US coastal area from
   [NOAA's chart catalog](https://charts.noaa.gov/InteractiveCatalog/nrnc.shtml) (pick "ENC" format, a
   handful of cells near any US coastline is enough).
2. A new toolbar button (a route glyph) should appear. Click it, then click a start point and an end point
   on the chart, both in water covered by the charts you loaded.
3. It should either draw a route into the Route Manager, or tell you why it couldn't (no chart coverage,
   blocked by a hazard, etc.) — either outcome is a successful test of this part.

## Reporting back

Whatever happens — success, an error message, a crash — the useful report is: what you clicked, what you
expected, and exactly what happened (a screenshot of any error is the most useful single thing you can send).
This is a planning aid still being built, not a finished product, so "it broke" is a completely normal and
useful result here.
