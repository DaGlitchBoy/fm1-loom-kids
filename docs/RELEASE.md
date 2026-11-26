# Publish LION KIDS

LION KIDS has a public project site for the FM‑1 firmware preview, desktop-browser emulator, manual, and USB Record & Export page. The downloadable firmware, browser ZIP, corresponding source bundles and SHA-256 checksums are on that site. [Project homepage](https://daglitchboy.github.io/fm1-loom-kids/)

## Repository listing

A suggested **About** description is:

> LION KIDS is an eight-track groovebox firmware for the M‑VAVE FM‑1, with a browser emulator, pixel-art song names and stereo USB recording. Based on LOOM‑8 by Charles Vestal.

Set **Website** to `https://daglitchboy.github.io/fm1-loom-kids/` in the repository's About settings. GitHub displays the default branch's README on the **Code** tab. The project website is served separately from `gh-pages`.

The repository can be renamed. Update the local remote, build metadata, and published links together after a rename. Changing a GitHub Pages path can move the emulator to a different browser storage scope. If retaining emulator songs at a previously published URL matters, keep that URL available until you have a copy. [GitHub rename documentation](https://docs.github.com/en/repositories/creating-and-managing-repositories/renaming-a-repository)

GitHub's **forked from** label shows the repository's fork relationship. Leaving the fork network permanently can remove issues, pull requests, stars and other GitHub metadata. LION KIDS can be published while the fork relationship remains. The license files and source acknowledgments accompany every distribution. [GitHub fork documentation](https://docs.github.com/en/pull-requests/how-tos/work-with-forks/detaching-a-fork)

## Build the downloadable assets

1. Review and merge the LION KIDS publication pull request into `main`.
2. Build the reviewed browser source with the **Build LION KIDS emulator** workflow. Its artifact contains the browser ZIP, complete corresponding source including submodules, and SHA-256 checksums.
3. For a new device version, use the **Build LION KIDS FM-1 preview** workflow on the maintained device branch or via workflow dispatch. Its artifact contains the installer, firmware package, resource file and corresponding source. Preserve those assets together.
4. Assemble the Pages directory using Python and Pandoc. Pass the unpacked installer artifact as the first directory, the browser artifact as the second, and a new output directory as the third.

```sh
python3 tools/build_lion_site.py firmware-artifact browser-artifact pages-output
cd pages-output
sha256sum -c SHA256SUMS
```

Deploy the generated files to `gh-pages`. Keep this branch set as the Pages source to preserve public URLs. The published firmware version can stay fixed while the front page or documentation is updated.

## Prepare a GitHub release

The firmware and emulator are built from separate source snapshots. For the currently hosted firmware, create the release tag at the exact 0.0.4 build commit if the tag does not already exist. Run this in the project checkout with the renamed repository configured as `origin`.

```sh
git fetch origin feat/lion-kids-fm1-device
git tag v0.0.4 e8d7213ba84f1c23df5fcd3b63066b87ab1a8f3d
git push origin refs/tags/v0.0.4
```

Then open **Releases → Draft a new release** and choose `v0.0.4`. The offline emulator ZIP records its own source commit in `SOURCE-COMMIT.txt`.

Title the release **LION KIDS 0.0.4 — FM‑1 preview** and mark it as a prerelease. Add the firmware and resource files, both corresponding-source archives, and the offline browser ZIP from the project homepage. Link to the site's complete checksums and include these release notes:

> LION KIDS turns the M‑VAVE FM‑1 into an eight-track groovebox for learning to play, layer loops and perform. Play synths, sampled instruments and drums. Overdub on the keys or edit steps, add effects and save three songs with pixel-art names.
>
> Try the browser emulator, read the manual, or install the FM‑1 preview from the [LION KIDS homepage](https://daglitchboy.github.io/fm1-loom-kids/). The Record & Export page downloads song backups, renders one loop to WAV, and records live stereo over USB.
>
> This preview includes the 0.0.4 audio recovery changes. Acid Bass runs on the primary core, and the firmware can continue on one core after a second-core fault. Back up your existing projects before switching firmware.
>
> LION KIDS builds on LOOM‑8 by Charles Vestal, with fm1-base and felucca-bsp. The combined builds are distributed under GPL-3.0-only with corresponding source.

### Community announcement draft

**LION KIDS · an eight-track groovebox for the M‑VAVE FM‑1**

> I made LION KIDS to make it easier to play a sound, record a loop and build up a song. It has eight tracks, Performance and Steps views, and three song slots you can name with pixel art. You can export a saved loop or record a live performance over USB.
>
> It builds on LOOM‑8 by Charles Vestal. You can try it without an FM‑1 in the browser. The installer, manual, screenshots and source are all on the [project homepage](https://daglitchboy.github.io/fm1-loom-kids/). The FM‑1 build is a preview, and feedback on playing and recording is welcome.

Use this as a starting point for the FM‑1 community and Reddit, following each community's rules. For a Firmware Hub listing, use the firmware file and the matching GitHub build's `firmware.json` metadata once the service accepts the build's format and provenance. [Firmware Hub metadata documentation](../base/docs/FIRMWARE-JSON.md)

## Personal data

The site serves static files and uses local browser audio and MIDI permissions. Its pages contain no tracking scripts. Review your public GitHub profile name, avatar and contact fields before posting. Use GitHub's no-reply address for public commits and enable **Block command line pushes that expose my email**. The publisher's account handle will be visible when you publish a GitHub release. Old public copies and cached history can remain accessible after a rename. [GitHub email settings](https://github.com/settings/emails) · [Removing sensitive data from GitHub](https://docs.github.com/en/authentication/keeping-your-account-and-data-secure/removing-sensitive-data-from-a-repository)
